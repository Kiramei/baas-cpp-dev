#include "service/app/RuntimeRepositoryUpdateApplication.h"

#include "runtime/repository/RuntimeRepositoryGit2.h"

#include <array>
#include <filesystem>
#include <istream>
#include <new>
#include <ostream>
#include <string>
#include <system_error>
#include <utility>

#if !defined(BAAS_RUNTIME_REPOSITORY_TRUSTED_PUBLIC_KEY_HEX)
#error "BAAS runtime repository update product key is required"
#endif

namespace baas::service::app {
namespace {

#if !defined(BAAS_RUNTIME_REPOSITORY_UPDATE_VERSION)
#define BAAS_RUNTIME_REPOSITORY_UPDATE_VERSION "1.1.1"
#endif

constexpr std::string_view project_root_option = "--project-root";
constexpr std::string_view help_option = "--help";
constexpr std::string_view version_option = "--version";
constexpr std::string_view state_directory = ".baas-updater";
constexpr std::string_view repository_directory = "runtime-repositories";

[[nodiscard]] consteval unsigned char hex_digit(const char value) {
  if (value >= '0' && value <= '9')
    return static_cast<unsigned char>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<unsigned char>(value - 'a' + 10);
  throw "invalid product public key";
}

[[nodiscard]] consteval auto product_public_key() {
  constexpr std::string_view encoded =
      BAAS_RUNTIME_REPOSITORY_TRUSTED_PUBLIC_KEY_HEX;
  static_assert(encoded.size() == 64);
  std::array<std::byte, 32> key{};
  for (std::size_t index = 0; index < key.size(); ++index) {
    key[index] = static_cast<std::byte>((hex_digit(encoded[index * 2]) << 4U) |
                                        hex_digit(encoded[index * 2 + 1]));
  }
  return key;
}

constexpr auto trusted_public_key = product_public_key();

struct ParsedArguments {
  std::filesystem::path project_root;
  RuntimeRepositoryUpdateProcessExit error{
      RuntimeRepositoryUpdateProcessExit::success};
  bool help{};
  bool version{};
};

[[nodiscard]] bool contains_nul(const std::string_view value) noexcept {
  return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] ParsedArguments
parse_arguments(const std::span<const std::string_view> arguments) {
  ParsedArguments result;
  if (arguments.size() == 1 && arguments.front() == help_option) {
    result.help = true;
    return result;
  }
  if (arguments.size() == 1 && arguments.front() == version_option) {
    result.version = true;
    return result;
  }
  if (arguments.size() != runtime_repository_update_max_argument_count ||
      arguments.front() != project_root_option || arguments.back().empty() ||
      arguments.back().size() > runtime_repository_update_max_argument_bytes ||
      contains_nul(arguments.back())) {
    result.error = RuntimeRepositoryUpdateProcessExit::command_line;
    return result;
  }
#if defined(_WIN32)
  std::u8string utf8;
  utf8.reserve(arguments.back().size());
  for (const unsigned char byte : arguments.back())
    utf8.push_back(static_cast<char8_t>(byte));
  result.project_root = std::filesystem::path{utf8};
#else
  result.project_root = std::filesystem::path{std::string{arguments.back()}};
#endif
  std::error_code error;
  if (!std::filesystem::is_directory(result.project_root, error) || error)
    result.error = RuntimeRepositoryUpdateProcessExit::command_line;
  return result;
}

struct InputResult {
  std::string value;
  bool valid{};
};

[[nodiscard]] InputResult read_input(std::istream &input) {
  InputResult result;
  std::array<char, 4U * 1'024U> buffer{};
  for (;;) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) {
      const auto bytes = static_cast<std::size_t>(count);
      if (bytes >
          runtime_repository_update_input_max_bytes - result.value.size())
        return {};
      result.value.append(buffer.data(), bytes);
    }
    if (input.eof())
      break;
    if (!input)
      return {};
  }
  result.valid = !result.value.empty();
  return result;
}

