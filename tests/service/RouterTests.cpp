#include "service/router/Router.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
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

[[nodiscard]] service_router::HealthSnapshot health_snapshot(
    const bool initialized = false,
    const std::uint64_t epoch = 7,
    std::string public_key = "c2VydmVyLWtleQ=="
)
{
    using service_router::HealthArray;
    using service_router::HealthObject;
    using service_router::HealthValue;
    return {
        {
            {"zeta", HealthValue{HealthObject{
                {"waiting_tasks", HealthValue{HealthArray{
                    HealthValue{"lesson"}, HealthValue{"shop"},
                }}},
                {"running", HealthValue{true}},
                {"exit_code", HealthValue{}},
            }}},
            {"alpha", HealthValue{HealthObject{
                {"timestamp", HealthValue{std::int64_t{1'725'000'000'123}}},
                {"current_task", HealthValue{"daily"}},
                {"progress", HealthValue{0.5}},
            }}},
        },
        {initialized, epoch, std::move(public_key)},
    };
}

[[nodiscard]] service_router::HealthReadinessSnapshot ready_snapshot(
    service_router::HealthSnapshot snapshot
)
{
    return {service_router::HealthReadinessState::ready, std::move(snapshot)};
}

void test_health_and_version_routes()
{
    const auto router = service_router::Router::with_health_snapshot(
        {"BAAS Service", "1.2.3"}, health_snapshot()
    );

    const auto health = router.handle({"GET", "/health", {}});
    check(health.status == 200, "health must return 200");
    check(health.body
              == R"({"ok":true,"statuses":{"alpha":{"current_task":"daily","progress":0.5,"timestamp":1725000000123},"zeta":{"exit_code":null,"running":true,"waiting_tasks":["lesson","shop"]}},"auth":{"initialized":false,"pwd_epoch":7,"server_sign_public_key":"c2VydmVyLWtleQ=="}})",
          "health response must match the frozen Python v1 field shape deterministically");
    check(header(health, "Content-Type") == "application/json; charset=utf-8",
          "health must be JSON UTF-8");

    const auto version = router.handle({"GET", "/version", {}});
    check(version.status == 200, "version must return 200");
    check(version.body
              == R"({"api_version":1,"ok":true,"service":"BAAS Service","version":"1.2.3"})",
          "version must expose injected service metadata and API version");
}

class ChangingHealthProvider final : public service_router::HealthSnapshotProvider {
public:
    [[nodiscard]] service_router::HealthReadinessSnapshot readiness_snapshot() const override
    {
        ++calls;
        auto snapshot = ::health_snapshot(calls > 1, static_cast<std::uint64_t>(calls));
        snapshot.statuses.emplace_back(
            "calls", service_router::HealthValue{static_cast<std::int64_t>(calls)}
        );
        return ready_snapshot(std::move(snapshot));
    }

    mutable int calls = 0;
};

class ThrowingHealthProvider final : public service_router::HealthSnapshotProvider {
public:
    [[nodiscard]] service_router::HealthReadinessSnapshot readiness_snapshot() const override
    {
        throw std::runtime_error("backend state unavailable");
    }
};

class InvalidHealthProvider final : public service_router::HealthSnapshotProvider {
public:
    [[nodiscard]] service_router::HealthReadinessSnapshot readiness_snapshot() const override
    {
        auto snapshot = ::health_snapshot();
        snapshot.statuses.emplace_back(
            "invalid", service_router::HealthValue{std::string{"\xC0\xAF"}}
        );
        return ready_snapshot(std::move(snapshot));
    }
};

