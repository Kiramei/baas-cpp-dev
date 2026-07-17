#include "script/runtime/SynchronousEvaluator.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace baas::script;

namespace {

int failures = 0;

class SwitchableProbe final : public runtime::HostCancellationProbe {
public:
    [[nodiscard]] bool cancelled() const noexcept override
    {
        return cancelled_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool deadline_exceeded() const noexcept override
    {
        return deadline_.load(std::memory_order_relaxed);
    }

    void set_cancelled() noexcept
    {
        cancelled_.store(true, std::memory_order_relaxed);
    }

    void set_deadline() noexcept
    {
        deadline_.store(true, std::memory_order_relaxed);
    }

private:
    std::atomic<bool> cancelled_{};
    std::atomic<bool> deadline_{};
};

class HostBudgetRaceProbe final : public runtime::HostCancellationProbe {
public:
    explicit HostBudgetRaceProbe(
        std::shared_ptr<const std::atomic<int>> completed_calls) noexcept
        : completed_calls_(std::move(completed_calls))
    {}

    [[nodiscard]] bool cancelled() const noexcept override
    {
        if (completed_calls_->load(std::memory_order_relaxed) == 0) return false;
        return post_call_polls_.fetch_add(1, std::memory_order_relaxed) + 1 >= 6;
    }

    [[nodiscard]] bool deadline_exceeded() const noexcept override
    {
        return false;
    }

    [[nodiscard]] std::size_t post_call_polls() const noexcept
    {
        return post_call_polls_.load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<const std::atomic<int>> completed_calls_;
    mutable std::atomic<std::size_t> post_call_polls_{};
};

void check(const bool condition, const std::string_view message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

template <typename Function>
runtime::EvaluationError expect_error(
    const runtime::LanguageErrorCode code, Function&& function,
    const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const runtime::EvaluationError& error) {
        check(error.code() == code, message);
        return error;
    } catch (...) {
        check(false, message);
    }
    return {code, "missing", {}, {}, 0};
}

std::shared_ptr<const runtime::HostModuleRegistry> log_metadata()
{
    runtime::HostExportDescriptor emit{
        "emit", "host.log.emit.v1", "log.emit"};
    return std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{
            {"baas/log", {1, 0}, {emit}},
            {"baas/log", {1, 2}, {emit}},
        });
}

runtime::SynchronousHostOptions log_options(
    std::shared_ptr<const runtime::SynchronousNativeBindingSet> bindings,
    const bool declared_capability = true,
    const bool policy = true,
    const bool platform = true,
    const bool task = true)
{
    runtime::SynchronousHostOptions options;
    options.metadata = log_metadata();
    options.bindings = std::move(bindings);
    options.permissions.declared_modules.push_back({"baas/log", 1, 0});
    if (declared_capability)
        options.permissions.declared_capabilities.push_back("log.emit");
    if (policy) options.permissions.policy_capabilities.push_back("log.emit");
    if (platform) options.permissions.platform_capabilities.push_back("log.emit");
    if (task) options.permissions.task_capabilities.push_back("log.emit");
    return options;
}

void test_log_emit_vertical_slice_and_version_selection()
{
    auto host = std::make_shared<runtime::InMemoryLogHost>();
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            runtime::make_in_memory_log_binding(host)});
    runtime::SynchronousEvaluator evaluator(
        {{"main",
          "import \"baas/log\" as log;\n"
          "let same = log.emit is log.emit;\n"
          "log.emit(\"info\", \"ready\", {\"attempt\": 1});\n"
          "let result = 7;\n"}},
        log_options(bindings));
    static_cast<void>(evaluator.execute("main"));
    const auto events = host->events();
    check(events.size() == 1 && events[0].level == "info" &&
              events[0].message == "ready" && events[0].fields &&
              events[0].fields->size() == 1,
          "baas/log.emit must cross the owning scalar/ordered-map ABI once");
    check(evaluator.module_export("main", "same").as_boolean(),
          "lazy Host member authorization must cache one callable identity");
    check(evaluator.stats().host_authorization_attempts == 1 &&
              evaluator.stats().authorized_host_exports == 1 &&
              evaluator.stats().host_calls == 1,
          "Host authorization and callback stats must count exact admitted work");

