#include "service/app/ServiceCommandLine.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

ServiceCommandLineResult parse(
    const std::vector<std::string>& arguments,
    const ServiceCommandLinePlatform platform = native_service_command_line_platform())
{
    std::vector<std::string_view> views;
    views.reserve(arguments.size());
    for (const auto& argument : arguments) views.emplace_back(argument);
    return parse_service_command_line(views, platform);
}

std::string project_root()
{
    return std::filesystem::current_path().string();
}

std::vector<std::string> required_arguments()
{
    return {
        "--project-root", project_root(), "--host", "127.0.0.1", "--port", "8190"};
}

void test_valid_run_forms()
{
    const auto separated = parse(required_arguments());
    check(separated.disposition == ServiceCommandLineDisposition::run,
          "separated run arguments must parse");
    check(separated.error == ServiceCommandLineError::none,
          "successful run must expose no error");
    check(separated.options.host == "127.0.0.1" && separated.options.port == 8190,
          "run options must preserve host and port");
    check(std::filesystem::equivalent(
              separated.options.project_root, std::filesystem::current_path()),
          "run options must preserve the directory project root");

    const std::vector<std::string> equals{
        "--port=65535", "--host=127.0.0.1", "--project-root=" + project_root()};
    const auto inline_values = parse(equals);
    check(inline_values && inline_values.options.port == 65'535,
          "equals syntax and option reordering must parse");
}

void test_pipe_platform_policy()
{
    auto windows = required_arguments();
    windows.emplace_back("--pipe-name");
    windows.emplace_back(R"(\\.\pipe\baas-contract)");
    const auto windows_result = parse(windows, ServiceCommandLinePlatform::windows);
    check(windows_result && windows_result.options.pipe_name == R"(\\.\pipe\baas-contract)",
          "Windows named pipe must use the canonical prefix");
    windows.back() = R"(\pipe\baas-contract)";
    check(parse(windows, ServiceCommandLinePlatform::windows).error
              == ServiceCommandLineError::invalid_pipe_name,
          "Windows pipe without canonical prefix must fail");
    windows.back() = R"(\\.\pipe\)";
    check(parse(windows, ServiceCommandLinePlatform::windows).error
              == ServiceCommandLineError::invalid_pipe_name,
          "Windows pipe prefix without a name must fail");
    windows.back() = std::string(service_command_line_max_windows_pipe_bytes + 1, 'p');
    windows.back().replace(0, 9, R"(\\.\pipe\)");
    check(parse(windows, ServiceCommandLinePlatform::windows).error
              == ServiceCommandLineError::invalid_pipe_name,
          "Windows pipe endpoint length must be bounded");

    auto unix = required_arguments();
    unix.emplace_back("--pipe-name=/tmp/baas-contract.sock");
    check(static_cast<bool>(parse(unix, ServiceCommandLinePlatform::unix_like)),
          "Unix absolute socket endpoint must parse");
    unix.back() = "--pipe-name=tmp/baas-contract.sock";
    check(parse(unix, ServiceCommandLinePlatform::unix_like).error
              == ServiceCommandLineError::invalid_pipe_name,
          "Unix relative socket endpoint must fail");
    unix.back() = "--pipe-name="
        + std::string(service_command_line_max_unix_pipe_bytes + 1, '/');
    check(parse(unix, ServiceCommandLinePlatform::unix_like).error
              == ServiceCommandLineError::invalid_pipe_name,
          "Unix socket endpoint length must be bounded");
    unix.back() = "--pipe-name=/tmp/baas-contract.sock";
    check(parse(unix, ServiceCommandLinePlatform::android).error
              == ServiceCommandLineError::pipe_not_supported,
          "Android must explicitly reject local Pipe mode");
}

void test_help_and_version_are_isolated()
{
    check(parse({"--help"}).disposition == ServiceCommandLineDisposition::help,
          "help must be an independent disposition");
    check(parse({"--version"}).disposition == ServiceCommandLineDisposition::version,
          "version must be an independent disposition");
    check(parse({"--help", "--version"}).error
              == ServiceCommandLineError::informational_option_mixed,
          "help and version must not mix");
    check(parse({"--help", "--help"}).error == ServiceCommandLineError::duplicate_option,
          "duplicate informational option must fail as a duplicate");
    check(parse({"--help", "--port", "8190"}).error
              == ServiceCommandLineError::informational_option_mixed,
          "help must not mix with run arguments");
    check(parse({"--help=true"}).error
              == ServiceCommandLineError::option_value_not_allowed,
          "help must reject values");
}

