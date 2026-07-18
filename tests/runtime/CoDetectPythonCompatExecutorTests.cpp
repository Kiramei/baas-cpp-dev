#include "runtime/procedure/CoDetectPythonCompatExecutor.h"
#include "resources/ResourceSnapshot.h"

#include <atomic>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace procedure = baas::runtime::procedure;
namespace host = baas::script::host;
namespace runtime = baas::script::runtime;
namespace resources = baas::resources;

namespace {

int failures{};
void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAILED: " << message << '\n';
    }
}

std::span<const std::byte> bytes(const std::string& value)
{
    return std::as_bytes(std::span{value.data(), value.size()});
}

std::string definition_json(const bool skip_first = false,
                            const std::uint64_t timeout = 1000,
                            const std::uint64_t tentative_after = 10,
                            const std::uint64_t foreground_interval = 100,
                            const std::uint64_t foreground_idle = 100)
{
    return std::string{R"({
"schema":"baas.procedure-definition/v1",
"engine":"co_detect.python-compat/v1",
"payload":{
"profile_source":"device.server-and-locale/v1",
"ends":{"rgb":["end"],"image":[{"feature":"image-end","threshold":0.9,"rgb_diff":10}]},
"reactions":{"rgb":[{"feature":"react","click":[10,20]}],"rgb_profiled":[],"image":[],"image_profiled":[]},
"popups":{"rgb":[{"feature":"popup","click":[30,40]}],"profiled_image":[]},
"loading":{"all_rgb":["loadA","loadB"]},
"foreground_check":{"android_only":true,"interval_ms":)"} +
        std::to_string(foreground_interval) + R"(,"idle_feature_ms":)" +
        std::to_string(foreground_idle) + R"(},
"loop":{"skip_first_screenshot":)" + (skip_first ? "true" : "false") +
        R"(,"timeout_ms":)" + std::to_string(timeout) +
        R"(,"duplicate_click_window_ms":2000,"tentative":{"enabled":true,"after_failed_cycles":)" +
        std::to_string(tentative_after) +
        R"(,"repeat_each_failed_cycle":true,"click":[50,60],"post_wait_screenshot_intervals":5}}
}})";
}

std::shared_ptr<const procedure::CoDetectPythonCompatDefinition> definition(
    const bool skip_first = false, const std::uint64_t timeout = 1000,
    const std::uint64_t tentative_after = 10,
    const std::uint64_t foreground_interval = 100,
    const std::uint64_t foreground_idle = 100)
{
    auto text = definition_json(skip_first, timeout, tentative_after,
                                foreground_interval, foreground_idle);
    auto loaded = procedure::load_co_detect_python_compat_definition(bytes(text));
    check(static_cast<bool>(loaded), "executor fixture definition must load");
    return loaded.definition;
}

std::shared_ptr<const std::vector<std::byte>> owned_bytes(std::string_view text)
{
    auto result = std::make_shared<std::vector<std::byte>>();
    for (const char character : text)
        result->push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    return result;
}

std::shared_ptr<const host::ProcedureSnapshot> snapshot()
{
    auto payload = owned_bytes("x");
    auto resources_snapshot = resources::ResourceSnapshot::build(
        {"CN", std::nullopt},
        {{"fixture/data", std::nullopt, std::nullopt, "application/octet-stream",
          payload->size(), resources::sha256_hex(*payload), payload}});
    host::ProcedureDescriptorInput descriptor{
        "fixture", {"done"},
        {host::ProcedureEffect::Capture, host::ProcedureEffect::Vision,
         host::ProcedureEffect::Input, host::ProcedureEffect::Wait,
         host::ProcedureEffect::ForegroundCheck},
        {}, resources::sha256_hex(*owned_bytes("executor")), {}};
    descriptor.sha256 = host::procedure_descriptor_sha256(descriptor);
    return host::ProcedureSnapshot::build({std::move(descriptor)}, resources_snapshot);
}

class Probe final : public runtime::HostCancellationProbe {
public:
    bool cancelled() const noexcept override { return cancel.load(); }
    bool deadline_exceeded() const noexcept override { return deadline.load(); }
    std::atomic<bool> cancel{};
    std::atomic<bool> deadline{};
};