    runtime::HostApiVersion observed{};
    auto observing = runtime::make_in_memory_log_binding(host);
    observing.callback = [&](const runtime::HostCallContext& context,
                             const runtime::HostArguments&) {
        observed = context.selected_version;
        return runtime::HostResult::success();
    };
    auto observed_bindings =
        std::make_shared<const runtime::SynchronousNativeBindingSet>(
            std::vector<runtime::SynchronousNativeBinding>{std::move(observing)});
    runtime::SynchronousEvaluator versioned(
        {{"main", "import \"baas/log\" as log; log.emit(\"i\", \"m\");\n"}},
        log_options(observed_bindings));
    static_cast<void>(versioned.execute("main"));
    check(observed == runtime::HostApiVersion{1, 2},
          "evaluator must pass the greatest compatible exact minor to the callback");
}

void test_defer_host_callbacks_mask_external_interrupts()
{
    for (const bool deadline_case : {false, true}) {
        auto probe = std::make_shared<SwitchableProbe>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto cleanup_masked = std::make_shared<std::atomic<bool>>(false);
        auto binding = runtime::make_in_memory_log_binding(
            std::make_shared<runtime::InMemoryLogHost>());
        binding.callback =
            [probe, calls, cleanup_masked, deadline_case](
                const runtime::HostCallContext& context,
                const runtime::HostArguments&) {
                const auto call = calls->fetch_add(1, std::memory_order_relaxed) + 1;
                if (call == 1) {
                    if (deadline_case) probe->set_deadline();
                    else probe->set_cancelled();
                } else {
                    cleanup_masked->store(
                        !context.deadline_exceeded() && !context.cancelled(),
                        std::memory_order_relaxed);
                }
                return runtime::HostResult::success();
            };
        auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
            std::vector<runtime::SynchronousNativeBinding>{std::move(binding)});
        auto options = log_options(bindings);
        options.cancellation = probe;
        options.budget_limits = {{"log_events", 2}};
        runtime::SynchronousEvaluator evaluator(
            {{"main",
              "import \"baas/log\" as log;\n"
              "fn run() {\n"
              "  defer log.emit(\"info\", \"cleanup-budget-blocked\");\n"
              "  defer log.emit(\"info\", \"cleanup-masked\");\n"
              "  log.emit(\"info\", \"arm\");\n"
              "  let after = 1;\n"
              "}\n"
              "run();\n"}},
            std::move(options));

        expect_error(
            deadline_case ? runtime::LanguageErrorCode::DeadlineExceeded
                          : runtime::LanguageErrorCode::Cancelled,
            [&] { static_cast<void>(evaluator.execute("main")); },
            "an external terminal must unwind through a deferred Host callback");
        check(calls->load(std::memory_order_relaxed) == 2
                  && cleanup_masked->load(std::memory_order_relaxed),
              "deferred Host callback entry and cooperative polling must mask deadline/cancel");
        check(evaluator.stats().registered_defers == 2
                  && evaluator.stats().executed_defers == 2
                  && evaluator.stats().cleanup_steps > 0
                  && evaluator.stats().host_calls == 2,
              "masked cleanup must still drain every defer and enforce its Host budget");
    }
}

