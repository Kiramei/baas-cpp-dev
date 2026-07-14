#pragma once

#include "service/router/Router.h"

#include <cstddef>

namespace httplib {
struct Request;
struct Response;
class Server;
}  // namespace httplib

namespace baas::service::http {

struct InputBudget {
    std::size_t max_method_bytes = 16;
    std::size_t max_path_bytes = 2'048;
    std::size_t max_body_bytes = 1'048'576;
};

class HttplibAdapter final {
public:
    explicit HttplibAdapter(router::Router& router, InputBudget budget = {});

    HttplibAdapter(const HttplibAdapter&) = delete;
    HttplibAdapter& operator=(const HttplibAdapter&) = delete;
    HttplibAdapter(HttplibAdapter&&) = delete;
    HttplibAdapter& operator=(HttplibAdapter&&) = delete;

    void install(httplib::Server& server) const;
    void handle(const httplib::Request& request, httplib::Response& response) const;

    [[nodiscard]] const InputBudget& budget() const noexcept;

private:
    void apply_transport_error(
        httplib::Response& response,
        int status,
        const char* code,
        const char* message
    ) const;

    router::Router& router_;
    InputBudget budget_;
};

}  // namespace baas::service::http
