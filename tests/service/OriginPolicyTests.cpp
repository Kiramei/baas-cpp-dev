#include "service/http/OriginPolicy.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace service_http = baas::service::http;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

service_http::CorsEvaluation evaluate(
    const service_http::OriginPolicy& policy,
    const std::optional<std::string_view> origin,
    const std::string_view method = "GET",
    const std::optional<std::string_view> requested_method = std::nullopt,
    const std::optional<std::string_view> requested_headers = std::nullopt,
    const bool malformed = false
)
{
    return policy.evaluate({origin, method, requested_method, requested_headers, malformed});
}

void test_default_boundary_and_native_behavior()
{
    const service_http::OriginPolicy policy;
    check(policy.allowed_origins().size() == 3, "default policy must have three evidenced origins");
    check(evaluate(policy, std::nullopt).decision == service_http::CorsDecision::native_request,
          "missing Origin must preserve native loopback clients");

    for (const auto origin : {
             "http://localhost:8191",
             "http://127.0.0.1:8191",
             "http://tauri.localhost",
         }) {
        const auto result = evaluate(policy, origin);
        check(result.decision == service_http::CorsDecision::actual_request,
              "each evidenced default origin must be allowed");
        check(result.allow_origin == origin, "allowed origin must be canonical and exact");
    }

    for (const auto origin : {
             "http://localhost:8192",
             "https://localhost:8191",
             "http://192.168.1.2:8191",
             "https://public.example",
             "tauri://localhost",
         }) {
        const auto result = evaluate(policy, origin);
        check(result.decision == service_http::CorsDecision::reject
                  && result.code == "origin_not_allowed",
              "unconfigured valid origins must fail closed");
    }
}

void test_strict_origin_parsing_and_normalization()
{
    service_http::CorsPolicyConfig config;
    config.allowed_origins = {
        "HTTP://LOCALHOST:80",
        "https://Example.COM:443",
        "tauri://localhost",
        "http://[::1]:8191",
    };
    const service_http::OriginPolicy policy{config};
    check(evaluate(policy, "http://localhost").allowed(), "default HTTP port must canonicalize");
    check(evaluate(policy, "https://example.com").allowed(), "default HTTPS port must canonicalize");
    check(evaluate(policy, "TAURI://LOCALHOST").allow_origin == "tauri://localhost",
          "Tauri origin must canonicalize case");
    check(evaluate(policy, "http://[::1]:8191").allowed(), "canonical IPv6 loopback may be explicit");

    for (const auto origin : {
             "",
             "null",
             "*",
             "file://localhost",
             "javascript://localhost",
             "ws://localhost:8191",
             "http://user@localhost:8191",
             "http://localhost:8191/",
             "http://localhost:8191/path",
             "http://localhost:8191?q=1",
             "http://localhost:8191#fragment",
             "http://local%68ost:8191",
             "http://localhost:8191\r\nX-Evil: 1",
             "http://127.1:8191",
             "http://127.000.0.1:8191",
             "http://256.0.0.1:8191",
             "http://localhost:",
             "http://localhost:080",
             "http://[::1]:",
             "http://[2001:db8::1]:8191",
             "tauri://evil.example",
             "tauri://localhost:8191",
         }) {
        const auto result = evaluate(policy, origin);
        check(result.decision == service_http::CorsDecision::reject
                  && result.code == "invalid_origin",
              "malformed/malicious Origin must be rejected as invalid");
    }
    const auto oversized = evaluate(policy, std::string(service_http::cors_max_origin_bytes + 1, 'a'));
    check(oversized.code == "invalid_origin", "oversized Origin must be rejected before lookup");
}

void test_configuration_validation_and_dos_bounds()
{
    const auto rejects = [](service_http::CorsPolicyConfig config) {
        try {
            [[maybe_unused]] service_http::OriginPolicy policy{std::move(config)};
            return false;
        } catch (const std::invalid_argument&) {
            return true;
        }
    };

    for (const auto invalid : {
             "*",
             "null",
             "file://localhost",
             "http://user@localhost",
             "http://localhost/path",
             "http://bad_host",
         }) {
        service_http::CorsPolicyConfig config;
        config.allowed_origins = {invalid};
        check(rejects(std::move(config)), "invalid configured origin must fail construction");
    }
    service_http::CorsPolicyConfig too_many;
    too_many.allowed_origins.clear();
    for (std::size_t i = 0; i <= service_http::cors_max_allowed_origins; ++i) {
        too_many.allowed_origins.push_back("http://host" + std::to_string(i) + ".example");
    }
    check(rejects(std::move(too_many)), "origin allowlist count must be bounded");

    service_http::CorsPolicyConfig empty_methods;
    empty_methods.allowed_methods.clear();
    check(rejects(std::move(empty_methods)), "empty method allowlist must fail construction");

    service_http::CorsPolicyConfig bad_header;
    bad_header.allowed_headers = {"content-type\r\nx-evil"};
    check(rejects(std::move(bad_header)), "invalid configured header token must fail construction");
}

