#include "service/app/ServiceRuntimeProviderBridge.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace baas::service::app {
namespace {

using Json = nlohmann::json;

[[nodiscard]] double system_clock_ms()
{
    return std::chrono::duration<double, std::milli>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

[[nodiscard]] std::string_view scan_error_name(
    const adapters::FileResourceScanError error) noexcept
{
    using enum adapters::FileResourceScanError;
    switch (error) {
        case none: return "none";
        case cancelled: return "cancelled";
        case config_list: return "config_list";
        case config_resource: return "config_resource";
        case static_resource: return "static_resource";
        case setup_resource: return "setup_resource";
        case internal_error: return "internal_error";
    }
    return "unknown";
}

}  // namespace

class ServiceRuntimeProviderBridge::Impl final {
public:
    struct Core final {
        std::shared_ptr<ProductionProviderBackend> provider;
        ServiceRuntimeProviderBridgeDependencies::Clock clock;
        ServiceRuntimeProviderBridgeLimits limits;
        std::recursive_mutex callback_mutex;
        std::string static_data_json;
        bool static_published{};
        std::atomic<bool> accepting{false};
        std::atomic<bool> ready{false};
        std::atomic<adapters::FileResourceScanError> last_error{
            adapters::FileResourceScanError::internal_error};

        channels::ProviderBackendError log(ServiceRuntimeLogEntry entry) noexcept
        {
            if (!accepting.load(std::memory_order_acquire)) {
                return channels::ProviderBackendError::internal_error;
            }
            if (entry.scope.empty() || entry.scope.size() > limits.max_scope_bytes
                || entry.level.empty() || entry.level.size() > limits.max_level_bytes
                || entry.message.size() > limits.max_message_bytes) {
                return channels::ProviderBackendError::capacity;
            }
            try {
                const auto now = clock();
                if (!std::isfinite(now)) {
                    return channels::ProviderBackendError::internal_error;
                }
                return provider->publish_log(Json{
                    {"scope", std::move(entry.scope)},
                    {"time", now},
                    {"level", std::move(entry.level)},
                    {"message", std::move(entry.message)},
                }.dump());
            } catch (...) {
                return channels::ProviderBackendError::internal_error;
            }
        }

        void scan_completed(const adapters::FileResourceScanResult& result) noexcept
        {
            std::lock_guard callback_lock(callback_mutex);
            if (!accepting.load(std::memory_order_acquire)) return;
            if (result && result.snapshot) {
                const bool static_changed = !static_published
                    || static_data_json != result.snapshot->static_data.data_json;
                const auto replaced = static_changed
                    ? provider->replace_static(
                          result.snapshot->static_data.timestamp_json,
                          result.snapshot->static_data.data_json)
                    : channels::ProviderBackendError::none;
                if (replaced == channels::ProviderBackendError::none) {
                    const bool was_ready = ready.exchange(true, std::memory_order_acq_rel);
                    if (static_changed) {
                        static_data_json = result.snapshot->static_data.data_json;
                        static_published = true;
                    }
                    last_error.store(
                        adapters::FileResourceScanError::none,
                        std::memory_order_release);
                    if (!was_ready) {
                        if (provider->set_initialized(true)
                            != channels::ProviderBackendError::none) {
                            ready.store(false, std::memory_order_release);
                            static_cast<void>(provider->set_initialized(false));
                            return;
                        }
                        static_cast<void>(log({
                            "global", "info",
                            "Runtime resources initialized from project files"}));
                    }
                    return;
                }
            }

            const auto error = result.error == adapters::FileResourceScanError::none
                ? adapters::FileResourceScanError::internal_error : result.error;
            const bool was_ready = ready.exchange(false, std::memory_order_acq_rel);
            const auto prior = last_error.exchange(error, std::memory_order_acq_rel);
            if (was_ready) {
                static_cast<void>(provider->set_initialized(false));
            }
            if (was_ready || prior != error) {
                static_cast<void>(log({
                    "global", "error",
                    "Runtime resource scan failed: "
                        + std::string{scan_error_name(error)}}));
            }
        }
    };

