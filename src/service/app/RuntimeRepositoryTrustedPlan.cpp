#include "service/app/RuntimeRepositoryTrustedPlan.h"

#include "service/adapters/BoundedJson.h"
#include "service/auth/Crypto.h"

#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace baas::service::app {
namespace {

using Json = nlohmann::json;
using runtime::repository::RepositoryFetchSpec;
using runtime::repository::RuntimeRepository;
using runtime::repository::RuntimeRepositoryId;
using runtime::repository::RuntimeRepositoryUpdatePlan;

[[nodiscard]] RuntimeRepositoryTrustedPlanResult
fail(const RuntimeRepositoryTrustedPlanError error) noexcept {
  return {nullptr, error};
}

[[nodiscard]] bool
valid_limits(const RuntimeRepositoryTrustedPlanLimits &limits) noexcept {
  return limits.max_payload_bytes >= 2 &&
         limits.max_envelope_bytes >= limits.max_payload_bytes &&
         limits.max_envelope_bytes <= 1U * 1'024U * 1'024U &&
         limits.max_payload_bytes <= 512U * 1'024U &&
         limits.max_json_depth >= 3 && limits.max_json_depth <= 64 &&
         limits.max_json_nodes >= 16 && limits.max_json_nodes <= 65'536 &&
         limits.max_previous_generations <= 256 &&
         limits.max_validity_seconds != 0 &&
         limits.max_validity_seconds <= 366ULL * 24ULL * 60ULL * 60ULL;
}

[[nodiscard]] bool lower_hex(const std::string_view value,
                             const std::size_t size) noexcept {
  if (value.size() != size)
    return false;
  for (const char byte : value) {
    if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool valid_manifest(const std::string_view value) noexcept {
  if (value.empty() || value.size() > 255 || value == "." || value == "..") {
    return false;
  }
  for (const char byte : value) {
    if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
          byte == '_' || byte == '-' || byte == '.')) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool
exact_fields(const Json &value,
             const std::initializer_list<std::string_view> fields) {
  if (!value.is_object() || value.size() != fields.size())
    return false;
  for (const auto field : fields) {
    if (!value.contains(std::string{field}))
      return false;
  }
  return true;
}

[[nodiscard]] std::optional<std::uint64_t>
canonical_decimal(const Json &value) {
  if (!value.is_string())
    return std::nullopt;
  const auto &text = value.get_ref<const std::string &>();
  if (text.empty() || (text.size() > 1 && text.front() == '0')) {
    return std::nullopt;
  }
  std::uint64_t parsed{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return parsed;
}

[[nodiscard]] std::optional<RepositoryFetchSpec>
parse_repository(const Json &value, const RuntimeRepositoryId expected_id) {
  if (!exact_fields(value,
                    {"id", "remote_url", "advertised_reference", "exact_commit",
                     "manifest", "expected_manifest_sha256"})) {
    return std::nullopt;
  }
  for (const auto field :
       {"id", "remote_url", "advertised_reference", "exact_commit", "manifest",
        "expected_manifest_sha256"}) {
    if (!value.at(field).is_string())
      return std::nullopt;
  }
  const auto id = value.at("id").get<std::string>();
  const auto expected_name = expected_id == RuntimeRepositoryId::Resources
                                 ? std::string_view{"resources"}
                                 : std::string_view{"scripts"};
  if (id != expected_name)
    return std::nullopt;

  RepositoryFetchSpec result;
  result.id = expected_id;
  result.remote_url = value.at("remote_url").get<std::string>();
  result.advertised_reference =
      value.at("advertised_reference").get<std::string>();
  result.exact_commit = value.at("exact_commit").get<std::string>();
  result.manifest = value.at("manifest").get<std::string>();
  result.expected_manifest_sha256 =
      value.at("expected_manifest_sha256").get<std::string>();
  if (result.remote_url.empty() || result.remote_url.size() > 8'192 ||
      result.advertised_reference.empty() ||
      result.advertised_reference.size() > 1'024 ||
      !lower_hex(result.exact_commit, 40) || !valid_manifest(result.manifest) ||
      !lower_hex(result.expected_manifest_sha256, 64)) {
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] std::string
target_generation(const RuntimeRepositoryUpdatePlan &plan) {
  std::array<RuntimeRepository, 2> repositories;
  for (std::size_t index = 0; index < repositories.size(); ++index) {
    const auto &spec = plan.repositories[index];
    const auto id = spec.id == RuntimeRepositoryId::Resources
                        ? std::string{"resources"}
                        : std::string{"scripts"};
    repositories[index] = {id, spec.exact_commit,
                           "objects/" + id + "/" + spec.exact_commit,
                           spec.manifest, spec.expected_manifest_sha256};
  }
  return runtime::repository::runtime_repository_generation(repositories);
}

[[nodiscard]] std::vector<std::byte>
signature_message(const std::span<const std::byte> payload) {
  std::vector<std::byte> result;
  result.reserve(runtime_repository_plan_signature_domain.size() +
                 payload.size());
  for (const unsigned char byte : runtime_repository_plan_signature_domain) {
    result.push_back(static_cast<std::byte>(byte));
  }
  result.insert(result.end(), payload.begin(), payload.end());
  return result;
}

[[nodiscard]] std::optional<std::string>
sha256_hex(const std::span<const std::byte> value) {
  const auto digest = auth::sha256(value);
  if (!digest)
    return std::nullopt;
  constexpr std::string_view alphabet = "0123456789abcdef";
  std::string result;
  result.reserve(digest.value->size() * 2);
  for (const auto byte : *digest.value) {
    const auto octet = std::to_integer<unsigned int>(byte);
    result.push_back(alphabet[octet >> 4U]);
    result.push_back(alphabet[octet & 0x0fU]);
  }
  return result;
}

} // namespace

VerifiedRuntimeRepositoryPlan::VerifiedRuntimeRepositoryPlan(
    RuntimeRepositoryUpdatePlan plan, std::string target_generation,
    const std::uint64_t sequence, const std::uint64_t not_before_unix,
    const std::uint64_t expires_unix, std::string payload_sha256) noexcept
    : plan_(std::move(plan)), target_generation_(std::move(target_generation)),
      sequence_(sequence), not_before_unix_(not_before_unix),
      expires_unix_(expires_unix), payload_sha256_(std::move(payload_sha256)) {}

RuntimeRepositoryUpdatePlan
VerifiedRuntimeRepositoryPlan::trusted_plan() const {
  return plan_;
}

const std::string &
VerifiedRuntimeRepositoryPlan::target_generation() const noexcept {
  return target_generation_;
}

std::uint64_t VerifiedRuntimeRepositoryPlan::sequence() const noexcept {
  return sequence_;
}

std::uint64_t VerifiedRuntimeRepositoryPlan::not_before_unix() const noexcept {
  return not_before_unix_;
}

std::uint64_t VerifiedRuntimeRepositoryPlan::expires_unix() const noexcept {
  return expires_unix_;
}

const std::string &
VerifiedRuntimeRepositoryPlan::payload_sha256() const noexcept {
  return payload_sha256_;
}

std::string_view runtime_repository_trusted_plan_error_name(
    const RuntimeRepositoryTrustedPlanError error) noexcept {
  using enum RuntimeRepositoryTrustedPlanError;
  switch (error) {
  case none:
    return "none";
  case invalid_limits:
    return "invalid_limits";
  case invalid_public_key:
    return "invalid_public_key";
  case invalid_state:
    return "invalid_state";
  case invalid_envelope:
    return "invalid_envelope";
  case invalid_signature:
    return "invalid_signature";
  case invalid_payload:
    return "invalid_payload";
  case noncanonical_payload:
    return "noncanonical_payload";
  case not_yet_valid:
    return "not_yet_valid";
  case expired:
    return "expired";
  case replay_rejected:
    return "replay_rejected";
  case bootstrap_rejected:
    return "bootstrap_rejected";
  case resource_exhausted:
    return "resource_exhausted";
  case internal_error:
    return "internal_error";
  }
  return "internal_error";
}

RuntimeRepositoryTrustedPlanVerifier::RuntimeRepositoryTrustedPlanVerifier(
    const std::span<const std::byte> trusted_public_key,
    std::shared_ptr<const RuntimeRepositoryTrustedStateProvider> state_provider,
    const RuntimeRepositoryTrustedPlanLimits limits)
    : trusted_public_key_(trusted_public_key.begin(), trusted_public_key.end()),
      state_provider_(std::move(state_provider)), limits_(limits) {}

RuntimeRepositoryTrustedPlanResult RuntimeRepositoryTrustedPlanVerifier::verify(
    const std::string_view envelope) const noexcept {
  try {
    if (!state_provider_)
      return fail(RuntimeRepositoryTrustedPlanError::invalid_state);
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    if (now < 0)
      return fail(RuntimeRepositoryTrustedPlanError::internal_error);
    return verify_at(envelope, state_provider_->trusted_state(),
                     static_cast<std::uint64_t>(now));
  } catch (const std::bad_alloc &) {
    return fail(RuntimeRepositoryTrustedPlanError::resource_exhausted);
  } catch (...) {
    return fail(RuntimeRepositoryTrustedPlanError::internal_error);
  }
}

RuntimeRepositoryTrustedPlanResult
RuntimeRepositoryTrustedPlanVerifier::verify_at(
    const std::string_view envelope,
    const std::optional<RuntimeRepositoryTrustedState> current_state,
    const std::uint64_t now_unix) const noexcept {
  using enum RuntimeRepositoryTrustedPlanError;
  if (!valid_limits(limits_))
    return fail(invalid_limits);
  if (trusted_public_key_.size() != auth::ed25519_public_key_bytes) {
    return fail(invalid_public_key);
  }
  if (current_state && (!lower_hex(current_state->generation, 64) ||
                        current_state->sequence == 0 ||
                        !lower_hex(current_state->payload_sha256, 64))) {
    return fail(invalid_state);
  }
  try {
    const auto parsed_envelope = adapters::bounded_json::parse_json(
        envelope, {limits_.max_envelope_bytes, limits_.max_json_depth,
                   limits_.max_json_nodes});
    if (!parsed_envelope ||
        !exact_fields(*parsed_envelope, {"schema", "payload", "signature"}) ||
        !parsed_envelope->at("schema").is_string() ||
        parsed_envelope->at("schema").get_ref<const std::string &>() !=
            runtime_repository_plan_envelope_schema ||
        !parsed_envelope->at("payload").is_string() ||
        !parsed_envelope->at("signature").is_string()) {
      return fail(invalid_envelope);
    }

    const auto payload = auth::decode_base64url_canonical(
        parsed_envelope->at("payload").get_ref<const std::string &>());
    if (!payload || payload.value->size() > limits_.max_payload_bytes) {
      return fail(invalid_envelope);
    }
    const auto signature = auth::decode_base64url_canonical(
        parsed_envelope->at("signature").get_ref<const std::string &>(),
        auth::ed25519_signature_bytes);
    if (!signature)
      return fail(invalid_envelope);
    const auto message = signature_message(*payload.value);
    if (auth::ed25519_verify(trusted_public_key_, message, *signature.value) !=
        auth::CryptoError::none) {
      return fail(invalid_signature);
    }
    const auto payload_hash = sha256_hex(*payload.value);
    if (!payload_hash)
      return fail(internal_error);

    const std::string payload_text{
        reinterpret_cast<const char *>(payload.value->data()),
        payload.value->size()};
    const auto parsed_payload = adapters::bounded_json::parse_json(
        payload_text, {limits_.max_payload_bytes, limits_.max_json_depth,
                       limits_.max_json_nodes});
    if (!parsed_payload)
      return fail(invalid_payload);
    if (parsed_payload->dump() != payload_text) {
      return fail(noncanonical_payload);
    }
    if (!exact_fields(*parsed_payload,
                      {"schema", "sequence", "not_before_unix", "expires_unix",
                       "allow_bootstrap", "previous_generations",
                       "repositories"}) ||
        !parsed_payload->at("schema").is_string() ||
        parsed_payload->at("schema").get_ref<const std::string &>() !=
            runtime_repository_plan_payload_schema ||
        !parsed_payload->at("allow_bootstrap").is_boolean() ||
        !parsed_payload->at("previous_generations").is_array() ||
        !parsed_payload->at("repositories").is_array() ||
        parsed_payload->at("repositories").size() != 2) {
      return fail(invalid_payload);
    }

    const auto sequence = canonical_decimal(parsed_payload->at("sequence"));
    const auto not_before =
        canonical_decimal(parsed_payload->at("not_before_unix"));
    const auto expires = canonical_decimal(parsed_payload->at("expires_unix"));
    if (!sequence || *sequence == 0 || !not_before || !expires ||
        *expires < *not_before ||
        *expires - *not_before > limits_.max_validity_seconds) {
      return fail(invalid_payload);
    }
    if (now_unix < *not_before)
      return fail(not_yet_valid);
    if (now_unix > *expires)
      return fail(expired);

    RuntimeRepositoryUpdatePlan update_plan;
    const auto resources = parse_repository(
        parsed_payload->at("repositories")[0], RuntimeRepositoryId::Resources);
    const auto scripts = parse_repository(parsed_payload->at("repositories")[1],
                                          RuntimeRepositoryId::Scripts);
    if (!resources || !scripts)
      return fail(invalid_payload);
    update_plan.repositories = {*resources, *scripts};
    const auto target = target_generation(update_plan);

    const auto &previous = parsed_payload->at("previous_generations");
    if (previous.size() > limits_.max_previous_generations) {
      return fail(invalid_payload);
    }
    std::unordered_set<std::string> predecessors;
    predecessors.reserve(previous.size());
    for (const auto &item : previous) {
      if (!item.is_string())
        return fail(invalid_payload);
      const auto generation = item.get<std::string>();
      if (!lower_hex(generation, 64) || generation == target ||
          !predecessors.insert(generation).second) {
        return fail(invalid_payload);
      }
    }

    if (!current_state) {
      if (!parsed_payload->at("allow_bootstrap").get<bool>()) {
        return fail(bootstrap_rejected);
      }
    } else if (*sequence < current_state->sequence) {
      return fail(replay_rejected);
    } else if (*sequence == current_state->sequence) {
      if (target != current_state->generation ||
          *payload_hash != current_state->payload_sha256) {
        return fail(replay_rejected);
      }
    } else if (current_state->generation != target &&
               !predecessors.contains(current_state->generation)) {
      return fail(replay_rejected);
    }

    auto verified = std::shared_ptr<const VerifiedRuntimeRepositoryPlan>{
        new VerifiedRuntimeRepositoryPlan{std::move(update_plan), target,
                                          *sequence, *not_before, *expires,
                                          *payload_hash}};
    return {std::move(verified), none};
  } catch (const std::bad_alloc &) {
    return fail(resource_exhausted);
  } catch (...) {
    return fail(internal_error);
  }
}

} // namespace baas::service::app
