#include "service/app/ServiceShutdown.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace baas::service;
using namespace baas::service::app;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_http_first_wins_and_waits()
{
    auto coordinator = std::make_shared<ServiceShutdownCoordinator>();
    check(coordinator->reason() == ServiceShutdownReason::none,
          "a new coordinator must have no reason");
    check(!coordinator->wait_for(1ms).has_value(),
          "bounded wait must time out before a request");

    std::atomic<bool> waiting{};
    ServiceShutdownReason observed{ServiceShutdownReason::none};
    std::thread waiter([&] {
        waiting.store(true, std::memory_order_release);
        observed = coordinator->wait();
    });
    while (!waiting.load(std::memory_order_acquire)) std::this_thread::yield();

    check(coordinator->request_shutdown() == router::ShutdownDecision::accepted,
          "the first HTTP shutdown request must be accepted");
    waiter.join();
    check(observed == ServiceShutdownReason::http_request,
          "blocking wait must return the HTTP reason");
    check(coordinator->request(ServiceShutdownReason::terminate)
              == router::ShutdownDecision::rejected,
          "later shutdown requests must be rejected");
    check(coordinator->request(ServiceShutdownReason::none)
              == router::ShutdownDecision::rejected,
          "none must never become a shutdown reason");
    check(coordinator->reason() == ServiceShutdownReason::http_request,
          "the first reason must remain immutable");
}

