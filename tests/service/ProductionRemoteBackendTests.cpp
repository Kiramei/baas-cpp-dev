#include "service/app/ProductionRemoteBackend.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <charconv>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace app = baas::service::app;
namespace adb = baas::service::adb;
namespace channels = baas::service::channels;
namespace auth = baas::service::auth;

struct Failure final : std::runtime_error { using std::runtime_error::runtime_error; };
#define CHECK(expr) do { if (!(expr)) throw Failure{std::string{"check failed: "} + #expr}; } while (false)

template <typename T>
adb::AdbTransportResult<T> ok(T value)
{ return {std::move(value), adb::AdbTransportError::none, {}}; }

template <typename T>
adb::AdbTransportResult<T> fail(adb::AdbTransportError error)
{ return {std::nullopt, error, "fake failure"}; }

auth::SecretBuffer secret(const std::string_view value)
{ return auth::SecretBuffer{std::as_bytes(std::span{value.data(), value.size()})}; }

std::string expected_cmdline()
{
    const std::vector<std::string> fields{
        "/system/bin/app_process", "/", "com.genymobile.scrcpy.Server",
        "1.19-ws7", "web", "ERROR", "8886", "true"};
    std::string value;
    for (const auto& field : fields) { value += field; value.push_back('\0'); }
    return value;
}

constexpr std::string_view owner_token =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr std::string_view other_owner_token =
    "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

std::string owner_environ(const std::string_view token)
{
    std::string value{"PATH=/system/bin"};
    value.push_back('\0');
    value += "BAAS_WS_SCRCPY_OWNER=";
    value += token;
    value.push_back('\0');
    return value;
}

class Store final : public channels::ResourceStore {
public:
    std::unordered_map<std::string, std::string> configs;

    channels::ResourceStoreResult<channels::ResourceSnapshot> config_list(
        std::stop_token) override
    { return {{channels::ResourceSnapshot{"0", "[]"}}, {}}; }

    channels::ResourceStoreResult<channels::ResourceSnapshot> pull(
        const channels::ResourceKey& key, std::stop_token) override
    {
        if (key.resource != channels::SyncResource::config || !key.resource_id)
            return {std::nullopt, channels::ResourceStoreError::not_found};
        const auto found = configs.find(*key.resource_id);
        if (found == configs.end())
            return {std::nullopt, channels::ResourceStoreError::not_found};
        return {{channels::ResourceSnapshot{"1", found->second}}, {}};
    }

    channels::ResourceStoreResult<channels::ResourcePatchResult> apply_patch(
        channels::ResourcePatchRequest, std::stop_token) override
    { return {std::nullopt, channels::ResourceStoreError::internal_error}; }

    channels::ResourceSubscribeResult subscribe_updates(UpdateCallback) override
    { return {nullptr, channels::ResourceStoreError::internal_error}; }
};

class FakeAdb final : public app::RemoteAdbClient {
public:
    struct Lease final {
        std::string token;
        unsigned supervisor_pid{201};
        std::uint64_t supervisor_start{1001};
        unsigned child_pid{202};
        std::uint64_t child_start{1002};
    };
    std::string device_state{"device"};
    std::string ps;
    std::unordered_map<unsigned, std::string> cmdlines;
    std::unordered_map<unsigned, std::string> environments;
    std::unordered_map<unsigned, std::uint64_t> start_times;
    std::optional<Lease> lease;
    std::optional<std::string> malicious_lease_target;
    std::vector<adb::AdbForwardItem> forwards;
    std::vector<std::string> commands;
    std::vector<unsigned> killed;
    std::vector<std::uint16_t> removed;
    std::uint16_t allocated_port{32123};
    bool pushed{};
    bool stopped{};
    bool fail_push{};
    bool fail_forward{};
    adb::AdbTransportError start_error{adb::AdbTransportError::none};
    bool matching_start_environment{true};
    bool fail_gate{};
    std::optional<std::string> start_output_override;
    bool block_state{};
    bool state_entered{};
    bool block_first_lease_probe{};
    bool first_lease_probe_entered{};
    bool release_first_lease_probe{};
    std::condition_variable cv;
    mutable std::mutex mutex;

