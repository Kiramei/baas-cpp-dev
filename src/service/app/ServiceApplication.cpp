#include "service/app/ServiceApplication.h"

#include "service/adapters/FileResourceStore.h"
#include "service/app/AdbDiscoveryTriggerRegistration.h"
#include "service/app/ConfigurationTriggerRegistration.h"
#include "service/app/ProductionProviderBackend.h"
#include "service/app/ProductionRemoteBackend.h"
#include "service/app/ProductionRuntimeTaskControl.h"
#include "service/app/RuntimeConfigurationDefaults.h"
#include "service/app/RuntimeTaskTriggerRegistration.h"
#include "service/app/ServiceRuntimeProviderBridge.h"
#include "service/app/ServiceRuntimeRepositoryOwner.h"
#include "service/app/StatusTriggerRegistration.h"
#include "service/auth/Crypto.h"
#include "service/channels/TriggerHandler.h"
#include "service/health/HealthReadiness.h"
#include "service/http/ProductionHttpHost.h"
#include "service/runtime/ProductionRuntimeScriptTaskFactory.h"
#include "service/trigger/TriggerDispatch.h"
#include "service/websocket/BusinessSessionFactory.h"

#include <atomic>
#include <filesystem>
#include <new>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace baas::service::app {
namespace {

[[nodiscard]] router::HealthSnapshot health_snapshot(
    const std::string_view phase,
    const ServiceRuntimeRepositoryOwner& runtime_repository,
    const std::optional<router::HealthAuthSnapshot>& authentication = std::nullopt)
{
    router::HealthSnapshot snapshot;
    router::HealthObject repository_status{
        {"phase", router::HealthValue{std::string{
            service_runtime_repository_phase_name(runtime_repository.phase())}}},
    };
    if (runtime_repository.phase() == ServiceRuntimeRepositoryPhase::pinned) {
        repository_status.emplace_back(
            "generation",
            router::HealthValue{runtime_repository.generation()});
    }
    snapshot.statuses.emplace_back(
        "runtime",
        router::HealthValue{router::HealthObject{
            {"phase", router::HealthValue{std::string{phase}}},
            {"pipe", router::HealthValue{"unavailable"}},
            {"repository", router::HealthValue{std::move(repository_status)}},
#if defined(__ANDROID__)
            {"remote", router::HealthValue{"disabled"}},
#else
            {"remote", router::HealthValue{"desktop_only"}},
#endif
        }});
    if (authentication) snapshot.auth = *authentication;
    return snapshot;
}

[[nodiscard]] StatusSourceResult provider_status(
    const std::shared_ptr<ProductionProviderBackend>& provider,
    const std::stop_token stop)
{
    if (stop.stop_requested()) return {{}, StatusSourceError::cancelled};
    auto result = provider->status(stop);
    if (result) return {std::move(*result.value), StatusSourceError::none};
    if (stop.stop_requested()) return {{}, StatusSourceError::cancelled};
    if (result.error == channels::ProviderBackendError::capacity) {
        return {{}, StatusSourceError::capacity};
    }
    return {{}, StatusSourceError::unavailable};
}

[[nodiscard]] std::string_view host_start_error_name(
    const http::HttpHostStartError error) noexcept
{
    using enum http::HttpHostStartError;
    switch (error) {
        case none: return "none";
        case already_active: return "already_active";
        case bind_failed: return "bind_failed";
        case ready_timeout: return "ready_timeout";
        case listen_failed: return "listen_failed";
        case listener_start_failed: return "listener_start_failed";
        case websocket_not_stopped: return "websocket_not_stopped";
    }
    return "unknown";
}

[[nodiscard]] ServiceProcessExit exit_for(
    const ServiceApplicationError error) noexcept
{
    switch (error) {
        case ServiceApplicationError::pipe_transport_unavailable:
            return ServiceProcessExit::pipe_unavailable;
        case ServiceApplicationError::signal_setup_failed:
            return ServiceProcessExit::signal_setup;
        case ServiceApplicationError::host_start_failed:
            return ServiceProcessExit::host_start;
        case ServiceApplicationError::readiness_failed:
            return ServiceProcessExit::readiness;
        case ServiceApplicationError::internal_failure:
            return ServiceProcessExit::internal_failure;
        default:
            return ServiceProcessExit::composition;
    }
}

}  // namespace

