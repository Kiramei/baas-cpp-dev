#include "service/app/RuntimeRepositoryTrustedPlan.h"
#include "service/app/RuntimeRepositoryTrustedPlanState.h"
#include "service/app/RuntimeRepositoryTrustedPlanUpdateOwner.h"

#include "service/auth/Crypto.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace app = baas::service::app;
namespace auth = baas::service::auth;
using Json = nlohmann::json;

[[nodiscard]] std::uint64_t unix_now();

class StaticStateProvider final
    : public app::RuntimeRepositoryTrustedStateProvider {
public:
  explicit StaticStateProvider(
      std::optional<app::RuntimeRepositoryTrustedState> value = std::nullopt)
      : value_(std::move(value)) {}

  [[nodiscard]] std::optional<app::RuntimeRepositoryTrustedState>
  trusted_state() const override {
    return value_;
  }

private:
  std::optional<app::RuntimeRepositoryTrustedState> value_;
};

class TemporaryDirectory final {
public:
  TemporaryDirectory() {
    static std::uint64_t sequence{};
    path_ = std::filesystem::temp_directory_path() /
            ("baas-trusted-plan-state-" + std::to_string(unix_now()) + "-" +
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

class FakeFetchBackend final
    : public baas::runtime::repository::RuntimeRepositoryFetchBackend {
public:
  [[nodiscard]] baas::runtime::repository::RepositoryStageResult
  stage_exact(const baas::runtime::repository::RepositoryFetchSpec &spec,
              const std::filesystem::path &staging_directory,
              std::stop_token) override {
    std::ofstream output(staging_directory / "payload.bin", std::ios::binary);
    output << "x";
    output.close();
    return {spec.exact_commit};
  }
};

class FakeTreeValidator final
    : public baas::runtime::repository::RuntimeRepositoryTreeValidator {
public:
  [[nodiscard]] baas::runtime::repository::RepositoryTreeSeal
  validate_and_seal(const baas::runtime::repository::RepositoryFetchSpec &spec,
                    const std::filesystem::path &,
                    std::stop_token) const override {
    return {spec.expected_manifest_sha256, std::string(64, 'e'), 1, 1};
  }
};

class RecordingCommitClaim final
    : public baas::runtime::repository::RuntimeRepositoryCommitClaim {
public:
  explicit RecordingCommitClaim(const bool accept) : accept_(accept) {}

  [[nodiscard]] bool claim(std::string_view) noexcept override {
    ++calls_;
    return accept_;
  }

  [[nodiscard]] std::size_t calls() const noexcept { return calls_; }

private:
  bool accept_{};
  std::size_t calls_{};
};

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

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view value) {
  std::vector<std::byte> result;
  result.reserve(value.size());
  for (const unsigned char byte : value)
    result.push_back(static_cast<std::byte>(byte));
  return result;
}

[[nodiscard]] std::array<std::byte, auth::ed25519_seed_bytes>
seed(const unsigned char first = 1) {
  std::array<std::byte, auth::ed25519_seed_bytes> result{};
  for (std::size_t index = 0; index < result.size(); ++index)
    result[index] = static_cast<std::byte>(first + index);
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

[[nodiscard]] Json payload() {
  const auto now = unix_now();
  return Json{{"schema", app::runtime_repository_plan_payload_schema},
              {"sequence", "42"},
              {"not_before_unix", std::to_string(now - 60)},
              {"expires_unix", std::to_string(now + 3'600)},
              {"allow_bootstrap", true},
              {"previous_generations", Json::array()},
              {"repositories", Json::array({repository("resources", '1', 'a'),
                                            repository("scripts", '2', 'b')})}};
}

[[nodiscard]] std::string signed_envelope(
    const std::string_view payload_text,
    const std::array<std::byte, auth::ed25519_seed_bytes> &signing_seed =
        seed()) {
  auto message = bytes(app::runtime_repository_plan_signature_domain);
  const auto payload_bytes = bytes(payload_text);
  message.insert(message.end(), payload_bytes.begin(), payload_bytes.end());
  const auto signature = auth::ed25519_sign(signing_seed, message);
  check(static_cast<bool>(signature), "test signature creation");
  const auto encoded_payload = auth::encode_base64url_padded(payload_bytes);
  const auto encoded_signature =
      auth::encode_base64url_padded(*signature.value);
  check(encoded_payload && encoded_signature, "test base64 creation");
  return Json{{"schema", app::runtime_repository_plan_envelope_schema},
              {"payload", *encoded_payload.value},
              {"signature", *encoded_signature.value}}
      .dump();
}

[[nodiscard]] auth::PublicBytes public_key() {
  const auto value = auth::ed25519_public_key_from_seed(seed());
  check(static_cast<bool>(value), "test public key creation");
  return *value.value;
}

[[nodiscard]] app::RuntimeRepositoryTrustedPlanResult verify(
    const Json &value,
    std::optional<app::RuntimeRepositoryTrustedState> current = std::nullopt) {
  const auto key = public_key();
  const auto state_provider =
      std::make_shared<StaticStateProvider>(std::move(current));
  const app::RuntimeRepositoryTrustedPlanVerifier verifier{key, state_provider};
  return verifier.verify(signed_envelope(value.dump()));
}

[[nodiscard]] app::RuntimeRepositoryTrustedState
state(const app::VerifiedRuntimeRepositoryPlan &plan) {
  return {plan.target_generation(), plan.sequence(), plan.payload_sha256()};
}

void test_valid_bootstrap_successor_and_idempotence() {
  const auto value = payload();
  const auto initial = verify(value);
  check(static_cast<bool>(initial), "valid bootstrap accepted");
  check(initial.plan->sequence() == 42, "sequence retained");
  check(initial.plan->not_before_unix() <= unix_now(), "not-before retained");
  check(initial.plan->expires_unix() > unix_now(), "expiry retained");
  check(initial.plan->target_generation().size() == 64 &&
            initial.plan->payload_sha256().size() == 64,
        "target and signed payload identities derived");
  const auto plan = initial.plan->trusted_plan();
  check(plan.repositories[0].remote_url ==
                "https://example.invalid/resources.git" &&
            plan.repositories[1].exact_commit == std::string(40, '2'),
        "trusted plan is immutable provider data");

  auto successor = payload();
  successor["sequence"] = "43";
  const std::string predecessor(64, 'c');
  successor["previous_generations"] = Json::array({predecessor});
  const auto accepted =
      verify(successor, app::RuntimeRepositoryTrustedState{
                            predecessor, 42, std::string(64, 'd')});
  check(static_cast<bool>(accepted), "authorized higher sequence accepted");

  const auto retry = verify(value, state(*initial.plan));
  check(static_cast<bool>(retry), "exact same-sequence retry accepted");

  auto changed_same_sequence = value;
  changed_same_sequence["expires_unix"] = std::to_string(unix_now() + 7'200);
  check(verify(changed_same_sequence, state(*initial.plan)).error ==
            app::RuntimeRepositoryTrustedPlanError::replay_rejected,
        "same sequence cannot change signed payload");
}

void test_signature_canonicalization_and_preparse_bounds() {
  const auto key = public_key();
  const auto state_provider = std::make_shared<StaticStateProvider>();
  const app::RuntimeRepositoryTrustedPlanVerifier verifier{key, state_provider};
  auto result = verifier.verify(signed_envelope(payload().dump(), seed(19)));
  check(result.error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_signature,
        "wrong signature rejected");

  result = verifier.verify(signed_envelope(payload().dump(2)));
  check(result.error ==
            app::RuntimeRepositoryTrustedPlanError::noncanonical_payload,
        "signed noncanonical payload rejected");

  const auto now = unix_now();
  const std::string duplicate =
      "{\"allow_bootstrap\":true,\"allow_bootstrap\":true,"
      "\"expires_unix\":\"" +
      std::to_string(now + 100) + "\",\"not_before_unix\":\"" +
      std::to_string(now - 100) +
      "\",\"previous_generations\":[],\"repositories\":[],"
      "\"schema\":\"baas.runtime-repositories.update-plan/v1\","
      "\"sequence\":\"42\"}";
  result = verifier.verify(signed_envelope(duplicate));
  check(result.error == app::RuntimeRepositoryTrustedPlanError::invalid_payload,
        "duplicate payload member rejected");

  auto envelope = Json::parse(signed_envelope(payload().dump()));
  envelope["unexpected"] = true;
  check(verifier.verify(envelope.dump()).error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_envelope,
        "unknown envelope field rejected");

  std::string deeply_nested(10'000, '[');
  deeply_nested += "0";
  deeply_nested.append(10'000, ']');
  check(verifier.verify(deeply_nested).error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_envelope,
        "deep unauthenticated envelope rejected during SAX preparse");
}

void test_time_sequence_replay_and_bootstrap_policy() {
  const auto now = unix_now();
  auto value = payload();
  value["not_before_unix"] = std::to_string(now + 3'600);
  value["expires_unix"] = std::to_string(now + 7'200);
  check(verify(value).error ==
            app::RuntimeRepositoryTrustedPlanError::not_yet_valid,
        "premature plan rejected using verifier-owned clock");

  value = payload();
  value["not_before_unix"] = std::to_string(now - 7'200);
  value["expires_unix"] = std::to_string(now - 3'600);
  check(verify(value).error == app::RuntimeRepositoryTrustedPlanError::expired,
        "expired plan rejected using verifier-owned clock");

  value = payload();
  value["expires_unix"] =
      std::to_string(now + 31ULL * 24ULL * 60ULL * 60ULL + 1);
  check(verify(value).error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_payload,
        "overlong validity rejected");

  value = payload();
  value["allow_bootstrap"] = false;
  check(verify(value).error ==
            app::RuntimeRepositoryTrustedPlanError::bootstrap_rejected,
        "bootstrap must be explicit");

  value = payload();
  const auto target = verify(value).plan->target_generation();
  value["sequence"] = "41";
  value["repositories"][0] = repository("resources", '3', 'c');
  value["previous_generations"] = Json::array({target});
  check(verify(value, app::RuntimeRepositoryTrustedState{target, 42,
                                                         std::string(64, 'e')})
                .error ==
            app::RuntimeRepositoryTrustedPlanError::replay_rejected,
        "lower-sequence signed rollback replay rejected");

  value = payload();
  check(
      verify(value, app::RuntimeRepositoryTrustedState{std::string(64, 'd'), 41,
                                                       std::string(64, 'e')})
              .error == app::RuntimeRepositoryTrustedPlanError::replay_rejected,
      "unauthorized predecessor rejected");

  check(
      verify(value, app::RuntimeRepositoryTrustedState{std::string(63, 'd'), 41,
                                                       std::string(64, 'e')})
              .error == app::RuntimeRepositoryTrustedPlanError::invalid_state,
      "malformed persisted trusted state rejected");
}

void test_strict_shape_limits_and_error_names() {
  auto value = payload();
  value["sequence"] = "01";
  check(verify(value).error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_payload,
        "noncanonical decimal rejected");
  value["sequence"] = "0";
  check(verify(value).error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_payload,
        "zero sequence rejected");

  value = payload();
  value["repositories"][0]["id"] = "scripts";
  check(verify(value).error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_payload,
        "repository order fixed");
  value = payload();
  value["repositories"][0]["exact_commit"] = std::string(40, 'A');
  check(verify(value).error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_payload,
        "commit must be lowercase full object id");
  value = payload();
  value["unexpected"] = true;
  check(verify(value).error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_payload,
        "unknown payload field rejected");

  const auto key = public_key();
  app::RuntimeRepositoryTrustedPlanLimits limits;
  limits.max_json_depth = 2;
  const auto state_provider = std::make_shared<StaticStateProvider>();
  const app::RuntimeRepositoryTrustedPlanVerifier invalid_limits{
      key, state_provider, limits};
  check(invalid_limits.verify(signed_envelope(payload().dump())).error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_limits,
        "unsafe limits rejected");
  const app::RuntimeRepositoryTrustedPlanVerifier invalid_key{
      std::span<const std::byte>{key}.first(31), state_provider};
  check(invalid_key.verify(signed_envelope(payload().dump())).error ==
            app::RuntimeRepositoryTrustedPlanError::invalid_public_key,
        "wrong public key size rejected");

  using Error = app::RuntimeRepositoryTrustedPlanError;
  check(app::runtime_repository_trusted_plan_error_name(Error::none) ==
                "none" &&
            app::runtime_repository_trusted_plan_error_name(
                Error::invalid_state) == "invalid_state" &&
            app::runtime_repository_trusted_plan_error_name(
                Error::replay_rejected) == "replay_rejected",
        "stable error names");
}

void test_durable_state_transaction_reconciliation() {
  TemporaryDirectory root;
  const auto first = app::RuntimeRepositoryTrustedState{
      std::string(64, 'a'), 42, std::string(64, 'b')};
  const auto second = app::RuntimeRepositoryTrustedState{
      std::string(64, 'c'), 43, std::string(64, 'd')};

  auto store = std::make_shared<app::RuntimeRepositoryTrustedPlanStateStore>(
      root.path());
  check(static_cast<bool>(store->reconcile(std::nullopt)),
        "empty store reconciles");
  check(!store->trusted_state(), "empty store supports first bootstrap");
  check(static_cast<bool>(store->prepare(std::nullopt, first)),
        "bootstrap commit intent persisted");

  store = std::make_shared<app::RuntimeRepositoryTrustedPlanStateStore>(
      root.path());
  check(static_cast<bool>(store->reconcile(first.generation)),
        "committed bootstrap intent completes after restart");
  check(store->trusted_state() == first, "bootstrap state recovered");

  check(static_cast<bool>(store->prepare(first.generation, second)),
        "successor commit intent persisted");
  store = std::make_shared<app::RuntimeRepositoryTrustedPlanStateStore>(
      root.path());
  check(static_cast<bool>(store->reconcile(first.generation)),
        "uncommitted successor intent discarded after restart");
  check(store->trusted_state() == first,
        "discard retains previous trusted state");

  check(static_cast<bool>(store->prepare(first.generation, second)),
        "successor intent can be retried");
  store = std::make_shared<app::RuntimeRepositoryTrustedPlanStateStore>(
      root.path());
  check(static_cast<bool>(store->reconcile(second.generation)),
        "committed successor intent completes after restart");
  check(store->trusted_state() == second,
        "successor state recovered atomically");

  check(
      store->reconcile(first.generation).error ==
          app::RuntimeRepositoryTrustedPlanStateError::inconsistent_generation,
      "trusted state and actual generation mismatch fails closed");
  std::filesystem::remove(root.path() / ".trusted-plan-state.json");
  store = std::make_shared<app::RuntimeRepositoryTrustedPlanStateStore>(
      root.path());
  check(store->reconcile(second.generation).error ==
            app::RuntimeRepositoryTrustedPlanStateError::invalid_state,
        "initialized ownership never treats a missing state as bootstrap");

  TemporaryDirectory malformed_root;
  {
    std::ofstream malformed(malformed_root.path() / ".trusted-plan-state.json");
    malformed << "{";
  }
  auto malformed_store =
      std::make_shared<app::RuntimeRepositoryTrustedPlanStateStore>(
          malformed_root.path());
  check(malformed_store->reconcile(std::nullopt).error ==
            app::RuntimeRepositoryTrustedPlanStateError::invalid_state,
        "malformed state never becomes an empty bootstrap state");
}

void test_update_owner_coordinates_updater_and_policy_state() {
  TemporaryDirectory root;
  const auto key = public_key();
  FakeFetchBackend backend;
  FakeTreeValidator validator;
  const auto value = payload();
  const auto envelope = signed_envelope(value.dump());

  {
    app::RuntimeRepositoryTrustedPlanUpdateOwner owner{root.path(), key};
    check(owner.apply(envelope, backend, validator).error ==
              app::RuntimeRepositoryTrustedPlanUpdateOwnerError::not_recovered,
          "publisher refuses requests before startup recovery");
    check(static_cast<bool>(owner.recover(validator)),
          "publisher startup recovery succeeds");
    RecordingCommitClaim terminal_claim{true};
    const auto applied =
        owner.apply(envelope, backend, validator, {}, &terminal_claim);
    check(static_cast<bool>(applied) &&
              applied.update.disposition ==
                  baas::runtime::repository::PublishDisposition::Committed &&
              applied.update.pinned_generation.size() == 64 &&
              terminal_claim.calls() == 1,
          "publisher commits generation and trusted state together");
  }

  {
    app::RuntimeRepositoryTrustedPlanUpdateOwner owner{root.path(), key};
    check(static_cast<bool>(owner.recover(validator)),
          "publisher reconciles persisted state after restart");
    RecordingCommitClaim rejected_claim{false};
    const auto rejected =
        owner.apply(envelope, backend, validator, {}, &rejected_claim);
    check(
        rejected.error ==
                app::RuntimeRepositoryTrustedPlanUpdateOwnerError::cancelled &&
            rejected_claim.calls() == 1,
        "policy-only no-op honors the external terminal claim");
    const auto retry = owner.apply(envelope, backend, validator);
    check(static_cast<bool>(retry) &&
              retry.update.disposition ==
                  baas::runtime::repository::PublishDisposition::Committed,
          "same signed plan is an idempotent no-op after restart");
    const auto wrong = owner.apply(signed_envelope(value.dump(), seed(19)),
                                   backend, validator);
    check(wrong.error == app::RuntimeRepositoryTrustedPlanUpdateOwnerError::
                             invalid_plan &&
              wrong.plan_error ==
                  app::RuntimeRepositoryTrustedPlanError::invalid_signature,
          "publisher never accepts a request-provided trust root");
  }
}

void test_existing_generation_requires_noop_policy_adoption() {
  TemporaryDirectory root;
  FakeFetchBackend backend;
  FakeTreeValidator validator;
  const auto initial_payload = payload();
  const auto initial_plan = verify(initial_payload);
  check(static_cast<bool>(initial_plan), "legacy fixture plan verifies");

  {
    baas::runtime::repository::RuntimeRepositoryUpdater updater{root.path()};
    check(static_cast<bool>(updater.recover(validator)),
          "legacy updater initializes");
    const auto published =
        updater.update(*initial_plan.plan, backend, validator,
                       baas::runtime::repository::ExpectedCurrent::absent());
    check(static_cast<bool>(published),
          "legacy publisher creates generation without policy state");
  }

  app::RuntimeRepositoryTrustedPlanUpdateOwner owner{root.path(), public_key()};
  check(static_cast<bool>(owner.recover(validator)),
        "existing generation enters signed adoption mode");
  auto changed = payload();
  changed["sequence"] = "43";
  changed["repositories"] = Json::array(
      {repository("resources", '3', 'c'), repository("scripts", '4', 'd')});
  const auto rejected =
      owner.apply(signed_envelope(changed.dump()), backend, validator);
  check(
      rejected.error ==
              app::RuntimeRepositoryTrustedPlanUpdateOwnerError::invalid_plan &&
          rejected.plan_error ==
              app::RuntimeRepositoryTrustedPlanError::bootstrap_rejected,
      "existing generation cannot bootstrap directly to a different target");

  check(static_cast<bool>(owner.apply(signed_envelope(initial_payload.dump()),
                                      backend, validator)),
        "existing generation adopts only a signed same-target plan");
}

void test_policy_writer_lock_rejects_unsafe_paths() {
  FakeTreeValidator validator;

  {
    TemporaryDirectory root;
    std::filesystem::create_directory(root.path() /
                                      ".trusted-plan-writer.lock");
    app::RuntimeRepositoryTrustedPlanUpdateOwner owner{root.path(),
                                                       public_key()};
    check(owner.recover(validator).error ==
              app::RuntimeRepositoryTrustedPlanUpdateOwnerError::internal_error,
          "policy writer lock rejects a directory in place of the lock file");
  }

  {
    TemporaryDirectory root;
    const auto target = root.path() / "outside-lock-target";
    std::ofstream(target) << "not-a-lock";
    std::error_code error;
    std::filesystem::create_symlink(
        target, root.path() / ".trusted-plan-writer.lock", error);
    if (!error) {
      app::RuntimeRepositoryTrustedPlanUpdateOwner owner{root.path(),
                                                         public_key()};
      check(
          owner.recover(validator).error ==
              app::RuntimeRepositoryTrustedPlanUpdateOwnerError::internal_error,
          "policy writer lock rejects a symlink or reparse point");
    }
  }
}

} // namespace

int main() {
  test_valid_bootstrap_successor_and_idempotence();
  test_signature_canonicalization_and_preparse_bounds();
  test_time_sequence_replay_and_bootstrap_policy();
  test_strict_shape_limits_and_error_names();
  test_durable_state_transaction_reconciliation();
  test_update_owner_coordinates_updater_and_policy_state();
  test_existing_generation_requires_noop_policy_adoption();
  test_policy_writer_lock_rejects_unsafe_paths();
  std::cout << "Runtime repository trusted plan tests passed\n";
  return 0;
}