void test_actual_and_preflight_matrix()
{
    const service_http::OriginPolicy policy;
    check(evaluate(policy, "http://localhost:8191", "PUT").code == "cors_method_not_allowed",
          "actual browser method must use the same allowlist as preflight");
    check(evaluate(policy, "http://localhost:8191", "OPTIONS", "PUT").code
              == "cors_method_not_allowed",
          "preflight method rejection must be stable");
    check(evaluate(policy, "http://localhost:8191", "OPTIONS",
                   std::string(service_http::cors_max_method_bytes + 1, 'A')).code
              == "cors_invalid_method",
          "preflight method must be bounded before allocation");

    const auto preflight = evaluate(
        policy,
        "http://localhost:8191",
        "OPTIONS",
        "post",
        " Content-Type, ACCEPT, content-type "
    );
    check(preflight.decision == service_http::CorsDecision::preflight,
          "allowed preflight must be recognized");
    check(preflight.allow_methods == "GET, HEAD, POST",
          "preflight allow-methods must be deterministic");
    check(preflight.allow_headers == "accept, content-type",
          "requested headers must be normalized, deduplicated, and sorted");

    const auto tab_ows = evaluate(
        policy, "http://localhost:8191", "OPTIONS", "GET", "accept,\tcontent-type"
    );
    check(tab_ows.decision == service_http::CorsDecision::preflight,
          "RFC OWS around requested header tokens must be accepted");

    check(evaluate(policy, "http://localhost:8191", "OPTIONS", "POST", "authorization").code
              == "cors_headers_not_allowed",
          "unconfigured preflight header must fail closed");
    check(evaluate(policy, "http://localhost:8191", "OPTIONS", "POST", "x-ok,,accept").code
              == "cors_invalid_headers",
          "malformed preflight header list must be rejected");
    check(evaluate(policy, "http://localhost:8191", "OPTIONS", "POST", "accept\r\nx-evil").code
              == "cors_invalid_headers",
          "preflight CRLF must be rejected");
    const std::string non_ascii_header{"accept,\xC3\xA9"};
    check(evaluate(policy, "http://localhost:8191", "OPTIONS", "POST", non_ascii_header).code
              == "cors_invalid_headers",
          "non-ASCII header tokens must be rejected independently of locale");
    check(evaluate(policy, "http://localhost:8191", "OPTIONS", "POST",
                   std::string(service_http::cors_max_preflight_headers_bytes + 1, 'x')).code
              == "cors_invalid_headers",
          "preflight header bytes must be bounded");
    check(evaluate(policy, "http://localhost:8191", "GET", std::nullopt, std::nullopt, true).code
              == "cors_invalid_request",
          "duplicate relevant headers must reject deterministically");
}

void test_configurable_no_origin_boundary()
{
    service_http::CorsPolicyConfig config;
    config.allow_requests_without_origin = false;
    const service_http::OriginPolicy policy{config};
    const auto result = evaluate(policy, std::nullopt);
    check(result.code == "origin_required", "embedding may explicitly require Origin");
}

void test_concurrent_evaluation_is_deterministic()
{
    const service_http::OriginPolicy policy;
    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 12; ++thread) {
        threads.emplace_back([&] {
            for (int iteration = 0; iteration < 2'000; ++iteration) {
                const auto allowed = evaluate(
                    policy, "http://localhost:8191", "OPTIONS", "POST", "content-type"
                );
                const auto rejected = evaluate(policy, "https://evil.example");
                if (allowed.decision != service_http::CorsDecision::preflight
                    || allowed.allow_origin != "http://localhost:8191"
                    || allowed.allow_headers != "content-type"
                    || rejected.code != "origin_not_allowed") {
                    mismatches.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& thread : threads) thread.join();
    check(mismatches.load() == 0, "shared policy evaluation must be race-free and deterministic");
}

}  // namespace

int main()
{
    test_default_boundary_and_native_behavior();
    test_strict_origin_parsing_and_normalization();
    test_configuration_validation_and_dos_bounds();
    test_actual_and_preflight_matrix();
    test_configurable_no_origin_boundary();
    test_concurrent_evaluation_is_deterministic();
    if (failures != 0) {
        std::cerr << failures << " origin policy test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "origin policy tests passed\n";
    return EXIT_SUCCESS;
}
