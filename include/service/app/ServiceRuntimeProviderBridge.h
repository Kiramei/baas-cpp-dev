#pragma once

#include "service/adapters/FileResourceWatcher.h"
#include "service/app/ProductionProviderBackend.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace baas::service::app {

enum class ServiceRuntimeProviderBridgeError {
    none,
    already_started,
    watcher_start_failed,
    initial_data_unavailable,
    internal_error,
};

[[nodiscard]] std::string_view service_runtime_provider_bridge_error_name(
    ServiceRuntimeProviderBridgeError error) noexcept;

struct ServiceRuntimeLogEntry {
    std::string scope{"global"};
    std::string level{"info"};
    std::string message;
};

struct ServiceRuntimeProviderBridgeLimits {
    std::size_t max_scope_bytes{256};
    std::size_t max_level_bytes{16};
    std::size_t max_message_bytes{4'096};
};

struct ServiceRuntimeProviderBridgeDependencies {
    using Clock = std::function<double()>;

    Clock clock;
    adapters::FileResourceWatcherConfig watcher;
};

// Owns the lifecycle boundary between durable project resources and the
// Provider channel. It never synthesizes static data: initialized becomes true
// only after a complete real scan and a successful provider static commit.
class ServiceRuntimeProviderBridge final {
public:
    ServiceRuntimeProviderBridge(
        std::shared_ptr<adapters::FileResourceStore> resources,
        std::shared_ptr<ProductionProviderBackend> provider,
        ServiceRuntimeProviderBridgeDependencies dependencies = {},
        ServiceRuntimeProviderBridgeLimits limits = {});
    ~ServiceRuntimeProviderBridge();

    ServiceRuntimeProviderBridge(const ServiceRuntimeProviderBridge&) = delete;
    ServiceRuntimeProviderBridge& operator=(const ServiceRuntimeProviderBridge&) = delete;

    [[nodiscard]] ServiceRuntimeProviderBridgeError start() noexcept;
    void stop() noexcept;

    [[nodiscard]] channels::ProviderBackendError publish_log(
        ServiceRuntimeLogEntry entry) noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool initialized() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace baas::service::app