void test_host_task_claims_outrank_same_boundary_cancellation()
{
    for (const bool scoped_budget : {false, true}) {
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto probe = std::make_shared<HostBudgetRaceProbe>(calls);
        auto binding = runtime::make_in_memory_log_binding(
            std::make_shared<runtime::InMemoryLogHost>());
        binding.callback =
            [calls](const runtime::HostCallContext&,
                    const runtime::HostArguments&) {
                calls->fetch_add(1, std::memory_order_relaxed);
                return runtime::HostResult::success();
            };
        auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
            std::vector<runtime::SynchronousNativeBinding>{std::move(binding)});
        auto options = log_options(bindings);
        options.cancellation = probe;
        if (scoped_budget) {
            options.limits.max_host_calls = 10;
            options.budget_limits = {{"log_events", 1}};
        } else {
            options.limits.max_host_calls = 1;
        }
        runtime::SynchronousEvaluator evaluator(
            {{"main",
              "import \"baas/log\" as log;\n"
              "log.emit(\"info\", \"first\");\n"
              "log.emit(\"info\", \"blocked\");\n"}},
            std::move(options));

        expect_error(
            runtime::LanguageErrorCode::TaskLimitExceeded,
            [&] { static_cast<void>(evaluator.execute("main")); },
            scoped_budget
                ? "scoped Host budget must outrank same-boundary cancellation"
                : "global Host task limit must outrank same-boundary cancellation");
        check(calls->load(std::memory_order_relaxed) == 1
                  && probe->post_call_polls() >= 6
                  && evaluator.stats().host_calls == 1,
              "Host task priority evidence must reach admission without a second callback");
    }
}

void test_import_dedup_dynamic_alias_and_gc_roots()
{
    auto host = std::make_shared<runtime::InMemoryLogHost>();
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            runtime::make_in_memory_log_binding(host)});
    runtime::SynchronousEvaluator evaluator(
        {
            {"main",
             "import \"left\" as left; import \"right\" as right;\n"
             "let same = left.emit is right.emit;\n"
             "fn identity(value) { return value; }\n"
             "let alias = identity(left.host_module);\n"
             "alias.emit(\"info\", \"aliased\");\n"},
            {"left",
             "import \"baas/log\" as host_module; let emit = host_module.emit;\n"},
            {"right",
             "import \"baas/log\" as host_module; let emit = host_module.emit;\n"},
        },
        log_options(bindings));
    static_cast<void>(evaluator.execute("left"));
    evaluator.heap().collect();
    static_cast<void>(evaluator.execute("main"));
    check(evaluator.module_export("main", "same").as_boolean(),
          "duplicate Host imports across modules must share one callable cache");
    check(host->events().size() == 1,
          "Host module values must remain authorized after alias flow and heap collection");
    check(evaluator.stats().host_authorization_attempts == 1,
          "source import duplication must not duplicate per-export authorization");
}

void test_no_host_constructor_preserves_explicit_boundary()
{
    runtime::SynchronousEvaluator evaluator({{
        "main", "import \"baas/log\" as log; let result = 1;\n"}});
    expect_error(runtime::LanguageErrorCode::HostUnavailable,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "legacy no-host construction must preserve explicit HostUnavailable behavior");
}

