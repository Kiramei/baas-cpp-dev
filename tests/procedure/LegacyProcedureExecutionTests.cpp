#include "BAASExceptions.h"
#include "procedure/LegacyProcedureExecution.h"
#include "device/screenshot/ScreenshotInterval.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <stop_token>
#include <thread>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <class Exception>
baas::LegacyProcedureRunResult mapped(Exception error)
{
    try {
        throw std::move(error);
    } catch (...) {
        return baas::map_legacy_procedure_exception(std::current_exception());
    }
}

void test_typed_mapping()
{
    using Code = baas::LegacyProcedureRunCode;
    check(mapped(baas::ValueError("device disconnected" )).code == Code::InvalidDefinition,
          "mapping must use exception type rather than misleading text");
    check(mapped(baas::TypeError("x")).code == Code::InvalidDefinition,
          "type errors must be invalid definitions");
    check(mapped(baas::KeyError("x")).code == Code::InvalidDefinition,
          "key errors must be invalid definitions");
    check(mapped(baas::HumanTakeOverError("x")).code == Code::Cancelled,
          "manual stop must be cancellation");
    check(mapped(baas::LegacyProcedureCancelled{}).code == Code::Cancelled,
          "typed control cancellation must remain cancellation");
    check(mapped(baas::LegacyProcedureDeadlineExceeded{}).code == Code::DeadlineExceeded,
          "typed deadline must remain deadline");
    check(mapped(baas::LegacyProcedureForegroundPackageMismatch{}).code ==
              Code::ForegroundPackageMismatch,
          "typed foreground mismatch must remain distinct from disconnection");
    check(mapped(baas::EmulatorNotRunningError("x")).code == Code::DeviceDisconnected,
          "emulator failures must be device disconnected");
    check(mapped(baas::ConnectionError("x")).code == Code::DeviceDisconnected,
          "connection failures must be device disconnected");
    check(mapped(baas::PathError("x")).code == Code::ResourceNotFound,
          "path failures must be missing resources");
    check(mapped(baas::GameStuckError("x")).code == Code::BudgetExceeded,
          "stuck guard must be a budget failure");
    check(mapped(baas::TooManyClicksAtOnePlaceError("x")).code == Code::BudgetExceeded,
          "click guard must be a budget failure");
    check(mapped(baas::RequestHumanTakeOver("x")).code == Code::Unavailable,
          "unhandled legacy state must be unavailable rather than invalid input");
    check(mapped(std::bad_alloc{}).code == Code::ResourceExhausted,
          "allocation exhaustion must not forge a business budget failure");
    check(mapped(std::runtime_error("cancelled")).code == Code::Internal,
          "generic text must never forge a typed cancellation");
    check(baas::legacy_procedure_run_code_name(Code::MissingProcedure) ==
              "missing_procedure" &&
              baas::legacy_procedure_run_code_name(Code::ForegroundPackageMismatch) ==
                  "foreground_package_mismatch" &&
              !baas::legacy_procedure_production_ready(),
          "public code names and fail-closed readiness must be stable");
    check(baas::valid_legacy_procedure_id("activity/group") &&
              !baas::valid_legacy_procedure_id("Activity/group") &&
              !baas::valid_legacy_procedure_id("../group") &&
              !baas::valid_legacy_procedure_id("activity//group"),
          "direct procedure IDs must match the activation canonical domain");
    check(!baas::valid_legacy_procedure_id(std::string(1'025, 'a')),
          "direct procedure ID limit must match the 1024-byte snapshot default");
}

void test_control_precedence()
{
    std::stop_source stop;
    stop.request_stop();
    baas::LegacyProcedureExecutionControl both(
        stop.get_token(), baas::LegacyProcedureExecutionControl::Clock::now() - 1ms);
    check(both.checkpoint() == baas::LegacyProcedureCheckpoint::DeadlineExceeded,
          "deadline must win over simultaneous cancellation");
    baas::LegacyProcedureExecutionControl cancelled(stop.get_token());
    check(cancelled.checkpoint() == baas::LegacyProcedureCheckpoint::Cancelled,
          "stop token must cancel execution");
    baas::LegacyProcedureExecutionControl running;
    check(running.checkpoint(false) == baas::LegacyProcedureCheckpoint::Cancelled &&
              running.checkpoint(true) == baas::LegacyProcedureCheckpoint::Proceed,
          "owner stop must participate without changing healthy execution");

    std::exception_ptr manual_stop;
    try {
        throw baas::HumanTakeOverError("flag changed after checkpoint");
    } catch (...) {
        manual_stop = std::current_exception();
    }
    const auto raced = baas::map_legacy_procedure_exception(manual_stop, both, false);
    check(raced.code == baas::LegacyProcedureRunCode::DeadlineExceeded && !raced.retryable,
          "deadline must override a concurrent flag cancellation by typed control state");

    std::exception_ptr device_error;
    try {
        throw baas::ConnectionError("device");
    } catch (...) {
        device_error = std::current_exception();
    }
    check(baas::map_legacy_procedure_exception(device_error, both, false).code ==
              baas::LegacyProcedureRunCode::DeviceDisconnected,
          "control precedence must not replace an already typed device failure");
}

class RecordingObserver final : public baas::LegacyProcedureEffectObserver {
public:
    bool report(
        const baas::LegacyProcedureEffect effect,
        const baas::LegacyProcedureEffectStage stage) noexcept override
    {
        events.emplace_back(effect, stage);
        return accept;
    }

    bool accept{true};
    std::vector<std::pair<baas::LegacyProcedureEffect,
                          baas::LegacyProcedureEffectStage>> events;
};

void test_effect_scope()
{
    RecordingObserver observer;
    {
        baas::LegacyProcedureEffectScope scope(
            &observer, baas::LegacyProcedureEffect::Capture);
        check(scope.began() && scope.commit(),
              "accepted operation must begin and commit");
    }
    check(observer.events.size() == 2 &&
              observer.events[0].second == baas::LegacyProcedureEffectStage::Began &&
              observer.events[1].second == baas::LegacyProcedureEffectStage::Committed,
          "committed scope must report one ordered terminal sequence");
    {
        baas::LegacyProcedureEffectScope scope(
            &observer, baas::LegacyProcedureEffect::Input);
        check(scope.began(), "second operation must begin");
    }
    check(observer.events.back().second == baas::LegacyProcedureEffectStage::Unknown,
          "uncommitted scope must report unknown during unwinding");
    observer.accept = false;
    {
        baas::LegacyProcedureEffectScope rejected(
            &observer, baas::LegacyProcedureEffect::Wait);
        check(!rejected.began() && !rejected.commit(),
              "rejected begin must fail closed without a terminal report");
    }
}

void test_controlled_screenshot_interval()
{
    check(baas::normalize_screenshot_interval_ms(0.3) == 300 &&
              baas::normalize_screenshot_interval_ms(60.001) == 60'001 &&
              baas::normalize_screenshot_interval_ms(-1.0) == 300 &&
              baas::normalize_screenshot_interval_ms(1e308) == 300 &&
              baas::normalize_screenshot_interval_ms(
                  std::numeric_limits<double>::quiet_NaN()) == 300,
          "screenshot interval must validate finite bounds before integer narrowing");
    check(baas::screenshot_interval_remaining_ms(
              300, std::numeric_limits<long long>::min()) == 300 &&
              baas::screenshot_interval_remaining_ms(300, 299) == 1 &&
              baas::screenshot_interval_remaining_ms(300, 300) == 0,
          "clock rollback and elapsed bounds must not narrow an out-of-range duration");

    std::stop_source stop;
    baas::LegacyProcedureExecutionControl cancelled(stop.get_token());
    int waited{};
    try {
        baas::wait_screenshot_interval_slices(
            500,
            [&](const int slice) {
                check(slice <= baas::maximum_screenshot_wait_slice_ms,
                      "controlled screenshot waits must use bounded slices");
                waited += slice;
                stop.request_stop();
            },
            [&] { cancelled.throw_if_stopped(); });
        check(false, "cancellation during screenshot interval must interrupt the wait");
    } catch (const baas::LegacyProcedureCancelled&) {
        check(waited <= baas::maximum_screenshot_wait_slice_ms,
              "cancellation must be observed after at most one bounded slice");
    }

    baas::LegacyProcedureExecutionControl deadline(
        {}, baas::LegacyProcedureExecutionControl::Clock::now() + 1ms);
    try {
        baas::wait_screenshot_interval_slices(
            500,
            [](const int) { std::this_thread::sleep_for(2ms); },
            [&] { deadline.throw_if_stopped(); });
        check(false, "deadline during screenshot interval must interrupt the wait");
    } catch (const baas::LegacyProcedureDeadlineExceeded&) {
    }
}

}  // namespace

int main()
{
    test_typed_mapping();
    test_control_precedence();
    test_effect_scope();
    test_controlled_screenshot_interval();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "legacy procedure execution foundation tests passed\n";
    return EXIT_SUCCESS;
}
