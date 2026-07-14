#include "script/runtime/ModuleGraph.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace runtime = baas::script::runtime;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Function>
void check_graph_error(
    const runtime::ModuleGraphErrorCode expected,
    Function&& function,
    const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const runtime::ModuleGraphError& error) {
        check(error.code() == expected, message);
    }
}

bool accept_nfc(std::string_view) noexcept { return true; }

void test_stable_dependency_first_order_and_host_edges()
{
    const std::vector<runtime::ModuleDefinition> modules{
        {"tasks/main", {"baas/log", "tasks/shared", "tasks/config"}},
        {"tasks/shared", {"tasks/config"}},
        {"tasks/independent", {}},
        {"tasks/config", {}},
    };
    const auto result = runtime::validate_module_graph(modules);
    const std::vector<std::string> expected{
        "tasks/config", "tasks/independent", "tasks/shared", "tasks/main"};
    check(result.initialization_order == expected,
          "topological order must place dependencies first and break ready ties lexically");
    check(result.package_import_edges == 3 && result.host_import_edges == 1,
          "package and host imports must be accounted independently");

    auto reversed = modules;
    std::reverse(reversed.begin(), reversed.end());
    check(runtime::validate_module_graph(reversed).initialization_order == expected,
          "manifest module ordering must not change deterministic graph output");
}

void test_missing_duplicate_and_host_definitions()
{
    using enum runtime::ModuleGraphErrorCode;
    check_graph_error(MissingModule,
                      [] {
                          static_cast<void>(runtime::validate_module_graph(
                              {{"main", {"missing"}}}));
                      },
                      "missing package import must fail before activation");
    check_graph_error(DuplicateModule,
                      [] {
                          static_cast<void>(runtime::validate_module_graph(
                              {{"main", {}}, {"main", {}}}));
                      },
                      "duplicate canonical module definition must fail");
    check_graph_error(HostModuleDefinition,
                      [] {
                          static_cast<void>(runtime::validate_module_graph(
                              {{"baas/log", {}}}));
                      },
                      "package graph must not define a registered host module");
}

void test_deterministic_cycle_uses_smallest_scc_and_source_order()
{
    const std::vector<runtime::ModuleDefinition> modules{
        {"z", {"z"}},
        {"b", {"a"}},
        {"a", {"c", "b"}},
        {"c", {"a"}},
    };
    try {
        static_cast<void>(runtime::validate_module_graph(modules));
        check(false, "cyclic graph must fail");
    } catch (const runtime::ModuleGraphError& error) {
        check(error.code() == runtime::ModuleGraphErrorCode::ImportCycle,
              "cycle must use the stable graph error code");
        const std::vector<std::string> expected{"a", "c", "a"};
        check(error.cycle() == expected,
              "cycle must choose smallest cyclic SCC then follow source-order edge");
        check(error.module() == "a", "cycle primary module must be its smallest canonical id");
    }
}

void test_limits_and_unicode_validation()
{
    using enum runtime::ModuleGraphErrorCode;
    check_graph_error(ModuleLimitExceeded,
                      [] {
                          runtime::ModuleGraphLimits limits;
                          limits.max_modules = 1;
                          static_cast<void>(runtime::validate_module_graph(
                              {{"a", {}}, {"b", {}}}, nullptr, limits));
                      },
                      "module count limit must fail transactionally");
    check_graph_error(ImportEdgeLimitExceeded,
                      [] {
                          runtime::ModuleGraphLimits limits;
                          limits.max_import_edges = 1;
                          static_cast<void>(runtime::validate_module_graph(
                              {{"a", {"b", "baas/log"}}, {"b", {}}}, nullptr, limits));
                      },
                      "package and host imports must both charge the edge budget");
    check_graph_error(ValidationWorkLimitExceeded,
                      [] {
                          runtime::ModuleGraphLimits limits;
                          limits.max_validation_work = 1;
                          static_cast<void>(runtime::validate_module_graph(
                              {{"a", {}}, {"b", {}}}, nullptr, limits));
                      },
                      "graph algorithms must share an explicit work budget");

    const std::string unicode = "\xE4\xBB\xBB\xE5\x8A\xA1/main";
    const auto accepted = runtime::validate_module_graph({{unicode, {}}}, accept_nfc);
    check(accepted.initialization_order == std::vector<std::string>{unicode},
          "graph validation must use the shared injected NFC predicate");
    check(runtime::module_graph_error_code_name(ImportCycle) == "MG007_IMPORT_CYCLE",
          "graph failures must expose stable foundation names");
}

}  // namespace

int main()
{
    test_stable_dependency_first_order_and_host_edges();
    test_missing_duplicate_and_host_definitions();
    test_deterministic_cycle_uses_smallest_scc_and_source_order();
    test_limits_and_unicode_validation();
    if (failures != 0) {
        std::cerr << failures << " module graph test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "module graph tests passed\n";
    return EXIT_SUCCESS;
}