class Reporter final : public host::ProcedureEffectReporter {
public:
    bool report(const host::ProcedureEffect effect,
                const host::ProcedureEffectStage stage) noexcept override
    {
        events.emplace_back(effect, stage);
        return accept;
    }
    bool accept{true};
    std::vector<std::pair<host::ProcedureEffect, host::ProcedureEffectStage>> events;
};

class Frame final : public procedure::CoDetectFrame {
public:
    explicit Frame(const int value) : id(value) {}
    int id{};
};

class Session final : public procedure::CoDetectDeviceSession {
public:
    const std::string& device_id() const noexcept override { return id; }
    procedure::CoDetectProfile profile() const noexcept override
    {
        return procedure::CoDetectProfile::cn;
    }
    bool is_android() const noexcept override { return android; }
    std::uint64_t monotonic_ms() const noexcept override { return now; }
    std::uint64_t screenshot_interval_ms() const noexcept override { return interval; }
    std::shared_ptr<const procedure::CoDetectFrame> latest_frame() const noexcept override
    {
        return latest;
    }
    void publish_latest_frame(
        std::shared_ptr<const procedure::CoDetectFrame> value) noexcept override
    {
        latest = std::move(value);
    }
    procedure::CoDetectResult<std::shared_ptr<const procedure::CoDetectFrame>> capture(
        const procedure::CoDetectControl&) override
    {
        ++captures;
        if (throw_bad_alloc_on_capture) throw std::bad_alloc{};
        if (throw_on_capture) throw std::runtime_error("capture failed");
        if (capture_error) return *capture_error;
        now += capture_advance;
        if (cancel_on_capture && probe) probe->cancel = true;
        return std::shared_ptr<const procedure::CoDetectFrame>(
            std::make_shared<Frame>(captures));
    }
    procedure::CoDetectResult<std::monostate> click(
        const procedure::CoDetectClick click,
        const procedure::CoDetectControl&) override
    {
        clicks.push_back(click);
        return std::monostate{};
    }
    procedure::CoDetectResult<std::monostate> wait(
        const std::uint64_t milliseconds,
        const procedure::CoDetectControl&) override
    {
        waits.push_back(milliseconds);
        now += milliseconds;
        return std::monostate{};
    }
    procedure::CoDetectResult<bool> foreground_matches(
        const procedure::CoDetectControl&) override
    {
        ++foreground_checks;
        return foreground;
    }

    std::string id{"device"};
    bool android{true};
    bool foreground{true};
    std::uint64_t now{};
    std::uint64_t interval{10};
    std::uint64_t capture_advance{};
    int captures{};
    int foreground_checks{};
    bool cancel_on_capture{};
    bool throw_bad_alloc_on_capture{};
    bool throw_on_capture{};
    std::optional<procedure::CoDetectOperationError> capture_error;
    Probe* probe{};
    std::shared_ptr<const procedure::CoDetectFrame> latest;
    std::vector<procedure::CoDetectClick> clicks;
    std::vector<std::uint64_t> waits;
};

class Features final : public procedure::CoDetectFeatureView {
public:
    procedure::CoDetectResult<bool> match_rgb(
        const procedure::CoDetectFrame& base, const std::string_view feature,
        const procedure::CoDetectControl&) override
    {
        const auto id = static_cast<const Frame&>(base).id;
        calls.push_back(std::to_string(id) + ":rgb:" + std::string(feature));
        return matches[{id, std::string(feature)}];
    }
    procedure::CoDetectResult<bool> match_image(
        const procedure::CoDetectFrame& base, const std::string_view feature,
        const procedure::CoDetectImageMatch match,
        const procedure::CoDetectControl&) override
    {
        const auto id = static_cast<const Frame&>(base).id;
        calls.push_back(std::to_string(id) + ":image:" + std::string(feature));
        image_matches.push_back(match);
        return matches[{id, std::string(feature)}];
    }
    std::map<std::pair<int, std::string>, bool> matches;
    std::vector<std::string> calls;
    std::vector<procedure::CoDetectImageMatch> image_matches;
};

