#include "service/app/ProductionProviderBackend.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace app = baas::service::app;
namespace channels = baas::service::channels;

void check(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void check_ok(const channels::ProviderBackendError error, const char* message)
{
    check(error == channels::ProviderBackendError::none, message);
}

void test_snapshots_and_bounded_history()
{
    app::ProductionProviderBackendLimits limits;
    limits.max_history_entries = 2;
    app::ProductionProviderBackend backend{limits};

    auto empty = backend.logs_full({});
    check(empty && empty->scopes_json == "[]" && empty->entries_json == "[]",
          "empty log snapshot must be valid arrays");
    auto initial_status = backend.status({});
    check(initial_status && *initial_status.value == "{}",
          "initial status must be an object");
    auto initial_static = backend.static_snapshot({});
    check(initial_static && initial_static->timestamp_json == "0"
              && initial_static->data_json == "null",
          "initial static snapshot must be valid");

    check_ok(backend.publish_log(R"({"scope":"main","message":"one"})"),
             "first log must publish");
    check_ok(backend.publish_log(R"({"scope":"config:a","message":"two"})"),
             "second log must publish");
    check_ok(backend.publish_log(
                 R"({"scope":"main","ratio":1.25,"future":{"x":true}})"),
             "third log must publish and evict oldest");
    auto logs = backend.logs_full({});
    check(logs && logs->scopes_json == R"(["main","config:a"])"
              && logs->entries_json
                  == R"([{"scope":"config:a","message":"two"},{"scope":"main","ratio":1.25,"future":{"x":true}}])",
          "history eviction must preserve retained bytes and bounded scopes");

    check_ok(backend.publish_status(
                 R"({"running":true,"progress":0.5,"future":"kept"})"),
             "status object must publish");
    auto status = backend.status({});
    check(status && *status.value
              == R"({"running":true,"progress":0.5,"future":"kept"})",
          "status snapshot must retain exact JSON");
    check_ok(backend.set_initialized(true), "initialized state must publish");
    auto initialized = backend.all_data_initialized({});
    check(initialized && initialized.value->has_value() && **initialized.value,
          "initialized snapshot must update");

    check_ok(backend.replace_static(
                 "1700000000123.5",
                 R"({"activities":[{"name":"a","weight":2.75}]})"),
             "static snapshot must accept JSON number and value");
    auto snapshot = backend.static_snapshot({});
    check(snapshot && snapshot->timestamp_json == "1700000000123.5"
              && snapshot->data_json
                  == R"({"activities":[{"name":"a","weight":2.75}]})",
          "static snapshot must retain exact JSON");
}

void test_validation_capacity_and_stop()
{
    app::ProductionProviderBackendLimits limits;
    limits.max_json_bytes = 64;
    limits.max_scope_bytes = 8;
    limits.max_history_bytes = 64;
    limits.max_snapshot_json_bytes = 96;
    app::ProductionProviderBackend backend{limits};

    check(backend.publish_log(R"({"scope":"a","scope":"b"})")
              == channels::ProviderBackendError::internal_error,
          "duplicate log keys must be rejected");
    check(backend.publish_log(R"({"message":"missing"})")
              == channels::ProviderBackendError::internal_error,
          "missing log scope must be rejected");
    check(backend.publish_log(R"({"scope":3})")
              == channels::ProviderBackendError::internal_error,
          "non-string log scope must be rejected");
    std::string bad_utf8{"{\"scope\":\""};
    bad_utf8.push_back(static_cast<char>(0xFF));
    bad_utf8 += "\"}";
    check(backend.publish_log(std::move(bad_utf8))
              == channels::ProviderBackendError::internal_error,
          "invalid UTF-8 must be rejected");
    check(backend.publish_log(R"({"scope":"too-long-scope"})")
              == channels::ProviderBackendError::capacity,
          "oversized scope must report capacity");
    check(backend.publish_status("[]")
              == channels::ProviderBackendError::internal_error,
          "status must be a JSON object");
    check(backend.replace_static(R"("not-number")", "{}")
              == channels::ProviderBackendError::internal_error,
          "static timestamp must be a JSON number");
    check(backend.replace_static("1", "{broken")
              == channels::ProviderBackendError::internal_error,
          "invalid static data must be rejected");
    check(backend.publish_status(std::string(65, 'x'))
              == channels::ProviderBackendError::capacity,
          "oversized JSON must report capacity before parsing");

    check_ok(backend.publish_log(
                 R"({"scope":"a","message":"11111111111111111111"})"),
             "first byte-bounded history entry must publish");
    check_ok(backend.publish_log(
                 R"({"scope":"b","message":"22222222222222222222"})"),
             "second byte-bounded history entry must evict the first");

    auto logs = backend.logs_full({});
    check(logs && logs->scopes_json == R"(["b"])"
              && logs->entries_json
                  == R"([{"scope":"b","message":"22222222222222222222"}])",
          "history byte bound must evict oldest entries and their scopes");
    auto status = backend.status({});
    check(status && *status.value == "{}",
          "invalid status must not replace current state");

    std::stop_source stopped;
    stopped.request_stop();
    check(!backend.logs_full(stopped.get_token())
              && !backend.status(stopped.get_token())
              && !backend.all_data_initialized(stopped.get_token())
              && !backend.static_snapshot(stopped.get_token()),
          "snapshot methods must honor an already-requested stop token");

    bool threw = false;
    try {
        app::ProductionProviderBackendLimits invalid;
        invalid.max_history_entries = 0;
        app::ProductionProviderBackend rejected{invalid};
    }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw, "zero limits must be rejected at construction");
}

