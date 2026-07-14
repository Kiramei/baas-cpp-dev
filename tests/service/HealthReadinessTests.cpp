#include "service/health/HealthReadiness.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace service_health = baas::service::health;
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

[[nodiscard]] service_router::HealthSnapshot snapshot(const std::uint64_t epoch)
{
    return {
        {{"runtime", service_router::HealthValue{service_router::HealthObject{
            {"epoch", service_router::HealthValue{static_cast<std::int64_t>(epoch)}},
            {"phase", service_router::HealthValue{"loaded"}},
        }}}},
        {epoch != 0, epoch, "key-" + std::to_string(epoch)},
    };
}

[[nodiscard]] bool is_correlated(const service_router::HealthReadinessSnapshot& value)
{
    if (value.health.statuses.size() != 1
        || value.health.statuses.front().first != "runtime") {
        return false;
    }
    const auto* object = std::get_if<service_router::HealthObject>(
        &value.health.statuses.front().second.storage
    );
    if (object == nullptr || object->size() != 2 || object->front().first != "epoch") {
        return false;
    }
    const auto* epoch = std::get_if<std::int64_t>(&object->front().second.storage);
    if (epoch == nullptr || *epoch < 0) return false;
    const auto public_epoch = static_cast<std::uint64_t>(*epoch);
    return value.health.auth.pwd_epoch == public_epoch
        && value.health.auth.server_sign_public_key == "key-" + std::to_string(public_epoch);
}

void test_state_machine_requires_explicit_restart()
{
    service_health::HealthReadinessOwner owner{snapshot(0)};
    const auto initial = owner.readiness_snapshot();
    check(initial.state == service_router::HealthReadinessState::starting,
          "owner must begin in starting state");
    check(is_correlated(initial),
          "initial projection must be one stable copy");

    check(owner.publish_ready(snapshot(1)),
          "starting owner must accept a complete ready projection");
    auto current = owner.readiness_snapshot();
    check(current.state == service_router::HealthReadinessState::ready
              && is_correlated(current),
          "ready publication must atomically replace runtime and auth fields");
    check(!owner.publish_ready(snapshot(2)),
          "ready owner must reject a second ready transition without restart");

    owner.publish_failed(snapshot(3));
    current = owner.readiness_snapshot();
    check(current.state == service_router::HealthReadinessState::failed
              && is_correlated(current),
          "failure must retain one stable public projection");
    check(!owner.publish_ready(snapshot(4)),
          "failed owner must not become ready without begin_startup");

    owner.begin_startup(snapshot(5));
    check(owner.state() == service_router::HealthReadinessState::starting,
          "restart must explicitly return the owner to starting");
    check(owner.publish_ready(snapshot(6)),
          "restarted owner must accept a new ready projection");
    current = owner.readiness_snapshot();
    check(current.state == service_router::HealthReadinessState::ready
              && current.health.auth.pwd_epoch == 6,
          "restart readiness must publish the new lifecycle state");
}

void test_concurrent_readers_never_observe_torn_projection()
{
    service_health::HealthReadinessOwner owner{snapshot(0)};
    std::atomic<bool> finished{false};
    std::atomic<int> torn{0};
    std::vector<std::thread> readers;
    for (int index = 0; index < 8; ++index) {
        readers.emplace_back([&] {
            while (!finished.load(std::memory_order_acquire)) {
                const auto current = owner.readiness_snapshot();
                if (!is_correlated(current)) torn.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (std::uint64_t epoch = 1; epoch <= 2'000; ++epoch) {
        owner.begin_startup(snapshot(epoch));
        if (!owner.publish_ready(snapshot(epoch))) {
            torn.fetch_add(1, std::memory_order_relaxed);
        }
    }
    finished.store(true, std::memory_order_release);
    for (auto& reader : readers) reader.join();

    const auto final = owner.readiness_snapshot();
    check(torn.load() == 0,
          "concurrent readers must never mix runtime and auth publications");
    check(final.state == service_router::HealthReadinessState::ready
              && final.health.auth.pwd_epoch == 2'000,
          "concurrent publication must finish on the complete final snapshot");
}

}  // namespace

int main()
{
    test_state_machine_requires_explicit_restart();
    test_concurrent_readers_never_observe_torn_projection();
    if (failures != 0) {
        std::cerr << failures << " health readiness foundation test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "health readiness foundation tests passed\n";
    return EXIT_SUCCESS;
}
