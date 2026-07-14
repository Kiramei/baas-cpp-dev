#include "service/http/HttpHost.h"

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace service_http = baas::service::http;
namespace service_router = baas::service::router;

namespace {

using namespace std::chrono_literals;

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] service_router::HealthSnapshot snapshot(const std::int64_t calls = 0)
{
    return {
        {{"host", service_router::HealthValue{service_router::HealthObject{
            {"calls", service_router::HealthValue{calls}},
            {"running", service_router::HealthValue{true}},
        }}}},
        {true, 3, "bG9vcGJhY2sta2V5"},
    };
}

[[nodiscard]] service_http::HttpHostRouterConfig static_router_config()
{
    service_http::HttpHostRouterConfig result;
    result.service = {"BAAS HTTP Host", "test"};
    result.health_snapshot = snapshot();
    return result;
}

[[nodiscard]] service_http::HttpHostConfig host_config()
{
    service_http::HttpHostConfig result;
    result.worker_count = 3;
    result.max_queued_requests = 16;
    result.ready_timeout = 1s;
    result.read_timeout = 1s;
    result.write_timeout = 1s;
    result.idle_interval = 10ms;
    return result;
}

[[nodiscard]] bool health_request(
    const std::uint16_t port,
    const std::chrono::milliseconds timeout = 1s
)
{
    httplib::Client client{std::string{service_http::http_host_loopback_address}, port};
    client.set_connection_timeout(timeout);
    client.set_read_timeout(timeout);
    client.set_write_timeout(timeout);
    const auto response = client.Get("/health");
    client.stop();
    return response && response->status == 200
        && response->body.find(R"("server_sign_public_key":"bG9vcGJhY2sta2V5")")
            != std::string::npos;
}

