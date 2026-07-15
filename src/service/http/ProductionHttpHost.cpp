#include "service/http/ProductionHttpHost.h"

#include <stdexcept>
#include <utility>

namespace baas::service::http {
namespace {

[[nodiscard]] ProductionHttpHostOpenError missing_dependency(
    const ProductionHttpHostConfig& config,
    const ProductionHttpHostDependencies& dependencies) noexcept
{
    if (!dependencies.authentication.storage)
        return ProductionHttpHostOpenError::missing_auth_storage;
    if (!dependencies.authentication.clock)
        return ProductionHttpHostOpenError::missing_auth_clock;
    if (!dependencies.authentication.random)
        return ProductionHttpHostOpenError::missing_auth_random;
    if (!dependencies.authentication.password_deriver)
        return ProductionHttpHostOpenError::missing_password_deriver;
    if (!dependencies.provider_backend)
        return ProductionHttpHostOpenError::missing_provider_backend;
    if (!dependencies.resource_store)
        return ProductionHttpHostOpenError::missing_resource_store;
    if (!dependencies.trigger)
        return ProductionHttpHostOpenError::missing_trigger_handler;
    if (production_remote_handler_required(config.remote) && !dependencies.remote)
        return ProductionHttpHostOpenError::missing_remote_handler;
    return ProductionHttpHostOpenError::none;
}

}  // namespace

std::string_view production_http_host_open_error_name(
    const ProductionHttpHostOpenError error) noexcept
{
    using enum ProductionHttpHostOpenError;
    switch (error) {
        case none: return "none";
        case missing_auth_storage: return "missing_auth_storage";
        case missing_auth_clock: return "missing_auth_clock";
        case missing_auth_random: return "missing_auth_random";
        case missing_password_deriver: return "missing_password_deriver";
        case missing_provider_backend: return "missing_provider_backend";
        case missing_resource_store: return "missing_resource_store";
        case missing_trigger_handler: return "missing_trigger_handler";
        case missing_remote_handler: return "missing_remote_handler";
        case authentication_failed: return "authentication_failed";
        case invalid_configuration: return "invalid_configuration";
        case construction_failed: return "construction_failed";
    }
    return "unknown";
}

ProductionHttpHost::ProductionHttpHost(
    std::shared_ptr<auth::AuthOwner> authentication,
    websocket::BusinessHandlerFactories handlers,
    std::shared_ptr<websocket::ProductionSessionFactory> sessions,
    std::shared_ptr<AuthHttpAdapter> auth_http,
    std::unique_ptr<HttpHost> host) noexcept
    : authentication_(std::move(authentication)),
      handlers_(std::move(handlers)),
      sessions_(std::move(sessions)),
      auth_http_(std::move(auth_http)),
      host_(std::move(host))
{}

ProductionHttpHost::~ProductionHttpHost()
{
    stop();
}

HttpHostStartResult ProductionHttpHost::start()
{
    const auto result = host_->start();
    if (!result.started && result.error != HttpHostStartError::already_active)
        host_->stop();
    return result;
}

void ProductionHttpHost::stop() noexcept
{
    if (host_) host_->stop();
}

HttpHostState ProductionHttpHost::state() const noexcept { return host_->state(); }
std::uint16_t ProductionHttpHost::port() const noexcept { return host_->port(); }
std::string ProductionHttpHost::address() const { return host_->address(); }
HttpHostStartError ProductionHttpHost::last_start_error() const noexcept
{
    return host_->last_start_error();
}
std::string ProductionHttpHost::last_error_message() const
{
    return host_->last_error_message();
}
websocket::WebSocketOwnerStats ProductionHttpHost::websocket_stats() const noexcept
{
    return host_->websocket_stats();
}
std::shared_ptr<auth::AuthOwner> ProductionHttpHost::authentication() const noexcept
{
    return authentication_;
}

ProductionHttpHostOpenResult open_production_http_host(
    ProductionHttpHostConfig config,
    ProductionHttpHostDependencies dependencies) noexcept
{
    const auto missing = missing_dependency(config, dependencies);
    if (missing != ProductionHttpHostOpenError::none) return {nullptr, missing, {}};

    auto opened = auth::AuthOwner::open(
        config.authentication, std::move(dependencies.authentication));
    if (!opened) {
        return {
            nullptr,
            ProductionHttpHostOpenError::authentication_failed,
            opened.error,
        };
    }

    try {
        std::shared_ptr<auth::AuthOwner> authentication{std::move(*opened)};
        auto provider = std::make_shared<channels::ProviderHandlerFactory>(
            std::move(dependencies.provider_backend), config.provider);
        auto sync = std::make_shared<channels::SyncHandlerFactory>(
            std::move(dependencies.resource_store), config.sync);
        websocket::BusinessHandlerFactories handlers{
            std::move(provider),
            std::move(sync),
            std::move(dependencies.trigger),
            std::move(dependencies.remote),
        };
        auto sessions = std::make_shared<websocket::ProductionSessionFactory>(
            authentication, handlers, config.control, config.business, config.remote);
        auto auth_http = std::make_shared<AuthHttpAdapter>(
            authentication, config.auth_http);
        HttpHostRouterConfig router_config{
            std::move(config.service),
            config.router_budget,
            std::move(config.health_snapshot),
            std::move(config.health_provider),
            std::move(config.shutdown_intent),
            auth_http,
            sessions,
        };
        auto host = std::make_unique<HttpHost>(
            std::move(router_config), config.input, std::move(config.host));
        auto composition = std::unique_ptr<ProductionHttpHost>{
            new ProductionHttpHost{
                std::move(authentication), std::move(handlers),
                std::move(sessions), std::move(auth_http), std::move(host)}};
        return {std::move(composition), ProductionHttpHostOpenError::none, {}};
    } catch (const std::invalid_argument&) {
        return {nullptr, ProductionHttpHostOpenError::invalid_configuration, {}};
    } catch (...) {
        return {nullptr, ProductionHttpHostOpenError::construction_failed, {}};
    }
}

}  // namespace baas::service::http
