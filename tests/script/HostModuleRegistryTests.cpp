#include "script/runtime/HostModuleRegistry.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
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

runtime::HostExportDescriptor host_export(
    std::string name,
    std::string binding,
    std::string capability)
{
    return {std::move(name), std::move(binding), std::move(capability)};
}

runtime::HostModuleDescriptor module(
    std::string id,
    const std::uint32_t major,
    const std::uint32_t minor,
    std::vector<runtime::HostExportDescriptor> exports)
{
    return {std::move(id), {major, minor}, std::move(exports)};
}

template <typename Function>
runtime::HostRegistryError expect_error(
    const runtime::HostRegistryErrorCode expected,
    Function&& function,
    const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const runtime::HostRegistryError& error) {
        check(error.code() == expected, message);
        return error;
    }
    return {expected, "missing expected error"};
}

std::vector<runtime::HostModuleDescriptor> catalog_shape()
{
    return {
        module("baas/vision", 1, 0,
               {host_export("match", "host.vision.match.v1", "vision.analyze")}),
        module("baas/ocr", 1, 0,
               {host_export("recognize", "host.ocr.recognize.v1", "ocr.infer")}),
        module("baas/device", 1, 0,
               {host_export("capture", "host.device.capture.v1", "device.capture")}),
        module("baas/config", 1, 0,
               {host_export("get", "host.config.get.v1", "config.read")}),
        module("baas/config", 1, 2,
               {host_export("transact", "host.config.transact.v1", "config.write"),
                host_export("get", "host.config.get.v1", "config.read")}),
        module("baas/log", 1, 0,
               {host_export("emit", "host.log.emit.v1", "log.emit")}),
        module("baas/scheduler", 1, 0,
               {host_export("schedule", "host.scheduler.schedule.v1", "scheduler.schedule")}),
        module("baas/resource", 1, 0,
               {host_export("read", "host.resource.read.v1", "resource.read")}),
        module("baas/fs", 1, 0,
               {host_export("read", "host.fs.read.v1", "filesystem.read")}),
        module("baas/service", 1, 0,
               {host_export("request", "host.service.request.v1", "service.request")}),
        module("baas/process", 1, 0,
               {host_export("inspect", "host.process.inspect.v1", "process.inspect")}),
        module("baas/http", 1, 0,
               {host_export("request", "host.http.request.v1", "network.http")}),
        module("baas/socket", 1, 0,
               {host_export("open", "host.socket.open.v1", "network.socket")}),
    };
}

runtime::HostResolutionRequest successful_request()
{
    return {
        {{"baas/device", 1, 0}, {"baas/config", 1, 0}},
        {"device.capture", "config.write", "config.read"},
        {{"baas/device", {"capture"}}, {"baas/config", {"transact", "get"}}},
        {"config.read", "device.capture", "config.write"},
        {"config.write", "config.read", "device.capture"},
        {"device.capture", "config.read", "config.write"},
    };
}