    adb::AdbTransportResult<std::string> get_state(
        std::string_view serial, std::stop_token) override
    {
        std::unique_lock lock{mutex};
        commands.emplace_back("state:" + std::string{serial});
        state_entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return !block_state; });
        return ok(device_state);
    }

    adb::AdbTransportResult<std::string> shell(
        std::string_view serial, std::string_view command, std::stop_token) override
    {
        std::unique_lock lock{mutex};
        commands.emplace_back(std::string{serial} + ':' + std::string{command});
        if (lease && !cmdlines.contains(lease->child_pid)) lease.reset();
        if (command.starts_with("# BAAS_WS_LEASE_PROBE")) {
            std::string response;
            if (malicious_lease_target) response = "BUSY\n";
            else if (!lease) response = "NONE\n";
            else {
                const auto start = start_times.find(lease->supervisor_pid);
                const auto environment = environments.find(lease->supervisor_pid);
                if (start != start_times.end()
                    && start->second == lease->supervisor_start
                    && environment != environments.end()
                    && environment->second == owner_environ(lease->token)) {
                    response = "BUSY\n";
                } else {
                    lease.reset();
                    response = "STALE\n";
                }
            }
            if (block_first_lease_probe) {
                block_first_lease_probe = false;
                first_lease_probe_entered = true;
                cv.notify_all();
                cv.wait(lock, [&] { return release_first_lease_probe; });
            }
            return ok(std::move(response));
        }
        if (command.starts_with("# BAAS_WS_SUPERVISOR_LAUNCH")) {
            constexpr std::string_view prefix = "token=";
            const auto begin = command.find(prefix);
            CHECK(begin != std::string_view::npos);
            const auto token_begin = begin + prefix.size();
            const auto token_end = command.find('\n', token_begin);
            const auto token = std::string{command.substr(
                token_begin, token_end - token_begin)};
            if (malicious_lease_target || lease) return ok(std::string{"BUSY\n"});
            if (fail_gate) return ok(std::string{"ERROR\n"});
            lease = Lease{token};
            cmdlines[lease->child_pid] = expected_cmdline();
            environments[lease->child_pid] = owner_environ(token);
            environments[lease->supervisor_pid] = owner_environ(
                matching_start_environment ? token : other_owner_token);
            start_times[lease->supervisor_pid] = lease->supervisor_start;
            start_times[lease->child_pid] = lease->child_start;
            if (start_error != adb::AdbTransportError::none)
                return fail<std::string>(start_error);
            if (start_output_override) return ok(*start_output_override);
            return ok("OWNED " + token + " "
                + std::to_string(lease->supervisor_pid) + " "
                + std::to_string(lease->supervisor_start) + " "
                + std::to_string(lease->child_pid) + " "
                + std::to_string(lease->child_start) + "\n");
        }
        if (command.starts_with("# BAAS_WS_SUPERVISOR_STOP")) {
            constexpr std::string_view prefix = "expected=";
            const auto begin = command.find(prefix);
            CHECK(begin != std::string_view::npos);
            const auto token_begin = begin + prefix.size();
            const auto token_end = command.find('\n', token_begin);
            const auto token = command.substr(token_begin, token_end - token_begin);
            if (malicious_lease_target) return ok(std::string{"OTHER\n"});
            if (!lease) return ok(std::string{"GONE\n"});
            if (lease->token != token) return ok(std::string{"OTHER\n"});
            const auto start = start_times.find(lease->supervisor_pid);
            const auto environment = environments.find(lease->supervisor_pid);
            if (start == start_times.end() || start->second != lease->supervisor_start
                || environment == environments.end()
                || environment->second != owner_environ(lease->token)) {
                lease.reset();
                return ok(std::string{"STALE\n"});
            }
            const auto child_start = start_times.find(lease->child_pid);
            const auto child_environment = environments.find(lease->child_pid);
            if (child_start != start_times.end()
                && child_start->second == lease->child_start
                && child_environment != environments.end()
                && child_environment->second == owner_environ(lease->token)) {
                killed.emplace_back(lease->child_pid);
                cmdlines.erase(lease->child_pid);
                environments.erase(lease->child_pid);
                start_times.erase(lease->child_pid);
            }
            environments.erase(lease->supervisor_pid);
            start_times.erase(lease->supervisor_pid);
            lease.reset();
            return ok(std::string{"REQUESTED\n"});
        }
        if (command == "ps -A -o PID,ARGS") return ok(ps);
        constexpr std::string_view proc_prefix = "cat /proc/";
        constexpr std::string_view cmdline_suffix = "/cmdline";
        constexpr std::string_view environ_suffix = "/environ";
        constexpr std::string_view stat_suffix = "/stat";
        if (command.starts_with(proc_prefix)
            && (command.ends_with(cmdline_suffix)
                || command.ends_with(environ_suffix)
                || command.ends_with(stat_suffix))) {
            const auto suffix = command.ends_with(cmdline_suffix)
                ? cmdline_suffix : command.ends_with(environ_suffix)
                    ? environ_suffix : stat_suffix;
            const auto token = command.substr(
                proc_prefix.size(), command.size() - proc_prefix.size() - suffix.size());
            unsigned pid{};
            std::from_chars(token.data(), token.data() + token.size(), pid);
            if (suffix == stat_suffix) {
                const auto found = start_times.find(pid);
                if (found == start_times.end())
                    return fail<std::string>(adb::AdbTransportError::adb_fail);
                return ok(std::to_string(pid) + " (fake process) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 "
                    + std::to_string(found->second));
            }
            const auto& values = suffix == cmdline_suffix ? cmdlines : environments;
            const auto found = values.find(pid);
            return found == values.end()
                ? fail<std::string>(adb::AdbTransportError::adb_fail)
                : ok(found->second);
        }
        if (command.starts_with("kill ")) {
            unsigned pid{};
            const auto token = command.substr(5);
            std::from_chars(token.data(), token.data() + token.size(), pid);
            killed.emplace_back(pid);
            cmdlines.erase(pid);
            environments.erase(pid);
            return ok(std::string{});
        }
        return ok(std::string{});
    }

    adb::AdbTransportResult<std::uint64_t> push_file(
        std::string_view, std::string_view path,
        const std::filesystem::path& local, std::stop_token) override
    {
        std::lock_guard lock{mutex};
        CHECK(path == "/data/local/tmp/baas-ws-scrcpy-server.jar");
        CHECK(local.filename() == "scrcpy-server.jar");
        pushed = true;
        return fail_push ? fail<std::uint64_t>(adb::AdbTransportError::local_io_error)
                         : ok<std::uint64_t>(114470);
    }

    adb::AdbTransportResult<std::vector<adb::AdbForwardItem>> list_forwards(
        std::stop_token) override
    { std::lock_guard lock{mutex}; return ok(forwards); }

    adb::AdbTransportResult<std::uint16_t> forward_tcp_zero(
        std::string_view serial, std::uint16_t port, std::stop_token) override
    {
        std::lock_guard lock{mutex};
        if (fail_forward) return fail<std::uint16_t>(adb::AdbTransportError::adb_fail);
        CHECK(port == 8886);
        forwards.push_back({std::string{serial}, "tcp:" + std::to_string(allocated_port), "tcp:8886"});
        return ok(allocated_port);
    }

    adb::AdbTransportResult<bool> remove_tcp_forward(
        std::string_view serial, std::uint16_t port, std::stop_token) override
    {
        std::lock_guard lock{mutex};
        removed.emplace_back(port);
        std::erase_if(forwards, [&](const auto& item) {
            return item.serial == serial && item.local == "tcp:" + std::to_string(port);
        });
        return ok(true);
    }

    void stop() noexcept override { std::lock_guard lock{mutex}; stopped = true; }
};