void test_capability_adapter_and_syntax_gates_precede_arguments()
{
    struct GateCase { bool declared; bool policy; bool platform; bool task; };
    const std::vector<GateCase> gates{
        {false, true, true, true},
        {true, false, true, true},
        {true, true, false, true},
        {true, true, true, false},
    };
    for (const auto gate : gates) {
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto binding = runtime::make_in_memory_log_binding(
            std::make_shared<runtime::InMemoryLogHost>());
        binding.callback = [calls](const runtime::HostCallContext&,
                                   const runtime::HostArguments&) {
            ++*calls;
            return runtime::HostResult::success();
        };
        auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
            std::vector<runtime::SynchronousNativeBinding>{std::move(binding)});
        runtime::SynchronousEvaluator evaluator(
            {{"main",
              "import \"baas/log\" as log; log.emit(1 / 0, \"never\");\n"}},
            log_options(bindings, gate.declared, gate.policy, gate.platform, gate.task));
        expect_error(runtime::LanguageErrorCode::CapabilityDenied,
            [&] { static_cast<void>(evaluator.execute("main")); },
            "each missing capability layer must deny before evaluating arguments");
        check(calls->load() == 0 && evaluator.stats().host_calls == 0,
              "capability denial must not enter the callback");
    }

    auto empty_bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{});
    runtime::SynchronousEvaluator missing_adapter(
        {{"main",
          "import \"baas/log\" as log; log.emit(1 / 0, \"never\");\n"}},
        log_options(empty_bindings));
    expect_error(runtime::LanguageErrorCode::HostUnavailable,
        [&] { static_cast<void>(missing_adapter.execute("main")); },
        "missing adapter must win before a failing argument expression");

    auto host = std::make_shared<runtime::InMemoryLogHost>();
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            runtime::make_in_memory_log_binding(host)});
    runtime::SynchronousEvaluator unknown(
        {{"main",
          "import \"baas/log\" as log;\n"
          "log.emit(level = \"i\", message = \"m\", bogus = 1 / 0);\n"}},
        log_options(bindings));
    expect_error(runtime::LanguageErrorCode::CallArgumentUnknown,
        [&] { static_cast<void>(unknown.execute("main")); },
        "unknown named Host arguments must be rejected before any expression evaluation");
    runtime::SynchronousEvaluator duplicate(
        {{"main",
          "import \"baas/log\" as log;\n"
          "log.emit(\"i\", level = 1 / 0, message = \"m\");\n"}},
        log_options(bindings));
    expect_error(runtime::LanguageErrorCode::CallArgumentDuplicate,
        [&] { static_cast<void>(duplicate.execute("main")); },
        "duplicate Host arguments must be rejected before any expression evaluation");
    check(host->events().empty(),
          "syntax-gate failures must not produce an in-memory log effect");
}

void test_argument_shapes_aggregate_limits_and_named_binding()
{
    auto host = std::make_shared<runtime::InMemoryLogHost>();
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            runtime::make_in_memory_log_binding(host)});

    runtime::SynchronousEvaluator wrong_shape(
        {{"main",
          "import \"baas/log\" as log; log.emit(\"i\", \"m\", 7);\n"}},
        log_options(bindings));
    expect_error(runtime::LanguageErrorCode::HostValidationFailed,
        [&] { static_cast<void>(wrong_shape.execute("main")); },
        "log fields must be an ordered map rather than arbitrary JSON");
    check(host->events().empty() && wrong_shape.stats().host_calls == 0,
          "shape validation must finish before Host side effects");

    auto node_limited_options = log_options(bindings);
    node_limited_options.limits.max_conversion_nodes = 2;
    runtime::SynchronousEvaluator node_limited(
        {{"main",
          "import \"baas/log\" as log; log.emit(\"i\", \"m\", {});\n"}},
        node_limited_options);
    expect_error(runtime::LanguageErrorCode::HostValidationFailed,
        [&] { static_cast<void>(node_limited.execute("main")); },
        "all Host arguments must share one aggregate node budget");
    check(node_limited.stats().host_calls == 0 &&
              node_limited.stats().host_conversion_nodes == 0,
          "rejected aggregate arguments must not publish callback or conversion stats");

    auto byte_limited_options = log_options(bindings);
    byte_limited_options.limits.max_conversion_bytes = 6;
    runtime::SynchronousEvaluator byte_limited(
        {{"main",
          "import \"baas/log\" as log; log.emit(\"a\", \"0123456789\");\n"}},
        byte_limited_options);
    expect_error(runtime::LanguageErrorCode::HostValidationFailed,
        [&] { static_cast<void>(byte_limited.execute("main")); },
        "later arguments must be converted under the remaining aggregate byte budget");
    check(byte_limited.stats().host_calls == 0 && host->events().empty(),
          "an oversized second argument must not enter the adapter");

    runtime::SynchronousEvaluator named(
        {{"main",
          "import \"baas/log\" as log;\n"
          "log.emit(message = \"named\", level = \"info\");\n"}},
        log_options(bindings));
    static_cast<void>(named.execute("main"));
    const auto events = host->events();
    check(events.size() == 1 && events[0].level == "info" &&
              events[0].message == "named",
          "named Host arguments must bind in catalog parameter order");
}