host::ProcedureExecutorOutcome run(
    const std::shared_ptr<const procedure::CoDetectPythonCompatDefinition>& model,
    const std::shared_ptr<Session>& session, const std::shared_ptr<Features>& features,
    const std::shared_ptr<Probe>& probe, Reporter& reporter)
{
    auto snap = snapshot();
    auto descriptor = snap->resolve("fixture");
    auto executor = procedure::make_co_detect_python_compat_executor(
        model, {{"end", "done"}, {"image-end", "done"}}, session, features);
    host::ProcedureExecutionRequest request(
        snap, descriptor, "device", {}, probe, reporter);
    return executor->execute(request);
}

void test_shared_frame_loading_and_priority()
{
    auto session = std::make_shared<Session>();
    session->latest = std::make_shared<Frame>(0);
    auto features = std::make_shared<Features>();
    features->matches[{0, "loadA"}] = true;
    features->matches[{0, "loadB"}] = true;
    features->matches[{1, "end"}] = true;
    features->matches[{1, "react"}] = true;
    Reporter reporter;
    auto outcome = run(definition(true), session, features,
                       std::make_shared<Probe>(), reporter);
    check(outcome.ok() && outcome.terminal_id() == "done",
          "loading exits to the ordered terminal");
    check(session->captures == 1, "skip-first consumes the shared owned frame");
    check(session->waits == std::vector<std::uint64_t>{10},
          "loading capture waits one screenshot interval");
    check(session->clicks.empty(), "end priority prevents reaction input");
    const auto latest = std::dynamic_pointer_cast<const Frame>(session->latest);
    check(latest && latest->id == 1,
          "successful capture publishes an owned latest frame");

    session = std::make_shared<Session>();
    features = std::make_shared<Features>();
    features->matches[{1, "image-end"}] = true;
    Reporter image_reporter;
    outcome = run(definition(), session, features, std::make_shared<Probe>(),
                  image_reporter);
    check(outcome.ok() && !features->image_matches.empty() &&
              features->image_matches.front().threshold == 0.9 &&
              features->image_matches.front().rgb_diff == 10,
          "image end uses its activated threshold and RGB override");
}

void test_duplicate_popup_and_tentative_state()
{
    auto session = std::make_shared<Session>();
    auto features = std::make_shared<Features>();
    features->matches[{1, "react"}] = true;
    features->matches[{2, "react"}] = true;
    features->matches[{3, "popup"}] = true;
    features->matches[{4, "popup"}] = true;
    features->matches[{5, "end"}] = true;
    Reporter reporter;
    auto outcome = run(definition(), session, features,
                       std::make_shared<Probe>(), reporter);
    check(outcome.ok(), "reaction/popup sequence reaches terminal");
    check(session->clicks == std::vector<procedure::CoDetectClick>{
              {10, 20}, {30, 40}, {30, 40}},
          "ordinary duplicate is suppressed while popups are never suppressed");

    session = std::make_shared<Session>();
    features = std::make_shared<Features>();
    features->matches[{3, "end"}] = true;
    Reporter tentative_reporter;
    outcome = run(definition(false, 1000, 1), session, features,
                  std::make_shared<Probe>(), tentative_reporter);
    check(outcome.ok(), "tentative sequence reaches terminal");
    check(session->clicks == std::vector<procedure::CoDetectClick>{{50, 60}},
          "first tentative click occurs at threshold plus one");
    check(session->waits == std::vector<std::uint64_t>{50},
          "tentative waits five screenshot intervals");
}