void test_concurrent_first_wins()
{
    auto coordinator = std::make_shared<ServiceShutdownCoordinator>();
    std::atomic<bool> start{};
    std::atomic<unsigned int> accepted{};
    std::vector<std::thread> requesters;
    requesters.reserve(24);
    for (unsigned int index = 0; index < 24; ++index) {
        requesters.emplace_back([&, index] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            const auto reason = index % 3 == 0 ? ServiceShutdownReason::http_request
                : index % 3 == 1 ? ServiceShutdownReason::interrupt
                                 : ServiceShutdownReason::terminate;
            if (coordinator->request(reason) == router::ShutdownDecision::accepted) {
                accepted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& requester : requesters) requester.join();

    check(accepted.load(std::memory_order_relaxed) == 1,
          "exactly one racing request must win");
    const auto reason = coordinator->reason();
    check(reason == ServiceShutdownReason::http_request
              || reason == ServiceShutdownReason::interrupt
              || reason == ServiceShutdownReason::terminate,
          "the winning reason must be one submitted by a requester");
    check(coordinator->wait() == reason,
          "wait after publication must return immediately with the stable reason");
}

void test_publication_wait_race_and_broadcast()
{
    for (unsigned int iteration = 0; iteration < 1'000; ++iteration) {
        auto coordinator = std::make_shared<ServiceShutdownCoordinator>();
        std::array<ServiceShutdownReason, 3> observed{};
        std::array<std::thread, 3> waiters{
            std::thread([&] { observed[0] = coordinator->wait(); }),
            std::thread([&] { observed[1] = coordinator->wait(); }),
            std::thread([&] {
                observed[2] = coordinator->wait_for(2s).value_or(
                    ServiceShutdownReason::none);
            }),
        };
        if (iteration % 2 == 0) std::this_thread::yield();
        check(coordinator->request(ServiceShutdownReason::interrupt)
                  == router::ShutdownDecision::accepted,
              "race publication must accept exactly once");
        for (auto& waiter : waiters) waiter.join();
        check(observed[0] == ServiceShutdownReason::interrupt
                  && observed[1] == ServiceShutdownReason::interrupt
                  && observed[2] == ServiceShutdownReason::interrupt,
              "publication must wake every blocking and timed waiter");
    }
}

void test_platform_signal_conversion(ServiceSignalBlockResult blocked)
{
    check(static_cast<bool>(blocked), "platform shutdown signals must block");
    if (!blocked) return;
    auto coordinator = std::make_shared<ServiceShutdownCoordinator>();
    auto opened = open_service_signal_owner(coordinator, std::move(blocked.block));
    check(static_cast<bool>(opened), "platform signal owner must start");
    if (!opened) return;

    auto second_block = block_service_shutdown_signals();
    check(static_cast<bool>(second_block), "a nested signal block token must open");
    if (second_block) {
        auto second_coordinator = std::make_shared<ServiceShutdownCoordinator>();
        auto duplicate = open_service_signal_owner(
            second_coordinator, std::move(second_block.block));
        check(!duplicate && duplicate.error == ServiceSignalError::already_active,
              "only one process signal owner may be active");
    }

    check(opened.owner->notify_for_test(ServiceShutdownReason::interrupt),
          "the test seam must notify through the platform event boundary");
    const auto reason = coordinator->wait_for(2s);
    check(reason == ServiceShutdownReason::interrupt,
          "the ordinary signal worker must translate interrupt into a request");
    opened.owner->stop();
    check(!opened.owner->notify_for_test(ServiceShutdownReason::terminate),
          "a stopped signal owner must reject later event injection");
    opened.owner.reset();

    // Reopening proves that stop/destruction released the process-global owner
    // and joined the previous platform waiter.
    auto reopened_block = block_service_shutdown_signals();
    check(static_cast<bool>(reopened_block), "signals must be blockable after owner stop");
    if (!reopened_block) return;
    auto reopened_coordinator = std::make_shared<ServiceShutdownCoordinator>();
    auto reopened = open_service_signal_owner(
        reopened_coordinator, std::move(reopened_block.block));
    check(static_cast<bool>(reopened), "signal owner must reopen after a join barrier");
    if (!reopened) return;
    check(reopened_coordinator->request_shutdown() == router::ShutdownDecision::accepted,
          "HTTP may win while the signal waiter is active");
    check(reopened_coordinator->wait() == ServiceShutdownReason::http_request,
          "HTTP reason must remain stable with a live signal owner");
    reopened.owner->stop();
}

void test_invalid_open_and_stable_names()
{
    auto block = block_service_shutdown_signals();
    check(static_cast<bool>(block), "invalid-open test needs a block token");
    if (block) {
        auto missing = open_service_signal_owner({}, std::move(block.block));
        check(!missing && missing.error == ServiceSignalError::invalid_coordinator,
              "signal owner must reject a missing coordinator");
    }
    auto coordinator = std::make_shared<ServiceShutdownCoordinator>();
    auto missing_block = open_service_signal_owner(coordinator, {});
    check(!missing_block
              && missing_block.error == ServiceSignalError::invalid_signal_block,
          "signal owner must reject a missing signal block token");

    constexpr std::array reasons{
        ServiceShutdownReason::none,
        ServiceShutdownReason::http_request,
        ServiceShutdownReason::interrupt,
        ServiceShutdownReason::terminate,
        ServiceShutdownReason::signal_failure,
    };
    for (const auto reason : reasons) {
        check(service_shutdown_reason_name(reason) != "unknown",
              "every public shutdown reason must have a stable name");
    }
    constexpr std::array errors{
        ServiceSignalError::none,
        ServiceSignalError::signal_block_failed,
        ServiceSignalError::invalid_coordinator,
        ServiceSignalError::invalid_signal_block,
        ServiceSignalError::already_active,
        ServiceSignalError::event_creation_failed,
        ServiceSignalError::handler_registration_failed,
        ServiceSignalError::thread_start_failed,
    };
    for (const auto error : errors) {
        check(service_signal_error_name(error) != "unknown",
              "every public signal error must have a stable name");
    }
}

}  // namespace

int main()
{
    // This is deliberately the first operation: POSIX application threads must
    // inherit the blocked SIGINT/SIGTERM mask.
    auto blocked = block_service_shutdown_signals();
    test_http_first_wins_and_waits();
    test_concurrent_first_wins();
    test_publication_wait_race_and_broadcast();
    test_platform_signal_conversion(std::move(blocked));
    test_invalid_open_and_stable_names();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Service shutdown coordinator tests passed\n";
    return EXIT_SUCCESS;
}