template <typename Predicate>
[[nodiscard]] bool wait_until(Predicate predicate, const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

class BlockingProvider : public service_router::HealthSnapshotProvider {
public:
    [[nodiscard]] service_router::HealthSnapshot health_snapshot() const override
    {
        std::unique_lock<std::mutex> lock{mutex_};
        ++entered_;
        entered_changed_.notify_all();
        released_.wait(lock, [this] { return release_; });
        return snapshot(static_cast<std::int64_t>(entered_));
    }

    [[nodiscard]] bool wait_for_entered(
        const std::size_t count,
        const std::chrono::milliseconds timeout
    ) const
    {
        std::unique_lock<std::mutex> lock{mutex_};
        return entered_changed_.wait_for(lock, timeout, [this, count] {
            return entered_ >= count;
        });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock{mutex_};
        release_ = true;
        released_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable entered_changed_;
    mutable std::condition_variable released_;
    mutable std::size_t entered_ = 0;
    bool release_ = false;
};

class CountingProvider final : public service_router::HealthSnapshotProvider {
public:
    [[nodiscard]] service_router::HealthSnapshot health_snapshot() const override
    {
        const int active = active_.fetch_add(1) + 1;
        int observed = maximum_active_.load();
        while (active > observed
               && !maximum_active_.compare_exchange_weak(observed, active)) {}
        const auto call = calls_.fetch_add(1) + 1;
        std::this_thread::sleep_for(15ms);
        active_.fetch_sub(1);
        return snapshot(call);
    }

    [[nodiscard]] int calls() const noexcept { return calls_.load(); }
    [[nodiscard]] int maximum_active() const noexcept { return maximum_active_.load(); }

private:
    mutable std::atomic<int> active_{0};
    mutable std::atomic<int> maximum_active_{0};
    mutable std::atomic<int> calls_{0};
};

class LifetimeProvider final : public service_router::HealthSnapshotProvider {
public:
    explicit LifetimeProvider(std::shared_ptr<std::atomic<bool>> destroyed)
        : destroyed_(std::move(destroyed))
    {}

    ~LifetimeProvider() override { destroyed_->store(true); }

    [[nodiscard]] service_router::HealthSnapshot health_snapshot() const override
    {
        return snapshot(1);
    }

private:
    std::shared_ptr<std::atomic<bool>> destroyed_;
};

class ReentrantStopProvider final : public service_router::HealthSnapshotProvider {
public:
    void attach(service_http::HttpHost& host) noexcept { host_ = &host; }

    [[nodiscard]] service_router::HealthSnapshot health_snapshot() const override
    {
        if (!stopped_.exchange(true)) host_->stop();
        return snapshot(1);
    }

private:
    service_http::HttpHost* host_ = nullptr;
    mutable std::atomic<bool> stopped_{false};
};

void test_config_is_bounded_and_loopback_only()
{
    auto config = host_config();
    service_http::HttpHost host{static_router_config(), {}, config};
    check(host.address() == "127.0.0.1", "host address must be forced to IPv4 loopback");
    check(host.config().worker_count == config.worker_count,
          "host must retain the explicit worker bound");
    check(host.config().max_queued_requests == config.max_queued_requests,
          "host must retain the explicit queued-request bound");

    bool zero_workers_rejected = false;
    try {
        config.worker_count = 0;
        [[maybe_unused]] service_http::HttpHost invalid{static_router_config(), {}, config};
    } catch (const std::invalid_argument&) {
        zero_workers_rejected = true;
    }
    check(zero_workers_rejected, "zero workers must be rejected");

    bool unbounded_queue_rejected = false;
    try {
        config = host_config();
        config.max_queued_requests = 0;
        [[maybe_unused]] service_http::HttpHost invalid{static_router_config(), {}, config};
    } catch (const std::invalid_argument&) {
        unbounded_queue_rejected = true;
    }
    check(unbounded_queue_rejected, "cpp-httplib's unbounded queue mode must be rejected");

    bool excessive_queue_rejected = false;
    try {
        config = host_config();
        config.max_queued_requests = service_http::http_host_max_queued_requests + 1;
        [[maybe_unused]] service_http::HttpHost invalid{static_router_config(), {}, config};
    } catch (const std::invalid_argument&) {
        excessive_queue_rejected = true;
    }
    check(excessive_queue_rejected, "queue values above the explicit maximum must be rejected");

    bool size_max_queue_rejected = false;
    try {
        config.max_queued_requests = std::numeric_limits<std::size_t>::max();
        [[maybe_unused]] service_http::HttpHost invalid{static_router_config(), {}, config};
    } catch (const std::invalid_argument&) {
        size_max_queue_rejected = true;
    }
    check(size_max_queue_rejected, "SIZE_MAX must not masquerade as a bounded queue");

    bool ambiguous_health_rejected = false;
    try {
        auto router_config = static_router_config();
        router_config.health_provider = std::make_shared<CountingProvider>();
        [[maybe_unused]] service_http::HttpHost invalid{
            std::move(router_config), {}, host_config()
        };
    } catch (const std::invalid_argument&) {
        ambiguous_health_rejected = true;
    }
    check(ambiguous_health_rejected,
          "host must not construct a Router with ambiguous health ownership");
}

void test_listener_thread_failure_is_transactional_and_observable()
{
    service_http::HttpHost probe{static_router_config(), {}, host_config()};
    const auto probed = probe.start();
    check(probed.started, "port probe host must start");
    if (!probed.started) return;
    const auto available_port = probed.port;
    probe.stop();

    auto failing_config = host_config();
    failing_config.port = available_port;
    failing_config.listener_thread_factory = [](std::function<void()>) -> std::thread {
        throw std::system_error{
            std::make_error_code(std::errc::resource_unavailable_try_again)
        };
    };
    service_http::HttpHost failing{static_router_config(), {}, failing_config};
    const auto failed = failing.start();
    check(!failed.started
              && failed.error == service_http::HttpHostStartError::listener_start_failed,
          "listener thread allocation failure must be returned, not thrown");
    check(failing.state() == service_http::HttpHostState::failed
              && failing.port() == 0 && !failing.last_error_message().empty(),
          "listener start failure must publish failed state without a bound port");

    auto fixed_config = host_config();
    fixed_config.port = available_port;
    service_http::HttpHost recovered{static_router_config(), {}, fixed_config};
    const auto started = recovered.start();
    check(started.started && started.port == available_port,
          "thread creation failure must occur before binding and leave the port reusable");
    recovered.stop();
}

void test_repeated_ephemeral_start_stop_is_idempotent()
{
    service_http::HttpHost host{static_router_config(), {}, host_config()};
    for (int cycle = 0; cycle < 3; ++cycle) {
        const auto started = host.start();
        check(started.started && started.port != 0,
              "ephemeral start must publish the selected port");
        check(host.state() == service_http::HttpHostState::running,
              "successful start must publish running state");
        check(host.port() == started.port, "running host must expose the bound port");
        check(health_request(started.port), "started host must serve injected health state");

        const auto repeated = host.start();
        check(!repeated.started
                  && repeated.error == service_http::HttpHostStartError::already_active,
              "repeated start while running must be observable and non-blocking");
        host.stop();
        host.stop();
        check(host.state() == service_http::HttpHostState::stopped && host.port() == 0,
              "stop must be idempotent and clear the published port");
    }
}

void test_fixed_port_conflict_is_observable_and_recoverable()
{
    service_http::HttpHost first{static_router_config(), {}, host_config()};
    const auto first_start = first.start();
    check(first_start.started, "first host must reserve a loopback port");
    if (!first_start.started) return;

    auto fixed = host_config();
    fixed.port = first_start.port;
    service_http::HttpHost conflicting{static_router_config(), {}, fixed};
    const auto conflict = conflicting.start();
    check(!conflict.started
              && conflict.error == service_http::HttpHostStartError::bind_failed,
          "fixed loopback port conflict must report bind_failed");
    check(conflicting.state() == service_http::HttpHostState::failed
              && !conflicting.last_error_message().empty(),
          "bind failure must remain observable through host state");

    first.stop();
    const auto recovered = conflicting.start();
    check(recovered.started && recovered.port == fixed.port,
          "a failed fixed-port host must start after the conflict is removed");
    conflicting.stop();
}

void test_concurrent_health_respects_worker_bound()
{
    auto provider = std::make_shared<CountingProvider>();
    service_http::HttpHostRouterConfig router_config;
    router_config.service = {"BAAS Concurrent Host", "test"};
    router_config.health_provider = provider;
    auto config = host_config();
    config.worker_count = 3;
    config.max_queued_requests = 16;
    service_http::HttpHost host{std::move(router_config), {}, config};
    const auto started = host.start();
    check(started.started, "concurrent host must start");
    if (!started.started) return;

    std::atomic<int> successes{0};
    std::vector<std::thread> clients;
    for (int index = 0; index < 12; ++index) {
        clients.emplace_back([&] {
            if (health_request(started.port, 2s)) successes.fetch_add(1);
        });
    }
    for (auto& client : clients) client.join();
    host.stop();

    check(successes.load() == 12 && provider->calls() == 12,
          "all health requests within the queue budget must complete");
    check(provider->maximum_active() >= 2
              && provider->maximum_active() <= static_cast<int>(config.worker_count),
          "provider concurrency must be bounded by the configured worker count");
}

void test_stop_drains_in_flight_and_rejects_new_connections()
{
    auto provider = std::make_shared<BlockingProvider>();
    service_http::HttpHostRouterConfig router_config;
    router_config.service = {"BAAS Drain Host", "test"};
    router_config.health_provider = provider;
    auto config = host_config();
    config.worker_count = 1;
    config.max_queued_requests = 2;
    service_http::HttpHost host{std::move(router_config), {}, config};
    const auto started = host.start();
    check(started.started, "drain host must start");
    if (!started.started) return;

    std::atomic<bool> in_flight_succeeded{false};
    std::thread request([&] {
        in_flight_succeeded.store(health_request(started.port, 2s));
    });
    check(provider->wait_for_entered(1, 1s),
          "in-flight request must enter the provider before stop");

    std::thread stopper([&] { host.stop(); });
    check(wait_until(
              [&] { return host.state() == service_http::HttpHostState::stopping; }, 1s
          ),
          "stop must publish stopping before draining workers");
    check(!health_request(started.port, 200ms),
          "new loopback connections must fail once stop closes the listener");
    provider->release();
    request.join();
    stopper.join();

    check(in_flight_succeeded.load(), "accepted request must finish during drain");
    check(host.state() == service_http::HttpHostState::stopped,
          "stop must join listener and worker threads");
}

void test_destructor_owns_provider_and_running_server_lifetime()
{
    auto destroyed = std::make_shared<std::atomic<bool>>(false);
    auto provider = std::make_shared<LifetimeProvider>(destroyed);
    {
        service_http::HttpHostRouterConfig router_config;
        router_config.service = {"BAAS Lifetime Host", "test"};
        router_config.health_provider = provider;
        service_http::HttpHost host{std::move(router_config), {}, host_config()};
        provider.reset();
        check(!destroyed->load(), "host must retain the provider shared owner");
        const auto started = host.start();
        check(started.started && health_request(started.port),
              "owned provider must remain valid for installed handlers");
        // Destructor performs stop/drain/join without an explicit stop call.
    }
    check(destroyed->load(), "provider must be released after Router and Server destruction");
}

void test_stop_from_request_worker_is_deferred_without_deadlock()
{
    auto provider = std::make_shared<ReentrantStopProvider>();
    service_http::HttpHostRouterConfig router_config;
    router_config.service = {"BAAS Reentrant Stop Host", "test"};
    router_config.health_provider = provider;
    auto config = host_config();
    config.worker_count = 1;
    config.max_queued_requests = 1;
    service_http::HttpHost host{std::move(router_config), {}, config};
    provider->attach(host);
    const auto started = host.start();
    check(started.started, "reentrant-stop host must start");
    if (!started.started) return;

    check(health_request(started.port, 2s),
          "request-worker stop must defer join until the handler returns");
    check(host.state() == service_http::HttpHostState::stopping
              && host.last_error_message().find("join deferred") != std::string::npos,
          "deferred worker stop must remain observable to the owner");
    host.stop();
    check(host.state() == service_http::HttpHostState::stopped,
          "owner stop must finish the deferred drain and join");
}

void test_bounded_queue_rejects_excess_connection()
{
    auto provider = std::make_shared<BlockingProvider>();
    service_http::HttpHostRouterConfig router_config;
    router_config.service = {"BAAS Queue Host", "test"};
    router_config.health_provider = provider;
    auto config = host_config();
    config.worker_count = 1;
    config.max_queued_requests = 1;
    service_http::HttpHost host{std::move(router_config), {}, config};
    const auto started = host.start();
    check(started.started, "queue host must start");
    if (!started.started) return;

    std::atomic<int> successes{0};
    auto request = [&] {
        if (health_request(started.port, 2s)) successes.fetch_add(1);
    };
    std::thread first{request};
    check(provider->wait_for_entered(1, 1s), "first request must occupy the sole worker");
    std::thread second{request};
    std::this_thread::sleep_for(10ms);
    std::thread third{request};

    check(wait_until([&] { return host.queue_rejections() >= 1; }, 1s),
          "third accepted connection must exceed the one-request waiting queue");
    provider->release();
    first.join();
    second.join();
    third.join();
    host.stop();

    check(successes.load() == 2,
          "one worker plus one queued request must complete exactly two requests");
    check(host.queue_rejections() == 1,
          "queue overflow must be counted exactly once in the controlled test");
}

}  // namespace

int main()
{
    test_config_is_bounded_and_loopback_only();
    test_listener_thread_failure_is_transactional_and_observable();
    test_repeated_ephemeral_start_stop_is_idempotent();
    test_fixed_port_conflict_is_observable_and_recoverable();
    test_concurrent_health_respects_worker_bound();
    test_stop_drains_in_flight_and_rejects_new_connections();
    test_destructor_owns_provider_and_running_server_lifetime();
    test_stop_from_request_worker_is_deferred_without_deadlock();
    test_bounded_queue_rejects_excess_connection();
    if (failures != 0) {
        std::cerr << failures << " HTTP host test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "HTTP host tests passed\n";
    return EXIT_SUCCESS;
}
