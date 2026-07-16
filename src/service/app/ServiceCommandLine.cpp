#include "service/app/ServiceCommandLine.h"

#include <array>
#include <charconv>
#include <limits>
#include <new>
#include <system_error>
#include <utility>

namespace baas::service::app {
namespace {

constexpr std::string_view project_root_option = "--project-root";
constexpr std::string_view host_option = "--host";
constexpr std::string_view port_option = "--port";
constexpr std::string_view pipe_name_option = "--pipe-name";
constexpr std::string_view runtime_repository_generation_option =
    "--runtime-repository-generation";
constexpr std::string_view help_option = "--help";
constexpr std::string_view version_option = "--version";
constexpr std::string_view required_host = "127.0.0.1";
constexpr std::string_view windows_pipe_prefix = R"(\\.\pipe\)";

struct ParsedOption {
    std::string_view name;
    std::optional<std::string_view> inline_value;
};

[[nodiscard]] ServiceCommandLineResult fail(
    const ServiceCommandLineError error,
    const std::size_t argument = ServiceCommandLineResult::no_argument) noexcept
{
    ServiceCommandLineResult result;
    result.disposition = ServiceCommandLineDisposition::error;
    result.error = error;
    result.error_argument = argument;
    return result;
}

[[nodiscard]] ServiceCommandLineResult disposition(
    const ServiceCommandLineDisposition value) noexcept
{
    ServiceCommandLineResult result;
    result.disposition = value;
    result.error = ServiceCommandLineError::none;
    result.error_argument = ServiceCommandLineResult::no_argument;
    return result;
}

[[nodiscard]] bool contains_nul(const std::string_view value) noexcept
{
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] ParsedOption split_option(const std::string_view argument) noexcept
{
    const auto equals = argument.find('=');
    if (equals == std::string_view::npos) {
        return ParsedOption{argument, std::nullopt};
    }
    return ParsedOption{argument.substr(0, equals), argument.substr(equals + 1)};
}

[[nodiscard]] bool is_known_option(const std::string_view name) noexcept
{
    return name == project_root_option || name == host_option || name == port_option
        || name == pipe_name_option || name == runtime_repository_generation_option
        || name == help_option || name == version_option;
}

[[nodiscard]] bool valid_runtime_repository_generation(
    const std::string_view value) noexcept
{
    if (value.size() != 64) return false;
    for (const char byte : value) {
        if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_option_token(const std::string_view value) noexcept
{
    return value.starts_with("--");
}

[[nodiscard]] ServiceCommandLineResult validate_input_bounds(
    const std::span<const std::string_view> arguments) noexcept
{
    if (arguments.size() > service_command_line_max_argument_count) {
        return fail(ServiceCommandLineError::too_many_arguments);
    }
    std::size_t aggregate = 0;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument.size() > service_command_line_max_argument_bytes) {
            return fail(ServiceCommandLineError::argument_too_long, index);
        }
        if (contains_nul(argument)) {
            return fail(ServiceCommandLineError::embedded_nul, index);
        }
        if (argument.size() > service_command_line_max_aggregate_bytes - aggregate) {
            return fail(ServiceCommandLineError::aggregate_too_long, index);
        }
        aggregate += argument.size();
        if (index != 0) {
            if (aggregate == service_command_line_max_aggregate_bytes) {
                return fail(ServiceCommandLineError::aggregate_too_long, index);
            }
            ++aggregate;
        }
    }
    return disposition(ServiceCommandLineDisposition::run);
}

[[nodiscard]] std::optional<ServiceCommandLineResult> informational_disposition(
    const std::span<const std::string_view> arguments) noexcept
{
    bool has_information_option = false;
    std::optional<std::string_view> information_option;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        const auto option = split_option(argument);
        if (option.name == help_option || option.name == version_option) {
            if (option.inline_value.has_value()) {
                return fail(ServiceCommandLineError::option_value_not_allowed, index);
            }
            if (information_option == option.name) {
                return fail(ServiceCommandLineError::duplicate_option, index);
            }
            if (information_option.has_value()) {
                return fail(ServiceCommandLineError::informational_option_mixed, index);
            }
            information_option = option.name;
            has_information_option = true;
        }
    }
    if (!has_information_option) {
        return std::nullopt;
    }
    if (arguments.size() != 1) {
        return fail(ServiceCommandLineError::informational_option_mixed);
    }
    if (arguments.front() == help_option) {
        return disposition(ServiceCommandLineDisposition::help);
    }
    return disposition(ServiceCommandLineDisposition::version);
}

[[nodiscard]] std::optional<std::string_view> take_value(
    const ParsedOption option,
    const std::span<const std::string_view> arguments,
    std::size_t& index,
    ServiceCommandLineResult& error) noexcept
{
    if (option.inline_value.has_value()) {
        if (option.inline_value->empty()) {
            error = fail(ServiceCommandLineError::empty_value, index);
            return std::nullopt;
        }
        return option.inline_value;
    }
    if (index + 1 >= arguments.size() || is_option_token(arguments[index + 1])) {
        error = fail(ServiceCommandLineError::missing_value, index);
        return std::nullopt;
    }
    ++index;
    if (arguments[index].empty()) {
        error = fail(ServiceCommandLineError::empty_value, index);
        return std::nullopt;
    }
    return arguments[index];
}

[[nodiscard]] bool valid_windows_pipe(const std::string_view value) noexcept
{
    return value.size() <= service_command_line_max_windows_pipe_bytes
        && value.starts_with(windows_pipe_prefix) && value.size() > windows_pipe_prefix.size();
}

[[nodiscard]] bool valid_unix_pipe(const std::string_view value) noexcept
{
    return value.size() <= service_command_line_max_unix_pipe_bytes && value.starts_with('/')
        && value.size() > 1;
}

[[nodiscard]] ServiceCommandLineResult parse_impl(
    const std::span<const std::string_view> arguments,
    const ServiceCommandLinePlatform platform)
{
    if (const auto bounds = validate_input_bounds(arguments); !bounds) {
        return bounds;
    }
    if (const auto information = informational_disposition(arguments); information.has_value()) {
        return *information;
    }

    std::optional<std::string_view> project_root;
    std::optional<std::string_view> host;
    std::optional<std::string_view> port;
    std::optional<std::string_view> pipe_name;
    std::optional<std::string_view> runtime_repository_generation;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (!is_option_token(argument)) {
            return fail(ServiceCommandLineError::positional_argument, index);
        }
        const auto option = split_option(argument);
        if (!is_known_option(option.name)) {
            return fail(ServiceCommandLineError::unknown_option, index);
        }
        if (option.name == help_option || option.name == version_option) {
            return fail(ServiceCommandLineError::informational_option_mixed, index);
        }

        auto* destination = &project_root;
        if (option.name == host_option) destination = &host;
        if (option.name == port_option) destination = &port;
        if (option.name == pipe_name_option) destination = &pipe_name;
        if (option.name == runtime_repository_generation_option) {
            destination = &runtime_repository_generation;
        }
        if (destination->has_value()) {
            return fail(ServiceCommandLineError::duplicate_option, index);
        }
        ServiceCommandLineResult value_error;
        const auto value = take_value(option, arguments, index, value_error);
        if (!value.has_value()) {
            return value_error;
        }
        *destination = *value;
    }