[[nodiscard]] std::string_view disposition_name(
    const runtime::repository::PublishDisposition disposition) noexcept {
  using enum runtime::repository::PublishDisposition;
  switch (disposition) {
  case NotCommitted:
    return "not_committed";
  case Committed:
    return "committed";
  case CommittedDurabilityUncertain:
    return "committed_durability_uncertain";
  }
  return "not_committed";
}

[[nodiscard]] std::string_view
update_error_name(const runtime::repository::RuntimeRepositoryUpdateErrorCode
                      error) noexcept {
  using enum runtime::repository::RuntimeRepositoryUpdateErrorCode;
  switch (error) {
  case Busy:
    return "busy";
  case Cancelled:
    return "cancelled";
  case InvalidPlan:
    return "invalid_plan";
  case FetchFailed:
    return "fetch_failed";
  case CommitMismatch:
    return "commit_mismatch";
  case ValidationFailed:
    return "validation_failed";
  case CurrentConflict:
    return "current_conflict";
  case NoPrevious:
    return "no_previous";
  case Io:
    return "io";
  case RecoveryFailed:
    return "recovery_failed";
  }
  return "io";
}

void write_process_error(std::ostream &output, const std::string_view error) {
  output << "{\"ok\":false,\"error\":\"" << error << "\"}\n";
}

[[nodiscard]] RuntimeRepositoryUpdateProcessExit
process_exit_for(const RuntimeRepositoryUpdateApplicationError error) noexcept {
  using enum RuntimeRepositoryUpdateApplicationError;
  switch (error) {
  case none:
    return RuntimeRepositoryUpdateProcessExit::success;
  case invalid_project_root:
    return RuntimeRepositoryUpdateProcessExit::command_line;
  case recovery_failed:
    return RuntimeRepositoryUpdateProcessExit::recovery;
  case invalid_plan:
    return RuntimeRepositoryUpdateProcessExit::invalid_plan;
  case update_failed:
    return RuntimeRepositoryUpdateProcessExit::update;
  case resource_exhausted:
  case internal_error:
    return RuntimeRepositoryUpdateProcessExit::internal_failure;
  }
  return RuntimeRepositoryUpdateProcessExit::internal_failure;
}

} // namespace

namespace detail {

void write_runtime_repository_update_application_result(
    std::ostream &output,
    const RuntimeRepositoryUpdateApplicationResult &result) {
  output << "{\"ok\":" << (result ? "true" : "false");
  if (!result) {
    output << ",\"error\":\""
           << runtime_repository_update_application_error_name(result.error)
           << "\",\"owner_error\":\""
           << runtime_repository_trusted_plan_update_owner_error_name(
                  result.owner.error)
           << "\",\"plan_error\":\""
           << runtime_repository_trusted_plan_error_name(
                  result.owner.plan_error)
           << "\",\"state_error\":\""
           << runtime_repository_trusted_plan_state_error_name(
                  result.owner.state_error)
           << "\",\"update_error\":\"";
    if (result.owner.update.error)
      output << update_error_name(result.owner.update.error->code);
    else
      output << "none";
    output << '"';
  }
  output << ",\"disposition\":\""
         << disposition_name(result.owner.update.disposition)
         << "\",\"generation\":\"" << result.owner.update.pinned_generation
         << "\"}\n";
}

} // namespace detail

std::string_view runtime_repository_update_application_error_name(
    const RuntimeRepositoryUpdateApplicationError error) noexcept {
  using enum RuntimeRepositoryUpdateApplicationError;
  switch (error) {
  case none:
    return "none";
  case invalid_project_root:
    return "invalid_project_root";
  case recovery_failed:
    return "recovery_failed";
  case invalid_plan:
    return "invalid_plan";
  case update_failed:
    return "update_failed";
  case resource_exhausted:
    return "resource_exhausted";
  case internal_error:
    return "internal_error";
  }
  return "internal_error";
}