void test_strict_option_structure()
{
    auto duplicate = required_arguments();
    duplicate.emplace_back("--port=9000");
    check(parse(duplicate).error == ServiceCommandLineError::duplicate_option,
          "duplicate run option must fail");
    check(parse({"--project-root", project_root(), "--host", "127.0.0.1", "--wat"})
              .error
              == ServiceCommandLineError::unknown_option,
          "unknown option must fail");
    check(parse({"positional"}).error == ServiceCommandLineError::positional_argument,
          "positional argument must fail");
    check(parse({"--project-root", "--host", "127.0.0.1", "--port", "8190"}).error
              == ServiceCommandLineError::missing_value,
          "separated option followed by another option must fail missing value");
    check(parse({"--project-root=", "--host=127.0.0.1", "--port=8190"}).error
              == ServiceCommandLineError::empty_value,
          "inline empty value must fail");
    check(parse({"--project-root", "", "--host", "127.0.0.1", "--port", "8190"})
              .error
              == ServiceCommandLineError::empty_value,
          "separated empty value must fail");
}

void test_required_values_and_filesystem_gate()
{
    check(parse({}).error == ServiceCommandLineError::missing_project_root,
          "project root must be required");
    check(parse({"--project-root", project_root()}).error
              == ServiceCommandLineError::missing_host,
          "host must be required");
    check(parse({"--project-root", project_root(), "--host", "127.0.0.1"}).error
              == ServiceCommandLineError::missing_port,
          "port must be required");
    check(parse({"--project-root", project_root(), "--host", "0.0.0.0", "--port", "8190"})
              .error
              == ServiceCommandLineError::invalid_host,
          "non-loopback host must fail");

    for (const std::string port : {"0", "65536", "-1", "+1", "1x", " 1"}) {
        check(parse({"--project-root", project_root(), "--host", "127.0.0.1",
                     "--port", port})
                  .error
                  == ServiceCommandLineError::invalid_port,
              "invalid port spelling or range must fail");
    }
    const auto missing = std::filesystem::current_path() / "definitely-missing-service-root";
    check(parse({"--project-root", missing.string(), "--host", "127.0.0.1", "--port",
                 "8190"})
              .error
              == ServiceCommandLineError::project_root_not_directory,
          "missing or non-directory project root must fail closed");
    const auto file = std::filesystem::current_path() / "CMakeCache.txt";
    check(std::filesystem::is_regular_file(file),
          "regular-file project-root fixture must exist");
    check(parse({"--project-root", file.string(), "--host", "127.0.0.1", "--port", "8190"})
              .error
              == ServiceCommandLineError::project_root_not_directory,
          "regular-file project root must fail closed");
}

void test_input_budgets_and_argument_vector()
{
    std::vector<std::string> too_many(
        service_command_line_max_argument_count + 1, "--unknown");
    check(parse(too_many).error == ServiceCommandLineError::too_many_arguments,
          "argument count must be bounded before parsing");
    check(parse({std::string(service_command_line_max_argument_bytes + 1, 'x')}).error
              == ServiceCommandLineError::argument_too_long,
          "individual argument bytes must be bounded");
    std::vector<std::string> aggregate(5, std::string(900, 'x'));
    check(parse(aggregate).error == ServiceCommandLineError::aggregate_too_long,
          "aggregate argument bytes must be bounded before parsing");
    check(parse({std::string{"--host\0hidden", 13}}).error
              == ServiceCommandLineError::embedded_nul,
          "span boundary must reject embedded NUL");

    check(parse_service_command_line(0, nullptr).error
              == ServiceCommandLineError::invalid_argument_vector,
          "argc/argv boundary must reject an invalid vector");
    const char* null_entry[] = {"BAAS_service", nullptr};
    check(parse_service_command_line(2, null_entry).error
              == ServiceCommandLineError::invalid_argument_vector,
          "argc/argv boundary must reject null argument entries");
    const auto root = project_root();
    const char* valid[] = {"BAAS_service", "--project-root", root.c_str(), "--host",
                           "127.0.0.1", "--port=1"};
    const auto valid_result = parse_service_command_line(
        static_cast<int>(std::size(valid)), valid);
    check(valid_result && valid_result.options.port == 1,
          "argc/argv adapter must ignore argv[0] and preserve a valid run");
}

void test_stable_error_names()
{
    using enum ServiceCommandLineError;
    constexpr std::array errors{
        none, invalid_argument_vector, too_many_arguments, argument_too_long,
        aggregate_too_long, embedded_nul, unknown_option, positional_argument,
        duplicate_option, missing_value, empty_value, option_value_not_allowed,
        informational_option_mixed, missing_project_root, missing_host, missing_port,
        project_root_not_directory, filesystem_error, invalid_host, invalid_port,
        pipe_not_supported, invalid_pipe_name, resource_exhausted, internal_error};
    for (const auto error : errors) {
        check(service_command_line_error_name(error) != "unknown",
              "every public command-line error must have a stable name");
    }
}

}  // namespace

int main()
{
    test_valid_run_forms();
    test_pipe_platform_policy();
    test_help_and_version_are_isolated();
    test_strict_option_structure();
    test_required_values_and_filesystem_gate();
    test_input_budgets_and_argument_vector();
    test_stable_error_names();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Service command-line tests passed\n";
    return EXIT_SUCCESS;
}
