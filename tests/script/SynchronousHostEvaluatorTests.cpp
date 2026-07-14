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

}  // namespace

int main()
{
    try {
        test_log_emit_vertical_slice_and_version_selection();
        test_import_dedup_dynamic_alias_and_gc_roots();
        test_no_host_constructor_preserves_explicit_boundary();
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
