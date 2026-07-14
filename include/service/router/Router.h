#pragma once

#include <cstddef>
#include <string>
#include <string_view>
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

enum class ShutdownDecision {
    accepted,
    rejected,
};

class ShutdownIntent {
public:
    virtual ~ShutdownIntent() = default;
    [[nodiscard]] virtual ShutdownDecision request_shutdown() noexcept = 0;
};

class Router final {
public:
    explicit Router(
        ServiceInfo service,
        SizeBudget budget = {},
        ShutdownIntent* shutdown_intent = nullptr
    );

    [[nodiscard]] Response handle(const Request& request) const;
    [[nodiscard]] const SizeBudget& budget() const noexcept;

private:
    [[nodiscard]] Response route(const Request& request) const;
    [[nodiscard]] Response finish(Response response) const;
    [[nodiscard]] static Response json_response(int status, std::string body);
    [[nodiscard]] static Response error(int status, std::string_view code, std::string_view message);

    ServiceInfo service_;
    SizeBudget budget_;
    ShutdownIntent* shutdown_intent_;
};

}  // namespace baas::service::router
