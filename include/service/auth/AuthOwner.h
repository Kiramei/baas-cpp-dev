#pragma once

#include "service/auth/Crypto.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::auth {

inline constexpr std::int64_t default_session_ttl_seconds = 12 * 60 * 60;
inline constexpr std::int64_t default_remember_ttl_seconds = 180LL * 24 * 60 * 60;
inline constexpr std::size_t auth_key_bytes = 32;

enum class AuthError : std::uint8_t {
    none,
    invalid_argument,
    invalid_password,
    password_too_large,
    password_derivation_busy,
    password_derivation_failed,
    already_initialized,
    not_initialized,
    authentication_failed,
    unknown_session,
    session_expired,
    stale_epoch,
    invalid_resume_ticket,
    invalid_remember_proof,
    invalid_remember_token,
    capacity_exceeded,
    entropy_failure,
    storage_failure,
    corrupted_storage,
    crypto_failure,
    subscription_not_found,
};

[[nodiscard]] std::string_view auth_error_name(AuthError error) noexcept;

template <typename T>
struct AuthResult {
    std::optional<T> value;
    AuthError error{AuthError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == AuthError::none && value.has_value();
    }

    [[nodiscard]] T* operator->() noexcept { return std::addressof(*value); }
    [[nodiscard]] const T* operator->() const noexcept { return std::addressof(*value); }
    [[nodiscard]] T& operator*() & noexcept { return *value; }
    [[nodiscard]] const T& operator*() const& noexcept { return *value; }
};

struct AuthStatus {
    AuthError error{AuthError::none};
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == AuthError::none;
    }
};

enum class AuthFile : std::uint8_t {
    password_state,
    ticket_key,
    remembered_logins,
    signing_key,
};

enum class StorageReadStatus : std::uint8_t { missing, value, failure };

struct StorageReadResult {
    StorageReadStatus status{StorageReadStatus::missing};
    SecretBuffer bytes;
};

// Storage keys are an enum rather than caller-controlled paths. The production
// implementation maps them exclusively below project_root/config and uses a
// same-directory atomic replacement for every write.
class AuthStorage {
public:
    virtual ~AuthStorage() = default;
    // The implementation must reject an oversized file before allocating or
    // reading its payload. This is part of the persistence DoS boundary.
    [[nodiscard]] virtual StorageReadResult read(
        AuthFile file, std::size_t maximum_bytes) noexcept = 0;
    [[nodiscard]] virtual bool write_atomic(
        AuthFile file, std::span<const std::byte> bytes) noexcept = 0;
};

class AuthClock {
public:
    virtual ~AuthClock() = default;
    [[nodiscard]] virtual std::int64_t now_unix_seconds() noexcept = 0;
};

class AuthRandom {
public:
    virtual ~AuthRandom() = default;
    [[nodiscard]] virtual bool fill(std::span<std::byte> output) noexcept = 0;
};

class PasswordDeriver {
public:
    virtual ~PasswordDeriver() = default;
    [[nodiscard]] virtual SecretBytesResult derive(
        std::span<const std::byte> password,
        std::span<const std::byte> salt) noexcept = 0;
};

[[nodiscard]] std::shared_ptr<AuthStorage> make_file_auth_storage(
    std::filesystem::path project_root);
[[nodiscard]] std::shared_ptr<AuthClock> make_system_auth_clock();
[[nodiscard]] std::shared_ptr<AuthRandom> make_system_auth_random();
[[nodiscard]] std::shared_ptr<PasswordDeriver> make_sodium_password_deriver();

// The Python/Tauri identity is intentionally a compatibility policy, not an
// accidental fallback. New deployments may select random_per_install; tests
// and embedders can inject deterministic randomness or preseed storage.
enum class SigningSeedPolicy : std::uint8_t {
    python_tauri_fixed_compatibility,
    random_per_install,
};