void test_budget_failure_cache_and_exception_translation()
{
    auto host = std::make_shared<runtime::InMemoryLogHost>();
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            runtime::make_in_memory_log_binding(host)});
    auto budgeted_options = log_options(bindings);
    budgeted_options.budget_limits.push_back({"log_events", 1});
    runtime::SynchronousEvaluator budgeted(
        {{"main",
          "import \"baas/log\" as log;\n"
          "log.emit(\"i\", \"first\");\n"
          "log.emit(1 / 0, \"never\");\n"}},
        budgeted_options);
    expect_error(runtime::LanguageErrorCode::TaskLimitExceeded,
        [&] { static_cast<void>(budgeted.execute("main")); },
        "named Host budget exhaustion must win before later argument evaluation");
    check(host->events().size() == 1 && budgeted.stats().host_calls == 1,
          "only the budget-admitted call may reach the adapter");

    auto calls = std::make_shared<std::atomic<int>>(0);
    auto timeout_binding = runtime::make_in_memory_log_binding(host);
    timeout_binding.callback = [calls](const runtime::HostCallContext&,
                                       const runtime::HostArguments&) {
        ++*calls;
        return runtime::HostResult::failure({
            runtime::HostErrorCode::DeadlineExceeded, "safe timeout", true,
            runtime::HostEffectState::Unknown,
            runtime::JsonValue(runtime::JsonObject{
                {"deadline_scope", runtime::JsonValue("call")}})});
    };
    auto timeout_bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{std::move(timeout_binding)});
    runtime::SynchronousEvaluator failed(
        {{"main", "import \"baas/log\" as log; log.emit(\"i\", \"m\");\n"}},
        log_options(timeout_bindings));
    const auto first = expect_error(runtime::LanguageErrorCode::Timeout,
        [&] { static_cast<void>(failed.execute("main")); },
        "Host deadline discriminator must translate through ERR-016");
    expect_error(runtime::LanguageErrorCode::Timeout,
        [&] { static_cast<void>(failed.execute("main")); },
        "failed module execution must rethrow its cached Host translation");
    check(calls->load() == 1 &&
              std::string(first.what()).find("host.log.emit.v1") != std::string::npos &&
              std::string(first.what()).find("effect_state=unknown") != std::string::npos,
          "Host failure cache must avoid repeated effects and retain safe identity/effect state");

    runtime::SynchronousEvaluator caught_host_error(
        {{"main",
          "import \"baas/log\" as log;\n"
          "let code = \"\"; let origin = \"\"; let frame_kind = \"\";\n"
          "let frame_module = \"\"; let frame_function = \"\";\n"
          "let host_code = \"\"; let effect = \"\"; let retryable = false;\n"
          "let detached_detail = \"\";\n"
          "try { log.emit(\"i\", \"m\"); } catch (error) {\n"
          "  code = error.code; origin = error.origin;\n"
          "  frame_kind = error.stack[0].kind;\n"
          "  frame_module = error.stack[0].module;\n"
          "  frame_function = error.stack[0].function;\n"
          "  let first = error.details;\n"
          "  host_code = first.host_code; effect = first.effect_state;\n"
          "  retryable = first.retryable;\n"
          "  first.host_details.deadline_scope = \"mutated\";\n"
          "  detached_detail = error.details.host_details.deadline_scope;\n"
          "}\n"}},
        log_options(timeout_bindings));
    static_cast<void>(caught_host_error.execute("main"));
    const auto read_string = [&](const std::string_view name) {
        return caught_host_error.heap().string_copy(
            caught_host_error.module_export("main", name).as_heap_ref());
    };
    check(read_string("code") == "Timeout" && read_string("origin") == "host"
              && read_string("frame_kind") == "host"
              && read_string("frame_module") == "baas/log"
              && read_string("frame_function") == "emit"
              && read_string("host_code") == "HOST004_DEADLINE_EXCEEDED"
              && read_string("effect") == "unknown"
              && caught_host_error.module_export("main", "retryable").as_boolean()
              && read_string("detached_detail") == "call",
          "Host failures must expose allowlisted metadata and detached read-only detail projections");

    struct RejectedDetailCase {
        runtime::HostErrorCode code;
        runtime::JsonObject details;
    };
    const std::vector<RejectedDetailCase> rejected_details{
        {runtime::HostErrorCode::DeadlineExceeded,
         {{"deadline_scope", runtime::JsonValue(runtime::JsonObject{
              {"secret", runtime::JsonValue("nested")}})}}},
        {runtime::HostErrorCode::DeadlineExceeded,
         {{"deadline_scope", runtime::JsonValue("later")},
          {"secret", runtime::JsonValue(runtime::JsonObject{
              {"token", runtime::JsonValue("hidden")}})}}},
        {runtime::HostErrorCode::Internal,
         {{"deadline_scope", runtime::JsonValue("call")},
          {"secret", runtime::JsonValue("hidden")}}},
    };
    for (const auto& rejected : rejected_details) {
        auto binding = runtime::make_in_memory_log_binding(host);
        binding.callback = [rejected](const runtime::HostCallContext&,
                                      const runtime::HostArguments&) {
            return runtime::HostResult::failure({
                rejected.code,
                "safe failure",
                false,
                runtime::HostEffectState::Unknown,
                runtime::JsonValue(rejected.details)});
        };
        auto rejected_bindings =
            std::make_shared<const runtime::SynchronousNativeBindingSet>(
                std::vector<runtime::SynchronousNativeBinding>{std::move(binding)});
        runtime::SynchronousEvaluator rejected_evaluator(
            {{"main",
              "import \"baas/log\" as log;\n"
              "let code = \"\"; let has_host_details = true;\n"
              "try { log.emit(\"i\", \"m\"); } catch (error) {\n"
              "  code = error.code;\n"
              "  has_host_details = \"host_details\" in error.details;\n"
              "}\n"}},
            log_options(rejected_bindings));
        static_cast<void>(rejected_evaluator.execute("main"));
        check(rejected_evaluator.heap().string_copy(
                  rejected_evaluator.module_export("main", "code").as_heap_ref())
                      == "HostInternal"
                  && !rejected_evaluator.module_export(
                          "main", "has_host_details").as_boolean(),
              "undeclared, wrong-type, unknown, or irrelevant Host details must be omitted");
    }

    auto bad_alloc_binding = runtime::make_in_memory_log_binding(host);
    bad_alloc_binding.callback = [](const runtime::HostCallContext&,
                                    const runtime::HostArguments&) -> runtime::HostResult {
        throw std::bad_alloc();
    };
    auto allocation_bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{std::move(bad_alloc_binding)});
    runtime::SynchronousEvaluator allocation(
        {{"main", "import \"baas/log\" as log; log.emit(\"i\", \"m\");\n"}},
        log_options(allocation_bindings));
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
        [&] { static_cast<void>(allocation.execute("main")); },
        "callback bad_alloc must map without allocation to MemoryLimitExceeded");

    auto throwing_binding = runtime::make_in_memory_log_binding(host);
    throwing_binding.callback = [](const runtime::HostCallContext&,
                                   const runtime::HostArguments&) -> runtime::HostResult {
        throw std::runtime_error("secret credential");
    };
    auto throwing_bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{std::move(throwing_binding)});
    runtime::SynchronousEvaluator throwing(
        {{"main", "import \"baas/log\" as log; log.emit(\"i\", \"m\");\n"}},
        log_options(throwing_bindings));
    const auto redacted = expect_error(runtime::LanguageErrorCode::HostInternal,
        [&] { static_cast<void>(throwing.execute("main")); },
        "unknown callback exceptions must translate to HostInternal");
    check(std::string(redacted.what()).find("secret") == std::string::npos,
          "native exception what() text must remain redacted");
}