struct WsState {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<app::RemoteWebSocketReadResult> reads;
    std::vector<std::string> sent;
    std::string url;
    bool connect_ok{true};
    bool interrupted{};
    bool send_block{};
    bool send_entered{};
    unsigned active_sends{};
    unsigned maximum_active_sends{};
    unsigned close_requests{};
};

class FakeWebSocket final : public app::RemoteWebSocketClient {
public:
    explicit FakeWebSocket(std::shared_ptr<WsState> state) : state_(std::move(state)) {}
    bool connect() override { std::lock_guard lock{state_->mutex}; return state_->connect_ok; }
    app::RemoteWebSocketReadResult read() override
    {
        std::unique_lock lock{state_->mutex};
        state_->cv.wait(lock, [&] { return state_->interrupted || !state_->reads.empty(); });
        if (state_->interrupted) return {app::RemoteWebSocketReadKind::closed, {}};
        auto result = std::move(state_->reads.front());
        state_->reads.pop_front();
        return result;
    }
    bool send_binary(std::span<const std::byte> bytes) override
    {
        std::unique_lock lock{state_->mutex};
        ++state_->active_sends;
        state_->maximum_active_sends = (std::max)(
            state_->maximum_active_sends, state_->active_sends);
        struct Exit final {
            WsState* state;
            ~Exit() { --state->active_sends; state->cv.notify_all(); }
        } exit{state_.get()};
        state_->send_entered = true;
        state_->cv.notify_all();
        state_->cv.wait(lock, [&] { return !state_->send_block || state_->interrupted; });
        if (state_->interrupted) return false;
        state_->sent.emplace_back(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    }
    bool request_close() noexcept override
    { std::lock_guard lock{state_->mutex}; ++state_->close_requests; return true; }
    void interrupt() noexcept override
    { std::lock_guard lock{state_->mutex}; state_->interrupted = true; state_->cv.notify_all(); }
private:
    std::shared_ptr<WsState> state_;
};

struct Fixture {
    std::shared_ptr<Store> store{std::make_shared<Store>()};
    std::shared_ptr<FakeAdb> adb{std::make_shared<FakeAdb>()};
    std::vector<std::shared_ptr<WsState>> sockets;
    app::ProductionRemoteBackendLimits limits;
    std::optional<std::string> generated_owner_token{std::string{owner_token}};

    Fixture()
    {
        store->configs["alpha"] = R"({"adbIP":"127.0.0.1","adbPort":"5557"})";
        limits.startup_timeout = 100ms;
        limits.startup_poll_interval = 1ms;
    }

