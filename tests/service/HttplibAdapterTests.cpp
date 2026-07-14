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
    test_real_loopback_ephemeral_port_lifecycle();
    if (failures != 0) {
        std::cerr << failures << " httplib adapter test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "httplib adapter tests passed\n";
    return EXIT_SUCCESS;
}