class ServiceApplication::Impl final {
public:
    void publish_failure(const std::string_view phase) noexcept
    {
        try {
            readiness->publish_failed(health_snapshot(phase, *runtime_repository));
        } catch (...) {
            try {
                readiness->publish_failed();
            } catch (...) {
            }
        }
    }

    ServiceRunOptions options;
    std::shared_ptr<ServiceShutdownCoordinator> shutdown;
    std::shared_ptr<health::HealthReadinessOwner> readiness;
    std::unique_ptr<ServiceSignalOwner> signal_owner;
    std::shared_ptr<ProductionProviderBackend> provider;
    std::shared_ptr<adapters::FileResourceStore> resources;
    std::unique_ptr<ServiceRuntimeRepositoryOwner> runtime_repository;
    std::shared_ptr<const ::baas::runtime::repository::RuntimeRepositoryReadBundle>
        runtime_repository_read_bundle;
    std::unique_ptr<ServiceRuntimeProviderBridge> runtime_provider;
    std::shared_ptr<ProductionRemoteBackend> remote_backend;
    std::shared_ptr<service_runtime::RuntimeTaskOwner> runtime_task_owner;
    std::shared_ptr<RuntimeTaskControl> runtime_task_control;
    std::shared_ptr<trigger::TriggerExecutor> executor;
    std::shared_ptr<channels::TriggerHandlerFactory> trigger_factory;
    std::unique_ptr<http::ProductionHttpHost> host;
    std::atomic<bool> stopped{false};
    std::atomic<bool> transport_started{false};
    std::atomic<bool> ready{false};
};

std::string_view service_application_error_name(
    const ServiceApplicationError error) noexcept
{
    using enum ServiceApplicationError;
    switch (error) {
        case none: return "none";
        case invalid_options: return "invalid_options";
        case pipe_transport_unavailable: return "pipe_transport_unavailable";
        case signal_setup_failed: return "signal_setup_failed";
        case trigger_registration_failed: return "trigger_registration_failed";
        case trigger_dispatch_failed: return "trigger_dispatch_failed";
        case remote_resource_unavailable: return "remote_resource_unavailable";
        case runtime_repository_invalid: return "runtime_repository_invalid";
        case runtime_repository_generation_mismatch:
            return "runtime_repository_generation_mismatch";
        case composition_failed: return "composition_failed";
        case authentication_failed: return "authentication_failed";
        case host_start_failed: return "host_start_failed";
        case readiness_failed: return "readiness_failed";
        case internal_failure: return "internal_failure";
    }
    return "unknown";
}

