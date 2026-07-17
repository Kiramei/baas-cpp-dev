#include "service/app/RuntimeRepositoryUpdateApplication.h"

#include "service/app/RuntimeRepositoryTrustedPlan.h"
#include "service/auth/Crypto.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace app = baas::service::app;
namespace auth = baas::service::auth;
using Json = nlohmann::json;

void check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

[[nodiscard]] std::uint64_t unix_now() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

class TemporaryDirectory final {
public:
  TemporaryDirectory() {
    static std::uint64_t sequence{};
    path_ = std::filesystem::temp_directory_path() /
            ("baas-runtime-update-app-" + std::to_string(unix_now()) + "-" +
             std::to_string(++sequence));
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
};

[[nodiscard]] std::array<std::byte, auth::ed25519_seed_bytes>
seed(const unsigned char first) {
  std::array<std::byte, auth::ed25519_seed_bytes> result{};
  for (std::size_t index = 0; index < result.size(); ++index)
    result[index] = static_cast<std::byte>(first + index);
  return result;
}

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view value) {
  std::vector<std::byte> result;
  result.reserve(value.size());
  for (const unsigned char byte : value)
    result.push_back(static_cast<std::byte>(byte));
  return result;
}

[[nodiscard]] Json repository(const std::string_view id, const char commit,
                              const char hash) {
  return Json{
      {"id", id},
      {"remote_url", "https://example.invalid/" + std::string{id} + ".git"},
      {"advertised_reference", "refs/heads/main"},
      {"exact_commit", std::string(40, commit)},
      {"manifest", "runtime-manifest.json"},
      {"expected_manifest_sha256", std::string(64, hash)}};
}

