#include "service/adapters/FileResourceWatcher.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace baas::service::adapters {
namespace {

using channels::ResourceKey;
using channels::SyncResource;
using Json = nlohmann::json;

constexpr auto minimum_poll_interval = std::chrono::milliseconds{10};
constexpr auto maximum_poll_interval = std::chrono::minutes{1};

[[nodiscard]] std::optional<std::vector<std::string>> parse_config_ids(
    const std::string& data_json)
{
    try {
        const auto document = Json::parse(data_json);
        if (!document.is_array()) return std::nullopt;
        std::vector<std::string> result;
        result.reserve(document.size());
        for (const auto& item : document) {
            if (!item.is_string()) return std::nullopt;
            result.push_back(item.get<std::string>());
        }
        if (!std::is_sorted(result.begin(), result.end())
            || std::adjacent_find(result.begin(), result.end()) != result.end()) {
            return std::nullopt;
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

class FileResourceWatcher::Impl final {
public:
    Impl(std::shared_ptr<FileResourceStore> supplied_store,
         FileResourceScanCallback supplied_callback,
         FileResourceWatcherConfig supplied_config)
        : store(std::move(supplied_store)), callback(std::move(supplied_callback)),
          config(std::move(supplied_config))
    {
        if (!store || !callback
            || config.poll_interval < minimum_poll_interval
            || config.poll_interval > maximum_poll_interval
            || config.origin.empty() || config.origin.size() > 64) {
            throw std::invalid_argument("invalid file resource watcher configuration");
        }
    }

    FileResourceScanResult scan(const std::stop_token stop) noexcept
    {
        try {
            if (stop.stop_requested()) {
                return {std::nullopt, FileResourceScanError::cancelled};
            }
            auto list = store->config_list(stop);
            if (!list) return {std::nullopt, FileResourceScanError::config_list};
            auto identifiers = parse_config_ids(list->data_json);
            if (!identifiers) {
                return {std::nullopt, FileResourceScanError::config_list};
            }

            std::vector<std::string> prior;
            {
                std::lock_guard lock(state_mutex);
                prior = config_ids;
            }

            for (const auto& identifier : *identifiers) {
                if (stop.stop_requested()) {
                    return {std::nullopt, FileResourceScanError::cancelled};
                }
                for (const auto resource : {SyncResource::config, SyncResource::event}) {
                    if (!store->refresh(ResourceKey{resource, identifier}, config.origin)) {
                        return {std::nullopt, FileResourceScanError::config_resource};
                    }
                }
            }

            // Removed identifiers must invalidate both cached snapshots. A
            // successful not_found result is the expected removal outcome.
            for (const auto& identifier : prior) {
                if (std::binary_search(
                        identifiers->begin(), identifiers->end(), identifier)) {
                    continue;
                }
                static_cast<void>(store->refresh(
                    ResourceKey{SyncResource::config, identifier}, config.origin));
                static_cast<void>(store->refresh(
                    ResourceKey{SyncResource::event, identifier}, config.origin));
            }

            // Retain the successfully validated structure even if a required
            // global resource later fails. This guarantees that the next scan
            // can invalidate cached entries removed during a degraded period.
            {
                std::lock_guard lock(state_mutex);
                config_ids = *identifiers;
            }

            // gui.json is optional in headless projects, but when present it
            // is refreshed and validated through the same store boundary.
            const auto gui = store->refresh(
                ResourceKey{SyncResource::gui, std::nullopt}, config.origin);
            if (gui.disposition != ResourceRefreshDisposition::not_found
                && gui.disposition != ResourceRefreshDisposition::removed
                && !gui) {
                return {std::nullopt, FileResourceScanError::config_resource};
            }

            auto static_data = store->refresh(
                ResourceKey{SyncResource::static_data, std::nullopt}, config.origin);
            if (!static_data || !static_data.snapshot) {
                return {std::nullopt, FileResourceScanError::static_resource};
            }
            auto setup = store->refresh(
                ResourceKey{SyncResource::setup_toml, std::nullopt}, config.origin);
            if (!setup || !setup.snapshot) {
                return {std::nullopt, FileResourceScanError::setup_resource};
            }

            FileResourceScanSnapshot snapshot{
                std::move(*list.value), std::move(*static_data.snapshot),
                std::move(*setup.snapshot), std::move(*identifiers)};
            {
                std::lock_guard lock(state_mutex);
                config_ids = snapshot.config_ids;
            }
            return {std::move(snapshot), FileResourceScanError::none};
        } catch (...) {
            return {std::nullopt, FileResourceScanError::internal_error};
        }
    }

    void publish(const FileResourceScanResult& result) noexcept
    {
        try {
            callback(result);
        } catch (...) {
        }
    }

    void run(const std::stop_token stop) noexcept
    {
        std::unique_lock wait_lock(wait_mutex);
        while (!stop.stop_requested()) {
            wait_condition.wait_for(
                wait_lock, stop, config.poll_interval, [] { return false; });
            if (stop.stop_requested()) break;
            wait_lock.unlock();
            const auto result = scan(stop);
            if (result.error != FileResourceScanError::cancelled) publish(result);
            wait_lock.lock();
        }
        {
            std::lock_guard lock(lifecycle_mutex);
            running.store(false, std::memory_order_release);
            completed = true;
            lifecycle_condition.notify_all();
        }
    }

    std::shared_ptr<FileResourceStore> store;
    FileResourceScanCallback callback;
    FileResourceWatcherConfig config;
    std::recursive_mutex lifecycle_mutex;
    std::condition_variable_any lifecycle_condition;
    std::mutex state_mutex;
    std::mutex wait_mutex;
    std::condition_variable_any wait_condition;
    std::vector<std::string> config_ids;
    std::jthread worker;
    std::atomic<bool> running{false};
    bool started{};
    bool stopping{};
    bool completed{true};
    bool join_in_progress{};
    std::thread::id worker_id;
    std::thread::id starting_id;
};

FileResourceWatcher::FileResourceWatcher(
    std::shared_ptr<FileResourceStore> store,
    FileResourceScanCallback callback,
    FileResourceWatcherConfig config)
    : impl_(std::make_shared<Impl>(
          std::move(store), std::move(callback), std::move(config)))
{}

FileResourceWatcher::~FileResourceWatcher()
{
    stop();
}

FileResourceWatcherStartResult FileResourceWatcher::start() noexcept
{
    std::lock_guard lock(impl_->lifecycle_mutex);
    if (impl_->started) {
        return {false, false, FileResourceScanError::internal_error};
    }
    impl_->started = true;
    impl_->completed = false;
    impl_->starting_id = std::this_thread::get_id();
    const auto initial = impl_->scan({});
    impl_->publish(initial);
    if (impl_->stopping) {
        impl_->completed = true;
        impl_->starting_id = {};
        impl_->lifecycle_condition.notify_all();
        return {false, static_cast<bool>(initial), FileResourceScanError::cancelled};
    }
    try {
        impl_->running.store(true, std::memory_order_release);
        impl_->worker = std::jthread(
            [impl = impl_](const std::stop_token stop) { impl->run(stop); });
        impl_->worker_id = impl_->worker.get_id();
        impl_->starting_id = {};
        return {true, static_cast<bool>(initial), initial.error};
    } catch (...) {
        impl_->running.store(false, std::memory_order_release);
        impl_->completed = true;
        impl_->starting_id = {};
        impl_->lifecycle_condition.notify_all();
        return {false, static_cast<bool>(initial), FileResourceScanError::internal_error};
    }
}

void FileResourceWatcher::stop() noexcept
{
    std::jthread joining;
    {
        std::unique_lock lock(impl_->lifecycle_mutex);
        impl_->stopping = true;
        const auto caller = std::this_thread::get_id();
        if (impl_->starting_id == caller) return;
        if (impl_->join_in_progress) {
            if (impl_->worker_id == caller) return;
            impl_->lifecycle_condition.wait(
                lock, [impl = impl_] { return !impl->join_in_progress; });
            return;
        }
        if (!impl_->worker.joinable()) {
            if (impl_->started && !impl_->completed
                && impl_->worker_id != caller) {
                impl_->lifecycle_condition.wait(
                    lock, [impl = impl_] { return impl->completed; });
            }
            return;
        }
        impl_->worker.request_stop();
        impl_->wait_condition.notify_all();
        if (impl_->worker_id == caller) {
            // A downstream synchronous Provider callback may stop or destroy
            // the bridge from this worker. The shared Impl capture keeps state
            // alive until run() exits after this detached self-stop.
            impl_->worker.detach();
            return;
        }
        impl_->join_in_progress = true;
        joining = std::move(impl_->worker);
    }
    joining.join();
    {
        std::lock_guard lock(impl_->lifecycle_mutex);
        impl_->running.store(false, std::memory_order_release);
        impl_->join_in_progress = false;
        impl_->worker_id = {};
        impl_->lifecycle_condition.notify_all();
    }
}

FileResourceScanResult FileResourceWatcher::scan_once(
    const std::stop_token stop) noexcept
{
    return impl_->scan(stop);
}

bool FileResourceWatcher::running() const noexcept
{
    return impl_->running.load(std::memory_order_acquire);
}

}  // namespace baas::service::adapters
