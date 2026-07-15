#pragma once

#include "service/channels/ProviderHandler.h"
#include "service/channels/SyncHandler.h"
#include "service/http/AuthHttpAdapter.h"
#include "service/http/HttpHost.h"
#include "service/websocket/BusinessSessionFactory.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace baas::service::http {

struct ProductionHttpHostConfig {
    router::ServiceInfo service;
    router::SizeBudget router_budget{};
    std::optional<router::HealthSnapshot> health_snapshot;
    std::shared_ptr<router::HealthSnapshotProvider> health_provider;
    std::shared_ptr<router::ShutdownIntent> shutdown_intent;

    auth::AuthOwnerConfig authentication{};
    AuthHttpAdapterConfig auth_http{};
    channels::ProviderHandlerLimits provider{};
    channels::SyncHandlerLimits sync{};
    websocket::ControlSessionConfig control{};
    websocket::BusinessSessionConfig business{};
    websocket::RemoteChannelPolicy remote{
        websocket::RemoteChannelPolicy::desktop_only};
    InputBudget input{};
    HttpHostConfig host{};
};

// Every production capability is explicit. Callers normally populate the auth
// fields with make_file_auth_storage/make_system_auth_clock/
// make_system_auth_random/make_sodium_password_deriver. Tests may inject their
// own implementations, but no in-memory/test backend is selected implicitly.
struct ProductionHttpHostDependencies {
    auth::AuthDependencies authentication;
    std::shared_ptr<channels::ProviderBackend> provider_backend;
    std::shared_ptr<channels::ResourceStore> resource_store;
    std::shared_ptr<websocket::BusinessChannelHandlerFactory> trigger;
    std::shared_ptr<websocket::BusinessChannelHandlerFactory> remote;
};

enum class ProductionHttpHostOpenError {
    none,
    missing_auth_storage,
    missing_auth_clock,
    missing_auth_random,
    missing_password_deriver,
    missing_provider_backend,
    missing_resource_store,
    missing_trigger_handler,
    missing_remote_handler,
    authentication_failed,
    invalid_configuration,
    construction_failed,
};

[[nodiscard]] std::string_view production_http_host_open_error_name(
    ProductionHttpHostOpenError error) noexcept;

// On Android the transport does not install /ws/remote. On desktop the
// desktop_only policy requires an injected production remote factory.
[[nodiscard]] constexpr bool production_remote_handler_required(
    const websocket::RemoteChannelPolicy policy) noexcept
{
#if defined(__ANDROID__)
    static_cast<void>(policy);
    return false;
#else
    return policy == websocket::RemoteChannelPolicy::desktop_only;
#endif
}

class ProductionHttpHost;

struct ProductionHttpHostOpenResult {
    std::unique_ptr<ProductionHttpHost> host;
    ProductionHttpHostOpenError error{ProductionHttpHostOpenError::none};
    auth::AuthError authentication_error{auth::AuthError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ProductionHttpHostOpenError::none && host != nullptr;
    }
};

class ProductionHttpHost final {
public:
    ~ProductionHttpHost();

    ProductionHttpHost(const ProductionHttpHost&) = delete;
    ProductionHttpHost& operator=(const ProductionHttpHost&) = delete;
    ProductionHttpHost(ProductionHttpHost&&) = delete;
    ProductionHttpHost& operator=(ProductionHttpHost&&) = delete;

    // New-listener failures roll back through HttpHost::stop. already_active
    // leaves the existing listener untouched.
    [[nodiscard]] HttpHostStartResult start();
    void stop() noexcept;

    [[nodiscard]] HttpHostState state() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::string address() const;
    [[nodiscard]] HttpHostStartError last_start_error() const noexcept;
    [[nodiscard]] std::string last_error_message() const;
    [[nodiscard]] websocket::WebSocketOwnerStats websocket_stats() const noexcept;
    [[nodiscard]] std::shared_ptr<auth::AuthOwner> authentication() const noexcept;

private:
    ProductionHttpHost(
        std::shared_ptr<auth::AuthOwner> authentication,
        websocket::BusinessHandlerFactories handlers,
        std::shared_ptr<websocket::ProductionSessionFactory> sessions,
        std::shared_ptr<AuthHttpAdapter> auth_http,
        std::unique_ptr<HttpHost> host) noexcept;

    std::shared_ptr<auth::AuthOwner> authentication_;
    websocket::BusinessHandlerFactories handlers_;
    std::shared_ptr<websocket::ProductionSessionFactory> sessions_;
    std::shared_ptr<AuthHttpAdapter> auth_http_;
    std::unique_ptr<HttpHost> host_;

    friend ProductionHttpHostOpenResult open_production_http_host(
        ProductionHttpHostConfig, ProductionHttpHostDependencies) noexcept;
};

[[nodiscard]] ProductionHttpHostOpenResult open_production_http_host(
    ProductionHttpHostConfig config,
    ProductionHttpHostDependencies dependencies) noexcept;

}  // namespace baas::service::http