void test_subscriptions_and_exception_isolation()
{
    app::ProductionProviderBackendLimits limits;
    limits.max_subscriptions_per_stream = 1;
    app::ProductionProviderBackend backend{limits};
    std::vector<std::string> statuses;
    auto status_subscription = backend.subscribe_status(
        [&](std::string payload) { statuses.push_back(std::move(payload)); });
    check(static_cast<bool>(status_subscription), "status subscription must open");
    auto excess = backend.subscribe_status([](std::string) {});
    check(!excess && excess.error == channels::ProviderBackendError::capacity,
          "subscription cap must be enforced");
    check_ok(backend.publish_status(R"({"phase":"ready"})"),
             "status callback must publish");
    check_ok(backend.set_initialized(false), "initialized callback must publish");
    check(statuses.size() == 2
              && statuses[0] == R"({"phase":"ready"})"
              && statuses[1] == R"({"is_all_data_initialized":false})",
          "status and initialized callbacks must preserve order and payload");
    status_subscription.subscription.reset();
    check_ok(backend.publish_status(R"({"phase":"after-close"})"),
             "publish after unsubscribe must still succeed");
    check(statuses.size() == 2, "unsubscribe must close callback admission");

    std::atomic<int> throwing_calls{};
    auto throwing = backend.subscribe_logs([&](std::string) {
        ++throwing_calls;
        throw std::runtime_error("subscriber failure");
    });
    check(static_cast<bool>(throwing), "throwing subscriber must open");
    check(backend.publish_log(R"({"scope":"main","message":"first"})")
              == channels::ProviderBackendError::internal_error,
          "callback exception must be contained and reported");
    check_ok(backend.publish_log(R"({"scope":"main","message":"second"})"),
             "throwing subscriber must be failed closed");
    check(throwing_calls == 1, "throwing subscriber must be removed exactly once");
}

void test_subscription_destruction_barrier()
{
    app::ProductionProviderBackend backend;
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    std::atomic<bool> destroyed{};
    std::atomic<bool> destroy_started{};
    std::atomic<int> calls{};

    auto subscription = backend.subscribe_logs([&](std::string) {
        ++calls;
        std::unique_lock lock{mutex};
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return release; });
    });
    check(static_cast<bool>(subscription), "blocking subscription must open");

    std::thread publisher{[&] {
        check_ok(backend.publish_log(R"({"scope":"main","message":"blocked"})"),
                 "blocked callback publication must finish");
    }};
    {
        std::unique_lock lock{mutex};
        changed.wait(lock, [&] { return entered; });
    }
    std::thread destroyer{[&] {
        destroy_started = true;
        subscription.subscription.reset();
        destroyed = true;
    }};
    while (!destroy_started.load()) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    check(!destroyed.load(), "subscription destruction must wait for entered callback");
    {
        std::lock_guard lock{mutex};
        release = true;
    }
    changed.notify_all();
    publisher.join();
    destroyer.join();
    check(destroyed && calls == 1, "destruction barrier must complete exactly once");
    check_ok(backend.publish_log(R"({"scope":"main","message":"after"})"),
             "publication after barrier must succeed");
    check(calls == 1, "no callback may enter after subscription destruction");
}

void test_concurrent_publish_and_subscribe()
{
    app::ProductionProviderBackendLimits limits;
    limits.max_history_entries = 32;
    limits.max_subscriptions_per_stream = 16;
    app::ProductionProviderBackend backend{limits};
    std::atomic<int> persistent_calls{};
    auto persistent = backend.subscribe_logs(
        [&](std::string) { ++persistent_calls; });
    check(static_cast<bool>(persistent), "persistent subscription must open");

    constexpr int publishers = 4;
    constexpr int per_publisher = 100;
    std::atomic<bool> failed{};
    std::vector<std::thread> threads;
    for (int publisher = 0; publisher < publishers; ++publisher) {
        threads.emplace_back([&, publisher] {
            for (int index = 0; index < per_publisher; ++index) {
                const std::string payload = "{\"scope\":\"worker:"
                    + std::to_string(publisher) + "\",\"index\":"
                    + std::to_string(index) + "}";
                if (backend.publish_log(payload) != channels::ProviderBackendError::none) {
                    failed = true;
                }
            }
        });
    }
    threads.emplace_back([&] {
        for (int index = 0; index < 100; ++index) {
            auto transient = backend.subscribe_logs([](std::string) {});
            if (!transient) failed = true;
        }
    });
    for (auto& thread : threads) thread.join();

    check(!failed, "concurrent publish/subscribe operations must succeed");
    check(persistent_calls == publishers * per_publisher,
          "persistent subscriber must observe every concurrent publication");
    auto logs = backend.logs_full({});
    check(logs && logs->entries_json.front() == '[' && logs->entries_json.back() == ']'
              && logs->scopes_json.front() == '[' && logs->scopes_json.back() == ']',
          "concurrent bounded history must remain a valid array snapshot");
}

}  // namespace

int main()
{
    test_snapshots_and_bounded_history();
    test_validation_capacity_and_stop();
    test_subscriptions_and_exception_isolation();
    test_subscription_destruction_barrier();
    test_concurrent_publish_and_subscribe();
    std::cout << "ProductionProviderBackendTests passed\n";
    return 0;
}
