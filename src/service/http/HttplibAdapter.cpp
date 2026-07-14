#include "service/http/HttplibAdapter.h"

#include <httplib.h>

#include <stdexcept>
#include <string>

namespace baas::service::http {
namespace {

void apply_router_response(const router::Response& source, httplib::Response& destination)
{
    destination.status = source.status;
    destination.headers.clear();
    for (const auto& header : source.headers) {
        destination.set_header(header.name, header.value);
    }
    destination.body = source.body;
}

[[nodiscard]] std::string error_body(
    const int status,
    const char* code,
    const char* message
)
{
    return std::string{"{\"error\":{\"code\":\""} + code + "\",\"message\":\""
        + message + "\",\"status\":" + std::to_string(status) + "},\"ok\":false}";
}

}  // namespace

HttplibAdapter::HttplibAdapter(router::Router& router, const InputBudget budget)
    : router_(router), budget_(budget)
{
    const auto& router_budget = router_.budget();
    if (budget_.max_method_bytes == 0 || budget_.max_path_bytes == 0
        || budget_.max_body_bytes == 0) {
        throw std::invalid_argument("HTTP adapter input budget must be positive");
    }
    if (budget_.max_method_bytes > router_budget.max_method_bytes
        || budget_.max_path_bytes > router_budget.max_path_bytes
        || budget_.max_body_bytes > router_budget.max_request_body_bytes) {
        throw std::invalid_argument("HTTP adapter input budget must not exceed router budget");
    }
}

const InputBudget& HttplibAdapter::budget() const noexcept
{
    return budget_;
}

void HttplibAdapter::install(httplib::Server& server) const
{
    server.set_payload_max_length(budget_.max_body_bytes);
    const auto handler = [this](const httplib::Request& request, httplib::Response& response) {
        handle(request, response);
    };
    constexpr const char* any_path = R"(.*)";
    server.Get(any_path, handler);
    server.Post(any_path, handler);
    server.Put(any_path, handler);
    server.Patch(any_path, handler);
    server.Delete(any_path, handler);
    server.Options(any_path, handler);
    server.set_error_handler(
        [this](const httplib::Request&, httplib::Response& response) {
            if (response.status != 413) {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            apply_transport_error(
                response,
                413,
                "request_too_large",
                "request body exceeds HTTP transport budget"
            );
            return httplib::Server::HandlerResponse::Handled;
        }
    );
}

void HttplibAdapter::handle(
    const httplib::Request& request,
    httplib::Response& response
) const
{
    if (request.method.size() > budget_.max_method_bytes) {
        apply_transport_error(
            response,
            400,
            "method_too_large",
            "request method exceeds HTTP transport budget"
        );
        return;
    }
    if (request.path.size() > budget_.max_path_bytes) {
        apply_transport_error(
            response,
            414,
            "path_too_large",
            "request path exceeds HTTP transport budget"
        );
        return;
    }
    if (request.body.size() > budget_.max_body_bytes) {
        apply_transport_error(
            response,
            413,
            "request_too_large",
            "request body exceeds HTTP transport budget"
        );
        return;
    }
    apply_router_response(
        router_.handle(router::Request{request.method, request.path, request.body}),
        response
    );
}

void HttplibAdapter::apply_transport_error(
    httplib::Response& response,
    const int status,
    const char* code,
    const char* message
) const
{
    response.status = status;
    response.headers.clear();
    response.set_header("Content-Type", "application/json; charset=utf-8");
    response.body = error_body(status, code, message);
}

}  // namespace baas::service::http
