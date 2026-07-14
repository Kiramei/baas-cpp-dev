#include "service/http/HttplibAdapter.h"
#include "service/router/Router.h"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace service_http = baas::service::http;
namespace service_router = baas::service::router;

namespace {

int failures = 0;

[[nodiscard]] service_router::Router router_with_health(
    service_router::ServiceInfo service,
    service_router::SizeBudget budget = {}
)
{
    return service_router::Router::with_health_snapshot(
        std::move(service),
        service_router::HealthSnapshot{{}, {false, 0, "dGVzdC1rZXk="}},
        budget
    );
}

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] httplib::Request request(
    std::string method,
    std::string path,
    std::string body = {}
)
{
    httplib::Request result;
    result.method = std::move(method);
    result.path = std::move(path);
    result.body = std::move(body);
    return result;
}

void test_exact_request_and_response_mapping()
{
    auto router = router_with_health({"BAAS Service", "1.2.3"});
    service_http::HttplibAdapter adapter{router};

    httplib::Response response;
    adapter.handle(request("GET", "/health"), response);
    check(response.status == 200, "health status must cross the adapter exactly");
    check(response.body
              == R"({"ok":true,"statuses":{},"auth":{"initialized":false,"pwd_epoch":0,"server_sign_public_key":"dGVzdC1rZXk="}})",
          "health body must cross the adapter exactly");
    check(response.get_header_value("Content-Type") == "application/json; charset=utf-8",
          "health content type must cross the adapter exactly");

    response = {};
    adapter.handle(request("POST", "/health", "{}"), response);
    check(response.status == 405, "method mismatch status must cross exactly");
    check(response.get_header_value("Allow") == "GET",
          "router Allow header must cross the adapter exactly");
    check(response.body.find(R"("code":"method_not_allowed")") != std::string::npos,
          "method mismatch body must cross exactly");

    response = {};
    adapter.handle(request("GET", "/version"), response);
    check(response.status == 200, "version status must cross exactly");
    check(response.body
              == R"({"api_version":1,"ok":true,"service":"BAAS Service","version":"1.2.3"})",
          "version body must cross exactly");

    response = {};
    adapter.handle(request("GET", "/api/v1/health"), response);
    check(response.status == 404, "adapter must not invent a versioned HTTP v1 path");
}

void test_transport_limits_precede_router_effects()
{
    service_router::SizeBudget router_budget;
    router_budget.max_method_bytes = 16;
    router_budget.max_path_bytes = 128;
    router_budget.max_request_body_bytes = 64;
    service_router::Router router{{"BAAS", "dev"}, router_budget};
    const service_http::InputBudget input_budget{8, 64, 32};
    service_http::HttplibAdapter adapter{router, input_budget};

    httplib::Response response;
    adapter.handle(request("TOO-LONG!", "/health"), response);
    check(response.status == 400 && response.body.find("method_too_large") != std::string::npos,
          "transport method limit must reject before router dispatch");

    response = {};
    adapter.handle(request("GET", std::string(65, '/')), response);
    check(response.status == 414 && response.body.find("path_too_large") != std::string::npos,
          "transport path limit must reject before router dispatch");

    response = {};
    adapter.handle(request("POST", "/shutdown", std::string(33, 'x')), response);
    check(response.status == 413 && response.body.find("request_too_large") != std::string::npos,
          "transport body limit must reject before router dispatch");
    check(response.get_header_value("Content-Type") == "application/json; charset=utf-8",
          "transport errors must use the shared JSON content type");
}