    std::unique_ptr<app::ProductionRemoteBackend> backend()
    {
        app::ProductionRemoteBackendDependencies dependencies;
        dependencies.resources = store;
        dependencies.adb = adb;
        dependencies.server_jar = "resource/ws-scrcpy/scrcpy-server.jar";
        dependencies.websocket_factory = [this](std::string url, auto, auto) {
            auto state = std::make_shared<WsState>();
            state->url = std::move(url);
            sockets.emplace_back(state);
            return std::make_unique<FakeWebSocket>(state);
        };
        dependencies.clock = [] { return std::chrono::steady_clock::now(); };
        dependencies.sleep = [](auto) { std::this_thread::yield(); };
        dependencies.owner_token_factory = [this] {
            return generated_owner_token;
        };
        return std::make_unique<app::ProductionRemoteBackend>(
            std::move(dependencies), limits);
    }
};

struct CallbackLog {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> frames;
    std::vector<channels::RemoteSessionEnd> ends;
    channels::RemoteIoStatus delivery{channels::RemoteIoStatus::accepted};
    channels::RemoteSessionCallbacks callbacks()
    {
        return {
            [this](std::string payload) {
                std::lock_guard lock{mutex};
                frames.emplace_back(std::move(payload));
                cv.notify_all();
                return delivery;
            },
            [this](channels::RemoteSessionEnd end) {
                std::lock_guard lock{mutex};
                ends.emplace_back(end);
                cv.notify_all();
            }};
    }
    bool wait(std::size_t frame_count, std::size_t end_count)
    {
        std::unique_lock lock{mutex};
        return cv.wait_for(lock, 2s, [&] {
            return frames.size() >= frame_count && ends.size() >= end_count;
        });
    }
};

void configure_existing(Fixture& fixture)
{
    fixture.adb->ps = "101 com.genymobile.scrcpy.Server 1.19-ws7 web ERROR 8886 true\n";
    fixture.adb->cmdlines[101] = expected_cmdline();
    fixture.adb->forwards.push_back({"127.0.0.1:5557", "tcp:31000", "tcp:8886"});
}

void exact_configuration_and_no_fallback()
{
    Fixture fixture;
    auto backend = fixture.backend();
    CallbackLog log;
    CHECK(backend->open(std::nullopt, log.callbacks(), {}).error
          == channels::RemoteBackendError::invalid_config);
    CHECK(backend->open(std::string{"missing"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::not_found);
    fixture.store->configs["bad"] = R"({"adbIP":"127.0.0.1","adbPort":"auto"})";
    CHECK(backend->open(std::string{"bad"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::invalid_config);
    fixture.store->configs["nul"] = R"({"adbIP":"evil\u0000serial","adbPort":"5555"})";
    CHECK(backend->open(std::string{"nul"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::invalid_config);
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(fixture.adb->commands.empty());
}

void reuses_exact_server_and_forward_with_ordered_duplex()
{
    Fixture fixture;
    configure_existing(fixture);
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    CHECK(fixture.sockets.size() == 1);
    CHECK(fixture.sockets[0]->url == "ws://127.0.0.1:31000/");
    CHECK(opened.session->send_to_device(secret(std::string{"x\0y", 3}), {})
          == channels::RemoteIoStatus::accepted);
    {
        std::lock_guard lock{fixture.sockets[0]->mutex};
        fixture.sockets[0]->reads.push_back({app::RemoteWebSocketReadKind::text, "first"});
        fixture.sockets[0]->reads.push_back({app::RemoteWebSocketReadKind::binary, std::string{"b\0", 2}});
        fixture.sockets[0]->reads.push_back({app::RemoteWebSocketReadKind::closed, {}});
        fixture.sockets[0]->cv.notify_all();
    }
    CHECK(log.wait(2, 1));
    {
        std::lock_guard lock{log.mutex};
        CHECK(log.frames == std::vector<std::string>({"first", std::string{"b\0", 2}}));
        CHECK(log.ends == std::vector{channels::RemoteSessionEnd::device_closed});
    }
    CHECK(opened.session->send_to_device(secret("late"), {})
          == channels::RemoteIoStatus::closed);
    opened.session->close();
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(!fixture.adb->pushed);
    CHECK(fixture.adb->removed.empty());
    CHECK(fixture.adb->killed.empty());
}

void owns_and_revalidates_cleanup()
{
    Fixture fixture;
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    CHECK(fixture.adb->pushed);
    opened.session->close();
    {
        std::lock_guard lock{fixture.adb->mutex};
        CHECK(fixture.adb->removed == std::vector<std::uint16_t>{32123});
        CHECK(fixture.adb->killed == std::vector<unsigned>{202});
        CHECK(!fixture.adb->lease);
    }
    // The per-serial lease is released only after cleanup finishes.
    configure_existing(fixture);
    auto reopened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(reopened);
    reopened.session->close();
}

void never_kills_reused_pid_identity()
{
    Fixture fixture;
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    {
        std::lock_guard lock{fixture.adb->mutex};
        fixture.adb->start_times[201] = 9'999;
    }
    opened.session->close();
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(fixture.adb->killed.empty());
    CHECK(!fixture.adb->lease);
}

void never_kills_pid_reused_with_a_different_owner_token()
{
    Fixture fixture;
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    {
        std::lock_guard lock{fixture.adb->mutex};
        CHECK(fixture.adb->cmdlines[202] == expected_cmdline());
        fixture.adb->environments[201] = owner_environ(other_owner_token);
    }
    opened.session->close();
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(fixture.adb->killed.empty());
    CHECK(!fixture.adb->lease);
}

void supervisor_never_kills_a_reused_child_pid()
{
    Fixture fixture;
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    {
        std::lock_guard lock{fixture.adb->mutex};
        fixture.adb->start_times[202] = 9'999;
        fixture.adb->environments[202] = owner_environ(other_owner_token);
        fixture.adb->cmdlines[202] = expected_cmdline();
    }
    opened.session->close();
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(fixture.adb->killed.empty());
    CHECK(!fixture.adb->lease);
    CHECK(fixture.adb->cmdlines.contains(202));
}

void second_backend_cannot_reuse_an_owned_server()
{
    Fixture fixture;
    auto first_backend = fixture.backend();
    auto second_backend = fixture.backend();
    CallbackLog log;
    auto opened = first_backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    {
        std::lock_guard lock{fixture.adb->mutex};
        fixture.adb->ps =
            "202 com.genymobile.scrcpy.Server 1.19-ws7 web ERROR 8886 true\n";
    }
    CHECK(second_backend->open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::capacity);
    opened.session->close();
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(fixture.adb->killed == std::vector<unsigned>{202});
    CHECK(!fixture.adb->lease);
}

void stale_none_probe_cannot_reclassify_a_new_owned_server_as_legacy()
{
    Fixture fixture;
    fixture.generated_owner_token = std::string{other_owner_token};
    fixture.adb->block_first_lease_probe = true;
    auto racing_backend = fixture.backend();
    CallbackLog log;
    auto racing_open = std::async(std::launch::async, [&] {
        return racing_backend->open(std::string{"alpha"}, log.callbacks(), {});
    });
    {
        std::unique_lock lock{fixture.adb->mutex};
        CHECK(fixture.adb->cv.wait_for(lock, 2s, [&] {
            return fixture.adb->first_lease_probe_entered;
        }));
    }

    fixture.generated_owner_token = std::string{owner_token};
    auto owning_backend = fixture.backend();
    auto owned = owning_backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(owned);
    {
        std::lock_guard lock{fixture.adb->mutex};
        fixture.adb->ps =
            "202 com.genymobile.scrcpy.Server 1.19-ws7 web ERROR 8886 true\n";
        fixture.adb->release_first_lease_probe = true;
        fixture.adb->cv.notify_all();
    }

    CHECK(racing_open.wait_for(2s) == std::future_status::ready);
    auto raced = racing_open.get();
    CHECK(raced.error == channels::RemoteBackendError::capacity);
    CHECK(!raced.session);
    CHECK(fixture.sockets.size() == 1);

    owned.session->close();
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(fixture.adb->killed == std::vector<unsigned>{202});
    CHECK(!fixture.adb->lease);
}

void owner_token_failure_prevents_launch()
{
    Fixture fixture;
    fixture.generated_owner_token.reset();
    auto backend = fixture.backend();
    CallbackLog log;
    CHECK(backend->open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::internal_error);
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(!fixture.adb->pushed);
    CHECK(!fixture.adb->lease);
    CHECK(std::none_of(
        fixture.adb->commands.begin(), fixture.adb->commands.end(),
        [](const std::string& command) {
            return command.find("CLASSPATH=/data/local/tmp/baas-ws-scrcpy-server.jar")
                != std::string::npos;
        }));
}

void gate_write_failure_never_executes_server()
{
    Fixture fixture;
    fixture.adb->fail_gate = true;
    auto backend = fixture.backend();
    CallbackLog log;
    CHECK(backend->open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::internal_error);
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(!fixture.adb->lease);
    CHECK(!fixture.adb->cmdlines.contains(202));
    CHECK(fixture.adb->killed.empty());
}

void natural_child_exit_cleans_lease_without_kill()
{
    Fixture fixture;
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    {
        std::lock_guard lock{fixture.adb->mutex};
        fixture.adb->cmdlines.erase(202);
        fixture.adb->environments.erase(202);
        fixture.adb->start_times.erase(202);
    }
    opened.session->close();
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(!fixture.adb->lease);
    CHECK(fixture.adb->killed.empty());
}

void stale_lease_recovers_to_legacy_reuse()
{
    Fixture fixture;
    fixture.adb->lease = FakeAdb::Lease{
        std::string{other_owner_token}, 301, 3001, 303, 3003};
    fixture.adb->cmdlines[303] = expected_cmdline();
    fixture.adb->environments[303] = owner_environ(other_owner_token);
    fixture.adb->start_times[303] = 3003;
    fixture.adb->ps =
        "303 com.genymobile.scrcpy.Server 1.19-ws7 web ERROR 8886 true\n";
    fixture.adb->forwards.push_back(
        {"127.0.0.1:5557", "tcp:31000", "tcp:8886"});
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    opened.session->close();
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(!fixture.adb->lease);
    CHECK(fixture.adb->cmdlines.contains(303));
    CHECK(fixture.adb->killed.empty());
}

void malicious_lease_symlink_fails_closed()
{
    Fixture fixture;
    fixture.adb->malicious_lease_target = "../../unrelated";
    auto backend = fixture.backend();
    CallbackLog log;
    CHECK(backend->open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::capacity);
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(fixture.adb->malicious_lease_target == "../../unrelated");
    CHECK(!fixture.adb->pushed);
    CHECK(fixture.adb->killed.empty());
}

void serial_lease_and_close_barrier()
{
    Fixture fixture;
    configure_existing(fixture);
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    CHECK(backend->open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::capacity);
    {
        std::lock_guard lock{fixture.sockets[0]->mutex};
        fixture.sockets[0]->send_block = true;
    }
    auto send = std::async(std::launch::async, [&] {
        return opened.session->send_to_device(secret("blocked"), {});
    });
    {
        std::unique_lock lock{fixture.sockets[0]->mutex};
        CHECK(fixture.sockets[0]->cv.wait_for(lock, 2s, [&] {
            return fixture.sockets[0]->send_entered;
        }));
    }
    auto close = std::async(std::launch::async, [&] { opened.session->close(); });
    auto concurrent_close = std::async(
        std::launch::async, [&] { opened.session->close(); });
    CHECK(close.wait_for(2s) == std::future_status::ready);
    CHECK(concurrent_close.wait_for(2s) == std::future_status::ready);
    close.get();
    concurrent_close.get();
    CHECK(send.wait_for(0s) == std::future_status::ready);
    CHECK(send.get() != channels::RemoteIoStatus::accepted);
    auto reopened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(reopened);
    reopened.session->close();
}

void device_callback_can_reenter_close()
{
    Fixture fixture;
    configure_existing(fixture);
    auto backend = fixture.backend();
    channels::RemoteSession* session{};
    std::mutex mutex;
    std::condition_variable cv;
    bool callback_returned{};
    channels::RemoteSessionCallbacks callbacks{
        [&](std::string) {
            session->close();
            {
                std::lock_guard lock{mutex};
                callback_returned = true;
            }
            cv.notify_all();
            return channels::RemoteIoStatus::accepted;
        },
        [](channels::RemoteSessionEnd) {}};
    auto opened = backend->open(std::string{"alpha"}, std::move(callbacks), {});
    CHECK(opened);
    session = opened.session.get();
    {
        std::lock_guard lock{fixture.sockets[0]->mutex};
        fixture.sockets[0]->reads.push_back(
            {app::RemoteWebSocketReadKind::binary, "close-from-device"});
        fixture.sockets[0]->cv.notify_all();
    }
    {
        std::unique_lock lock{mutex};
        CHECK(cv.wait_for(lock, 2s, [&] { return callback_returned; }));
    }
    auto external = std::async(
        std::launch::async, [&] { opened.session->close(); });
    CHECK(external.wait_for(2s) == std::future_status::ready);
    external.get();
    CallbackLog reopened_log;
    auto reopened = backend->open(
        std::string{"alpha"}, reopened_log.callbacks(), {});
    CHECK(reopened);
    reopened.session->close();
}

void ended_callback_can_reenter_close()
{
    Fixture fixture;
    configure_existing(fixture);
    auto backend = fixture.backend();
    channels::RemoteSession* session{};
    std::mutex mutex;
    std::condition_variable cv;
    bool callback_returned{};
    channels::RemoteSessionCallbacks callbacks{
        [](std::string) { return channels::RemoteIoStatus::accepted; },
        [&](channels::RemoteSessionEnd) {
            session->close();
            {
                std::lock_guard lock{mutex};
                callback_returned = true;
            }
            cv.notify_all();
        }};
    auto opened = backend->open(std::string{"alpha"}, std::move(callbacks), {});
    CHECK(opened);
    session = opened.session.get();
    {
        std::lock_guard lock{fixture.sockets[0]->mutex};
        fixture.sockets[0]->reads.push_back(
            {app::RemoteWebSocketReadKind::closed, {}});
        fixture.sockets[0]->cv.notify_all();
    }
    {
        std::unique_lock lock{mutex};
        CHECK(cv.wait_for(lock, 2s, [&] { return callback_returned; }));
    }
    auto external = std::async(
        std::launch::async, [&] { opened.session->close(); });
    CHECK(external.wait_for(2s) == std::future_status::ready);
    external.get();
}

void serializes_concurrent_websocket_sends()
{
    Fixture fixture;
    configure_existing(fixture);
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    {
        std::lock_guard lock{fixture.sockets[0]->mutex};
        fixture.sockets[0]->send_block = true;
    }
    auto first = std::async(std::launch::async, [&] {
        return opened.session->send_to_device(secret("one"), {});
    });
    {
        std::unique_lock lock{fixture.sockets[0]->mutex};
        CHECK(fixture.sockets[0]->cv.wait_for(lock, 2s, [&] {
            return fixture.sockets[0]->send_entered;
        }));
        fixture.sockets[0]->send_entered = false;
    }
    auto second = std::async(std::launch::async, [&] {
        return opened.session->send_to_device(secret("two"), {});
    });
    std::this_thread::sleep_for(20ms);
    {
        std::lock_guard lock{fixture.sockets[0]->mutex};
        CHECK(fixture.sockets[0]->active_sends == 1);
        fixture.sockets[0]->send_block = false;
        fixture.sockets[0]->cv.notify_all();
    }
    CHECK(first.get() == channels::RemoteIoStatus::accepted);
    CHECK(second.get() == channels::RemoteIoStatus::accepted);
    {
        std::lock_guard lock{fixture.sockets[0]->mutex};
        CHECK(fixture.sockets[0]->maximum_active_sends == 1);
        CHECK(fixture.sockets[0]->sent.size() == 2);
    }
    opened.session->close();
}

void retries_listener_and_cleans_invalid_start_marker()
{
    Fixture fixture;
    configure_existing(fixture);
    std::atomic_uint attempts{};
    app::ProductionRemoteBackendDependencies dependencies;
    dependencies.resources = fixture.store;
    dependencies.adb = fixture.adb;
    dependencies.server_jar = "resource/ws-scrcpy/scrcpy-server.jar";
    dependencies.websocket_factory = [&attempts](std::string, auto, auto) {
        auto state = std::make_shared<WsState>();
        state->connect_ok = ++attempts >= 3;
        return std::make_unique<FakeWebSocket>(state);
    };
    dependencies.clock = [] { return std::chrono::steady_clock::now(); };
    dependencies.sleep = [](auto) { std::this_thread::yield(); };
    app::ProductionRemoteBackend backend{std::move(dependencies), fixture.limits};
    CallbackLog log;
    auto opened = backend.open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    CHECK(attempts == 3);
    opened.session->close();

    Fixture invalid;
    invalid.adb->start_output_override = std::string{};
    auto invalid_backend = invalid.backend();
    CHECK(invalid_backend->open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::internal_error);
    std::lock_guard lock{invalid.adb->mutex};
    CHECK(invalid.adb->killed == std::vector<unsigned>{202});
    CHECK(!invalid.adb->lease);
}

void startup_deadline_and_forward_failure_unwind_owned_process()
{
    Fixture timeout;
    app::ProductionRemoteBackendDependencies dependencies;
    dependencies.resources = timeout.store;
    dependencies.adb = timeout.adb;
    dependencies.server_jar = "resource/ws-scrcpy/scrcpy-server.jar";
    dependencies.websocket_factory = [](std::string, auto, auto) {
        return std::make_unique<FakeWebSocket>(std::make_shared<WsState>());
    };
    const auto epoch = std::chrono::steady_clock::time_point{};
    std::atomic_uint clock_calls{};
    dependencies.clock = [epoch, &clock_calls] {
        return ++clock_calls == 1 ? epoch : epoch + 1s;
    };
    dependencies.sleep = [](auto) {};
    app::ProductionRemoteBackend timeout_backend{
        std::move(dependencies), timeout.limits};
    CallbackLog log;
    CHECK(timeout_backend.open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::internal_error);
    {
        std::lock_guard lock{timeout.adb->mutex};
        CHECK(timeout.adb->killed == std::vector<unsigned>{202});
        CHECK(!timeout.adb->lease);
    }

    Fixture forward;
    forward.adb->fail_forward = true;
    auto forward_backend = forward.backend();
    CHECK(forward_backend->open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::internal_error);
    std::lock_guard lock{forward.adb->mutex};
    CHECK(forward.adb->killed == std::vector<unsigned>{202});
    CHECK(forward.adb->removed.empty());
}

void lost_start_response_cleans_only_exact_owned_marker()
{
    CallbackLog log;
    Fixture matching;
    matching.adb->start_error = adb::AdbTransportError::timeout;
    auto matching_backend = matching.backend();
    CHECK(matching_backend->open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::internal_error);
    {
        std::lock_guard lock{matching.adb->mutex};
        CHECK(matching.adb->killed == std::vector<unsigned>{202});
        CHECK(!matching.adb->lease);
    }

    Fixture unrelated;
    unrelated.adb->start_error = adb::AdbTransportError::timeout;
    unrelated.adb->matching_start_environment = false;
    auto unrelated_backend = unrelated.backend();
    CHECK(unrelated_backend->open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::internal_error);
    std::lock_guard lock{unrelated.adb->mutex};
    CHECK(unrelated.adb->killed.empty());
    CHECK(!unrelated.adb->lease);
}

void oversize_and_failed_open_cleanup()
{
    Fixture fixture;
    fixture.limits.max_device_frame_bytes = 4;
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    {
        std::lock_guard lock{fixture.sockets[0]->mutex};
        fixture.sockets[0]->reads.push_back({app::RemoteWebSocketReadKind::binary, "12345"});
        fixture.sockets[0]->cv.notify_all();
    }
    CHECK(log.wait(0, 1));
    {
        std::lock_guard lock{log.mutex};
        CHECK(log.frames.empty());
        CHECK(log.ends == std::vector{channels::RemoteSessionEnd::capacity});
    }
    opened.session->close();

    Fixture failed;
    // Factory state is created during open; force failure by replacing factory
    // after constructing a dedicated backend.
    app::ProductionRemoteBackendDependencies dependencies;
    dependencies.resources = failed.store;
    dependencies.adb = failed.adb;
    dependencies.server_jar = "resource/ws-scrcpy/scrcpy-server.jar";
    auto ws = std::make_shared<WsState>();
    ws->connect_ok = false;
    dependencies.websocket_factory = [ws](std::string, auto, auto) {
        return std::make_unique<FakeWebSocket>(ws);
    };
    dependencies.clock = [] { return std::chrono::steady_clock::now(); };
    dependencies.sleep = [](auto) { std::this_thread::yield(); };
    app::ProductionRemoteBackend failed_backend{std::move(dependencies), failed.limits};
    CHECK(failed_backend.open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::internal_error);
    std::lock_guard lock{failed.adb->mutex};
    CHECK(failed.adb->removed == std::vector<std::uint16_t>{32123});
    CHECK(failed.adb->killed == std::vector<unsigned>{202});
}

void stop_closes_sessions_and_transport()
{
    Fixture fixture;
    configure_existing(fixture);
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    backend->stop();
    {
        std::lock_guard lock{fixture.adb->mutex};
        CHECK(fixture.adb->stopped);
    }
    CHECK(opened.session->send_to_device(secret("late"), {})
          == channels::RemoteIoStatus::closed);
    CHECK(backend->open(std::string{"alpha"}, log.callbacks(), {}).error
          == channels::RemoteBackendError::capacity);
}

void stop_linearizes_with_inflight_open()
{
    Fixture fixture;
    {
        std::lock_guard lock{fixture.adb->mutex};
        fixture.adb->block_state = true;
    }
    auto backend = fixture.backend();
    CallbackLog log;
    auto opening = std::async(std::launch::async, [&] {
        return backend->open(std::string{"alpha"}, log.callbacks(), {});
    });
    {
        std::unique_lock lock{fixture.adb->mutex};
        CHECK(fixture.adb->cv.wait_for(lock, 2s, [&] {
            return fixture.adb->state_entered;
        }));
    }
    auto stopping = std::async(std::launch::async, [&] { backend->stop(); });
    auto concurrent_stopping = std::async(
        std::launch::async, [&] { backend->stop(); });
    CHECK(stopping.wait_for(50ms) == std::future_status::timeout);
    CHECK(concurrent_stopping.wait_for(50ms) == std::future_status::timeout);
    {
        std::lock_guard lock{fixture.adb->mutex};
        fixture.adb->block_state = false;
        fixture.adb->cv.notify_all();
    }
    CHECK(opening.wait_for(2s) == std::future_status::ready);
    auto opened = opening.get();
    CHECK(!opened);
    CHECK(opened.error == channels::RemoteBackendError::internal_error);
    CHECK(stopping.wait_for(2s) == std::future_status::ready);
    CHECK(concurrent_stopping.wait_for(2s) == std::future_status::ready);
    stopping.get();
    concurrent_stopping.get();
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(fixture.adb->stopped);
    CHECK(fixture.adb->removed == std::vector<std::uint16_t>{32123});
    CHECK(fixture.adb->killed == std::vector<unsigned>{202});
}

void concurrent_stop_waits_for_blocked_send()
{
    Fixture fixture;
    configure_existing(fixture);
    auto backend = fixture.backend();
    CallbackLog log;
    auto opened = backend->open(std::string{"alpha"}, log.callbacks(), {});
    CHECK(opened);
    {
        std::lock_guard lock{fixture.sockets[0]->mutex};
        fixture.sockets[0]->send_block = true;
    }
    auto send = std::async(std::launch::async, [&] {
        return opened.session->send_to_device(secret("blocked"), {});
    });
    {
        std::unique_lock lock{fixture.sockets[0]->mutex};
        CHECK(fixture.sockets[0]->cv.wait_for(lock, 2s, [&] {
            return fixture.sockets[0]->send_entered;
        }));
    }
    auto first = std::async(std::launch::async, [&] { backend->stop(); });
    auto second = std::async(std::launch::async, [&] { backend->stop(); });
    CHECK(first.wait_for(2s) == std::future_status::ready);
    CHECK(second.wait_for(2s) == std::future_status::ready);
    first.get();
    second.get();
    CHECK(send.wait_for(0s) == std::future_status::ready);
    CHECK(send.get() != channels::RemoteIoStatus::accepted);
    std::lock_guard lock{fixture.adb->mutex};
    CHECK(fixture.adb->stopped);
}

}  // namespace

int main()
{
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"exact_configuration_and_no_fallback", exact_configuration_and_no_fallback},
        {"reuses_exact_server_and_forward_with_ordered_duplex", reuses_exact_server_and_forward_with_ordered_duplex},
        {"owns_and_revalidates_cleanup", owns_and_revalidates_cleanup},
        {"never_kills_reused_pid_identity", never_kills_reused_pid_identity},
        {"never_kills_pid_reused_with_a_different_owner_token", never_kills_pid_reused_with_a_different_owner_token},
        {"supervisor_never_kills_a_reused_child_pid", supervisor_never_kills_a_reused_child_pid},
        {"second_backend_cannot_reuse_an_owned_server", second_backend_cannot_reuse_an_owned_server},
        {"stale_none_probe_cannot_reclassify_a_new_owned_server_as_legacy", stale_none_probe_cannot_reclassify_a_new_owned_server_as_legacy},
        {"owner_token_failure_prevents_launch", owner_token_failure_prevents_launch},
        {"gate_write_failure_never_executes_server", gate_write_failure_never_executes_server},
        {"natural_child_exit_cleans_lease_without_kill", natural_child_exit_cleans_lease_without_kill},
        {"stale_lease_recovers_to_legacy_reuse", stale_lease_recovers_to_legacy_reuse},
        {"malicious_lease_symlink_fails_closed", malicious_lease_symlink_fails_closed},
        {"serial_lease_and_close_barrier", serial_lease_and_close_barrier},
        {"device_callback_can_reenter_close", device_callback_can_reenter_close},
        {"ended_callback_can_reenter_close", ended_callback_can_reenter_close},
        {"serializes_concurrent_websocket_sends", serializes_concurrent_websocket_sends},
        {"retries_listener_and_cleans_invalid_start_marker", retries_listener_and_cleans_invalid_start_marker},
        {"startup_deadline_and_forward_failure_unwind_owned_process", startup_deadline_and_forward_failure_unwind_owned_process},
        {"lost_start_response_cleans_only_exact_owned_marker", lost_start_response_cleans_only_exact_owned_marker},
        {"oversize_and_failed_open_cleanup", oversize_and_failed_open_cleanup},
        {"stop_closes_sessions_and_transport", stop_closes_sessions_and_transport},
        {"stop_linearizes_with_inflight_open", stop_linearizes_with_inflight_open},
        {"concurrent_stop_waits_for_blocked_send", concurrent_stop_waits_for_blocked_send},
    };
    for (const auto& [name, test] : tests) {
        try { test(); }
        catch (const std::exception& error) {
            std::cerr << name << ": " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