    if (!project_root.has_value()) return fail(ServiceCommandLineError::missing_project_root);
    if (!host.has_value()) return fail(ServiceCommandLineError::missing_host);
    if (!port.has_value()) return fail(ServiceCommandLineError::missing_port);
    if (!runtime_repository_generation.has_value()) {
        return fail(ServiceCommandLineError::missing_runtime_repository_generation);
    }
    if (project_root->size() > service_command_line_max_project_root_bytes) {
        return fail(ServiceCommandLineError::argument_too_long);
    }
    if (*host != required_host) {
        return fail(ServiceCommandLineError::invalid_host);
    }
    if (!valid_runtime_repository_generation(*runtime_repository_generation)) {
        return fail(ServiceCommandLineError::invalid_runtime_repository_generation);
    }

    unsigned int parsed_port = 0;
    const auto [port_end, port_error] =
        std::from_chars(port->data(), port->data() + port->size(), parsed_port);
    if (port_error != std::errc{} || port_end != port->data() + port->size()
        || parsed_port == 0 || parsed_port > std::numeric_limits<std::uint16_t>::max()) {
        return fail(ServiceCommandLineError::invalid_port);
    }

    if (pipe_name.has_value()) {
        if (platform == ServiceCommandLinePlatform::android) {
            return fail(ServiceCommandLineError::pipe_not_supported);
        }
        const bool valid = platform == ServiceCommandLinePlatform::windows
            ? valid_windows_pipe(*pipe_name)
            : valid_unix_pipe(*pipe_name);
        if (!valid) return fail(ServiceCommandLineError::invalid_pipe_name);
    }