void test_adapter_budget_must_fit_router_budget()
{
    service_router::SizeBudget router_budget;
    router_budget.max_request_body_bytes = 8;
    service_router::Router router{{"BAAS", "dev"}, router_budget};
    bool rejected = false;
    try {
        [[maybe_unused]] service_http::HttplibAdapter invalid{
            router,
            service_http::InputBudget{16, 2'048, 9},
        };
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "transport budget must not exceed the router budget");
}

void test_origin_cors_actual_preflight_and_rejection_matrix()
{
    auto router = router_with_health({"BAAS", "cors"});
    service_http::HttplibAdapter adapter{router};

    auto native = request("GET", "/health");
    httplib::Response response;
    adapter.handle(native, response);
    check(response.status == 200, "native no-Origin request must retain router behavior");
    check(!response.has_header("Access-Control-Allow-Origin"),
          "native request must not receive fabricated CORS headers");

    auto allowed = request("GET", "/health");
    allowed.headers.emplace("Origin", "http://localhost:8191");
    response = {};
    adapter.handle(allowed, response);
    check(response.status == 200, "allowed actual browser request must reach Router");
    check(response.get_header_value("Access-Control-Allow-Origin") == "http://localhost:8191",
          "actual response must echo only the canonical allowed origin");
    check(response.get_header_value("Access-Control-Allow-Credentials") == "true",
          "actual allowed response must permit credential flow");
    check(response.get_header_value("Vary") == "Origin",
          "actual allowed response must vary on Origin");
    check(!response.has_header("Access-Control-Allow-Methods"),
          "actual response must not emit preflight-only headers");

    auto denied = request("GET", "/health");
    denied.headers.emplace("Origin", "https://evil.example");
    response = {};
    adapter.handle(denied, response);
    check(response.status == 403,
          "unconfigured browser origin must be rejected before Router dispatch");
    check(response.body
              == R"({"error":{"code":"origin_not_allowed","message":"request Origin is not allowed","status":403},"ok":false})",
          "origin rejection response must be stable and JSON safe");
    check(response.get_header_value("Vary") == "Origin",
          "actual origin rejection must vary on Origin");
    check(!response.has_header("Access-Control-Allow-Origin")
              && !response.has_header("Access-Control-Allow-Credentials"),
          "origin rejection must not emit allow or credential headers");

    auto preflight = request("OPTIONS", "/health");
    preflight.headers.emplace("Origin", "http://127.0.0.1:8191");
    preflight.headers.emplace("Access-Control-Request-Method", "GET");
    preflight.headers.emplace("Access-Control-Request-Headers", "Content-Type, Accept");
    response = {};
    adapter.handle(preflight, response);
    check(response.status == 204 && response.body.empty(),
          "allowed preflight must terminate in the adapter with empty 204");
    check(response.get_header_value("Access-Control-Allow-Origin")
              == "http://127.0.0.1:8191",
          "preflight must return exact allowed origin");
    check(response.get_header_value("Access-Control-Allow-Credentials") == "true",
          "preflight credentials must match actual response");
    check(response.get_header_value("Access-Control-Allow-Methods") == "GET, HEAD, POST",
          "preflight methods must be deterministic");
    check(response.get_header_value("Access-Control-Allow-Headers") == "accept, content-type",
          "preflight requested headers must be normalized and bounded");
    check(response.get_header_value("Vary")
              == "Origin, Access-Control-Request-Method, Access-Control-Request-Headers",
          "preflight response must vary on all decision inputs");

    auto denied_preflight = request("OPTIONS", "/health");
    denied_preflight.headers.emplace("Origin", "http://localhost:8191");
    denied_preflight.headers.emplace("Access-Control-Request-Method", "DELETE");
    response = {};
    adapter.handle(denied_preflight, response);
    check(response.status == 403
              && response.body.find("cors_method_not_allowed") != std::string::npos,
          "preflight method rejection must be stable");
    check(!response.has_header("Access-Control-Allow-Origin"),
          "denied preflight must not emit allow-origin");

    auto duplicate = request("GET", "/health");
    duplicate.headers.emplace("Origin", "http://localhost:8191");
    duplicate.headers.emplace("Origin", "http://127.0.0.1:8191");
    response = {};
    adapter.handle(duplicate, response);
    check(response.status == 403 && response.body.find("cors_invalid_request") != std::string::npos,
          "multiple Origin fields must fail closed");

    auto many_duplicates = request("GET", "/health");
    for (int i = 0; i < 128; ++i) {
        many_duplicates.headers.emplace("Origin", "http://localhost:8191");
    }
    response = {};
    adapter.handle(many_duplicates, response);
    check(response.status == 403 && response.body.find("cors_invalid_request") != std::string::npos,
          "many duplicate Origin fields must reject with constant cardinality work");
}

void test_custom_policy_is_wired_into_adapter()
{
    auto router = router_with_health({"BAAS", "cors-custom"});
    service_http::CorsPolicyConfig config;
    config.allowed_origins = {"https://configured-long-origin.example"};
    config.allowed_methods = {"GET", "POST"};
    config.allowed_headers = {"x-this-is-a-long-request-header-name"};
    config.allow_requests_without_origin = false;
    service_http::HttplibAdapter adapter{router, {}, config};

    httplib::Response response;
    adapter.handle(request("GET", "/health"), response);
    check(response.status == 403 && response.body.find("origin_required") != std::string::npos,
          "adapter must honor configured no-Origin boundary");

    auto allowed = request("GET", "/health");
    allowed.headers.emplace("Origin", "https://CONFIGURED-LONG-ORIGIN.example:443");
    response = {};
    adapter.handle(allowed, response);
    check(response.status == 200
              && response.get_header_value("Access-Control-Allow-Origin")
                  == "https://configured-long-origin.example",
          "adapter must use custom canonical allowlist");

    auto preflight = request("OPTIONS", "/health");
    preflight.headers.emplace("Origin", "https://configured-long-origin.example");
    preflight.headers.emplace("Access-Control-Request-Method", "POST");
    preflight.headers.emplace(
        "Access-Control-Request-Headers", "x-this-is-a-long-request-header-name"
    );
    response = {};
    adapter.handle(preflight, response);
    check(response.status == 204
              && response.get_header_value("Access-Control-Allow-Headers")
                  == "x-this-is-a-long-request-header-name",
          "owning header extraction must preserve values longer than SSO");

    std::atomic<int> mismatches{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&] {
            for (int iteration = 0; iteration < 250; ++iteration) {
                auto concurrent = request("GET", "/version");
                concurrent.headers.emplace("Origin", "https://configured-long-origin.example");
                httplib::Response concurrent_response;
                adapter.handle(concurrent, concurrent_response);
                if (concurrent_response.status != 200
                    || concurrent_response.get_header_value("Access-Control-Allow-Origin")
                        != "https://configured-long-origin.example") {
                    mismatches.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    check(mismatches.load() == 0,
          "concurrent adapter header extraction and policy evaluation must be deterministic");
}

void test_attacker_headers_are_bounded_before_policy_copies()
{
    auto router = router_with_health({"BAAS", "cors-bounds"});
    service_http::HttplibAdapter adapter{router};
    constexpr std::size_t attack_bytes = 1'048'576;

    auto huge_origin = request("GET", "/health");
    huge_origin.headers.emplace("Origin", std::string(attack_bytes, 'a'));
    httplib::Response response;
    adapter.handle(huge_origin, response);
    check(response.status == 403 && response.body.find("invalid_origin") != std::string::npos,
          "oversized Origin must hit the fixed policy byte gate");

    auto huge_method = request("OPTIONS", "/health");
    huge_method.headers.emplace("Origin", "http://localhost:8191");
    huge_method.headers.emplace("Access-Control-Request-Method", std::string(attack_bytes, 'A'));
    response = {};
    adapter.handle(huge_method, response);
    check(response.status == 403 && response.body.find("cors_invalid_method") != std::string::npos,
          "oversized ACR-Method must hit the fixed method gate before copying");

    auto huge_headers = request("OPTIONS", "/health");
    huge_headers.headers.emplace("Origin", "http://localhost:8191");
    huge_headers.headers.emplace("Access-Control-Request-Method", "GET");
    huge_headers.headers.emplace("Access-Control-Request-Headers", std::string(attack_bytes, 'x'));
    response = {};
    adapter.handle(huge_headers, response);
    check(response.status == 403 && response.body.find("cors_invalid_headers") != std::string::npos,
          "oversized ACR-Headers must hit the fixed header gate before parsing/copying");
}

class StopAndJoin final {
public:
    StopAndJoin(httplib::Server& server, std::thread& thread) : server_(server), thread_(thread) {}
    ~StopAndJoin()
    {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }

    StopAndJoin(const StopAndJoin&) = delete;
    StopAndJoin& operator=(const StopAndJoin&) = delete;

private:
    httplib::Server& server_;
    std::thread& thread_;
};

void test_real_loopback_ephemeral_port_lifecycle()
{
    using namespace std::chrono_literals;

    service_router::SizeBudget router_budget;
    router_budget.max_path_bytes = 128;
    router_budget.max_request_body_bytes = 64;
    auto router = router_with_health({"BAAS Loopback", "test"}, router_budget);
    service_http::HttplibAdapter adapter{router, service_http::InputBudget{16, 128, 32}};

    httplib::Server server;
    server.set_read_timeout(2s);
    server.set_write_timeout(2s);
    server.set_idle_interval(100ms);
    adapter.install(server);

    const int port = server.bind_to_any_port("127.0.0.1");
    check(port > 0, "loopback server must bind an ephemeral port");
    if (port <= 0) return;

    std::atomic<bool> listen_returned{false};
    std::thread worker([&] {
        static_cast<void>(server.listen_after_bind());
        listen_returned.store(true);
    });
    StopAndJoin cleanup{server, worker};

    const auto ready_deadline = std::chrono::steady_clock::now() + 2s;
    while (!server.is_running() && !listen_returned.load()
           && std::chrono::steady_clock::now() < ready_deadline) {
        std::this_thread::sleep_for(5ms);
    }
    check(server.is_running(), "loopback server must become ready within two seconds");
    if (!server.is_running()) return;

    httplib::Client client{"127.0.0.1", port};
    client.set_connection_timeout(2s);
    client.set_read_timeout(2s);
    client.set_write_timeout(2s);

    const auto health = client.Get("/health");
    check(static_cast<bool>(health), "loopback health request must complete");
    if (health) {
        check(health->status == 200, "loopback health must return 200");
        check(health->body
                  == R"({"ok":true,"statuses":{},"auth":{"initialized":false,"pwd_epoch":0,"server_sign_public_key":"dGVzdC1rZXk="}})",
              "loopback health body must come from Router");
    }

    const auto prefixed_health = client.Get("/api/v1/health");
    check(static_cast<bool>(prefixed_health), "prefixed loopback request must complete");
    if (prefixed_health) {
        check(prefixed_health->status == 404,
              "loopback adapter must keep HTTP v1 paths unversioned");
    }

    const auto wrong_method = client.Post("/health", "{}", "application/json");
    check(static_cast<bool>(wrong_method), "loopback method mismatch must complete");
    if (wrong_method) {
        check(wrong_method->status == 405, "loopback method mismatch must return 405");
        check(wrong_method->get_header_value("Allow") == "GET",
              "loopback method mismatch must retain Allow");
    }

    const auto oversized = client.Post(
        "/shutdown",
        std::string(33, 'x'),
        "application/octet-stream"
    );
    check(static_cast<bool>(oversized), "loopback oversized request must complete");
    if (oversized) {
        if (oversized->status != 413) {
            std::cerr << "oversized loopback status=" << oversized->status
                      << " body=" << oversized->body << '\n';
        }
        check(oversized->status == 413, "httplib payload gate must return 413");
        check(oversized->body.find("request_too_large") != std::string::npos,
              "httplib payload gate must use adapter JSON error");
    }

    const auto allowed_oversized = client.Post(
        "/shutdown",
        httplib::Headers{{"Origin", "http://localhost:8191"}},
        std::string(33, 'x'),
        "application/octet-stream"
    );
    check(allowed_oversized && allowed_oversized->status == 413,
          "real payload rejection must preserve 413 for an allowed actual origin");
    if (allowed_oversized) {
        check(allowed_oversized->get_header_value("Access-Control-Allow-Origin")
                  == "http://localhost:8191"
                  && allowed_oversized->get_header_value("Access-Control-Allow-Credentials") == "true"
                  && allowed_oversized->get_header_value("Vary") == "Origin",
              "real 413 must carry the same actual-request CORS headers");
    }

    const auto denied_oversized = client.Post(
        "/shutdown",
        httplib::Headers{{"Origin", "https://evil.example"}},
        std::string(33, 'x'),
        "application/octet-stream"
    );
    check(denied_oversized && denied_oversized->status == 403,
          "real payload rejection must still fail a denied Origin before exposing 413");
    if (denied_oversized) {
        check(denied_oversized->body.find("origin_not_allowed") != std::string::npos
                  && denied_oversized->get_header_value("Vary") == "Origin"
                  && !denied_oversized->has_header("Access-Control-Allow-Origin"),
              "real denied oversized request must use stable fail-closed response");
    }

    client.stop();
    server.stop();
    if (worker.joinable()) worker.join();
    check(!server.is_running(), "loopback server must stop and join cleanly");
}

}  // namespace

int main()
{
    test_exact_request_and_response_mapping();
    test_transport_limits_precede_router_effects();
    test_adapter_budget_must_fit_router_budget();
    test_origin_cors_actual_preflight_and_rejection_matrix();
    test_custom_policy_is_wired_into_adapter();
    test_attacker_headers_are_bounded_before_policy_copies();
    test_real_loopback_ephemeral_port_lifecycle();
    if (failures != 0) {
        std::cerr << failures << " httplib adapter test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "httplib adapter tests passed\n";
    return EXIT_SUCCESS;
}
