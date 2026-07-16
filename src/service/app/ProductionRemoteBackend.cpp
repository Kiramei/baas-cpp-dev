#include "service/app/ProductionRemoteBackend.h"

#include "service/auth/CanonicalJson.h"
#include "service/adb/ServiceAdbSync.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>

namespace baas::service::app {
namespace {

using Json = nlohmann::json;
using adb::AdbTransportError;
using channels::RemoteBackendError;
using channels::RemoteIoStatus;
using channels::RemoteOpenResult;
using channels::RemoteSessionCallbacks;
using channels::RemoteSessionEnd;

constexpr std::uint16_t server_port = 8886;
constexpr std::string_view remote_jar =
    "/data/local/tmp/baas-ws-scrcpy-server.jar";
constexpr std::string_view package = "com.genymobile.scrcpy.Server";
constexpr std::string_view version = "1.19-ws7";
constexpr std::string_view server_type = "web";
constexpr std::string_view log_level = "ERROR";
constexpr std::string_view all_interfaces = "true";

class ConcreteAdbClient final : public RemoteAdbClient {
public:
    explicit ConcreteAdbClient(std::shared_ptr<adb::ServiceAdbTransport> transport)
        : transport_(std::move(transport)), sync_(*transport_)
    {}

    adb::AdbTransportResult<std::string> get_state(
        const std::string_view serial, const std::stop_token stop) override
    { return transport_->get_state(serial, stop); }

    adb::AdbTransportResult<std::string> shell(
        const std::string_view serial, const std::string_view command,
        const std::stop_token stop) override
    { return transport_->shell_legacy(serial, command, stop); }

    adb::AdbTransportResult<std::uint64_t> push_file(
        const std::string_view serial, const std::string_view path,
        const std::filesystem::path& local, const std::stop_token stop) override
    { return sync_.push_file(serial, path, local, 0644, 0, stop); }

    adb::AdbTransportResult<std::vector<adb::AdbForwardItem>> list_forwards(
        const std::stop_token stop) override
    { return transport_->list_forwards(stop); }

    adb::AdbTransportResult<std::uint16_t> forward_tcp_zero(
        const std::string_view serial, const std::uint16_t port,
        const std::stop_token stop) override
    { return transport_->forward_tcp_zero(serial, port, stop); }

    adb::AdbTransportResult<bool> remove_tcp_forward(
        const std::string_view serial, const std::uint16_t port,
        const std::stop_token stop) override
    { return transport_->remove_tcp_forward(serial, port, stop); }

    void stop() noexcept override { transport_->stop(); }

private:
    std::shared_ptr<adb::ServiceAdbTransport> transport_;
    adb::ServiceAdbSync sync_;
};

void set_timeout(httplib::ws::WebSocketClient& client,
                 const std::chrono::milliseconds timeout,
                 void (httplib::ws::WebSocketClient::*setter)(time_t, time_t))
{
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        timeout - seconds);
    (client.*setter)(static_cast<time_t>(seconds.count()),
                     static_cast<time_t>(micros.count()));
}

class HttplibRemoteWebSocketClient final : public RemoteWebSocketClient {
public:
    HttplibRemoteWebSocketClient(
        std::string url, const std::chrono::milliseconds connect_timeout,
        const std::chrono::milliseconds write_timeout)
        : client_(std::move(url))
    {
        set_timeout(client_, connect_timeout,
                    &httplib::ws::WebSocketClient::set_connection_timeout);
        set_timeout(client_, write_timeout,
                    &httplib::ws::WebSocketClient::set_write_timeout);
        client_.set_websocket_ping_interval(0);
        client_.set_tcp_nodelay(true);
    }

    bool connect() override { return client_.is_valid() && client_.connect(); }

    RemoteWebSocketReadResult read() override
    {
        std::string payload;
        const auto result = client_.read(payload);
        if (result == httplib::ws::Binary)
            return {RemoteWebSocketReadKind::binary, std::move(payload)};
        if (result == httplib::ws::Text)
            return {RemoteWebSocketReadKind::text, std::move(payload)};
        return {client_.is_open() ? RemoteWebSocketReadKind::error
                                  : RemoteWebSocketReadKind::closed, {}};
    }

