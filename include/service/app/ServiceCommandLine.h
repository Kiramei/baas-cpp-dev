#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace baas::service::app {

inline constexpr std::size_t service_command_line_max_argument_count = 16;
inline constexpr std::size_t service_command_line_max_argument_bytes = 1'024;
inline constexpr std::size_t service_command_line_max_aggregate_bytes = 4'096;
inline constexpr std::size_t service_command_line_max_project_root_bytes = 1'024;
inline constexpr std::size_t service_command_line_max_windows_pipe_bytes = 512;
// 103 payload bytes fit the smallest common sockaddr_un::sun_path including NUL.
inline constexpr std::size_t service_command_line_max_unix_pipe_bytes = 103;

enum class ServiceCommandLinePlatform : std::uint8_t {
    windows,
    unix_like,
    android,
};

[[nodiscard]] constexpr ServiceCommandLinePlatform
native_service_command_line_platform() noexcept
{
#if defined(__ANDROID__)
    return ServiceCommandLinePlatform::android;
#elif defined(_WIN32)
    return ServiceCommandLinePlatform::windows;
#else
    return ServiceCommandLinePlatform::unix_like;
#endif
}

enum class ServiceCommandLineDisposition : std::uint8_t {
    run,
    help,
    version,
    error,
};

enum class ServiceCommandLineError : std::uint8_t {
    none,
    invalid_argument_vector,
    too_many_arguments,
    argument_too_long,
    aggregate_too_long,
    embedded_nul,
    unknown_option,
    positional_argument,
    duplicate_option,
    missing_value,
    empty_value,
    option_value_not_allowed,
    informational_option_mixed,
    missing_project_root,
    missing_host,
    missing_port,
    project_root_not_directory,
    filesystem_error,
    invalid_host,
    invalid_port,
    pipe_not_supported,
    invalid_pipe_name,
    resource_exhausted,
    internal_error,
};

[[nodiscard]] std::string_view service_command_line_error_name(
    ServiceCommandLineError error) noexcept;

struct ServiceRunOptions {
    std::filesystem::path project_root;
    std::string host;
    std::uint16_t port = 0;
    std::optional<std::string> pipe_name;
};

struct ServiceCommandLineResult {
    static constexpr std::size_t no_argument = static_cast<std::size_t>(-1);

    ServiceCommandLineDisposition disposition = ServiceCommandLineDisposition::error;
    ServiceCommandLineError error = ServiceCommandLineError::internal_error;
    // Zero-based index into the argument span, excluding argv[0].
    std::size_t error_argument = no_argument;
    // Valid only when disposition == run.
    ServiceRunOptions options;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return disposition != ServiceCommandLineDisposition::error;
    }
};

// Parses arguments excluding argv[0]. User-controlled input is bounded before
// conversion or filesystem queries; all failures are returned as stable errors.
[[nodiscard]] ServiceCommandLineResult parse_service_command_line(
    std::span<const std::string_view> arguments,
    ServiceCommandLinePlatform platform = native_service_command_line_platform()) noexcept;

// Convenience boundary for main(argc, argv). argv[0] is required and ignored.
[[nodiscard]] ServiceCommandLineResult parse_service_command_line(
    int argc,
    const char* const argv[],
    ServiceCommandLinePlatform platform = native_service_command_line_platform()) noexcept;

}  // namespace baas::service::app