[[nodiscard]] std::string signed_envelope(
    const std::array<std::byte, auth::ed25519_seed_bytes> &signing_seed) {
  const auto now = unix_now();
  const auto payload =
      Json{{"schema", app::runtime_repository_plan_payload_schema},
           {"sequence", "1"},
           {"not_before_unix", std::to_string(now - 60)},
           {"expires_unix", std::to_string(now + 3'600)},
           {"allow_bootstrap", true},
           {"previous_generations", Json::array()},
           {"repositories", Json::array({repository("resources", '1', 'a'),
                                         repository("scripts", '2', 'b')})}}
          .dump();
  auto message = bytes(app::runtime_repository_plan_signature_domain);
  const auto payload_bytes = bytes(payload);
  message.insert(message.end(), payload_bytes.begin(), payload_bytes.end());
  const auto signature = auth::ed25519_sign(signing_seed, message);
  const auto encoded_payload = auth::encode_base64url_padded(payload_bytes);
  check(signature && encoded_payload, "test signed envelope primitives");
  const auto encoded_signature =
      auth::encode_base64url_padded(*signature.value);
  check(static_cast<bool>(encoded_signature), "test signature encoding");
  return Json{{"schema", app::runtime_repository_plan_envelope_schema},
              {"payload", *encoded_payload.value},
              {"signature", *encoded_signature.value}}
      .dump();
}

[[nodiscard]] int run(const std::span<const std::string_view> arguments,
                      const std::string &input, std::string &output,
                      std::string &diagnostics) {
  std::istringstream input_stream{input};
  std::ostringstream output_stream;
  std::ostringstream diagnostic_stream;
  const auto result = app::run_runtime_repository_update_application(
      arguments, input_stream, output_stream, diagnostic_stream);
  output = output_stream.str();
  diagnostics = diagnostic_stream.str();
  return result;
}

void test_informational_and_bounded_input_contract() {
  std::string output;
  std::string diagnostics;
  const std::array help{std::string_view{"--help"}};
  check(run(help, {}, output, diagnostics) == 0 &&
            output.starts_with("Usage: BAAS_runtime_repository_update") &&
            diagnostics.empty(),
        "help is side-effect-free and stable");

  TemporaryDirectory root;
  const auto root_text = root.path().string();
  const std::array arguments{std::string_view{"--project-root"},
                             std::string_view{root_text}};
  check(run(arguments, {}, output, diagnostics) ==
                static_cast<int>(
                    app::RuntimeRepositoryUpdateProcessExit::input) &&
            output == "{\"ok\":false,\"error\":\"input\"}\n" &&
            !std::filesystem::exists(root.path() / ".baas-updater"),
        "empty input is rejected before publisher ownership side effects");

  const std::string oversized(
      app::runtime_repository_update_input_max_bytes + 1, 'x');
  check(run(arguments, oversized, output, diagnostics) ==
                static_cast<int>(
                    app::RuntimeRepositoryUpdateProcessExit::input) &&
            !std::filesystem::exists(root.path() / ".baas-updater"),
        "oversized stdin is rejected before publisher construction");
}

void test_product_key_precedes_libgit2_fetch() {
  TemporaryDirectory root;
  const auto root_text = root.path().string();
  const std::array arguments{std::string_view{"--project-root"},
                             std::string_view{root_text}};
  std::string output;
  std::string diagnostics;
  const auto exit =
      run(arguments, signed_envelope(seed(19)), output, diagnostics);
  check(exit == static_cast<int>(
                    app::RuntimeRepositoryUpdateProcessExit::invalid_plan) &&
            output.find("\"error\":\"invalid_plan\"") != std::string::npos &&
            output.find("\"plan_error\":\"invalid_signature\"") !=
                std::string::npos &&
            output.find("example.invalid") == std::string::npos &&
            std::filesystem::exists(root.path() / ".baas-updater" /
                                    "runtime-repositories" /
                                    ".trusted-plan-owner"),
        "fixed product key rejects the envelope before any libgit2 fetch");
}

void test_recovery_failure_is_machine_readable() {
  TemporaryDirectory root;
  const auto state_root =
      root.path() / ".baas-updater" / "runtime-repositories";
  std::filesystem::create_directories(state_root / ".trusted-plan-writer.lock");
  const auto root_text = root.path().string();
  const std::array arguments{std::string_view{"--project-root"},
                             std::string_view{root_text}};
  std::string output;
  std::string diagnostics;
  const auto exit =
      run(arguments, signed_envelope(seed(1)), output, diagnostics);
  check(exit == static_cast<int>(
                    app::RuntimeRepositoryUpdateProcessExit::recovery) &&
            output.find("\"error\":\"recovery_failed\"") != std::string::npos &&
            output.find(root.path().string()) == std::string::npos,
        "recovery failure exposes only stable classifications");
}

void test_ambiguous_commit_result_retains_handoff_data() {
  app::RuntimeRepositoryUpdateApplicationResult result;
  result.error = app::RuntimeRepositoryUpdateApplicationError::update_failed;
  result.owner.error =
      app::RuntimeRepositoryTrustedPlanUpdateOwnerError::updater_failure;
  result.owner.update.error =
      baas::runtime::repository::RuntimeRepositoryUpdateError{
          baas::runtime::repository::RuntimeRepositoryUpdateErrorCode::Io,
          "must not be serialized"};
  result.owner.update.disposition = baas::runtime::repository::
      PublishDisposition::CommittedDurabilityUncertain;
  result.owner.update.pinned_generation = std::string(64, 'a');

  std::ostringstream output;
  app::detail::write_runtime_repository_update_application_result(output,
                                                                  result);
  const auto serialized = output.str();
  check(serialized.find("\"disposition\":\"committed_durability_uncertain\"") !=
                std::string::npos &&
            serialized.find("\"generation\":\"" + std::string(64, 'a') +
                            "\"") != std::string::npos &&
            serialized.find("must not be serialized") == std::string::npos,
        "ambiguous commit failure retains only bounded handoff data");
}

} // namespace

int main() {
  test_informational_and_bounded_input_contract();
  test_product_key_precedes_libgit2_fetch();
  test_recovery_failure_is_machine_readable();
  test_ambiguous_commit_result_retains_handoff_data();
  std::cout << "Runtime repository update application tests passed\n";
  return 0;
}