RuntimeRepositoryUpdateApplicationResult
apply_runtime_repository_update(const std::filesystem::path &project_root,
                                const std::string_view signed_envelope,
                                const std::stop_token stop_token) noexcept {
  try {
    std::error_code error;
    if (!std::filesystem::is_directory(project_root, error) || error)
      return {{},
              RuntimeRepositoryUpdateApplicationError::invalid_project_root};
    const auto state_root =
        project_root / state_directory / repository_directory;
    runtime::repository::StrictRuntimeRepositoryTreeValidator validator;
    runtime::repository::Libgit2RuntimeRepositoryFetchBackend backend;
    RuntimeRepositoryTrustedPlanUpdateOwner owner{state_root,
                                                  trusted_public_key};
    auto recovered = owner.recover(validator);
    if (!recovered)
      return {std::move(recovered),
              RuntimeRepositoryUpdateApplicationError::recovery_failed};
    auto applied = owner.apply(signed_envelope, backend, validator, stop_token);
    if (applied)
      return {std::move(applied),
              RuntimeRepositoryUpdateApplicationError::none};
    const auto application_error =
        applied.error ==
                RuntimeRepositoryTrustedPlanUpdateOwnerError::invalid_plan
            ? RuntimeRepositoryUpdateApplicationError::invalid_plan
            : RuntimeRepositoryUpdateApplicationError::update_failed;
    return {std::move(applied), application_error};
  } catch (const std::bad_alloc &) {
    return {{}, RuntimeRepositoryUpdateApplicationError::resource_exhausted};
  } catch (...) {
    return {{}, RuntimeRepositoryUpdateApplicationError::internal_error};
  }
}

int run_runtime_repository_update_application(
    const std::span<const std::string_view> arguments, std::istream &input,
    std::ostream &output, std::ostream &diagnostics) noexcept {
  try {
    if (arguments.size() > runtime_repository_update_max_argument_count) {
      write_process_error(output, "command_line");
      diagnostics << "BAAS_runtime_repository_update: command_line\n";
      return static_cast<int>(RuntimeRepositoryUpdateProcessExit::command_line);
    }
    for (const auto argument : arguments) {
      if (argument.size() > runtime_repository_update_max_argument_bytes ||
          contains_nul(argument)) {
        write_process_error(output, "command_line");
        diagnostics << "BAAS_runtime_repository_update: command_line\n";
        return static_cast<int>(
            RuntimeRepositoryUpdateProcessExit::command_line);
      }
    }
    const auto parsed = parse_arguments(arguments);
    if (parsed.help) {
      output << "Usage: BAAS_runtime_repository_update --project-root "
                "<directory> < signed-plan.json\n";
      return static_cast<int>(RuntimeRepositoryUpdateProcessExit::success);
    }
    if (parsed.version) {
      output << "BAAS_runtime_repository_update "
             << BAAS_RUNTIME_REPOSITORY_UPDATE_VERSION << '\n';
      return static_cast<int>(RuntimeRepositoryUpdateProcessExit::success);
    }
    if (parsed.error != RuntimeRepositoryUpdateProcessExit::success) {
      write_process_error(output, "command_line");
      diagnostics << "BAAS_runtime_repository_update: command_line\n";
      return static_cast<int>(parsed.error);
    }
    const auto envelope = read_input(input);
    if (!envelope.valid) {
      write_process_error(output, "input");
      diagnostics << "BAAS_runtime_repository_update: input\n";
      return static_cast<int>(RuntimeRepositoryUpdateProcessExit::input);
    }
    const auto result =
        apply_runtime_repository_update(parsed.project_root, envelope.value);
    detail::write_runtime_repository_update_application_result(output, result);
    if (!result)
      diagnostics << "BAAS_runtime_repository_update: "
                  << runtime_repository_update_application_error_name(
                         result.error)
                  << '\n';
    return static_cast<int>(process_exit_for(result.error));
  } catch (const std::bad_alloc &) {
    write_process_error(output, "resource_exhausted");
    diagnostics << "BAAS_runtime_repository_update: resource_exhausted\n";
    return static_cast<int>(
        RuntimeRepositoryUpdateProcessExit::internal_failure);
  } catch (...) {
    write_process_error(output, "internal_error");
    diagnostics << "BAAS_runtime_repository_update: internal_error\n";
    return static_cast<int>(
        RuntimeRepositoryUpdateProcessExit::internal_failure);
  }
}

} // namespace baas::service::app
