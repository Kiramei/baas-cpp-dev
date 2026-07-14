#include "script/runtime/ModuleSpecifier.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

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
void check_error(
    const runtime::ModuleSpecifierErrorCode expected,
    Function&& function,
    const std::string_view message)
{
    try {
        function();
        check(false, message);
    } catch (const runtime::ModuleSpecifierError& error) {
        check(error.code() == expected, message);
    }
}

bool accept_nfc(std::string_view) noexcept { return true; }
bool reject_nfc(std::string_view) noexcept { return false; }

void test_valid_package_and_host_ids()
{
    const auto package = runtime::validate_module_specifier("tasks/common");
    check(package.kind == runtime::ModuleKind::Package,
          "ordinary logical IDs must resolve as package modules");
    check(package.canonical_id == "tasks/common",
          "canonical package ID must preserve exact bytes and case");
    check(package.manifest_source_path() == "tasks/common.baas",
          "package source path must append the one source extension");

    const auto host = runtime::validate_module_specifier("baas/log");
    check(host.kind == runtime::ModuleKind::Host,
          "baas/ prefix must resolve as a host module");
    bool rejected_manifest_path = false;
    try {
        static_cast<void>(host.manifest_source_path());
    } catch (const std::logic_error&) {
        rejected_manifest_path = true;
    }
    check(rejected_manifest_path, "host module must not manufacture a package path");

    const auto different_case = runtime::validate_module_specifier("Tasks/Common");
    check(different_case.canonical_id != package.canonical_id,
          "logical matching must remain case-sensitive on every platform");
}

void test_unicode_is_validated_and_fails_closed()
{
    const std::string unicode = "tasks/\xE5\x85\xB1\xE9\x80\x9A";
    check_error(runtime::ModuleSpecifierErrorCode::NfcCheckUnavailable,
                [&] { static_cast<void>(runtime::validate_module_specifier(unicode)); },
                "non-ASCII ID without an injected NFC validator must fail closed");
    check_error(runtime::ModuleSpecifierErrorCode::NotNfc,
                [&] { static_cast<void>(runtime::validate_module_specifier(unicode, reject_nfc)); },
                "NFC validator rejection must be preserved");
    const auto accepted = runtime::validate_module_specifier(unicode, accept_nfc);
    check(accepted.canonical_id == unicode,
          "accepted NFC identifier must retain its exact UTF-8 bytes");

    const std::string invalid_utf8{"tasks/\xC0\xAF", 8};
    check_error(runtime::ModuleSpecifierErrorCode::InvalidUtf8,
                [&] { static_cast<void>(runtime::validate_module_specifier(invalid_utf8)); },
                "overlong UTF-8 must be rejected before normalization");
}

void test_path_escape_and_ambiguity_are_rejected()
{
    using enum runtime::ModuleSpecifierErrorCode;
    check_error(Empty, [] { static_cast<void>(runtime::validate_module_specifier("")); },
                "empty IDs must be rejected");
    check_error(LeadingSlash,
                [] { static_cast<void>(runtime::validate_module_specifier("/tasks/common")); },
                "leading slash must be rejected");
    check_error(DrivePrefix,
                [] { static_cast<void>(runtime::validate_module_specifier("C:/tasks/common")); },
                "drive prefix must be rejected");
    check_error(Backslash,
                [] { static_cast<void>(runtime::validate_module_specifier("tasks\\common")); },
                "platform separator alias must be rejected");
    check_error(EmptySegment,
                [] { static_cast<void>(runtime::validate_module_specifier("tasks//common")); },
                "empty middle segment must be rejected");
    check_error(EmptySegment,
                [] { static_cast<void>(runtime::validate_module_specifier("tasks/common/")); },
                "empty trailing segment must be rejected");
    check_error(DotSegment,
                [] { static_cast<void>(runtime::validate_module_specifier("tasks/./common")); },
                "dot segment must be rejected");
    check_error(DotSegment,
                [] { static_cast<void>(runtime::validate_module_specifier("tasks/../common")); },
                "parent segment must be rejected");
    check_error(SourceExtension,
                [] { static_cast<void>(runtime::validate_module_specifier("tasks/common.baas")); },
                "source extension must not be accepted in logical IDs");

    const std::string embedded_nul{"tasks/\0common", 13};
    check_error(EmbeddedNul,
                [&] { static_cast<void>(runtime::validate_module_specifier(embedded_nul)); },
                "embedded NUL must be rejected");
}

void test_explicit_limits_and_stable_codes()
{
    check_error(runtime::ModuleSpecifierErrorCode::ByteLimitExceeded,
                [] {
                    static_cast<void>(runtime::validate_module_specifier(
                        "tasks/common", nullptr, runtime::ModuleSpecifierLimits{5, 8}));
                },
                "byte budget must be checked before allocation");
    check_error(runtime::ModuleSpecifierErrorCode::SegmentLimitExceeded,
                [] {
                    static_cast<void>(runtime::validate_module_specifier(
                        "a/b/c", nullptr, runtime::ModuleSpecifierLimits{16, 2}));
                },
                "segment budget must be enforced deterministically");
    check(runtime::module_specifier_error_code_name(
              runtime::ModuleSpecifierErrorCode::SourceExtension)
              == "MS013_SOURCE_EXTENSION",
          "module validation failures must expose stable foundation names");

    bool rejected_limits = false;
    try {
        static_cast<void>(runtime::validate_module_specifier(
            "tasks/common", nullptr, runtime::ModuleSpecifierLimits{0, 1}));
    } catch (const std::invalid_argument&) {
        rejected_limits = true;
    }
    check(rejected_limits, "zero implementation limits must be rejected as configuration errors");
}

}  // namespace

int main()
{
    test_valid_package_and_host_ids();
    test_unicode_is_validated_and_fails_closed();
    test_path_escape_and_ambiguity_are_rejected();
    test_explicit_limits_and_stable_codes();
    if (failures != 0) {
        std::cerr << failures << " module specifier test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "module specifier tests passed\n";
    return EXIT_SUCCESS;
}
