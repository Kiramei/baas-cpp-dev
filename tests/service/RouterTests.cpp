#include "service/router/Router.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace service_router = baas::service::router;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::string_view header(
    const service_router::Response& response,
    const std::string_view name
)
{
    for (const auto& candidate : response.headers) {
        if (candidate.name == name) return candidate.value;
    }
    return {};
}

class RecordingShutdown final : public service_router::ShutdownIntent {
public:
    explicit RecordingShutdown(const service_router::ShutdownDecision decision)
        : decision_(decision)
    {}

    [[nodiscard]] service_router::ShutdownDecision request_shutdown() noexcept override
    {
        ++calls;
        return decision_;
    }

    int calls = 0;

private:
    service_router::ShutdownDecision decision_;
};

void test_health_and_version_routes()
{
    const service_router::Router router{{"BAAS Service", "1.2.3"}};

    const auto health = router.handle({"GET", "/api/v1/health", {}});
    check(health.status == 200, "health must return 200");
    check(health.body == R"({"api_version":1,"ok":true,"status":"healthy"})",
          "health response must be stable versioned JSON");
    check(header(health, "Content-Type") == "application/json; charset=utf-8",
          "health must be JSON UTF-8");

    const auto version = router.handle({"GET", "/api/v1/version", {}});
    check(version.status == 200, "version must return 200");
    check(version.body
              == R"({"api_version":1,"ok":true,"service":"BAAS Service","version":"1.2.3"})",
          "version must expose injected service metadata and API version");
}

void test_exact_method_and_path_matching()
{
    const service_router::Router router{{"BAAS", "dev"}};

    const auto missing = router.handle({"GET", "/api/v1/missing", {}});
    check(missing.status == 404, "unknown exact path must return 404");
    check(missing.body
              == R"({"error":{"code":"route_not_found","message":"no route matches the request path","status":404},"ok":false})",
          "not-found error model must be exact JSON");

    const auto method = router.handle({"POST", "/api/v1/health", {}});
    check(method.status == 405, "known path with wrong method must return 405");
    check(header(method, "Allow") == "GET", "405 must expose the allowed method");
    check(method.body.find(R"("code":"method_not_allowed")") != std::string::npos,
          "405 must use the shared error model");

    check(router.handle({"GET", "api/v1/health", {}}).status == 400,
          "relative paths must be rejected");
    check(router.handle({"GET", "/api/v1/health?verbose=1", {}}).status == 400,
          "adapter must pass a normalized path without query text");
    check(router.handle({"get", "/api/v1/health", {}}).status == 405,
          "method matching must remain exact and adapter-owned");
}

void test_json_escaping_is_transport_independent_and_safe()
{
    const std::string version{"v\"\\\n\x01\xC3\xA9"};
    const service_router::Router router{{"B\nAAS", version}};
    const auto response = router.handle({"GET", "/api/v1/version", {}});
    check(response.status == 200, "escaped version response must remain valid");
    const auto expected = std::string{R"({"api_version":1,"ok":true,"service":"B\nAAS","version":"v\"\\\n\u0001)"}
        + "\xC3\xA9" + R"("})";
    check(response.body == expected,
          "quotes and controls must be escaped while valid UTF-8 keeps its meaning");
}

void test_request_and_response_budgets()
{
    service_router::SizeBudget budget;
    budget.max_method_bytes = 4;
    budget.max_path_bytes = 20;
    budget.max_request_body_bytes = 4;
    budget.max_response_body_bytes = 128;
    const service_router::Router router{{"BAAS", std::string(200, 'v')}, budget};

    const auto method = router.handle({"DELETE", "/api/v1/health", {}});
    check(method.status == 400 && method.body.find("method_too_large") != std::string::npos,
          "oversized method must be rejected before routing");
    const auto path = router.handle({"GET", std::string(21, '/'), {}});
    check(path.status == 414 && path.body.find("path_too_large") != std::string::npos,
          "oversized path must return 414");
    const auto request = router.handle({"GET", "/api/v1/health", "12345"});
    check(request.status == 413 && request.body.find("request_too_large") != std::string::npos,
          "oversized request body must return 413");
    const auto response = router.handle({"GET", "/api/v1/version", {}});
    check(response.status == 500, "oversized generated response must be contained");
    check(response.body
              == R"({"error":{"code":"response_too_large","message":"response exceeds configured budget","status":500},"ok":false})",
          "response budget fallback must itself be stable and bounded");
    check(response.body.size() <= budget.max_response_body_bytes,
          "response budget fallback must fit the configured limit");
}

