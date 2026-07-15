#include "service/http/HttplibAdapter.h"

#include <httplib.h>

#include <stdexcept>
#include <string>
#include <string_view>

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

void replace_header(
    httplib::Response& response,
    const char* name,
    const std::string_view value
)
{
    response.headers.erase(name);
    response.set_header(name, std::string{value});
}

void apply_actual_cors_headers(
    httplib::Response& response,
    const CorsEvaluation& evaluation
)
{
    response.headers.erase("Access-Control-Allow-Methods");
    response.headers.erase("Access-Control-Allow-Headers");
    replace_header(response, "Access-Control-Allow-Origin", evaluation.allow_origin);
    replace_header(response, "Access-Control-Allow-Credentials", "true");
    replace_header(response, "Vary", "Origin");
}

void apply_rejection_vary(
    httplib::Response& response,
    const bool may_be_preflight
)
{
    replace_header(
        response,
        "Vary",
        may_be_preflight
            ? "Origin, Access-Control-Request-Method, Access-Control-Request-Headers"
            : "Origin"
    );
}

void apply_preflight_cors_headers(
    httplib::Response& response,
    const CorsEvaluation& evaluation
)
{
    replace_header(response, "Access-Control-Allow-Origin", evaluation.allow_origin);
    replace_header(response, "Access-Control-Allow-Credentials", "true");
    replace_header(response, "Access-Control-Allow-Methods", evaluation.allow_methods);
    if (!evaluation.allow_headers.empty()) {
        replace_header(response, "Access-Control-Allow-Headers", evaluation.allow_headers);
    } else {
        response.headers.erase("Access-Control-Allow-Headers");
    }
    replace_header(
        response,
        "Vary",
        "Origin, Access-Control-Request-Method, Access-Control-Request-Headers"
    );
}

[[nodiscard]] std::optional<std::string_view> single_header(
    const httplib::Request& request,
    const char* name,
    bool& malformed
)
{
    const auto range = request.headers.equal_range(name);
    if (range.first == range.second) return std::nullopt;
    const auto first = range.first;
    auto next = first;
    ++next;
    if (next != range.second) {
        malformed = true;
        return std::nullopt;
    }
    // Request owns header values for the entire synchronous evaluation. A view
    // avoids copying attacker-controlled bytes before policy length gates.
    return first->second;
}

struct RequestCorsEvaluation {
    CorsEvaluation cors;
    bool may_be_preflight = false;
};

[[nodiscard]] RequestCorsEvaluation evaluate_cors_request(
    const OriginPolicy& policy,
    const httplib::Request& request
)
{
    bool malformed_headers = false;
    const auto origin = single_header(request, "Origin", malformed_headers);
    const auto requested_method = single_header(
        request, "Access-Control-Request-Method", malformed_headers
    );
    const auto requested_headers = single_header(
        request, "Access-Control-Request-Headers", malformed_headers
    );
    return {
        policy.evaluate({
            origin,
            request.method,
            requested_method,
            requested_headers,
            malformed_headers,
        }),
        request.method == "OPTIONS" && requested_method.has_value(),
    };
}

}  // namespace

HttplibAdapter::HttplibAdapter(
    router::Router& router,
    const InputBudget budget,
    CorsPolicyConfig cors_config
)
    : router_(router), budget_(budget), origin_policy_(std::move(cors_config))
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

const OriginPolicy& HttplibAdapter::origin_policy() const noexcept
{
    return origin_policy_;
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
        [this](const httplib::Request& request, httplib::Response& response) {
            if (response.status != 413) {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            const auto evaluated = evaluate_cors_request(origin_policy_, request);
            if (!evaluated.cors.allowed()) {
                apply_transport_error(
                    response,
                    evaluated.cors.status,
                    evaluated.cors.code.c_str(),
                    evaluated.cors.message.c_str()
                );
                apply_rejection_vary(response, evaluated.may_be_preflight);
                return httplib::Server::HandlerResponse::Handled;
            }
            apply_transport_error(
                response,
                413,
                "request_too_large",
                "request body exceeds HTTP transport budget"
            );
            if (evaluated.cors.decision == CorsDecision::actual_request) {
                apply_actual_cors_headers(response, evaluated.cors);
            } else if (evaluated.cors.decision == CorsDecision::preflight) {
                apply_preflight_cors_headers(response, evaluated.cors);
            }
            return httplib::Server::HandlerResponse::Handled;
        }
    );
}

void HttplibAdapter::handle(
    const httplib::Request& request,
    httplib::Response& response
) const
{
    const auto evaluated = evaluate_cors_request(origin_policy_, request);
    const auto& cors = evaluated.cors;
    if (!cors.allowed()) {
        apply_transport_error(response, cors.status, cors.code.c_str(), cors.message.c_str());
        apply_rejection_vary(response, evaluated.may_be_preflight);
        return;
    }

    const auto finish_actual_cors = [&] {
        if (cors.decision == CorsDecision::actual_request) {
            apply_actual_cors_headers(response, cors);
        }
    };
    if (request.method.size() > budget_.max_method_bytes) {
        apply_transport_error(
            response,
            400,
            "method_too_large",
            "request method exceeds HTTP transport budget"
        );
        finish_actual_cors();
        return;
    }
    if (request.path.size() > budget_.max_path_bytes) {
        apply_transport_error(
            response,
            414,
            "path_too_large",
            "request path exceeds HTTP transport budget"
        );
        finish_actual_cors();
        return;
    }
    if (request.body.size() > budget_.max_body_bytes) {
        apply_transport_error(
            response,
            413,
            "request_too_large",
            "request body exceeds HTTP transport budget"
        );
        finish_actual_cors();
        return;
    }
    if (cors.decision == CorsDecision::preflight) {
        response.status = 204;
        response.headers.clear();
        response.body.clear();
        apply_preflight_cors_headers(response, cors);
        return;
    }
    bool malformed_cookie_headers = false;
    const auto cookie = single_header(request, "Cookie", malformed_cookie_headers);
    apply_router_response(
        router_.handle(router::Request{
            request.method,
            request.path,
            request.body,
            cookie,
            malformed_cookie_headers,
            false,
        }),
        response
    );
    finish_actual_cors();
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