void test_catalog_shape_and_deterministic_success()
{
    auto descriptors = catalog_shape();
    const runtime::HostModuleRegistry registry(descriptors);
    check(registry.module_version_count() == 13,
          "registry must retain independently versioned module descriptors");
    const std::vector<std::string> expected_modules{
        "baas/config", "baas/device", "baas/fs", "baas/http", "baas/log",
        "baas/ocr", "baas/process", "baas/resource", "baas/scheduler",
        "baas/service", "baas/socket", "baas/vision"};
    check(registry.canonical_module_ids() == expected_modules,
          "all twelve catalog modules must be expressible in bytewise order");

    const auto first = registry.resolve(successful_request());
    check(first.modules.size() == 2, "declared modules must resolve exactly once");
    check(first.modules[0].canonical_id == "baas/config"
              && first.modules[0].selected_version == runtime::HostApiVersion{1, 2},
          "same-major negotiation must choose the highest sufficient minor");
    check(first.modules[0].bindings.size() == 2
              && first.modules[0].bindings[0].export_name == "get"
              && first.modules[0].bindings[1].export_name == "transact",
          "resolved exports must be bytewise deterministic");
    check(first.effective_capabilities
              == std::vector<std::string>{
                  "config.read", "config.write", "device.capture"},
          "effective capabilities must be the sorted four-layer intersection");

    auto reversed_descriptors = descriptors;
    std::reverse(reversed_descriptors.begin(), reversed_descriptors.end());
    runtime::HostModuleRegistry reversed_registry(std::move(reversed_descriptors));
    auto reversed_request = successful_request();
    std::reverse(reversed_request.declared_modules.begin(), reversed_request.declared_modules.end());
    std::reverse(reversed_request.declared_capabilities.begin(), reversed_request.declared_capabilities.end());
    std::reverse(reversed_request.imports.begin(), reversed_request.imports.end());
    std::reverse(reversed_request.policy_capabilities.begin(), reversed_request.policy_capabilities.end());
    check(reversed_registry.resolve(reversed_request) == first,
          "descriptor and request ordering must not change the stable resolution");
}

void test_duplicate_and_minor_contract_validation()
{
    using enum runtime::HostRegistryErrorCode;
    const auto base = module(
        "baas/config", 1, 0,
        {host_export("get", "host.config.get.v1", "config.read")});
    expect_error(DuplicateModuleVersion,
                 [&] { runtime::HostModuleRegistry registry({base, base}); },
                 "duplicate module/version registration must fail");
    expect_error(DuplicateExport,
                 [] {
                     runtime::HostModuleRegistry registry({module(
                         "baas/config", 1, 0,
                         {host_export("get", "host.config.get.v1", "config.read"),
                          host_export("get", "host.config.other.v1", "config.read")})});
                 },
                 "duplicate export names must fail");
    expect_error(DuplicateBinding,
                 [] {
                     runtime::HostModuleRegistry registry({
                         module("baas/config", 1, 0,
                                {host_export("get", "host.config.get.v1", "config.read")}),
                         module("baas/device", 1, 0,
                                {host_export("capture", "host.config.get.v1", "device.capture")}),
                     });
                 },
                 "a binding id cannot acquire conflicting ownership");
    expect_error(IncompatibleMinorContract,
                 [] {
                     runtime::HostModuleRegistry registry({
                         module("baas/config", 1, 0,
                                {host_export("get", "host.config.get.v1", "config.read")}),
                         module("baas/config", 1, 1,
                                {host_export("transact", "host.config.transact.v1", "config.write")}),
                     });
                 },
                 "higher minor versions cannot remove an earlier export");
}

void test_version_declaration_and_capability_failures()
{
    using enum runtime::HostRegistryErrorCode;
    const runtime::HostModuleRegistry registry(catalog_shape());

    auto request = successful_request();
    request.declared_modules[0].major = 2;
    expect_error(VersionIncompatible,
                 [&] { static_cast<void>(registry.resolve(request)); },
                 "major mismatch must fail without fallback");
    request = successful_request();
    request.declared_modules[1].min_minor = 3;
    expect_error(VersionIncompatible,
                 [&] { static_cast<void>(registry.resolve(request)); },
                 "insufficient registered minor must fail");

    request = successful_request();
    request.declared_modules.erase(request.declared_modules.begin());
    const auto undeclared_module = expect_error(
        UndeclaredModule,
        [&] { static_cast<void>(registry.resolve(request)); },
        "an imported module absent from host_modules must fail");
    check(undeclared_module.module() == "baas/device",
          "undeclared module error must carry stable module identity");

    request = successful_request();
    request.declared_capabilities.erase(
        std::find(request.declared_capabilities.begin(),
                  request.declared_capabilities.end(),
                  "config.write"));
    const auto undeclared_capability = expect_error(
        UndeclaredCapability,
        [&] { static_cast<void>(registry.resolve(request)); },
        "an imported export capability absent from the manifest must fail");
    check(undeclared_capability.capability() == "config.write"
              && undeclared_capability.layer() == "manifest",
          "undeclared capability error must identify capability and manifest layer");

    request = successful_request();
    request.policy_capabilities.erase(
        std::find(request.policy_capabilities.begin(),
                  request.policy_capabilities.end(),
                  "device.capture"));
    const auto denied = expect_error(
        CapabilityDenied,
        [&] { static_cast<void>(registry.resolve(request)); },
        "policy narrowing must deny an otherwise declared capability");
    check(denied.capability() == "device.capture" && denied.layer() == "policy",
          "capability denial must report the first fixed narrowing layer");
}

