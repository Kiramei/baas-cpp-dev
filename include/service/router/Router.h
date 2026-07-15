#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace baas::service::router {

inline constexpr unsigned int api_version = 1;

struct Header {
    std::string name;
    std::string value;
};

struct Request {
    std::string_view method;
    std::string_view path;
    std::string_view body;
    // Transport adapters expose only the bounded metadata needed by HTTP
    // authentication. The views remain valid for the synchronous handle call.
    std::optional<std::string_view> cookie;
    bool malformed_cookie_headers = false;
    bool secure_transport = false;
};

struct Response {
    int status = 500;
    std::vector<Header> headers;
    std::string body;
};

struct SizeBudget {
    std::size_t max_method_bytes = 16;
    std::size_t max_path_bytes = 2'048;
    std::size_t max_request_body_bytes = 1'048'576;
    std::size_t max_response_body_bytes = 1'048'576;
};

struct ServiceInfo {
    std::string name;
    std::string version;
};

struct HealthValue;
using HealthArray = std::vector<HealthValue>;
using HealthObject = std::vector<std::pair<std::string, HealthValue>>;

enum class HealthValueKind { null, boolean, integer, floating, string, array, object };

// JSON-safe value used only for the public /health statuses snapshot. Objects
// are canonicalized by key before serialization, and duplicate keys are
// rejected rather than silently overwritten.
struct HealthValue {
    using Storage = std::variant<std::monostate, bool, std::int64_t, double,
                                 std::string, HealthArray, HealthObject>;

    HealthValue() noexcept = default;
    explicit HealthValue(bool value) : storage(value) {}
    explicit HealthValue(std::int64_t value) : storage(value) {}
    explicit HealthValue(double value) : storage(value) {}
    explicit HealthValue(std::string value) : storage(std::move(value)) {}
    explicit HealthValue(const char* value) : storage(std::string(value)) {}
    explicit HealthValue(HealthArray value) : storage(std::move(value)) {}
    explicit HealthValue(HealthObject value) : storage(std::move(value)) {}

    [[nodiscard]] HealthValueKind kind() const noexcept;

    Storage storage;
};

struct HealthAuthSnapshot {
    bool initialized = false;
    std::uint64_t pwd_epoch = 0;
    std::string server_sign_public_key;
};

struct HealthSnapshot {
    HealthObject statuses;
    HealthAuthSnapshot auth;
};

enum class HealthReadinessState { starting, ready, failed };

// A single provider read returns readiness and its complete public projection
// together. Implementations must not expose fields from different updates.
struct HealthReadinessSnapshot {
    HealthReadinessState state = HealthReadinessState::starting;
    HealthSnapshot health;
};

class HealthSnapshotProvider {
public:
    virtual ~HealthSnapshotProvider() = default;
    // Called synchronously from Router::handle(). A provider used by a shared
    // Router must be thread-safe. Router retains the provider's shared owner.
    [[nodiscard]] virtual HealthReadinessSnapshot readiness_snapshot() const = 0;
};

enum class ShutdownDecision {
    accepted,
    rejected,
};

class ShutdownIntent {
public:
    virtual ~ShutdownIntent() = default;
    [[nodiscard]] virtual ShutdownDecision request_shutdown() noexcept = 0;
};

// A transport-independent optional route family. Returning nullopt leaves the
// request to Router's built-in health/version/shutdown routes.
class RouteExtension {
public:
    virtual ~RouteExtension() = default;
    [[nodiscard]] virtual std::optional<Response> handle(
        const Request& request) const = 0;
};

class Router final {
public:
    explicit Router(
        ServiceInfo service,
        SizeBudget budget = {},
        ShutdownIntent* shutdown_intent = nullptr,
        std::shared_ptr<RouteExtension> extension = {}
    );

    [[nodiscard]] static Router with_health_snapshot(
        ServiceInfo service,
        HealthSnapshot health,
        SizeBudget budget = {},
        ShutdownIntent* shutdown_intent = nullptr,
        std::shared_ptr<RouteExtension> extension = {}
    );

    [[nodiscard]] static Router with_health_provider(
        ServiceInfo service,
        std::shared_ptr<HealthSnapshotProvider> health_provider,
        SizeBudget budget = {},
        ShutdownIntent* shutdown_intent = nullptr,
        std::shared_ptr<RouteExtension> extension = {}
    );

    [[nodiscard]] Response handle(const Request& request) const;
    [[nodiscard]] const SizeBudget& budget() const noexcept;

private:
    Router(
        ServiceInfo service,
        SizeBudget budget,
        ShutdownIntent* shutdown_intent,
        std::optional<HealthSnapshot> health_snapshot,
        std::shared_ptr<HealthSnapshotProvider> health_provider,
        std::shared_ptr<RouteExtension> extension
    );

    [[nodiscard]] Response route(const Request& request) const;
    [[nodiscard]] Response health() const;
    [[nodiscard]] Response finish(Response response) const;
    [[nodiscard]] static Response json_response(int status, std::string body);
    [[nodiscard]] static Response error(int status, std::string_view code, std::string_view message);

    ServiceInfo service_;
    SizeBudget budget_;
    ShutdownIntent* shutdown_intent_;
    std::optional<HealthSnapshot> health_snapshot_;
    std::shared_ptr<HealthSnapshotProvider> health_provider_;
    std::shared_ptr<RouteExtension> extension_;
};

}  // namespace baas::service::router