    bool send_binary(const std::span<const std::byte> bytes) override
    {
        return client_.send(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    bool request_close() noexcept override { return client_.request_close(); }
    void interrupt() noexcept override { client_.interrupt(); }

private:
    httplib::ws::WebSocketClient client_;
};

std::optional<std::uint16_t> parse_port(const std::string_view value)
{
    if (value.empty()) return std::nullopt;
    unsigned port{};
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size()
        || port == 0 || port > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(port);
}

bool valid_serial_component(const std::string_view value) noexcept
{
    return !value.empty() && value.size() <= 256
        && value.find('\0') == std::string_view::npos
        && value.find(';') == std::string_view::npos
        && std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
            return byte >= 0x21U && byte <= 0x7eU;
        });
}

struct DeviceConfig { std::string serial; };

std::optional<DeviceConfig> parse_device_config(
    const std::string_view bytes, const std::size_t maximum_bytes)
{
    if (bytes.empty() || bytes.size() > maximum_bytes
        || !auth::is_valid_utf8(bytes)) return std::nullopt;
    bool duplicate{};
    std::size_t nodes{};
    std::vector<std::unordered_set<std::string>> keys;
    const auto callback = [&](int depth, const Json::parse_event_t event,
                              Json& parsed) {
        if (depth > 64 || ++nodes > 65'536) return false;
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::key && !keys.empty()) {
            if (!keys.back().insert(parsed.get<std::string>()).second)
                duplicate = true;
        }
        else if (event == Json::parse_event_t::object_end && !keys.empty())
            keys.pop_back();
        return !duplicate;
    };
    const auto value = Json::parse(bytes, callback, false);
    if (duplicate || value.is_discarded() || !value.is_object()) return std::nullopt;
    const auto ip_it = value.find("adbIP");
    const auto port_it = value.find("adbPort");
    if (ip_it == value.end() || port_it == value.end()
        || !ip_it->is_string()
        || !(port_it->is_string() || port_it->is_number_unsigned()
             || port_it->is_number_integer())) return std::nullopt;
    const auto ip = ip_it->get<std::string>();
    std::string port;
    try {
        if (port_it->is_string()) port = port_it->get<std::string>();
        else if (port_it->is_number_unsigned()) {
            const auto number = port_it->get<std::uint64_t>();
            if (number > 65'535) return std::nullopt;
            port = std::to_string(number);
        }
        else {
            const auto number = port_it->get<std::int64_t>();
            if (number < 0 || number > 65'535) return std::nullopt;
            port = std::to_string(number);
        }
    }
    catch (...) { return std::nullopt; }
    if (ip.empty() && port.empty()) return std::nullopt;
    std::string serial;
    if (ip.empty()) serial = port;
    else if (port.empty()) serial = ip;
    else {
        if (!parse_port(port)) return std::nullopt;
        serial = ip + ':' + port;
    }
    if (!valid_serial_component(serial)) return std::nullopt;
    return DeviceConfig{std::move(serial)};
}

std::optional<unsigned> parse_pid(const std::string_view text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return std::nullopt;
    const auto end = text.find_first_of(" \t\r\n", begin);
    const auto token = text.substr(begin, end == std::string_view::npos
        ? text.size() - begin : end - begin);
    unsigned pid{};
    const auto [parsed, error] = std::from_chars(
        token.data(), token.data() + token.size(), pid);
    if (error != std::errc{} || parsed != token.data() + token.size() || pid == 0)
        return std::nullopt;
    return pid;
}

bool expected_cmdline(const std::string_view bytes)
{
    std::vector<std::string_view> fields;
    std::size_t offset{};
    while (offset < bytes.size()) {
        auto end = bytes.find('\0', offset);
        if (end == std::string_view::npos) end = bytes.size();
        if (end != offset) fields.emplace_back(bytes.substr(offset, end - offset));
        offset = end + 1;
    }
    const std::array expected{
        package, version, server_type, log_level,
        std::string_view{"8886"}, all_interfaces};
    const auto found = std::find(fields.begin(), fields.end(), package);
    return found != fields.end()
        && static_cast<std::size_t>(fields.end() - found) == expected.size()
        && std::equal(expected.begin(), expected.end(), found);
}

RemoteBackendError map_resource_error(const channels::ResourceStoreError error)
{
    switch (error) {
        case channels::ResourceStoreError::not_found: return RemoteBackendError::not_found;
        case channels::ResourceStoreError::capacity: return RemoteBackendError::capacity;
        case channels::ResourceStoreError::invalid_data: return RemoteBackendError::invalid_config;
        case channels::ResourceStoreError::none:
        case channels::ResourceStoreError::internal_error:
            return RemoteBackendError::internal_error;
    }
    return RemoteBackendError::internal_error;
}

RemoteIoStatus map_send_failure() noexcept { return RemoteIoStatus::internal_error; }

}  // namespace

namespace {

class ReapedReader {
public:
    virtual ~ReapedReader() = default;
    virtual void reap_reader() noexcept = 0;
private:
    friend class ReaderThreadReaper;
    ReapedReader* reaper_next_{};
};

class ReaderThreadReaper final {
public:
    static ReaderThreadReaper& instance()
    {
        static ReaderThreadReaper reaper;
        return reaper;
    }

    void enqueue(ReapedReader* reader) noexcept
    {
        std::lock_guard lock{mutex_};
        if (tail_) tail_->reaper_next_ = reader;
        else head_ = reader;
        tail_ = reader;
        ready_.notify_one();
    }

private:
    ReaderThreadReaper() : worker_([this] { run(); }) {}
    ~ReaderThreadReaper()
    {
        {
            std::lock_guard lock{mutex_};
            stopping_ = true;
        }
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    void run() noexcept
    {
        for (;;) {
            ReapedReader* item{};
            {
                std::unique_lock lock{mutex_};
                ready_.wait(lock, [this] { return stopping_ || head_; });
                if (stopping_ && !head_) return;
                item = head_;
                head_ = head_->reaper_next_;
                if (!head_) tail_ = nullptr;
                item->reaper_next_ = nullptr;
            }
            item->reap_reader();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    ReapedReader* head_{};
    ReapedReader* tail_{};
    bool stopping_{};
    std::thread worker_;
};

class ProductionRemoteSessionCore final
    : public ReapedReader,
      public std::enable_shared_from_this<ProductionRemoteSessionCore> {
public:
    using Release = std::function<void(
        const std::string&, std::uint16_t, bool, std::optional<unsigned>)>;

    ProductionRemoteSessionCore(
        std::string serial, std::unique_ptr<RemoteWebSocketClient> websocket,
        RemoteSessionCallbacks callbacks, const std::size_t max_frame,
        const std::uint16_t local_port, const bool owns_forward,
        std::optional<unsigned> owned_pid, Release release)
        : serial_(std::move(serial)), websocket_(std::move(websocket)),
          callbacks_(std::move(callbacks)), max_frame_(max_frame),
          local_port_(local_port), owns_forward_(owns_forward),
          owned_pid_(owned_pid), release_(std::move(release))
    {}

    ~ProductionRemoteSessionCore() { close(); }

    void start()
    {
        // Construct the allocation-free reaper before a reader exists. If its
        // worker cannot be created, open fails while close still has no thread.
        static_cast<void>(ReaderThreadReaper::instance());
        const auto self = shared_from_this();
        std::scoped_lock lock{gate_, reader_mutex_};
        reader_ = std::thread([self] { self->read_loop(); });
        reader_id_ = reader_.get_id();
        reader_started_ = true;
    }

    RemoteIoStatus send(auth::SecretBuffer payload, const std::stop_token stop)
    {
        if (stop.stop_requested() || !enter_send()) return RemoteIoStatus::closed;
        struct Leave { ProductionRemoteSessionCore* self; ~Leave() { self->leave(); } } leave{this};
        if (stop.stop_requested()) return RemoteIoStatus::closed;
        try {
            std::lock_guard send_lock{send_mutex_};
            return websocket_->send_binary(payload.bytes())
                ? RemoteIoStatus::accepted : map_send_failure();
        }
        catch (...) { return RemoteIoStatus::internal_error; }
    }

    void close() noexcept
    {
        bool first{};
        bool reentrant{};
        {
            std::lock_guard lock{gate_};
            reentrant = reader_started_
                && reader_id_ == std::this_thread::get_id();
            if (reentrant) reentrant_close_ = true;
            if (!closing_) { closing_ = true; first = true; }
        }
        if (first) {
            try { static_cast<void>(websocket_->request_close()); } catch (...) {}
            try { websocket_->interrupt(); } catch (...) {}
        }
        // A callback cannot wait for itself to return. Its reader transfers its
        // join handle to ReaderThreadReaper on exit; every external close still
        // waits for the join and complete cleanup barrier below.
        if (reentrant) return;

        std::unique_lock lifecycle{close_mutex_};
        if (close_complete_) return;
        std::thread reader;
        bool started{};
        {
            std::lock_guard lock{reader_mutex_};
            started = reader_started_;
            if (reader_.joinable()) reader = std::move(reader_);
        }
        if (reader.joinable()) {
            reader.join();
            finish_close_locked();
        }
        else if (!started) {
            finish_close_locked();
        }
        else {
            close_completed_.wait(lifecycle, [this] { return close_complete_; });
        }
    }

    void reap_reader() noexcept override
    {
        std::thread reader;
        {
            std::lock_guard lock{reader_mutex_};
            if (reader_.joinable()) reader = std::move(reader_);
        }
        if (reader.joinable()) reader.join();
        {
            std::lock_guard lifecycle{close_mutex_};
            finish_close_locked();
        }
        std::shared_ptr<ProductionRemoteSessionCore> keepalive;
        {
            std::lock_guard lock{reader_mutex_};
            keepalive = std::move(reaper_keepalive_);
        }
    }

private:
    bool enter_send() noexcept
    {
        std::lock_guard lock{gate_};
        if (closing_ || reader_ended_) return false;
        ++active_;
        return true;
    }

    bool enter_callback() noexcept
    {
        std::lock_guard lock{gate_};
        if (closing_) return false;
        ++active_;
        return true;
    }

    void leave() noexcept
    {
        std::lock_guard lock{gate_};
        if (active_ != 0) --active_;
        if (closing_ && active_ == 0) drained_.notify_all();
    }

    void read_loop() noexcept
    {
        struct Exit final {
            ProductionRemoteSessionCore* self;
            ~Exit() { self->reader_exiting(); }
        } exit{this};
        RemoteSessionEnd reason = RemoteSessionEnd::device_closed;
        for (;;) {
            RemoteWebSocketReadResult result;
            try { result = websocket_->read(); }
            catch (...) { result.kind = RemoteWebSocketReadKind::error; }
            if (result.kind == RemoteWebSocketReadKind::closed) break;
            if (result.kind == RemoteWebSocketReadKind::error) {
                reason = RemoteSessionEnd::internal_error;
                break;
            }
            if (result.payload.size() > max_frame_) {
                reason = RemoteSessionEnd::capacity;
                break;
            }
            if (!enter_callback()) return;
            RemoteIoStatus delivered{RemoteIoStatus::internal_error};
            try { delivered = callbacks_.device_bytes(std::move(result.payload)); }
            catch (...) { delivered = RemoteIoStatus::internal_error; }
            leave();
            if (delivered != RemoteIoStatus::accepted) {
                reason = delivered == RemoteIoStatus::capacity
                    ? RemoteSessionEnd::capacity
                    : delivered == RemoteIoStatus::closed
                        ? RemoteSessionEnd::clean
                        : RemoteSessionEnd::internal_error;
                break;
            }
        }
        bool notify{};
        {
            std::lock_guard lock{gate_};
            if (!closing_) {
                reader_ended_ = true;
                ++active_;
                notify = true;
            }
        }
        if (notify) {
            try { callbacks_.ended(reason); } catch (...) {}
            leave();
        }
    }

    void reader_exiting() noexcept
    {
        {
            std::lock_guard lock{gate_};
            if (!reentrant_close_) return;
        }
        std::lock_guard lock{reader_mutex_};
        if (!reader_.joinable()
            || reader_.get_id() != std::this_thread::get_id()) return;
        reaper_keepalive_ = shared_from_this();
        ReaderThreadReaper::instance().enqueue(this);
    }

    void finish_close_locked() noexcept
    {
        if (close_complete_) return;
        {
            std::unique_lock lock{gate_};
            drained_.wait(lock, [this] { return active_ == 0; });
        }
        cleanup_once();
        close_complete_ = true;
        close_completed_.notify_all();
    }

    void cleanup_once() noexcept
    {
        if (cleaned_.exchange(true, std::memory_order_acq_rel)) return;
        try { release_(serial_, local_port_, owns_forward_, owned_pid_); }
        catch (...) {}
    }

    std::string serial_;
    std::unique_ptr<RemoteWebSocketClient> websocket_;
    RemoteSessionCallbacks callbacks_;
    std::size_t max_frame_{};
    std::uint16_t local_port_{};
    bool owns_forward_{};
    std::optional<unsigned> owned_pid_;
    Release release_;
    std::mutex gate_;
    std::mutex send_mutex_;
    std::mutex close_mutex_;
    std::mutex reader_mutex_;
    std::condition_variable drained_;
    std::condition_variable close_completed_;
    std::size_t active_{};
    bool closing_{};
    bool reader_ended_{};
    bool reader_started_{};
    bool reentrant_close_{};
    bool close_complete_{};
    std::thread::id reader_id_;
    std::thread reader_;
    std::shared_ptr<ProductionRemoteSessionCore> reaper_keepalive_;
    std::atomic_bool cleaned_{};
};

class ProductionRemoteSession final : public channels::RemoteSession {
public:
    explicit ProductionRemoteSession(std::shared_ptr<ProductionRemoteSessionCore> core)
        : core_(std::move(core)) {}
    ~ProductionRemoteSession() override { close(); }
    RemoteIoStatus send_to_device(
        auth::SecretBuffer payload, const std::stop_token stop) override
    { return core_->send(std::move(payload), stop); }
    void close() noexcept override { core_->close(); }
private:
    std::shared_ptr<ProductionRemoteSessionCore> core_;
};

}  // namespace

class ProductionRemoteBackend::Impl
    : public std::enable_shared_from_this<ProductionRemoteBackend::Impl> {
public:
    struct PendingOpen final {
        Impl* owner;
        const std::string* serial;
        std::uint16_t local_port{};
        bool owns_forward{};
        std::optional<unsigned> owned_pid;
        bool active{true};
        ~PendingOpen()
        {
            if (!active) return;
            owner->cleanup(*serial, local_port, owns_forward, owned_pid);
            owner->unreserve(*serial);
        }
    };

    Impl(ProductionRemoteBackendDependencies dependencies,
         ProductionRemoteBackendLimits limits)
        : dependencies_(std::move(dependencies)), limits_(limits)
    {
        if (!dependencies_.adb) {
            dependencies_.adb = std::make_shared<ConcreteAdbClient>(
                dependencies_.adb_transport);
        }
        if (!dependencies_.websocket_factory) {
            dependencies_.websocket_factory = [](std::string url,
                const std::chrono::milliseconds connect,
                const std::chrono::milliseconds write) {
                return std::make_unique<HttplibRemoteWebSocketClient>(
                    std::move(url), connect, write);
            };
        }
        if (!dependencies_.clock) dependencies_.clock = [] {
            return std::chrono::steady_clock::now();
        };
        if (!dependencies_.sleep) dependencies_.sleep = [](const auto duration) {
            std::this_thread::sleep_for(duration);
        };
    }

    RemoteOpenResult open(
        std::optional<std::string> config_id, RemoteSessionCallbacks callbacks,
        const std::stop_token stop)
    {
        if (!config_id || config_id->empty() || stop.stop_requested())
            return {nullptr, RemoteBackendError::invalid_config};
        auto snapshot = dependencies_.resources->pull(
            {channels::SyncResource::config, *config_id}, stop);
        if (!snapshot) return {nullptr, map_resource_error(snapshot.error)};
        auto config = parse_device_config(snapshot->data_json, limits_.max_config_bytes);
        if (!config) return {nullptr, RemoteBackendError::invalid_config};
        if (!reserve(config->serial)) return {nullptr, RemoteBackendError::capacity};
        struct OpenGuard final {
            Impl* owner;
            ~OpenGuard() { owner->finish_open(); }
        } open_guard{this};
        PendingOpen pending{this, &config->serial};

        const auto fail = [](RemoteBackendError error) {
            return RemoteOpenResult{nullptr, error};
        };
        if (stop.stop_requested()) return fail(RemoteBackendError::internal_error);
        const auto state = dependencies_.adb->get_state(config->serial, stop);
        if (!state) return fail(map_adb_error(state.error));
        if (*state.value != "device") return fail(RemoteBackendError::not_found);

        std::optional<unsigned> owned_pid;
        auto server = find_server(config->serial, stop);
        if (server.error != RemoteBackendError::none)
            return fail(server.error);
        if (!server.pid) {
            const auto pushed = dependencies_.adb->push_file(
                config->serial, remote_jar, dependencies_.server_jar, stop);
            if (!pushed) return fail(map_adb_error(pushed.error));
            const auto started = dependencies_.adb->shell(
                config->serial,
                "rm -f /data/local/tmp/ws_scrcpy.pid; "
                "(CLASSPATH=/data/local/tmp/baas-ws-scrcpy-server.jar app_process / "
                "com.genymobile.scrcpy.Server 1.19-ws7 web ERROR 8886 true "
                "> /data/local/tmp/ws_scrcpy.log 2>&1 & pid=$!; echo $pid > "
                "/data/local/tmp/ws_scrcpy.pid; echo $pid)", stop);
            if (!started) {
                // The device shell may have launched the process and written
                // the marker even when its response is lost. Re-read both and
                // only kill an exact ws-scrcpy identity; never infer ownership
                // from an uncertain response alone.
                cleanup_owned_marker(config->serial, std::nullopt);
                return fail(map_adb_error(started.error));
            }
            const auto launched_pid = parse_pid(*started.value);
            if (!launched_pid) {
                cleanup_owned_marker(config->serial, std::nullopt);
                return fail(RemoteBackendError::internal_error);
            }
            const auto ready = wait_for_owned_server(
                config->serial, *launched_pid, stop);
            if (!ready) {
                cleanup(config->serial, 0, false, launched_pid);
                cleanup_owned_marker(config->serial, launched_pid);
                return fail(RemoteBackendError::internal_error);
            }
            owned_pid = ready;
            pending.owned_pid = ready;
        }

        std::uint16_t local_port{};
        bool owns_forward{};
        const auto forwards = dependencies_.adb->list_forwards(stop);
        if (!forwards) return fail(map_adb_error(forwards.error));
        for (const auto& item : *forwards.value) {
            if (item.serial != config->serial || item.remote != "tcp:8886") continue;
            constexpr std::string_view prefix = "tcp:";
            if (!item.local.starts_with(prefix)) continue;
            const auto parsed = parse_port(
                std::string_view{item.local}.substr(prefix.size()));
            if (parsed && (local_port == 0 || *parsed < local_port)) local_port = *parsed;
        }
        if (local_port == 0) {
            const auto forwarded = dependencies_.adb->forward_tcp_zero(
                config->serial, server_port, stop);
            if (!forwarded) return fail(map_adb_error(forwarded.error));
            local_port = *forwarded.value;
            owns_forward = true;
            pending.local_port = local_port;
            pending.owns_forward = true;
        }

        auto websocket = connect_websocket(local_port, stop);
        if (!websocket) return fail(RemoteBackendError::internal_error);

        std::shared_ptr<ProductionRemoteSessionCore> core;
        try {
            const auto weak = weak_from_this();
            core = std::make_shared<ProductionRemoteSessionCore>(
                config->serial, std::move(websocket), std::move(callbacks),
                limits_.max_device_frame_bytes, local_port, owns_forward, owned_pid,
                [weak](const std::string& serial, const std::uint16_t port,
                       const bool own_forward, const std::optional<unsigned> pid) {
                    if (const auto self = weak.lock())
                        self->release(serial, port, own_forward, pid);
                });
            pending.active = false;
            {
                std::lock_guard lock{mutex_};
                if (stopped_) throw std::runtime_error("remote backend stopped");
                sessions_.emplace_back(core);
            }
            core->start();
            return {std::make_unique<ProductionRemoteSession>(std::move(core)),
                    RemoteBackendError::none};
        }
        catch (...) {
            if (core) core->close();
            return {nullptr, RemoteBackendError::internal_error};
        }
    }

    void stop() noexcept
    {
        std::vector<std::shared_ptr<ProductionRemoteSessionCore>> live;
        {
            std::unique_lock lock{mutex_};
            if (stopped_) {
                stop_drained_.wait(lock, [this] { return stop_complete_; });
                return;
            }
            stopped_ = true;
            opens_drained_.wait(lock, [this] { return opening_ == 0; });
            for (auto& weak : sessions_) if (auto item = weak.lock())
                live.emplace_back(std::move(item));
        }
        for (auto& session : live) session->close();
        dependencies_.adb->stop();
        {
            std::lock_guard lock{mutex_};
            stop_complete_ = true;
        }
        stop_drained_.notify_all();
    }

private:
    struct ServerSearch {
        std::optional<unsigned> pid;
        RemoteBackendError error{RemoteBackendError::none};
    };

    bool reserve(const std::string& serial)
    {
        std::lock_guard lock{mutex_};
        if (stopped_ || serials_.contains(serial)
            || serials_.size() >= limits_.max_sessions) return false;
        serials_.emplace(serial);
        ++opening_;
        return true;
    }

    void finish_open() noexcept
    {
        std::lock_guard lock{mutex_};
        if (opening_ != 0) --opening_;
        if (stopped_ && opening_ == 0) opens_drained_.notify_all();
    }

    void unreserve(const std::string& serial) noexcept
    {
        std::lock_guard lock{mutex_};
        serials_.erase(serial);
    }

    static RemoteBackendError map_adb_error(const AdbTransportError error)
    {
        switch (error) {
            case AdbTransportError::capacity: return RemoteBackendError::capacity;
            case AdbTransportError::invalid_argument: return RemoteBackendError::invalid_config;
            case AdbTransportError::none:
            case AdbTransportError::connection_failed:
            case AdbTransportError::timeout:
            case AdbTransportError::cancelled:
            case AdbTransportError::protocol_error:
            case AdbTransportError::adb_fail:
            case AdbTransportError::local_io_error:
            case AdbTransportError::closed:
            case AdbTransportError::internal_error:
                return RemoteBackendError::internal_error;
        }
        return RemoteBackendError::internal_error;
    }

    bool is_expected_pid(const std::string& serial, const unsigned pid,
                         const std::stop_token stop)
    {
        const auto command = "cat /proc/" + std::to_string(pid) + "/cmdline";
        const auto result = dependencies_.adb->shell(serial, command, stop);
        return result && expected_cmdline(*result.value);
    }

    ServerSearch find_server(const std::string& serial, const std::stop_token stop)
    {
        const auto result = dependencies_.adb->shell(serial, "ps -A -o PID,ARGS", stop);
        if (!result) return {{}, map_adb_error(result.error)};
        std::size_t candidates{};
        std::size_t offset{};
        while (offset < result->size()) {
            auto end = result->find('\n', offset);
            if (end == std::string::npos) end = result->size();
            const std::string_view line{result->data() + offset, end - offset};
            offset = end + 1;
            if (line.find(package) == std::string_view::npos
                || line.find(version) == std::string_view::npos) continue;
            if (++candidates > limits_.max_process_candidates)
                return {{}, RemoteBackendError::capacity};
            const auto pid = parse_pid(line);
            if (pid && is_expected_pid(serial, *pid, stop)) return {*pid, {}};
        }
        return {};
    }

    std::optional<unsigned> wait_for_owned_server(
        const std::string& serial, const unsigned launched_pid,
        const std::stop_token stop)
    {
        const auto deadline = dependencies_.clock() + limits_.startup_timeout;
        while (!stop.stop_requested() && dependencies_.clock() < deadline) {
            const auto result = dependencies_.adb->shell(
                serial, "cat /data/local/tmp/ws_scrcpy.pid", stop);
            if (result) {
                const auto pid = parse_pid(*result.value);
                if (pid && *pid == launched_pid
                    && is_expected_pid(serial, *pid, stop)) return pid;
            }
            dependencies_.sleep(limits_.startup_poll_interval);
        }
        return std::nullopt;
    }

    std::unique_ptr<RemoteWebSocketClient> connect_websocket(
        const std::uint16_t local_port, const std::stop_token stop)
    {
        const auto deadline = dependencies_.clock() + limits_.startup_timeout;
        for (;;) {
            const auto now = dependencies_.clock();
            if (stop.stop_requested() || now >= deadline) break;
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now);
            if (remaining.count() <= 0) remaining = std::chrono::milliseconds{1};
            const auto connect_timeout = (std::min)(
                limits_.websocket_connect_timeout, remaining);
            std::unique_ptr<RemoteWebSocketClient> websocket;
            try {
                websocket = dependencies_.websocket_factory(
                    "ws://127.0.0.1:" + std::to_string(local_port) + '/',
                    connect_timeout,
                    limits_.websocket_write_timeout);
                if (websocket && websocket->connect()) return websocket;
            }
            catch (...) {}
            if (stop.stop_requested()) break;
            dependencies_.sleep(limits_.startup_poll_interval);
        }
        return {};
    }

    void cleanup(const std::string& serial, const std::uint16_t local_port,
                 const bool owns_forward,
                 const std::optional<unsigned> owned_pid) noexcept
    {
        try {
            if (owns_forward && local_port != 0) {
                const auto forwards = dependencies_.adb->list_forwards({});
                if (forwards && std::any_of(forwards->begin(), forwards->end(),
                    [&](const adb::AdbForwardItem& item) {
                        return item.serial == serial
                            && item.local == "tcp:" + std::to_string(local_port)
                            && item.remote == "tcp:8886";
                    })) {
                    static_cast<void>(dependencies_.adb->remove_tcp_forward(
                        serial, local_port, {}));
                }
            }
            if (owned_pid && is_expected_pid(serial, *owned_pid, {})) {
                static_cast<void>(dependencies_.adb->shell(
                    serial, "kill " + std::to_string(*owned_pid), {}));
                const auto marker = dependencies_.adb->shell(
                    serial, "cat /data/local/tmp/ws_scrcpy.pid", {});
                if (marker && parse_pid(*marker.value) == owned_pid
                    && !is_expected_pid(serial, *owned_pid, {})) {
                    static_cast<void>(dependencies_.adb->shell(
                        serial, "rm -f /data/local/tmp/ws_scrcpy.pid", {}));
                }
            }
        }
        catch (...) {}
    }

    void cleanup_owned_marker(
        const std::string& serial,
        const std::optional<unsigned> expected_pid) noexcept
    {
        try {
            const auto marker = dependencies_.adb->shell(
                serial, "cat /data/local/tmp/ws_scrcpy.pid", {});
            if (!marker) return;
            const auto pid = parse_pid(*marker.value);
            if (!pid || (expected_pid && *pid != *expected_pid)
                || !is_expected_pid(serial, *pid, {})) return;
            static_cast<void>(dependencies_.adb->shell(
                serial, "kill " + std::to_string(*pid), {}));
            const auto after = dependencies_.adb->shell(
                serial, "cat /data/local/tmp/ws_scrcpy.pid", {});
            if (after && parse_pid(*after.value) == pid
                && !is_expected_pid(serial, *pid, {})) {
                static_cast<void>(dependencies_.adb->shell(
                    serial, "rm -f /data/local/tmp/ws_scrcpy.pid", {}));
            }
        }
        catch (...) {}
    }

    void release(const std::string& serial, const std::uint16_t local_port,
                 const bool owns_forward,
                 const std::optional<unsigned> owned_pid) noexcept
    {
        cleanup(serial, local_port, owns_forward, owned_pid);
        unreserve(serial);
        std::lock_guard lock{mutex_};
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->expired()) it = sessions_.erase(it); else ++it;
        }
    }

    ProductionRemoteBackendDependencies dependencies_;
    ProductionRemoteBackendLimits limits_;
    std::mutex mutex_;
    std::condition_variable opens_drained_;
    std::condition_variable stop_drained_;
    bool stopped_{};
    bool stop_complete_{};
    std::size_t opening_{};
    std::unordered_set<std::string> serials_;
    std::vector<std::weak_ptr<ProductionRemoteSessionCore>> sessions_;
};

ProductionRemoteBackend::ProductionRemoteBackend(
    ProductionRemoteBackendDependencies dependencies,
    const ProductionRemoteBackendLimits limits)
{
    if (!dependencies.resources || (!dependencies.adb && !dependencies.adb_transport)
        || dependencies.server_jar.empty() || limits.max_sessions == 0
        || limits.max_config_bytes == 0 || limits.max_device_frame_bytes == 0
        || limits.max_process_candidates == 0 || limits.startup_timeout.count() <= 0
        || limits.startup_poll_interval.count() <= 0
        || limits.websocket_connect_timeout.count() <= 0
        || limits.websocket_write_timeout.count() <= 0) {
        throw std::invalid_argument("invalid production remote backend configuration");
    }
    impl_ = std::make_shared<Impl>(std::move(dependencies), limits);
}

ProductionRemoteBackend::~ProductionRemoteBackend() { stop(); }

RemoteOpenResult ProductionRemoteBackend::open(
    std::optional<std::string> config_id, RemoteSessionCallbacks callbacks,
    const std::stop_token stop)
{
    if (!callbacks.device_bytes || !callbacks.ended)
        return {nullptr, RemoteBackendError::invalid_config};
    try {
        return impl_->open(std::move(config_id), std::move(callbacks), stop);
    }
    catch (...) {
        return {nullptr, RemoteBackendError::internal_error};
    }
}

void ProductionRemoteBackend::stop() noexcept
{
    if (impl_) impl_->stop();
}

}  // namespace baas::service::app