void test_cache_transaction_permission_preflight_and_failure_cache()
{
    auto host = std::make_shared<runtime::InMemoryLogHost>();
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            runtime::make_in_memory_log_binding(host)});
    auto options = log_options(bindings);
    runtime::HostResolutionRequest initial;
    initial.declared_modules = options.permissions.declared_modules;
    initial.declared_capabilities = options.permissions.declared_capabilities;
    initial.policy_capabilities = options.permissions.policy_capabilities;
    initial.platform_capabilities = options.permissions.platform_capabilities;
    initial.task_capabilities = options.permissions.task_capabilities;
    initial.imports.push_back({"baas/log", {}});
    auto authorized = initial;
    authorized.imports[0].exports.push_back("emit");
    const auto initial_work = options.metadata->resolve(initial).validation_work;
    const auto authorization_work = options.metadata->resolve(authorized).validation_work;
    options.limits.max_registry_validation_work =
        initial_work + authorization_work - 1;
    runtime::SynchronousEvaluator transactional(
        {
            {"first", "import \"baas/log\" as log; log.emit(\"i\", \"m\");\n"},
            {"second", "import \"baas/log\" as log; log.emit(\"i\", \"m\");\n"},
        },
        options);
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
        [&] { static_cast<void>(transactional.execute("first")); },
        "aggregate registry work exact-over must reject the first authorization");
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
        [&] { static_cast<void>(transactional.execute("second")); },
        "rolled-back authorization cache must retry rather than return a null callable");
    check(transactional.stats().host_authorization_attempts == 2 &&
              transactional.stats().authorized_host_exports == 0,
          "transient authorization failures must leave no partially published cache entry");

    auto no_adapter = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{});
    runtime::SynchronousEvaluator stable_failure(
        {
            {"first", "import \"baas/log\" as log; log.emit(\"i\", \"m\");\n"},
            {"second", "import \"baas/log\" as log; log.emit(\"i\", \"m\");\n"},
        },
        log_options(no_adapter));
    expect_error(runtime::LanguageErrorCode::HostUnavailable,
        [&] { static_cast<void>(stable_failure.execute("first")); },
        "stable missing-adapter failure must be reported");
    expect_error(runtime::LanguageErrorCode::HostUnavailable,
        [&] { static_cast<void>(stable_failure.execute("second")); },
        "stable missing-adapter failure must be cacheable across modules");
    check(stable_failure.stats().host_authorization_attempts == 1,
          "stable authorization failure must not repeat registry resolution");

    auto too_many_permissions = log_options(bindings);
    too_many_permissions.limits.max_permission_entries = 1;
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
        [&] {
            runtime::SynchronousEvaluator rejected(
                {{"main", "import \"baas/log\" as log;\n"}},
                too_many_permissions);
        },
        "permission vectors must be count-bounded before resolution request copies");
    auto too_many_bytes = log_options(bindings);
    too_many_bytes.limits.max_permission_string_bytes = 3;
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
        [&] {
            runtime::SynchronousEvaluator rejected(
                {{"main", "import \"baas/log\" as log;\n"}},
                too_many_bytes);
        },
        "permission vectors must be string-bounded before resolution request copies");
}