ServiceApplication::ServiceApplication(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{}

ServiceApplication::~ServiceApplication()
{
    stop();
}

ServiceApplicationOpenResult ServiceApplication::open(
    ServiceRunOptions options,
    ServiceApplicationDependencies dependencies) noexcept
{
    // Pipe listener composition is not complete. This is deliberately the
    // first branch: no signal mask, lock file, worker, or socket is touched.
    if (options.pipe_name.has_value()) {
        return {nullptr, ServiceApplicationError::pipe_transport_unavailable, {}};
    }
    if (options.host != http::http_host_loopback_address || options.port == 0) {
        return {nullptr, ServiceApplicationError::invalid_options, {}};
    }
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(options.project_root, filesystem_error)
        || filesystem_error) {
        return {nullptr, ServiceApplicationError::invalid_options, {}};
    }
    auto runtime_repository = open_service_runtime_repository_owner(
        options.project_root, options.runtime_repository_generation);
    if (!runtime_repository) {
        ServiceApplicationError error = ServiceApplicationError::internal_failure;
        switch (runtime_repository.error) {
            case ServiceRuntimeRepositoryOpenError::invalid_expected_generation:
                error = ServiceApplicationError::invalid_options;
                break;
            case ServiceRuntimeRepositoryOpenError::generation_mismatch:
            case ServiceRuntimeRepositoryOpenError::
                trusted_state_generation_mismatch:
                error = ServiceApplicationError::runtime_repository_generation_mismatch;
                break;
            case ServiceRuntimeRepositoryOpenError::invalid_activation:
            case ServiceRuntimeRepositoryOpenError::trusted_state_invalid:
            case ServiceRuntimeRepositoryOpenError::trusted_state_pending_recovery:
                error = ServiceApplicationError::runtime_repository_invalid;
                break;
            case ServiceRuntimeRepositoryOpenError::none:
            case ServiceRuntimeRepositoryOpenError::internal_error:
                break;
        }
        return {nullptr, error, {}};
    }
    std::shared_ptr<const ::baas::runtime::repository::RuntimeRepositoryReadBundle>
        runtime_repository_read_bundle;
    std::shared_ptr<const adapters::ConfigurationDefaults>
        configuration_defaults;
    channels::ResourceStoreLimits resource_limits;
    try {
        runtime_repository_read_bundle =
            runtime_repository.owner->open_read_bundle();
        if (!runtime_repository_read_bundle
            || runtime_repository_read_bundle->generation()
                != runtime_repository.owner->generation()) {
            return {nullptr, ServiceApplicationError::runtime_repository_invalid, {}};
        }
        configuration_defaults = load_runtime_configuration_defaults(
            runtime_repository_read_bundle->resources(), resource_limits);
    } catch (const ::baas::runtime::repository::RuntimeRepositoryReadError& error) {
        const auto application_error = error.code()
                == ::baas::runtime::repository::RuntimeRepositoryReadErrorCode::resource_exhausted
            ? ServiceApplicationError::internal_failure
            : ServiceApplicationError::runtime_repository_invalid;
        return {nullptr, application_error, {}};
    } catch (const std::bad_alloc&) {
        return {nullptr, ServiceApplicationError::internal_failure, {}};
    } catch (const std::invalid_argument&) {
        return {nullptr, ServiceApplicationError::runtime_repository_invalid, {}};
    } catch (...) {
        return {nullptr, ServiceApplicationError::internal_failure, {}};
    }
#if !defined(__ANDROID__)
    const auto remote_server_jar =
        options.project_root / "service" / "remote" / "scrcpy-server.jar";
    filesystem_error.clear();
    if (!std::filesystem::is_regular_file(remote_server_jar, filesystem_error)
        || filesystem_error) {
        return {nullptr, ServiceApplicationError::remote_resource_unavailable, {}};
    }
#endif

    auto signal_block = block_service_shutdown_signals();
    if (!signal_block) {
        return {nullptr, ServiceApplicationError::signal_setup_failed, {}};
    }

    try {
        auto impl = std::make_unique<Impl>();
        impl->options = std::move(options);
        impl->runtime_repository = std::move(runtime_repository.owner);
        impl->runtime_repository_read_bundle =
            std::move(runtime_repository_read_bundle);
        impl->shutdown = std::make_shared<ServiceShutdownCoordinator>();
        impl->readiness = std::make_shared<health::HealthReadinessOwner>(
            health_snapshot("starting", *impl->runtime_repository));
        impl->readiness->begin_startup(
            health_snapshot("starting", *impl->runtime_repository));

        impl->provider = std::make_shared<ProductionProviderBackend>();
        adapters::FileResourceStoreDependencies resource_dependencies;
        resource_dependencies.configuration_defaults =
            std::move(configuration_defaults);
        impl->resources = std::make_shared<adapters::FileResourceStore>(
            impl->options.project_root, std::move(resource_dependencies),
            resource_limits);
        impl->runtime_provider = std::make_unique<ServiceRuntimeProviderBridge>(
            impl->resources, impl->provider);

#if !defined(__ANDROID__)
        ProductionRemoteBackendDependencies remote_dependencies;
        remote_dependencies.resources = impl->resources;
        remote_dependencies.adb_transport =
            std::make_shared<adb::ServiceAdbTransport>();
        remote_dependencies.server_jar = remote_server_jar;
        impl->remote_backend = std::make_shared<ProductionRemoteBackend>(
            std::move(remote_dependencies));
#endif

        auto registration = make_status_trigger_registration(
            StatusSourceCallback{[provider = impl->provider](const std::stop_token stop) {
                return provider_status(provider, stop);
            }});
        if (!registration) {
            return {nullptr, ServiceApplicationError::trigger_registration_failed, {}};
        }
        std::vector<trigger::TriggerHandlerRegistration> registrations;
        registrations.push_back(std::move(*registration.registration));
        auto adb_discovery_source = make_production_adb_discovery_source();
        auto adb_discovery = make_adb_discovery_trigger_registration(
            std::move(adb_discovery_source));
        if (!adb_discovery) {
            return {nullptr, ServiceApplicationError::trigger_registration_failed, {}};
        }
        registrations.push_back(std::move(*adb_discovery.registration));
        auto configuration = make_configuration_trigger_registrations(
            impl->resources);
        if (!configuration) {
            return {nullptr, ServiceApplicationError::trigger_registration_failed, {}};
        }
        for (auto& item : configuration.registrations) {
            registrations.push_back(std::move(item));
        }
        if (dependencies.production_runtime_script_provider) {
            auto factory = service_runtime::
                make_production_runtime_script_task_factory(
                    std::move(
                        dependencies.production_runtime_script_provider));
            auto backend = service_runtime::make_runtime_script_task_backend(
                std::move(factory), dependencies.runtime_script_backend);
            impl->runtime_task_owner =
                std::make_shared<service_runtime::RuntimeTaskOwner>(
                    std::move(backend), dependencies.runtime_task_owner);
            impl->runtime_task_control =
                make_production_runtime_task_control(
                    impl->runtime_task_owner);
            auto runtime_tasks = make_runtime_task_trigger_registrations(
                impl->runtime_task_control);
            if (!runtime_tasks) {
                return {
                    nullptr,
                    ServiceApplicationError::trigger_registration_failed,
                    {}};
            }
            for (auto& item : runtime_tasks.registrations) {
                registrations.push_back(std::move(item));
            }
        }
        auto dispatch = trigger::TriggerDispatcher::create(std::move(registrations));
        if (!dispatch) {
            return {nullptr, ServiceApplicationError::trigger_dispatch_failed, {}};
        }
        auto dispatcher = std::make_shared<const trigger::TriggerDispatcher>(
            std::move(*dispatch.dispatcher));
        impl->executor = std::make_shared<trigger::TriggerExecutor>(
            std::move(dispatcher));
        impl->trigger_factory = std::make_shared<channels::TriggerHandlerFactory>(
            impl->executor);

        http::ProductionHttpHostConfig config;
        config.service = {
            std::string{service_application_wire_name},
            std::string{service_application_version},
        };
        config.health_provider = impl->readiness;
        config.shutdown_intent = impl->shutdown;
        config.remote = websocket::RemoteChannelPolicy::desktop_only;
        config.host.port = impl->options.port;

        http::ProductionHttpHostDependencies http_dependencies;
        http_dependencies.authentication.storage = auth::make_file_auth_storage(
            impl->options.project_root);
        http_dependencies.authentication.clock = auth::make_system_auth_clock();
        http_dependencies.authentication.random = auth::make_system_auth_random();
        http_dependencies.authentication.password_deriver =
            auth::make_sodium_password_deriver();
        http_dependencies.provider_backend = impl->provider;
        http_dependencies.resource_store = impl->resources;
        http_dependencies.trigger = impl->trigger_factory;
#if defined(__ANDROID__)
        http_dependencies.remote = nullptr;
#else
        http_dependencies.remote = std::make_shared<channels::RemoteHandlerFactory>(
            impl->remote_backend);
#endif

        auto opened = http::open_production_http_host(
            std::move(config), std::move(http_dependencies));
        if (!opened) {
            const auto error = opened.error
                    == http::ProductionHttpHostOpenError::authentication_failed
                ? ServiceApplicationError::authentication_failed
                : ServiceApplicationError::composition_failed;
            return {nullptr, error, opened.authentication_error};
        }
        impl->host = std::move(opened.host);

        // Every worker created above inherited the blocked POSIX mask. Install
        // the sole signal waiter only after composition succeeds, so an auth
        // installation-lock failure remains the observable second-instance
        // error instead of being hidden by process-global signal ownership.
        auto signal = open_service_signal_owner(
            impl->shutdown, std::move(signal_block.block));
        if (!signal) {
            return {nullptr, ServiceApplicationError::signal_setup_failed, {}};
        }
        impl->signal_owner = std::move(signal.owner);
        return {
            std::unique_ptr<ServiceApplication>{
                new ServiceApplication{std::move(impl)}},
            ServiceApplicationError::none,
            auth::AuthError::none,
        };
    } catch (...) {
        return {nullptr, ServiceApplicationError::internal_failure, {}};
    }
}