void test_limits_and_utf8_fail_closed()
{
    using enum runtime::HostRegistryErrorCode;
    runtime::HostRegistryLimits limits;
    limits.max_validation_work = 1;
    expect_error(ValidationWorkLimitExceeded,
                 [&] { runtime::HostModuleRegistry registry(catalog_shape(), limits); },
                 "registry construction must share an explicit work budget");

    const runtime::HostModuleRegistry resolution_budget_registry({}, limits);
    runtime::HostResolutionRequest budget_request;
    budget_request.declared_capabilities = {"config.read"};
    expect_error(ValidationWorkLimitExceeded,
                 [&] {
                     static_cast<void>(resolution_budget_registry.resolve(budget_request));
                 },
                 "resolution validation must obey its own explicit work budget");

    limits = {};
    limits.max_string_bytes = 4;
    expect_error(StringBudgetExceeded,
                 [&] { runtime::HostModuleRegistry registry(catalog_shape(), limits); },
                 "descriptor strings must be bounded before internal copies");

    const std::string invalid_module{"baas/\xC0\xAF", 7};
    expect_error(InvalidUtf8,
                 [&] {
                     runtime::HostModuleRegistry registry({module(
                         invalid_module, 1, 0,
                         {host_export("x", "host.x.x.v1", "x.y")})});
                 },
                 "invalid UTF-8 module identity must fail closed");

    auto invalid_request = successful_request();
    invalid_request.imports[0].canonical_id = invalid_module;
    const runtime::HostModuleRegistry utf8_registry(catalog_shape());
    expect_error(InvalidUtf8,
                 [&] { static_cast<void>(utf8_registry.resolve(invalid_request)); },
                 "invalid UTF-8 request metadata must fail before lookup");

    auto request = successful_request();
    limits = {};
    limits.max_capabilities = 1;
    const runtime::HostModuleRegistry narrow_registry(catalog_shape(), limits);
    expect_error(CapabilityLimitExceeded,
                 [&] { static_cast<void>(narrow_registry.resolve(request)); },
                 "manifest and narrowing capabilities must obey explicit limits");
    check(runtime::host_registry_error_code_name(CapabilityDenied)
              == "HREG025_CAPABILITY_DENIED",
          "registry failures must expose stable foundation names");
}

void test_concurrent_read_only_resolution()
{
    const runtime::HostModuleRegistry registry(catalog_shape());
    const auto request = successful_request();
    const auto expected = registry.resolve(request);
    std::atomic<bool> ok{true};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 8; ++thread) {
        threads.emplace_back([&] {
            for (int iteration = 0; iteration < 250; ++iteration) {
                try {
                    if (registry.resolve(request) != expected) ok.store(false);
                } catch (...) {
                    ok.store(false);
                }
            }
        });
    }
    for (auto& thread : threads) thread.join();
    check(ok.load(), "safely published registry must support concurrent const resolution");
}

}  // namespace

int main()
{
    static_assert(!std::is_pointer_v<runtime::HostExportDescriptor>);
    test_catalog_shape_and_deterministic_success();
    test_duplicate_and_minor_contract_validation();
    test_version_declaration_and_capability_failures();
    test_limits_and_utf8_fail_closed();
    test_concurrent_read_only_resolution();
    if (failures != 0) {
        std::cerr << failures << " host registry test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "host module registry tests passed\n";
    return EXIT_SUCCESS;
}