class OversizedHealthProvider final : public service_router::HealthSnapshotProvider {
public:
    [[nodiscard]] service_router::HealthReadinessSnapshot readiness_snapshot() const override
    {
        auto snapshot = ::health_snapshot();
        snapshot.statuses.emplace_back(
            "large", service_router::HealthValue{std::string(1'024, 'x')}
        );
        return ready_snapshot(std::move(snapshot));
    }
};

class FixedReadinessProvider final : public service_router::HealthSnapshotProvider {
public:
    explicit FixedReadinessProvider(const service_router::HealthReadinessState state)
        : state_(state)
    {}

    [[nodiscard]] service_router::HealthReadinessSnapshot readiness_snapshot() const override
    {
        return {state_, ::health_snapshot()};
    }

private:
    service_router::HealthReadinessState state_;
};

void test_dynamic_health_provider_and_failure_containment()
{
    auto changing = std::make_shared<ChangingHealthProvider>();
    const auto router = service_router::Router::with_health_provider(
        {"BAAS", "dev"}, changing
    );
    const auto first = router.handle({"GET", "/health", {}});
    const auto second = router.handle({"GET", "/health", {}});
    check(first.status == 200 && first.body.find(R"("initialized":false)") != std::string::npos,
          "dynamic provider must supply the first auth snapshot");
    check(second.status == 200 && second.body.find(R"("initialized":true)") != std::string::npos,
          "dynamic provider must be queried again for a fresh auth snapshot");
    check(first.body.find(R"("calls":1)") != std::string::npos
              && second.body.find(R"("calls":2)") != std::string::npos,
          "dynamic provider status changes must be visible per request");
    check(changing->calls == 2, "provider must be invoked exactly once per valid health request");

    auto throwing = std::make_shared<ThrowingHealthProvider>();
    const auto unavailable = service_router::Router::with_health_provider(
        {"BAAS", "dev"}, throwing
    ).handle({"GET", "/health", {}});
    check(unavailable.status == 503
              && unavailable.body.find("health_provider_failed") != std::string::npos,
          "provider exceptions must be contained as a stable unavailable response");

    auto invalid = std::make_shared<InvalidHealthProvider>();
    const auto invalid_response = service_router::Router::with_health_provider(
        {"BAAS", "dev"}, invalid
    ).handle({"GET", "/health", {}});
    check(invalid_response.status == 500
              && invalid_response.body.find("invalid_health_snapshot") != std::string::npos,
          "dynamic invalid UTF-8 must never be emitted as JSON");

    service_router::SizeBudget budget;
    budget.max_response_body_bytes = 128;
    auto oversized = std::make_shared<OversizedHealthProvider>();
    const auto oversized_response = service_router::Router::with_health_provider(
        {"BAAS", "dev"}, oversized, budget
    ).handle({"GET", "/health", {}});
    check(oversized_response.status == 500
              && oversized_response.body.find("response_too_large") != std::string::npos,
          "oversized provider output must use the bounded response fallback");
    check(oversized_response.body.size() <= budget.max_response_body_bytes,
          "provider output fallback must fit the configured response budget");

    const auto starting = service_router::Router::with_health_provider(
        {"BAAS", "dev"},
        std::make_shared<FixedReadinessProvider>(
            service_router::HealthReadinessState::starting
        )
    ).handle({"GET", "/health", {}});
    check(starting.status == 503
              && starting.body
                  == R"({"error":{"code":"health_starting","message":"service readiness is starting","status":503},"ok":false})",
          "starting readiness must return the stable 503 contract");

    const auto failed = service_router::Router::with_health_provider(
        {"BAAS", "dev"},
        std::make_shared<FixedReadinessProvider>(
            service_router::HealthReadinessState::failed
        )
    ).handle({"GET", "/health", {}});
    check(failed.status == 503
              && failed.body
                  == R"({"error":{"code":"health_failed","message":"service readiness failed","status":503},"ok":false})",
          "failed readiness must return the stable 503 contract");

    const auto invalid_state = service_router::Router::with_health_provider(
        {"BAAS", "dev"},
        std::make_shared<FixedReadinessProvider>(
            static_cast<service_router::HealthReadinessState>(99)
        )
    ).handle({"GET", "/health", {}});
    check(invalid_state.status == 500
              && invalid_state.body.find("invalid_health_snapshot") != std::string::npos,
          "invalid provider readiness values must fail closed as stable 500");
}

void test_health_requires_explicit_state_and_json_safe_static_snapshot()
{
    const service_router::Router missing{{"BAAS", "dev"}};
    const auto missing_response = missing.handle({"GET", "/health", {}});
    check(missing_response.status == 503
              && missing_response.body.find("health_unavailable") != std::string::npos,
          "router without injected state must not fabricate readiness");

    bool invalid_utf8_rejected = false;
    try {
        auto snapshot = health_snapshot(false, 0, std::string{"\xC0\xAF"});
        [[maybe_unused]] const auto invalid = service_router::Router::with_health_snapshot(
            {"BAAS", "dev"}, std::move(snapshot)
        );
    } catch (const std::invalid_argument&) {
        invalid_utf8_rejected = true;
    }
    check(invalid_utf8_rejected, "static health auth UTF-8 must be validated at injection");

    bool duplicate_rejected = false;
    try {
        auto snapshot = health_snapshot();
        snapshot.statuses.emplace_back("alpha", service_router::HealthValue{});
        [[maybe_unused]] const auto invalid = service_router::Router::with_health_snapshot(
            {"BAAS", "dev"}, std::move(snapshot)
        );
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    check(duplicate_rejected, "duplicate status keys must be rejected deterministically");

    bool non_finite_rejected = false;
    try {
        auto snapshot = health_snapshot();
        snapshot.statuses.emplace_back(
            "nan", service_router::HealthValue{std::nan("")}
        );
        [[maybe_unused]] const auto invalid = service_router::Router::with_health_snapshot(
            {"BAAS", "dev"}, std::move(snapshot)
        );
    } catch (const std::invalid_argument&) {
        non_finite_rejected = true;
    }
    check(non_finite_rejected, "non-finite status numbers must be rejected as non-JSON");
}

void test_router_retains_shared_provider_owner()
{
    auto provider = std::make_shared<ChangingHealthProvider>();
    const std::weak_ptr<ChangingHealthProvider> lifetime = provider;
    {
        const auto router = service_router::Router::with_health_provider(
            {"BAAS", "owner"}, provider
        );
        provider.reset();
        check(!lifetime.expired(),
              "Router must retain the provider shared owner for request lifetime");
        check(router.handle({"GET", "/health", {}}).status == 200,
              "owned provider must remain callable after the external owner is reset");
    }
    check(lifetime.expired(), "Router destruction must release its provider owner");
}

void test_exact_method_and_path_matching()
{
    const service_router::Router router{{"BAAS", "dev"}};

    const auto missing = router.handle({"GET", "/missing", {}});
    check(missing.status == 404, "unknown exact path must return 404");
    check(missing.body
              == R"({"error":{"code":"route_not_found","message":"no route matches the request path","status":404},"ok":false})",
          "not-found error model must be exact JSON");

    const auto method = router.handle({"POST", "/health", {}});
    check(method.status == 405, "known path with wrong method must return 405");
    check(header(method, "Allow") == "GET", "405 must expose the allowed method");
    check(method.body.find(R"("code":"method_not_allowed")") != std::string::npos,
          "405 must use the shared error model");

    check(router.handle({"GET", "health", {}}).status == 400,
          "relative paths must be rejected");
    check(router.handle({"GET", "/health?verbose=1", {}}).status == 400,
          "adapter must pass a normalized path without query text");
    check(router.handle({"get", "/health", {}}).status == 405,
          "method matching must remain exact and adapter-owned");
    check(router.handle({"GET", "/api/v1/health", {}}).status == 404,
          "invented version-prefixed routes must not masquerade as frozen HTTP v1");
}

void test_json_escaping_is_transport_independent_and_safe()
{
    const std::string version{"v\"\\\n\x01\xC3\xA9"};
    const service_router::Router router{{"B\nAAS", version}};
    const auto response = router.handle({"GET", "/version", {}});
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

    const auto method = router.handle({"DELETE", "/health", {}});
    check(method.status == 400 && method.body.find("method_too_large") != std::string::npos,
          "oversized method must be rejected before routing");
    const auto path = router.handle({"GET", std::string(21, '/'), {}});
    check(path.status == 414 && path.body.find("path_too_large") != std::string::npos,
          "oversized path must return 414");
    const auto request = router.handle({"GET", "/health", "12345"});
    check(request.status == 413 && request.body.find("request_too_large") != std::string::npos,
          "oversized request body must return 413");
    const auto response = router.handle({"GET", "/version", {}});
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
    const auto accepted_response = accepting.handle({"POST", "/shutdown", {}});
    check(accepted_response.status == 202, "accepted shutdown intent must return 202");
    check(accepted_response.body == R"({"accepted":true,"api_version":1,"ok":true})",
          "accepted shutdown intent must have stable JSON");
    check(accepted.calls == 1, "accepted shutdown intent must be invoked exactly once");

    RecordingShutdown rejected{service_router::ShutdownDecision::rejected};
    const service_router::Router rejecting{{"BAAS", "dev"}, {}, &rejected};
    const auto rejected_response = rejecting.handle({"POST", "/shutdown", {}});
    check(rejected_response.status == 409
              && rejected_response.body.find("shutdown_rejected") != std::string::npos,
          "rejected shutdown intent must remain an explicit conflict");
    check(rejected.calls == 1, "rejected shutdown intent must be invoked exactly once");

    const service_router::Router unavailable{{"BAAS", "dev"}};
    check(unavailable.handle({"POST", "/shutdown", {}}).status == 503,
          "missing shutdown intent must return 503");
    check(accepting.handle({"GET", "/shutdown", {}}).status == 405,
          "wrong shutdown method must not invoke the intent");
    check(accepted.calls == 1, "wrong method must not request shutdown");

    service_router::SizeBudget bounded_budget;
    bounded_budget.max_request_body_bytes = 4;
    RecordingShutdown bounded_intent{service_router::ShutdownDecision::accepted};
    const service_router::Router bounded{{"BAAS", "dev"}, bounded_budget, &bounded_intent};
    check(bounded.handle({"POST", "/shutdown", "12345"}).status == 413,
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
    test_dynamic_health_provider_and_failure_containment();
    test_health_requires_explicit_state_and_json_safe_static_snapshot();
    test_router_retains_shared_provider_owner();
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