struct AuthOwnerConfig {
    std::int64_t session_ttl_seconds{default_session_ttl_seconds};
    std::int64_t remember_ttl_seconds{default_remember_ttl_seconds};
    std::size_t max_password_utf8_bytes{1'024};
    std::size_t max_sessions{1'024};
    std::size_t max_remembered_logins{1'024};
    std::size_t max_subscriptions{1'024};
    std::size_t max_revocations_per_subscription{4};
    std::size_t max_file_bytes{1U * 1'024U * 1'024U};
    std::size_t max_token_bytes{512};
    std::size_t max_identifier_bytes{64};
    std::size_t max_concurrent_password_derivations{1};
    SigningSeedPolicy signing_seed_policy{
        SigningSeedPolicy::python_tauri_fixed_compatibility};
};

struct AuthDependencies {
    std::shared_ptr<AuthStorage> storage;
    std::shared_ptr<AuthClock> clock;
    std::shared_ptr<AuthRandom> random;
    std::shared_ptr<PasswordDeriver> password_deriver;
    // Explicit injection is preferred. When absent, AuthOwner also honors the
    // Python-compatible BAAS_SERVICE_SIGN_SEED_B64 and
    // BAAS_SERVICE_TICKET_KEY_B64 environment variables.
    std::shared_ptr<const SecretBuffer> signing_seed_override;
    std::shared_ptr<const SecretBuffer> ticket_key_override;
};

struct PasswordPublicState {
    bool initialized{};
    std::uint64_t pwd_epoch{};
    PublicBytes pwd_salt;
};

struct HandshakeMaterial {
    SecretBuffer shared_key;
    PublicBytes transcript_hash;

    HandshakeMaterial(SecretBuffer shared, PublicBytes transcript)
        : shared_key(std::move(shared)), transcript_hash(std::move(transcript))
    {}
    HandshakeMaterial(const HandshakeMaterial&) = delete;
    HandshakeMaterial& operator=(const HandshakeMaterial&) = delete;
    HandshakeMaterial(HandshakeMaterial&&) noexcept = default;
    HandshakeMaterial& operator=(HandshakeMaterial&&) noexcept = default;
};

struct ControlSessionMaterial {
    std::string session_id;
    std::int64_t expires_at{};
    std::uint64_t pwd_epoch{};
    SecretBuffer resume_ticket;
    SecretBuffer control_server_tx;
    SecretBuffer control_server_rx;
    // Only remember-cookie resume discloses fresh session secrets to the
    // already authenticated client, matching the Python preauth envelope.
    std::optional<SecretBuffer> disclosed_master_secret;
    std::optional<SecretBuffer> disclosed_resume_secret;
};

struct RememberTokenMaterial {
    SecretBuffer token;
    std::int64_t expires_at{};
};

enum class RevocationReason : std::uint8_t {
    password_changed,
    password_reset,
};

struct RevocationEvent {
    std::string session_id;
    RevocationReason reason{RevocationReason::password_changed};
    std::uint64_t pwd_epoch{};
};

using SubscriptionId = std::uint64_t;

class AuthOwner final {
public:
    [[nodiscard]] static AuthResult<std::unique_ptr<AuthOwner>> open(
        AuthOwnerConfig config,
        AuthDependencies dependencies) noexcept;

    ~AuthOwner();
    AuthOwner(const AuthOwner&) = delete;
    AuthOwner& operator=(const AuthOwner&) = delete;
    AuthOwner(AuthOwner&&) = delete;
    AuthOwner& operator=(AuthOwner&&) = delete;

    [[nodiscard]] PasswordPublicState password_state() const;
    [[nodiscard]] PublicBytes signing_public_key() const;

    [[nodiscard]] AuthStatus initialize_password(SecretBuffer password) noexcept;
    [[nodiscard]] AuthResult<ControlSessionMaterial> initialize_control(
        HandshakeMaterial handshake,
        SecretBuffer password) noexcept;
    [[nodiscard]] AuthStatus change_password(
        std::string_view session_id, SecretBuffer password) noexcept;
    [[nodiscard]] AuthStatus reset_password(SecretBuffer password) noexcept;

    [[nodiscard]] AuthResult<ControlSessionMaterial> authenticate_control(
        HandshakeMaterial handshake,
        std::span<const std::byte> proof) noexcept;
    [[nodiscard]] AuthResult<ControlSessionMaterial> resume_control(
        HandshakeMaterial handshake,
        SecretBuffer remember_token) noexcept;

    [[nodiscard]] AuthResult<RememberTokenMaterial> issue_remember_token(
        std::string_view session_id,
        std::span<const std::byte> proof) noexcept;
    [[nodiscard]] AuthStatus logout_remember_token(SecretBuffer token) noexcept;
    [[nodiscard]] AuthStatus verify_resume_ticket(
        std::string_view session_id,
        std::span<const std::byte> ticket) noexcept;

    void close_session(std::string_view session_id) noexcept;
    [[nodiscard]] std::size_t active_session_count() const noexcept;
    [[nodiscard]] std::size_t remembered_login_count() const noexcept;

    [[nodiscard]] AuthResult<SubscriptionId> subscribe_revocations(
        std::string_view session_id) noexcept;
    [[nodiscard]] AuthResult<std::vector<RevocationEvent>> drain_revocations(
        SubscriptionId subscription,
        std::size_t maximum) noexcept;
    void unsubscribe_revocations(SubscriptionId subscription) noexcept;

private:
    class Impl;
    explicit AuthOwner(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace baas::service::auth