http::HttpHostStartResult ServiceApplication::start_transport() noexcept
{
    if (!impl_ || impl_->stopped.load(std::memory_order_acquire)) {
        return {false, http::HttpHostStartError::listen_failed, 0};
    }
    try {
        const auto started = impl_->host->start();
        if (started.error == http::HttpHostStartError::already_active) {
            return started;
        }
        if (!started.started) {
            impl_->publish_failure("start_failed");
            stop();
            return started;
        }
        impl_->transport_started.store(true, std::memory_order_release);
        return started;
    } catch (...) {
        impl_->publish_failure("start_failed");
        stop();
        return {false, http::HttpHostStartError::listen_failed, 0};
    }
}

bool ServiceApplication::publish_ready() noexcept
{
    if (!impl_ || impl_->stopped.load(std::memory_order_acquire)
        || !impl_->transport_started.load(std::memory_order_acquire)) {
        return false;
    }
    if (impl_->ready.load(std::memory_order_acquire)) return true;
    try {
        const auto runtime_started = impl_->runtime_provider->start();
        if (runtime_started != ServiceRuntimeProviderBridgeError::none) {
            impl_->publish_failure("resources_failed");
            stop();
            return false;
        }
        const auto authentication = impl_->host->authentication();
        if (!authentication) {
            impl_->publish_failure("auth_failed");
            stop();
            return false;
        }
        const auto state = authentication->password_state();
        auto public_key = auth::encode_base64url_padded(
            authentication->signing_public_key());
        if (!public_key) {
            impl_->publish_failure("auth_failed");
            stop();
            return false;
        }
        router::HealthAuthSnapshot auth_snapshot{
            state.initialized,
            state.pwd_epoch,
            std::move(*public_key.value),
        };
        if (!impl_->readiness->publish_ready(
                health_snapshot("ready", *impl_->runtime_repository, auth_snapshot))) {
            impl_->publish_failure("readiness_failed");
            stop();
            return false;
        }
        impl_->ready.store(true, std::memory_order_release);
        return true;
    } catch (...) {
        impl_->publish_failure("readiness_failed");
        stop();
        return false;
    }
}

