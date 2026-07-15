#include "service/http/ProductionHttpHost.h"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace auth = baas::service::auth;
namespace channels = baas::service::channels;
namespace http = baas::service::http;
namespace router = baas::service::router;
namespace ws = baas::service::websocket;

namespace {

using namespace std::chrono_literals;
int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

class MemoryStorage final : public auth::AuthStorage {
public:
    [[nodiscard]] auth::StorageReadResult read(
        auth::AuthFile, std::size_t) noexcept override
    {
        return {};
    }

    [[nodiscard]] bool write_atomic(
        auth::AuthFile, std::span<const std::byte>) noexcept override
    {
        return true;
    }
};

class FixedClock final : public auth::AuthClock {
public:
    [[nodiscard]] std::int64_t now_unix_seconds() noexcept override
    {
        return 1'700'000'000;
    }
};

class FixedRandom final : public auth::AuthRandom {
public:
    [[nodiscard]] bool fill(std::span<std::byte> output) noexcept override
    {
        for (auto& value : output) value = static_cast<std::byte>(next_++);
        return true;
    }
private:
    unsigned int next_{1};
};

class FixedDeriver final : public auth::PasswordDeriver {
public:
    [[nodiscard]] auth::SecretBytesResult derive(
        std::span<const std::byte>, std::span<const std::byte>) noexcept override
    {
        try {
            auth::SecretBuffer result{auth::argon2id_output_bytes};
            std::fill(result.mutable_bytes().begin(), result.mutable_bytes().end(),
                      std::byte{0x5a});
            return {std::move(result), auth::CryptoError::none};
        } catch (...) {
            return {std::nullopt, auth::CryptoError::resource_exhausted};
        }
    }
};

class EmptyProvider final : public channels::ProviderBackend {
public:
    [[nodiscard]] channels::ProviderBackendResult<channels::ProviderLogsFull>
        logs_full(std::stop_token) override
    {
        return {std::nullopt, channels::ProviderBackendError::internal_error};
    }
    [[nodiscard]] channels::ProviderBackendResult<std::string> status(
        std::stop_token) override
    {
        return {std::nullopt, channels::ProviderBackendError::internal_error};
    }
    [[nodiscard]] channels::ProviderBackendResult<std::optional<bool>>
        all_data_initialized(std::stop_token) override
    {
        return {std::nullopt, channels::ProviderBackendError::internal_error};
    }
    [[nodiscard]] channels::ProviderBackendResult<channels::ProviderStaticSnapshot>
        static_snapshot(std::stop_token) override
    {
        return {std::nullopt, channels::ProviderBackendError::internal_error};
    }
    [[nodiscard]] channels::ProviderSubscribeResult subscribe_logs(
        PushCallback) override
    {
        return {nullptr, channels::ProviderBackendError::internal_error};
    }
    [[nodiscard]] channels::ProviderSubscribeResult subscribe_status(
        PushCallback) override
    {
        return {nullptr, channels::ProviderBackendError::internal_error};
    }
};

class EmptyStore final : public channels::ResourceStore {
public:
    [[nodiscard]] channels::ResourceStoreResult<channels::ResourceSnapshot>
        config_list(std::stop_token) override
    {
        return {std::nullopt, channels::ResourceStoreError::internal_error};
    }
    [[nodiscard]] channels::ResourceStoreResult<channels::ResourceSnapshot> pull(
        const channels::ResourceKey&, std::stop_token) override
    {
        return {std::nullopt, channels::ResourceStoreError::internal_error};
    }
    [[nodiscard]] channels::ResourceStoreResult<channels::ResourcePatchResult>
        apply_patch(channels::ResourcePatchRequest, std::stop_token) override
    {
        return {std::nullopt, channels::ResourceStoreError::internal_error};
    }
    [[nodiscard]] channels::ResourceSubscribeResult subscribe_updates(
        UpdateCallback) override
    {
        return {nullptr, channels::ResourceStoreError::internal_error};
    }
};

class EmptyHandlerFactory final : public ws::BusinessChannelHandlerFactory {
public:
    [[nodiscard]] ws::BusinessHandlerCreateResult create(
        ws::BusinessSessionContext,
        std::shared_ptr<ws::BusinessPlaintextSink>,
        std::stop_token) override
    {
        return {nullptr, ws::BusinessHandlerCreateError::internal_error};
    }
};

[[nodiscard]] http::ProductionHttpHostConfig config()
{
    http::ProductionHttpHostConfig result;
    result.service = {"BAAS production composition", "test"};
    result.health_snapshot = router::HealthSnapshot{};
    result.remote = ws::RemoteChannelPolicy::disabled;
    result.host.websocket.enabled = false;
    result.host.worker_count = 2;
    result.host.max_queued_requests = 16;
    result.host.ready_timeout = 1s;
    result.host.read_timeout = 1s;
    result.host.write_timeout = 1s;
    result.host.idle_interval = 10ms;
    return result;
}

[[nodiscard]] http::ProductionHttpHostDependencies dependencies(
    const bool include_remote = false)
{
    http::ProductionHttpHostDependencies result;
    result.authentication.storage = std::make_shared<MemoryStorage>();
    result.authentication.clock = std::make_shared<FixedClock>();
    result.authentication.random = std::make_shared<FixedRandom>();
    result.authentication.password_deriver = std::make_shared<FixedDeriver>();
    result.provider_backend = std::make_shared<EmptyProvider>();
    result.resource_store = std::make_shared<EmptyStore>();
    result.trigger = std::make_shared<EmptyHandlerFactory>();
    if (include_remote) result.remote = std::make_shared<EmptyHandlerFactory>();
    return result;
}

void test_missing_dependencies_and_platform_remote_policy()
{
    auto missing = http::open_production_http_host(config(), {});
    check(missing.error == http::ProductionHttpHostOpenError::missing_auth_storage,
          "production composition must not install implicit test auth dependencies");

    auto missing_clock = dependencies();
    missing_clock.authentication.clock.reset();
    check(http::open_production_http_host(config(), std::move(missing_clock)).error
              == http::ProductionHttpHostOpenError::missing_auth_clock,
          "missing auth clock must be reported structurally");

    auto missing_random = dependencies();
    missing_random.authentication.random.reset();
    check(http::open_production_http_host(config(), std::move(missing_random)).error
              == http::ProductionHttpHostOpenError::missing_auth_random,
          "missing auth random source must be reported structurally");

    auto missing_deriver = dependencies();
    missing_deriver.authentication.password_deriver.reset();
    check(http::open_production_http_host(config(), std::move(missing_deriver)).error
              == http::ProductionHttpHostOpenError::missing_password_deriver,
          "missing password deriver must be reported structurally");

    auto missing_provider = dependencies();
    missing_provider.provider_backend.reset();
    auto provider = http::open_production_http_host(
        config(), std::move(missing_provider));
    check(provider.error
              == http::ProductionHttpHostOpenError::missing_provider_backend,
          "missing provider dependency must be reported structurally");

    auto missing_store = dependencies();
    missing_store.resource_store.reset();
    check(http::open_production_http_host(config(), std::move(missing_store)).error
              == http::ProductionHttpHostOpenError::missing_resource_store,
          "missing sync resource store must be reported structurally");

    auto missing_trigger = dependencies();
    missing_trigger.trigger.reset();
    check(http::open_production_http_host(config(), std::move(missing_trigger)).error
              == http::ProductionHttpHostOpenError::missing_trigger_handler,
          "missing trigger factory must be reported structurally");

    auto desktop = config();
    desktop.remote = ws::RemoteChannelPolicy::desktop_only;
    auto remote = http::open_production_http_host(
        std::move(desktop), dependencies());
#if defined(__ANDROID__)
    check(remote && !http::production_remote_handler_required(
                         ws::RemoteChannelPolicy::desktop_only),
          "Android composition must not require the unavailable remote channel");
#else
    check(remote.error == http::ProductionHttpHostOpenError::missing_remote_handler
              && http::production_remote_handler_required(
                  ws::RemoteChannelPolicy::desktop_only),
          "desktop composition must reject a missing remote factory");
#endif
}

void test_start_stop_and_http_projection()
{
    auto opened = http::open_production_http_host(config(), dependencies());
    check(static_cast<bool>(opened),
          "valid explicit production dependencies must compose");
    if (!opened) return;
    check(opened.host->authentication() != nullptr,
          "composition must retain the AuthOwner used by HTTP and WebSocket sessions");

    const auto started = opened.host->start();
    check(started.started && started.port != 0,
          "composition root must start the owned HttpHost");
    if (started.started) {
        const auto duplicate = opened.host->start();
        check(!duplicate.started
                  && duplicate.error == http::HttpHostStartError::already_active
                  && opened.host->state() == http::HttpHostState::running,
              "duplicate start must not tear down an already running host");
        httplib::Client client{std::string{http::http_host_loopback_address},
                               started.port};
        client.set_connection_timeout(1s);
        client.set_read_timeout(1s);
        client.set_write_timeout(1s);
        const auto version = client.Get("/version");
        check(version && version->status == 200
                  && version->body.find("BAAS production composition")
                      != std::string::npos,
              "composition must install the production Router");
        const auto auth_response = client.Post(
            "/auth/remember", "{}", "application/json");
        check(auth_response && auth_response->status == 400
                  && auth_response->body.find("invalid_remember_request")
                      != std::string::npos,
              "composition must install AuthHttpAdapter as the route extension");
        client.stop();
    }
    opened.host->stop();
    check(opened.host->state() == http::HttpHostState::stopped
              && opened.host->port() == 0,
          "composition stop must drain and reset the HttpHost lifecycle");
    opened.host->stop();
}

void test_start_exception_and_construction_rollback()
{
    auto throwing = config();
    throwing.host.listener_thread_factory = [](std::function<void()>) -> std::thread {
        throw std::runtime_error("injected listener failure");
    };
    auto opened = http::open_production_http_host(
        std::move(throwing), dependencies());
    check(static_cast<bool>(opened),
          "listener injection must not affect construction");
    if (opened) {
        const auto started = opened.host->start();
        check(!started.started
                  && started.error == http::HttpHostStartError::listener_start_failed
                  && opened.host->state() == http::HttpHostState::stopped,
              "listener exception must roll the full host back to stopped");
    }

    auto invalid = config();
    invalid.host.worker_count = 0;
    auto owned = dependencies();
    std::weak_ptr<auth::AuthStorage> storage = owned.authentication.storage;
    std::weak_ptr<channels::ProviderBackend> provider = owned.provider_backend;
    std::weak_ptr<channels::ResourceStore> store = owned.resource_store;
    std::weak_ptr<ws::BusinessChannelHandlerFactory> trigger = owned.trigger;
    auto failed = http::open_production_http_host(
        std::move(invalid), std::move(owned));
    check(failed.error == http::ProductionHttpHostOpenError::invalid_configuration,
          "constructor validation failure must be returned structurally");
    check(storage.expired() && provider.expired() && store.expired()
              && trigger.expired(),
          "partial composition must release AuthOwner and business dependencies");
}

}  // namespace

int main()
{
    try {
        test_missing_dependencies_and_platform_remote_policy();
        test_start_stop_and_http_projection();
        test_start_exception_and_construction_rollback();
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED: " << error.what() << '\n';
        return 2;
    }
    return failures == 0 ? 0 : 1;
}