void test_owner_thread_and_reentry_guards()
{
    auto host = std::make_shared<runtime::InMemoryLogHost>();
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            runtime::make_in_memory_log_binding(host)});
    runtime::SynchronousEvaluator owner(
        {{"main", "let value = 1;\n"}}, log_options(bindings));
    std::atomic<int> observed{-1};
    std::thread foreign([&] {
        try {
            static_cast<void>(owner.execute("main"));
        } catch (const runtime::EvaluationError& error) {
            observed = static_cast<int>(error.code());
        }
    });
    foreign.join();
    check(observed.load() == static_cast<int>(runtime::LanguageErrorCode::HostUnavailable),
          "synchronous evaluator must reject execution from a non-owning thread");

    runtime::SynchronousEvaluator* evaluator = nullptr;
    std::atomic<int> reentry_code{-1};
    auto reentrant_binding = runtime::make_in_memory_log_binding(host);
    reentrant_binding.callback = [&](const runtime::HostCallContext&,
                                     const runtime::HostArguments&) {
        try {
            static_cast<void>(evaluator->execute("main"));
        } catch (const runtime::EvaluationError& error) {
            reentry_code = static_cast<int>(error.code());
        }
        return runtime::HostResult::success();
    };
    auto reentrant_bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{std::move(reentrant_binding)});
    runtime::SynchronousEvaluator reentrant(
        {{"main", "import \"baas/log\" as log; log.emit(\"i\", \"m\");\n"}},
        log_options(reentrant_bindings));
    evaluator = &reentrant;
    static_cast<void>(reentrant.execute("main"));
    check(reentry_code.load() == static_cast<int>(runtime::LanguageErrorCode::HostUnavailable),
          "Host callbacks must not re-enter the owning evaluator");
}