    Impl(std::shared_ptr<adapters::FileResourceStore> resources,
         std::shared_ptr<ProductionProviderBackend> provider,
         ServiceRuntimeProviderBridgeDependencies dependencies,
         ServiceRuntimeProviderBridgeLimits limits)
        : core(std::make_shared<Core>())
    {
        if (!resources || !provider || limits.max_scope_bytes == 0
            || limits.max_level_bytes == 0 || limits.max_message_bytes == 0) {
            throw std::invalid_argument("invalid runtime provider bridge configuration");
        }
        core->provider = std::move(provider);
        core->clock = dependencies.clock
            ? std::move(dependencies.clock) : system_clock_ms;
        core->limits = limits;
        std::weak_ptr<Core> weak = core;
        watcher = std::make_unique<adapters::FileResourceWatcher>(
            std::move(resources),
            [weak](const adapters::FileResourceScanResult& result) {
                if (const auto current = weak.lock()) current->scan_completed(result);
            },
            std::move(dependencies.watcher));
    }

    std::shared_ptr<Core> core;
    std::unique_ptr<adapters::FileResourceWatcher> watcher;
    std::recursive_mutex lifecycle_mutex;
    bool started{};
    bool stopped{};
};

std::string_view service_runtime_provider_bridge_error_name(
    const ServiceRuntimeProviderBridgeError error) noexcept
{
    using enum ServiceRuntimeProviderBridgeError;
    switch (error) {
        case none: return "none";
        case already_started: return "already_started";
        case watcher_start_failed: return "watcher_start_failed";
        case initial_data_unavailable: return "initial_data_unavailable";
        case internal_error: return "internal_error";
    }
    return "unknown";
}

ServiceRuntimeProviderBridge::ServiceRuntimeProviderBridge(
    std::shared_ptr<adapters::FileResourceStore> resources,
    std::shared_ptr<ProductionProviderBackend> provider,
    ServiceRuntimeProviderBridgeDependencies dependencies,
    ServiceRuntimeProviderBridgeLimits limits)
    : impl_(std::make_unique<Impl>(
          std::move(resources), std::move(provider),
          std::move(dependencies), limits))
{}

ServiceRuntimeProviderBridge::~ServiceRuntimeProviderBridge()
{
    stop();
}

ServiceRuntimeProviderBridgeError ServiceRuntimeProviderBridge::start() noexcept
{
    std::lock_guard lock(impl_->lifecycle_mutex);
    if (impl_->started || impl_->stopped) {
        return ServiceRuntimeProviderBridgeError::already_started;
    }
    impl_->started = true;
    impl_->core->accepting.store(true, std::memory_order_release);
    static_cast<void>(impl_->core->provider->set_initialized(false));
    if (impl_->stopped) {
        impl_->core->accepting.store(false, std::memory_order_release);
        return ServiceRuntimeProviderBridgeError::internal_error;
    }
    const auto result = impl_->watcher->start();
    if (!result.started) {
        impl_->core->ready.store(false, std::memory_order_release);
        static_cast<void>(impl_->core->provider->set_initialized(false));
        impl_->core->accepting.store(false, std::memory_order_release);
        return ServiceRuntimeProviderBridgeError::watcher_start_failed;
    }
    return result.initial_scan_ready
        ? ServiceRuntimeProviderBridgeError::none
        : ServiceRuntimeProviderBridgeError::initial_data_unavailable;
}

void ServiceRuntimeProviderBridge::stop() noexcept
{
    {
        std::lock_guard lock(impl_->lifecycle_mutex);
        if (!impl_->stopped) {
            impl_->stopped = true;
            // Linearize admission before cancellation. A callback that has not
            // entered its Core gate can no longer publish logs or initialized=true.
            impl_->core->accepting.store(false, std::memory_order_release);
        }
    }
    // Never hold the bridge lifecycle gate while joining: downstream Provider
    // callbacks run synchronously and are allowed to re-enter stop().
    impl_->watcher->stop();
    {
        std::lock_guard callback_lock(impl_->core->callback_mutex);
        const bool was_ready =
            impl_->core->ready.exchange(false, std::memory_order_acq_rel);
        if (was_ready) {
            static_cast<void>(impl_->core->provider->set_initialized(false));
        }
    }
}

channels::ProviderBackendError ServiceRuntimeProviderBridge::publish_log(
    ServiceRuntimeLogEntry entry) noexcept
{
    const auto core = impl_->core;
    return core->log(std::move(entry));
}

bool ServiceRuntimeProviderBridge::running() const noexcept
{
    return impl_->watcher->running();
}

bool ServiceRuntimeProviderBridge::initialized() const noexcept
{
    return impl_->core->ready.load(std::memory_order_acquire);
}

}  // namespace baas::service::app