void test_foreground_and_control_precedence()
{
    auto session = std::make_shared<Session>();
    session->capture_advance = 11;
    session->foreground = false;
    auto features = std::make_shared<Features>();
    Reporter reporter;
    auto outcome = run(definition(false, 1000, 10, 10, 10), session, features,
                       std::make_shared<Probe>(), reporter);
    check(!outcome.ok() && outcome.error().code ==
              host::ProcedureExecutorErrorCode::ForegroundPackageMismatch,
          "strict greater-than foreground gates produce typed mismatch");
    check(session->foreground_checks == 1, "both foreground gates are required");

    auto probe = std::make_shared<Probe>();
    probe->deadline = true;
    probe->cancel = true;
    session = std::make_shared<Session>();
    features = std::make_shared<Features>();
    Reporter precedence_reporter;
    outcome = run(definition(), session, features, probe, precedence_reporter);
    check(!outcome.ok() && outcome.error().code ==
              host::ProcedureExecutorErrorCode::DeadlineExceeded &&
              outcome.error().deadline_scope == host::ProcedureDeadlineScope::Context,
          "context deadline has precedence over cancellation");

    probe = std::make_shared<Probe>();
    session = std::make_shared<Session>();
    session->capture_advance = 10;
    session->cancel_on_capture = true;
    session->probe = probe.get();
    features = std::make_shared<Features>();
    Reporter call_reporter;
    outcome = run(definition(false, 10), session, features, probe, call_reporter);
    check(!outcome.ok() && outcome.error().code ==
              host::ProcedureExecutorErrorCode::DeadlineExceeded &&
              outcome.error().deadline_scope == host::ProcedureDeadlineScope::Call,
          "call deadline wins when cancellation becomes observable together");
    check(call_reporter.events.size() == 2 &&
              call_reporter.events[0].second == host::ProcedureEffectStage::Began &&
              call_reporter.events[1].second == host::ProcedureEffectStage::Committed,
          "effect reporter brackets the real capture boundary");
}

void test_missing_shared_frame_fails_closed()
{
    auto session = std::make_shared<Session>();
    auto features = std::make_shared<Features>();
    Reporter reporter;
    const auto outcome = run(definition(true), session, features,
                             std::make_shared<Probe>(), reporter);
    check(!outcome.ok() && outcome.error().code ==
              host::ProcedureExecutorErrorCode::Unavailable &&
              outcome.error().unavailable_reason ==
                  host::ProcedureUnavailableReason::RecentFrameUnavailable,
          "missing skip-first cache has a typed fail-closed reason");
    check(reporter.events.empty() && features->calls.empty(),
          "missing shared frame fails before any effect");
}

void test_operation_failures_are_typed_and_fail_closed()
{
    auto session = std::make_shared<Session>();
    session->capture_error = procedure::CoDetectOperationError::DeviceDisconnected;
    auto features = std::make_shared<Features>();
    Reporter reporter;
    auto outcome = run(definition(), session, features,
                       std::make_shared<Probe>(), reporter);
    check(!outcome.ok() && outcome.error().code ==
              host::ProcedureExecutorErrorCode::DeviceDisconnected,
          "device operation failure preserves its typed executor error");
    check(reporter.events.size() == 2 &&
              reporter.events.back().second == host::ProcedureEffectStage::Unknown,
          "failed operation closes its begun effect as unknown");

    session = std::make_shared<Session>();
    session->throw_bad_alloc_on_capture = true;
    features = std::make_shared<Features>();
    Reporter allocation_reporter;
    outcome = run(definition(), session, features, std::make_shared<Probe>(),
                  allocation_reporter);
    check(!outcome.ok() && outcome.error().code ==
              host::ProcedureExecutorErrorCode::ResourceExhausted,
          "operation allocation failure is fail-closed and typed");

    session = std::make_shared<Session>();
    session->throw_on_capture = true;
    features = std::make_shared<Features>();
    Reporter exception_reporter;
    outcome = run(definition(), session, features, std::make_shared<Probe>(),
                  exception_reporter);
    check(!outcome.ok() && outcome.error().code ==
              host::ProcedureExecutorErrorCode::Internal,
          "unexpected operation exception is fail-closed");
}

}  // namespace

int main()
{
    test_shared_frame_loading_and_priority();
    test_duplicate_popup_and_tentative_state();
    test_foreground_and_control_precedence();
    test_missing_shared_frame_fails_closed();
    test_operation_failures_are_typed_and_fail_closed();
    if (failures != 0) {
        std::cerr << failures << " co-detect executor test(s) failed\n";
        return 1;
    }
    std::cout << "co-detect executor tests passed\n";
    return 0;
}