void test_shutdown_intent_is_injected_and_does_not_terminate()
{
    RecordingShutdown accepted{service_router::ShutdownDecision::accepted};
    const service_router::Router accepting{{"BAAS", "dev"}, {}, &accepted};
    const auto accepted_response = accepting.handle({"POST", "/api/v1/shutdown", {}});
    check(accepted_response.status == 202, "accepted shutdown intent must return 202");
    check(accepted_response.body == R"({"accepted":true,"api_version":1,"ok":true})",
          "accepted shutdown intent must have stable JSON");
    check(accepted.calls == 1, "accepted shutdown intent must be invoked exactly once");

    RecordingShutdown rejected{service_router::ShutdownDecision::rejected};
    const service_router::Router rejecting{{"BAAS", "dev"}, {}, &rejected};
    const auto rejected_response = rejecting.handle({"POST", "/api/v1/shutdown", {}});
    check(rejected_response.status == 409
              && rejected_response.body.find("shutdown_rejected") != std::string::npos,
          "rejected shutdown intent must remain an explicit conflict");
    check(rejected.calls == 1, "rejected shutdown intent must be invoked exactly once");

    const service_router::Router unavailable{{"BAAS", "dev"}};
    check(unavailable.handle({"POST", "/api/v1/shutdown", {}}).status == 503,
          "missing shutdown intent must return 503");
    check(accepting.handle({"GET", "/api/v1/shutdown", {}}).status == 405,
          "wrong shutdown method must not invoke the intent");
    check(accepted.calls == 1, "wrong method must not request shutdown");

    service_router::SizeBudget bounded_budget;
    bounded_budget.max_request_body_bytes = 4;
    RecordingShutdown bounded_intent{service_router::ShutdownDecision::accepted};
    const service_router::Router bounded{{"BAAS", "dev"}, bounded_budget, &bounded_intent};
    check(bounded.handle({"POST", "/api/v1/shutdown", "12345"}).status == 413,
          "oversized shutdown body must be rejected before the intent");
    check(bounded_intent.calls == 0, "request budget must contain shutdown side effects");
}

void test_invalid_construction_is_rejected()
{
    bool empty_service_rejected = false;
    try {
        [[maybe_unused]] const service_router::Router invalid{{"", "dev"}};
    } catch (const std::invalid_argument&) {
        empty_service_rejected = true;
    }
    check(empty_service_rejected, "empty service metadata must be rejected");

    bool invalid_utf8_rejected = false;
    try {
        [[maybe_unused]] const service_router::Router invalid{{"BAAS", "\xC0\xAF"}};
    } catch (const std::invalid_argument&) {
        invalid_utf8_rejected = true;
    }
    check(invalid_utf8_rejected, "invalid UTF-8 service metadata must be rejected");

    bool small_response_budget_rejected = false;
    try {
        service_router::SizeBudget budget;
        budget.max_response_body_bytes = 127;
        [[maybe_unused]] const service_router::Router invalid{{"BAAS", "dev"}, budget};
    } catch (const std::invalid_argument&) {
        small_response_budget_rejected = true;
    }
    check(small_response_budget_rejected,
          "response budget must always fit the bounded fallback error");
}

}  // namespace

int main()
{
    test_health_and_version_routes();
    test_exact_method_and_path_matching();
    test_json_escaping_is_transport_independent_and_safe();
    test_request_and_response_budgets();
    test_shutdown_intent_is_injected_and_does_not_terminate();
    test_invalid_construction_is_rejected();
    if (failures != 0) {
        std::cerr << failures << " service router test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "service router tests passed\n";
    return EXIT_SUCCESS;
}