    ServiceCommandLineResult result;
#if defined(_WIN32)
    // wmain adapts UTF-16 arguments to UTF-8 before this bounded parser. Use
    // the explicit UTF-8 filesystem conversion instead of the process ANSI
    // code page so non-ASCII Tauri project roots survive unchanged.
    std::u8string utf8_project_root;
    utf8_project_root.reserve(project_root->size());
    for (const unsigned char byte : *project_root) {
        utf8_project_root.push_back(static_cast<char8_t>(byte));
    }
    result.options.project_root = std::filesystem::path{utf8_project_root};
#else
    result.options.project_root = std::filesystem::path{std::string{*project_root}};
#endif
    std::error_code filesystem_error;
    const bool is_directory = std::filesystem::is_directory(
        result.options.project_root, filesystem_error);
    if (filesystem_error
        && filesystem_error != std::errc::no_such_file_or_directory
        && filesystem_error != std::errc::not_a_directory) {
        return fail(ServiceCommandLineError::filesystem_error);
    }
    if (filesystem_error || !is_directory) {
        return fail(ServiceCommandLineError::project_root_not_directory);
    }

    result.options.host = std::string{*host};
    result.options.port = static_cast<std::uint16_t>(parsed_port);
    if (pipe_name.has_value()) result.options.pipe_name = std::string{*pipe_name};
    result.options.runtime_repository_generation =
        std::string{*runtime_repository_generation};
    result.disposition = ServiceCommandLineDisposition::run;
    result.error = ServiceCommandLineError::none;
    result.error_argument = ServiceCommandLineResult::no_argument;
    return result;
}

[[nodiscard]] std::size_t bounded_c_string_length(const char* value) noexcept
{
    std::size_t length = 0;
    while (length <= service_command_line_max_argument_bytes && value[length] != '\0') {
        ++length;
    }
    return length;
}

}  // namespace

std::string_view service_command_line_error_name(const ServiceCommandLineError error) noexcept
{
    using enum ServiceCommandLineError;
    switch (error) {
    case none: return "none";
    case invalid_argument_vector: return "invalid_argument_vector";
    case too_many_arguments: return "too_many_arguments";
    case argument_too_long: return "argument_too_long";
    case aggregate_too_long: return "aggregate_too_long";
    case embedded_nul: return "embedded_nul";
    case unknown_option: return "unknown_option";
    case positional_argument: return "positional_argument";
    case duplicate_option: return "duplicate_option";
    case missing_value: return "missing_value";
    case empty_value: return "empty_value";
    case option_value_not_allowed: return "option_value_not_allowed";
    case informational_option_mixed: return "informational_option_mixed";
    case missing_project_root: return "missing_project_root";
    case missing_host: return "missing_host";
    case missing_port: return "missing_port";
    case missing_runtime_repository_generation:
        return "missing_runtime_repository_generation";
    case project_root_not_directory: return "project_root_not_directory";
    case filesystem_error: return "filesystem_error";
    case invalid_host: return "invalid_host";
    case invalid_port: return "invalid_port";
    case invalid_runtime_repository_generation:
        return "invalid_runtime_repository_generation";
    case pipe_not_supported: return "pipe_not_supported";
    case invalid_pipe_name: return "invalid_pipe_name";
    case resource_exhausted: return "resource_exhausted";
    case internal_error: return "internal_error";
    }
    return "unknown";
}

ServiceCommandLineResult parse_service_command_line(
    const std::span<const std::string_view> arguments,
    const ServiceCommandLinePlatform platform) noexcept
{
    try {
        return parse_impl(arguments, platform);
    } catch (const std::filesystem::filesystem_error&) {
        return fail(ServiceCommandLineError::filesystem_error);
    } catch (const std::bad_alloc&) {
        return fail(ServiceCommandLineError::resource_exhausted);
    } catch (...) {
        return fail(ServiceCommandLineError::internal_error);
    }
}

ServiceCommandLineResult parse_service_command_line(
    const int argc,
    const char* const argv[],
    const ServiceCommandLinePlatform platform) noexcept
{
    if (argc < 1 || argv == nullptr || argv[0] == nullptr) {
        return fail(ServiceCommandLineError::invalid_argument_vector);
    }
    const auto argument_count = static_cast<std::size_t>(argc - 1);
    if (argument_count > service_command_line_max_argument_count) {
        return fail(ServiceCommandLineError::too_many_arguments);
    }
    std::array<std::string_view, service_command_line_max_argument_count> arguments{};
    for (std::size_t index = 0; index < argument_count; ++index) {
        const char* value = argv[index + 1];
        if (value == nullptr) {
            return fail(ServiceCommandLineError::invalid_argument_vector, index);
        }
        const auto length = bounded_c_string_length(value);
        if (length > service_command_line_max_argument_bytes) {
            return fail(ServiceCommandLineError::argument_too_long, index);
        }
        arguments[index] = std::string_view{value, length};
    }
    return parse_service_command_line(
        std::span<const std::string_view>{arguments.data(), argument_count}, platform);
}

}  // namespace baas::service::app