ServiceShutdownReason ServiceApplication::wait_for_shutdown()
{
    return impl_->shutdown->wait();
}

std::optional<ServiceShutdownReason> ServiceApplication::wait_for_shutdown(
    const std::chrono::milliseconds timeout)
{
    return impl_->shutdown->wait_for(timeout);
}

void ServiceApplication::stop() noexcept
{
    if (!impl_ || impl_->stopped.exchange(true, std::memory_order_acq_rel)) return;
    try {
        auto current = impl_->readiness->readiness_snapshot();
        current.health.statuses =
            health_snapshot("stopped", *impl_->runtime_repository).statuses;
        impl_->readiness->publish_failed(std::move(current.health));
    } catch (...) {
        try {
            impl_->readiness->publish_failed();
        } catch (...) {
        }
    }
    if (impl_->host) impl_->host->stop();
    // Trigger handlers retain RuntimeTaskControl. Drain their prepare/claim
    // windows before closing task admission, then cooperatively stop and join
    // every native runtime worker while its provider/device dependencies are
    // still alive.
    if (impl_->executor) impl_->executor->shutdown();
    if (impl_->runtime_task_owner) impl_->runtime_task_owner->shutdown();
    if (impl_->remote_backend) impl_->remote_backend->stop();
    if (impl_->runtime_provider) impl_->runtime_provider->stop();
    if (impl_->signal_owner) impl_->signal_owner->stop();
    impl_->ready.store(false, std::memory_order_release);
    impl_->transport_started.store(false, std::memory_order_release);
}