void test_result_heap_memory_failure_remains_terminal()
{
    runtime::SynchronousNativeBinding binding;
    binding.binding_id = "host.log.text.v1";
    binding.contract = {
        {{"level", runtime::HostValueType::String, true},
         {"message", runtime::HostValueType::String, true}},
        runtime::HostValueType::String,
        "log_events",
        runtime::HostExecutionMode::ThreadSafe,
        runtime::HostCancellationMode::Preflight};
    binding.callback = [](const runtime::HostCallContext&,
                          const runtime::HostArguments&) {
        return runtime::HostResult::success(
            runtime::HostValue(std::string(2'048, 'r')));
    };
    runtime::SynchronousHostLimits host_limits;
    host_limits.max_string_bytes = 4'096;
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{std::move(binding)},
        host_limits);
    auto options = log_options(bindings);
    options.metadata = std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{{
            "baas/log", {1, 0},
            {{"text", "host.log.text.v1", "log.emit"}}}});
    runtime::HeapLimits heap_limits;
    heap_limits.max_string_bytes = 1'024;
    runtime::SynchronousEvaluator evaluator(
        {{"main",
          "import \"baas/log\" as log; log.text(\"i\", \"m\");\n"}},
        options,
        {},
        heap_limits);
    expect_error(runtime::LanguageErrorCode::MemoryLimitExceeded,
        [&] { static_cast<void>(evaluator.execute("main")); },
        "real heap StringLimitExceeded during Host result publication must remain terminal memory exhaustion");
    check(evaluator.stats().host_calls == 1,
          "result publication failure occurs only after one successful callback result");
}

}  // namespace

int main()
{
    try {
        test_log_emit_vertical_slice_and_version_selection();
        test_defer_host_callbacks_mask_external_interrupts();
        test_host_task_claims_outrank_same_boundary_cancellation();
        test_import_dedup_dynamic_alias_and_gc_roots();
        test_no_host_constructor_preserves_explicit_boundary();
        test_capability_adapter_and_syntax_gates_precede_arguments();
        test_argument_shapes_aggregate_limits_and_named_binding();
        test_budget_failure_cache_and_exception_translation();
        test_cache_transaction_permission_preflight_and_failure_cache();
        test_owner_thread_and_reentry_guards();
        test_result_heap_memory_failure_remains_terminal();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    if (failures != 0) {
        std::cerr << failures << " synchronous Host evaluator test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "synchronous Host evaluator tests passed\n";
    return EXIT_SUCCESS;
}