std::uint16_t ServiceApplication::port() const noexcept
{
    return impl_ && impl_->host ? impl_->host->port() : 0;
}

router::HealthReadinessSnapshot ServiceApplication::readiness_snapshot() const
{
    return impl_->readiness->readiness_snapshot();
}

std::shared_ptr<trigger::TriggerExecutor>
ServiceApplication::trigger_executor() const noexcept
{
    return impl_ ? impl_->executor : nullptr;
}

std::shared_ptr<const ::baas::runtime::repository::RuntimeRepositoryReadBundle>
ServiceApplication::runtime_repository_read_bundle() const noexcept
{
    return impl_ ? impl_->runtime_repository_read_bundle : nullptr;
}

std::optional<service_runtime::RuntimeTaskSnapshot>
ServiceApplication::runtime_task_snapshot(
    const std::string_view config_id) const
{
    if (!impl_ || !impl_->runtime_task_owner) return std::nullopt;
    return impl_->runtime_task_owner->snapshot(config_id);
}

int run_service_application(
    const std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& diagnostics) noexcept
{
    try {
        const auto parsed = parse_service_command_line(arguments);
        if (!parsed) {
            diagnostics << "BAAS_service: command_line:"
                        << service_command_line_error_name(parsed.error);
            if (parsed.error_argument != ServiceCommandLineResult::no_argument) {
                diagnostics << ":argument=" << parsed.error_argument;
            }
            diagnostics << '\n';
            return static_cast<int>(ServiceProcessExit::command_line);
        }
        if (parsed.disposition == ServiceCommandLineDisposition::help) {
            output
                << "Usage: BAAS_service --project-root <directory> --host 127.0.0.1 "
                   "--port <1..65535> --runtime-repository-generation "
                   "<64-lowercase-hex>\n"
                << "       BAAS_service --help | --version\n";
#if defined(__ANDROID__)
            output << "Pipe and host-side remote transports are not enabled.\n";
#else
            output
                << "Pipe transport is not enabled; desktop remote transport is enabled.\n";
#endif
            return static_cast<int>(ServiceProcessExit::success);
        }
        if (parsed.disposition == ServiceCommandLineDisposition::version) {
            output << service_application_executable_name << ' '
                   << service_application_version << '\n';
            return static_cast<int>(ServiceProcessExit::success);
        }

        auto opened = ServiceApplication::open(parsed.options);
        if (!opened) {
            diagnostics << "BAAS_service: open:"
                        << service_application_error_name(opened.error);
            if (opened.authentication_error != auth::AuthError::none) {
                diagnostics << ":auth="
                            << auth::auth_error_name(opened.authentication_error);
            }
            diagnostics << '\n';
            return static_cast<int>(exit_for(opened.error));
        }
        const auto started = opened.application->start_transport();
        if (!started.started) {
            diagnostics << "BAAS_service: start:"
                        << host_start_error_name(started.error) << '\n';
            return static_cast<int>(ServiceProcessExit::host_start);
        }
        if (!opened.application->publish_ready()) {
            diagnostics << "BAAS_service: readiness:failed\n";
            return static_cast<int>(ServiceProcessExit::readiness);
        }
        output << "BAAS_service: ready:http://127.0.0.1:"
               << started.port << '\n';
        const auto reason = opened.application->wait_for_shutdown();
        opened.application->stop();
        if (reason == ServiceShutdownReason::signal_failure) {
            diagnostics << "BAAS_service: shutdown:signal_failure\n";
            return static_cast<int>(ServiceProcessExit::internal_failure);
        }
        return static_cast<int>(ServiceProcessExit::success);
    } catch (...) {
        diagnostics << "BAAS_service: internal_failure\n";
        return static_cast<int>(ServiceProcessExit::internal_failure);
    }
}

}  // namespace baas::service::app
