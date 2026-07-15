#include "service/auth/AuthOwner.h"
#include "service/auth/CanonicalJson.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace auth = baas::service::auth;
namespace {

int failures = 0;

template <typename Condition>
void check(const Condition& condition, const std::string_view message)
{
    if (static_cast<bool>(condition)) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value) noexcept
{
    return std::as_bytes(std::span{value.data(), value.size()});
}

[[nodiscard]] auth::SecretBuffer secret(const std::string_view value)
{
    return auth::SecretBuffer{bytes(value)};
}

class MemoryStorage final : public auth::AuthStorage {
public:
    [[nodiscard]] auth::StorageReadResult read(
        const auth::AuthFile file, const std::size_t maximum_bytes) noexcept override
    {
        std::lock_guard lock(mutex);
        observed_read_limit = maximum_bytes;
        if (fail_reads) return {auth::StorageReadStatus::failure, {}};
        const auto found = files.find(file);
        if (found == files.end()) return {};
        if (found->second.size() > maximum_bytes) {
            return {auth::StorageReadStatus::failure, {}};
        }
        return {auth::StorageReadStatus::value, auth::SecretBuffer{found->second}};
    }

    [[nodiscard]] bool write_atomic(
        const auth::AuthFile file, const std::span<const std::byte> value) noexcept override
    {
        std::unique_lock lock(mutex);
        if (blocked_file && *blocked_file == file) {
            blocked_file.reset();
            blocked_write_entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return release_blocked_write; });
        }
        if (fail_writes || (fail_file && *fail_file == file)) return false;
        files[file] = auth::PublicBytes{value.begin(), value.end()};
        return true;
    }

    void block_next_write(const auth::AuthFile file)
    {
        std::lock_guard lock(mutex);
        blocked_file = file;
        blocked_write_entered = false;
        release_blocked_write = false;
    }

    void wait_for_blocked_write()
    {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return blocked_write_entered; });
    }

    void release_write()
    {
        std::lock_guard lock(mutex);
        release_blocked_write = true;
        changed.notify_all();
    }

    std::mutex mutex;
    std::condition_variable changed;
    std::unordered_map<auth::AuthFile, auth::PublicBytes> files;
    std::optional<auth::AuthFile> fail_file;
    std::optional<auth::AuthFile> blocked_file;
    bool fail_reads{};
    bool fail_writes{};
    bool blocked_write_entered{};
    bool release_blocked_write{};
    std::size_t observed_read_limit{};
};

class FakeClock final : public auth::AuthClock {
public:
    [[nodiscard]] std::int64_t now_unix_seconds() noexcept override { return now; }
    std::int64_t now{1'700'000'000};
};

class FakeRandom final : public auth::AuthRandom {
public:
    [[nodiscard]] bool fill(const std::span<std::byte> output) noexcept override
    {
        std::lock_guard lock(mutex);
        if (fail) return false;
        for (auto& item : output) item = static_cast<std::byte>(next++);
        return true;
    }
    std::mutex mutex;
    unsigned int next{1};
    bool fail{};
};

[[nodiscard]] auth::SecretBytesResult fake_derive(
    const std::span<const std::byte> password,
    const std::span<const std::byte> salt) noexcept
{
    try {
        auth::SecretBuffer input{password.size() + salt.size()};
        std::copy(password.begin(), password.end(), input.mutable_bytes().begin());
        std::copy(salt.begin(), salt.end(), input.mutable_bytes().begin() + password.size());
        auto digest = auth::sha256(input.bytes());
        if (!digest) return {std::nullopt, digest.error};
        return {std::optional<auth::SecretBuffer>{std::in_place, *digest.value},
                auth::CryptoError::none};
    }
    catch (...) {
        return {std::nullopt, auth::CryptoError::resource_exhausted};
    }
}

class FakeDeriver : public auth::PasswordDeriver {
public:
    [[nodiscard]] auth::SecretBytesResult derive(
        const std::span<const std::byte> password,
        const std::span<const std::byte> salt) noexcept override
    {
        return fake_derive(password, salt);
    }
};

class BlockingDeriver final : public auth::PasswordDeriver {
public:
    [[nodiscard]] auth::SecretBytesResult derive(
        const std::span<const std::byte> password,
        const std::span<const std::byte> salt) noexcept override
    {
        {
            std::unique_lock lock(mutex);
            entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return released; });
        }
        return fake_derive(password, salt);
    }

    void wait_entered()
    {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return entered; });
    }

    void release()
    {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }

    std::mutex mutex;
    std::condition_variable changed;
    bool entered{};
    bool released{};
};

class FirstBlockingDeriver final : public auth::PasswordDeriver {
public:
    [[nodiscard]] auth::SecretBytesResult derive(
        const std::span<const std::byte> password,
        const std::span<const std::byte> salt) noexcept override
    {
        {
            std::unique_lock lock(mutex);
            ++calls;
            if (calls == 1) {
                first_entered = true;
                changed.notify_all();
                changed.wait(lock, [&] { return first_released; });
            }
        }
        return fake_derive(password, salt);
    }

    void wait_first()
    {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return first_entered; });
    }

    void release_first()
    {
        std::lock_guard lock(mutex);
        first_released = true;
        changed.notify_all();
    }

private:
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t calls{};
    bool first_entered{};
    bool first_released{};
};

struct Fixture {
    std::shared_ptr<MemoryStorage> storage{std::make_shared<MemoryStorage>()};
    std::shared_ptr<FakeClock> clock{std::make_shared<FakeClock>()};
    std::shared_ptr<FakeRandom> random{std::make_shared<FakeRandom>()};
    std::shared_ptr<auth::PasswordDeriver> deriver{std::make_shared<FakeDeriver>()};
    std::shared_ptr<const auth::SecretBuffer> signing_override;
    std::shared_ptr<const auth::SecretBuffer> ticket_override;

    [[nodiscard]] auth::AuthResult<std::unique_ptr<auth::AuthOwner>> open(
        auth::AuthOwnerConfig config = {}) const
    {
        return auth::AuthOwner::open(
            config, {storage, clock, random, deriver,
                     signing_override, ticket_override});
    }
};

[[nodiscard]] auth::PublicBytes transcript(const std::string_view label)
{
    auto value = auth::sha256(bytes(label));
    if (!value) throw std::runtime_error("sha256 failed");
    return std::move(*value.value);
}

[[nodiscard]] auth::PublicBytes shared(const std::byte fill = std::byte{0x31})
{
    return auth::PublicBytes(auth::auth_key_bytes, fill);
}

[[nodiscard]] auth::HandshakeMaterial handshake(
    const auth::PublicBytes& shared_key,
    const auth::PublicBytes& transcript_hash)
{
    return {auth::SecretBuffer{shared_key}, transcript_hash};
}

[[nodiscard]] auth::SecretBuffer password_hash(
    const std::string_view password, const auth::PublicBytes& salt)
{
    auto value = fake_derive(bytes(password), salt);
    if (!value) throw std::runtime_error("fake password derivation failed");
    return std::move(*value.value);
}

[[nodiscard]] auth::PublicBytes password_proof(
    const auth::PublicBytes& shared_key,
    const auth::PublicBytes& transcript_hash,
    const auth::SecretBuffer& hash,
    const std::uint64_t epoch)
{
    const auto info = std::string{"auth-proof:"} + std::to_string(epoch);
    auto context = auth::hkdf_sha256(
        shared_key, transcript_hash, bytes(info), auth::auth_key_bytes);
    if (!context) throw std::runtime_error("auth context failed");
    auto proof = auth::hmac_sha256(hash.bytes(), context.value->bytes());
    if (!proof) throw std::runtime_error("auth proof failed");
    return std::move(*proof.value);
}

[[nodiscard]] auth::SecretBuffer resume_secret(
    const auth::PublicBytes& shared_key,
    const auth::PublicBytes& transcript_hash,
    const auth::SecretBuffer& hash)
{
    auth::SecretBuffer combined{shared_key.size() + hash.size()};
    std::copy(shared_key.begin(), shared_key.end(), combined.mutable_bytes().begin());
    std::copy(hash.bytes().begin(), hash.bytes().end(),
              combined.mutable_bytes().begin() + shared_key.size());
    auto master = auth::hkdf_sha256(
        combined.bytes(), transcript_hash, bytes("master-secret"), auth::auth_key_bytes);
    if (!master) throw std::runtime_error("master derivation failed");
    auto resume = auth::hkdf_sha256(
        master.value->bytes(), transcript_hash, bytes("resume-secret"), auth::auth_key_bytes);
    if (!resume) throw std::runtime_error("resume derivation failed");
    return std::move(*resume.value);
}

[[nodiscard]] auth::PublicBytes remember_proof(
    const std::string_view session_id, const std::uint64_t epoch,
    const auth::SecretBuffer& resume)
{
    auto payload = auth::encode_canonical_json_value(auth::CanonicalJsonValue{
        auth::CanonicalJsonValue::Object{
            {"type", auth::CanonicalJsonValue{std::string{"remember_session"}}},
            {"session_id", auth::CanonicalJsonValue{std::string{session_id}}},
            {"pwd_epoch", auth::CanonicalJsonValue{static_cast<std::int64_t>(epoch)}},
        }});
    if (!payload) throw std::runtime_error("remember payload failed");
    auto proof = auth::hmac_sha256(resume.bytes(), bytes(payload.text));
    if (!proof) throw std::runtime_error("remember proof failed");
    return std::move(*proof.value);
}

void test_open_initialization_and_restart()
{
    Fixture fixture;
    auto opened = fixture.open();
    check(opened, "fresh AuthOwner must open");
    auto owner = std::move(*opened.value);
    check(!owner->password_state().initialized, "fresh owner must be uninitialized");
    auto public_b64 = auth::encode_base64url_padded(owner->signing_public_key());
    check(public_b64 && *public_b64.value == "_GMKcfOCE-0_erXPJQRQv6mLiNBnT3tdHmAaXwWRis4=",
          "compatibility signing policy must retain Tauri pinned identity");
    check(fixture.storage->observed_read_limit == 1U * 1'024U * 1'024U,
          "storage reads must receive the configured pre-allocation limit");

    check(owner->initialize_password(secret("   ")).error == auth::AuthError::invalid_password,
          "whitespace-only password must fail");
    check(owner->initialize_password(secret("\xE3\x80\x80")).error
              == auth::AuthError::invalid_password,
          "Python-compatible Unicode whitespace-only password must fail");
    check(owner->initialize_password(secret("correct horse battery staple")),
          "valid password initialization must persist");
    check(owner->password_state().pwd_epoch == 1, "initial password epoch must be one");
    check(owner->initialize_password(secret("again")).error == auth::AuthError::already_initialized,
          "initialization must be one-shot");
    owner.reset();

    auto restarted = fixture.open();
    check(restarted && (*restarted.value)->password_state().initialized,
          "password state and keys must load across restart");
    check((*restarted.value)->password_state().pwd_epoch == 1,
          "password epoch must survive restart");
}

void test_atomic_initialize_control_and_handshake_bounds()
{
    Fixture fixture;
    auto opened = fixture.open();
    auto owner = std::move(*opened.value);
    const auto key = shared(std::byte{0x24});
    const auto hash = transcript("initialize-control");
    const auth::PublicBytes empty;
    check(owner->initialize_control(
              handshake(empty, hash), secret("must-not-commit")).error
              == auth::AuthError::invalid_argument
              && !owner->password_state().initialized,
          "invalid initialization handshake must not commit password state");
    auto initialized = owner->initialize_control(
        handshake(key, hash), secret("first-password"));
    check(initialized && owner->active_session_count() == 1,
          "protocol initialization must atomically issue its proof-free session");
    check(owner->initialize_control(
              handshake(key, transcript("second-initialize")), secret("other")).error
              == auth::AuthError::already_initialized,
          "proof-free session issuance must not be reusable after initialization");

    const auth::PublicBytes fake_proof(auth::hmac_sha256_bytes, std::byte{0});
    check(owner->authenticate_control(handshake(empty, hash), fake_proof).error
              == auth::AuthError::invalid_argument,
          "AuthOwner must reject non-X25519 shared-key widths at its boundary");
    check(owner->authenticate_control(handshake(key, empty), fake_proof).error
              == auth::AuthError::invalid_argument,
          "AuthOwner must reject non-SHA256 transcript widths at its boundary");
}

void test_initialize_control_epoch_race()
{
    Fixture fixture;
    auto deriver = std::make_shared<FirstBlockingDeriver>();
    fixture.deriver = deriver;
    auth::AuthOwnerConfig config;
    config.max_concurrent_password_derivations = 2;
    auto opened = fixture.open(config);
    auto owner = std::move(*opened.value);
    auth::AuthResult<auth::ControlSessionMaterial> initialization;
    std::thread first([&] {
        initialization = owner->initialize_control(
            handshake(shared(), transcript("initialization-race")),
            secret("initial-password"));
    });
    deriver->wait_first();
    const auto reset = owner->reset_password(secret("winning-reset"));
    check(reset && owner->password_state().pwd_epoch == 1,
          "concurrent reset must be able to win before initialization commits");
    deriver->release_first();
    first.join();
    check(initialization.error == auth::AuthError::already_initialized
              && owner->active_session_count() == 0,
          "losing initialization must not gain a proof-free session for the winning epoch");
}

void test_signing_compatibility_and_override()
{
    Fixture fixture;
    auto first = fixture.open();
    check(first, "signing compatibility fixture must open");
    first.value->reset();
    fixture.storage->files[auth::AuthFile::signing_key] =
        auth::PublicBytes(auth::ed25519_seed_bytes, std::byte{0xA5});
    auto repaired = fixture.open();
    auto repaired_b64 = auth::encode_base64url_padded(
        (*repaired.value)->signing_public_key());
    check(repaired_b64
              && *repaired_b64.value == "_GMKcfOCE-0_erXPJQRQv6mLiNBnT3tdHmAaXwWRis4=",
          "fixed compatibility policy must repair a mismatched legacy signing file");
    repaired.value->reset();

    auth::PublicBytes custom_seed(auth::ed25519_seed_bytes, std::byte{0x19});
    auth::PublicBytes custom_ticket(auth::auth_key_bytes, std::byte{0x42});
    fixture.signing_override =
        std::make_shared<const auth::SecretBuffer>(custom_seed);
    fixture.ticket_override =
        std::make_shared<const auth::SecretBuffer>(custom_ticket);
    auto overridden = fixture.open();
    auto expected = auth::ed25519_public_key_from_seed(custom_seed);
    check(overridden && expected
              && auth::constant_time_equal(
                  (*overridden.value)->signing_public_key(), *expected.value),
          "explicit signing override must take precedence over compatibility storage");
    auto initialized = (*overridden.value)->initialize_control(
        handshake(shared(), transcript("override-ticket")), secret("override-password"));
    check(initialized, "ticket override fixture must initialize a control session");
    if (!initialized) return;
    const auto ticket_text = std::string_view{
        reinterpret_cast<const char*>(initialized->resume_ticket.bytes().data()),
        initialized->resume_ticket.size()};
    const auto dot = ticket_text.find('.');
    auto payload = auth::decode_base64url_canonical(ticket_text.substr(0, dot));
    auto signature = auth::decode_base64url_canonical(
        ticket_text.substr(dot + 1), auth::hmac_sha256_bytes);
    check(payload && signature, "override ticket must retain canonical two-part encoding");
    if (!payload || !signature) return;
    auto expected_signature = auth::hmac_sha256(custom_ticket, *payload.value);
    check(expected_signature
              && auth::constant_time_equal(*signature.value, *expected_signature.value),
          "explicit ticket-key override must sign newly issued resume tickets");
}

void test_python_environment_signing_override()
{
    auth::PublicBytes custom_seed(auth::ed25519_seed_bytes, std::byte{0x2A});
    auto encoded = auth::encode_base64url_padded(custom_seed);
#if defined(_WIN32)
    char* previous_raw{};
    std::size_t previous_size{};
    static_cast<void>(_dupenv_s(
        &previous_raw, &previous_size, "BAAS_SERVICE_SIGN_SEED_B64"));
    const std::optional<std::string> previous = previous_raw == nullptr
        ? std::nullopt : std::optional<std::string>{previous_raw};
    std::free(previous_raw);
    _putenv_s("BAAS_SERVICE_SIGN_SEED_B64", encoded.value->c_str());
#else
    const char* previous_raw = std::getenv("BAAS_SERVICE_SIGN_SEED_B64");
    const std::optional<std::string> previous = previous_raw == nullptr
        ? std::nullopt : std::optional<std::string>{previous_raw};
    setenv("BAAS_SERVICE_SIGN_SEED_B64", encoded.value->c_str(), 1);
#endif
    Fixture fixture;
    auto opened = fixture.open();
#if defined(_WIN32)
    _putenv_s("BAAS_SERVICE_SIGN_SEED_B64", previous ? previous->c_str() : "");
#else
    if (previous) setenv("BAAS_SERVICE_SIGN_SEED_B64", previous->c_str(), 1);
    else unsetenv("BAAS_SERVICE_SIGN_SEED_B64");
#endif
    auto expected = auth::ed25519_public_key_from_seed(custom_seed);
    check(opened && expected
              && auth::constant_time_equal(
                  (*opened.value)->signing_public_key(), *expected.value),
          "Python BAAS_SERVICE_SIGN_SEED_B64 must override persisted compatibility identity");
}

void test_auth_ticket_remember_resume_logout_and_revocation()
{
    Fixture fixture;
    auto opened = fixture.open();
    auto owner = std::move(*opened.value);
    check(owner->initialize_password(secret("pw-1")), "test password must initialize");
    const auto state = owner->password_state();
    auto hash = password_hash("pw-1", state.pwd_salt);
    const auto shared_key = shared();
    const auto transcript_hash = transcript("control-1");
    const auto proof = password_proof(shared_key, transcript_hash, hash, state.pwd_epoch);

    auto bad = proof;
    bad[0] ^= std::byte{1};
    check(owner->authenticate_control(handshake(shared_key, transcript_hash), bad).error
              == auth::AuthError::authentication_failed,
          "wrong password proof must fail without a session");
    auto session = owner->authenticate_control(
        handshake(shared_key, transcript_hash), proof);
    check(session && owner->active_session_count() == 1,
          "valid proof must open exactly one control session");
    check(session->control_server_tx.size() == auth::auth_key_bytes
              && session->control_server_rx.size() == auth::auth_key_bytes
              && !auth::constant_time_equal(
                  session->control_server_tx.bytes(), session->control_server_rx.bytes()),
          "control session must expose distinct directional keys");
    check(owner->verify_resume_ticket(
              session->session_id, session->resume_ticket.bytes()),
          "issued resume ticket must verify against its live session");
    auto tampered_ticket = auth::PublicBytes{
        session->resume_ticket.bytes().begin(), session->resume_ticket.bytes().end()};
    tampered_ticket.back() ^= std::byte{1};
    check(owner->verify_resume_ticket(session->session_id, tampered_ticket).error
              == auth::AuthError::invalid_resume_ticket,
          "tampered resume ticket must fail");

    auto resume = resume_secret(shared_key, transcript_hash, hash);
    auto remember = remember_proof(session->session_id, state.pwd_epoch, resume);
    auto token = owner->issue_remember_token(session->session_id, remember);
    check(token && owner->remembered_login_count() == 1,
          "valid remember proof must persist one bearer token");
    auth::SecretBuffer token_copy{token->token.bytes()};
    owner.reset();

    auto restarted = fixture.open();
    owner = std::move(*restarted.value);
    check(owner->remembered_login_count() == 1,
          "remembered login must survive owner restart");
    const auto resume_transcript = transcript("control-resume");
    auto resumed = owner->resume_control(
        handshake(shared(std::byte{0x52}), resume_transcript),
        auth::SecretBuffer{token_copy.bytes()});
    check(resumed && resumed->disclosed_master_secret && resumed->disclosed_resume_secret,
          "remember token must create fresh disclosed session secrets");
    check(owner->logout_remember_token(std::move(token_copy)),
          "logout must revoke the server-side remember token");
    check(owner->remembered_login_count() == 0,
          "logout must remove persisted remember state");
    check(owner->resume_control(
              handshake(shared(std::byte{0x53}), transcript("after-logout")),
              auth::SecretBuffer{token->token.bytes()}).error
              == auth::AuthError::invalid_remember_token,
          "revoked remember token must not resume");

    auto subscription = owner->subscribe_revocations(resumed->session_id);
    check(subscription, "live control session must allow bounded revocation subscription");
    check(owner->change_password(resumed->session_id, secret("pw-2")),
          "authenticated password rotation must succeed");
    auto events = owner->drain_revocations(*subscription.value, 4);
    check(events && events->size() == 1
              && events->front().reason == auth::RevocationReason::password_changed
              && events->front().pwd_epoch == 2,
          "password rotation must publish one terminal revocation event");
    check(owner->active_session_count() == 0 && owner->remembered_login_count() == 0,
          "password rotation must erase every old session and bearer token");
}

void test_remember_persistence_does_not_block_session_reads()
{
    using namespace std::chrono_literals;
    Fixture fixture;
    auto opened = fixture.open();
    auto owner = std::move(*opened.value);
    check(owner->initialize_password(secret("persistence-concurrency")),
          "persistence concurrency fixture must initialize");
    const auto state = owner->password_state();
    auto hash = password_hash("persistence-concurrency", state.pwd_salt);
    const auto key = shared(std::byte{0x3A});
    const auto transcript_hash = transcript("persistence-concurrency");
    const auto proof = password_proof(key, transcript_hash, hash, state.pwd_epoch);
    auto session = owner->authenticate_control(handshake(key, transcript_hash), proof);
    check(session, "persistence concurrency fixture must authenticate");
    if (!session) return;
    auto remember = remember_proof(session->session_id, state.pwd_epoch,
                                   resume_secret(key, transcript_hash, hash));

    fixture.storage->block_next_write(auth::AuthFile::remembered_logins);
    auth::AuthResult<auth::RememberTokenMaterial> issued;
    std::thread issuer([&] {
        issued = owner->issue_remember_token(session->session_id, remember);
    });
    fixture.storage->wait_for_blocked_write();
    auto issue_reader = std::async(std::launch::async, [&] {
        return owner->active_session_count();
    });
    check(issue_reader.wait_for(500ms) == std::future_status::ready,
          "remember-token disk write must not hold the global session lock");
    fixture.storage->release_write();
    issuer.join();
    check(issue_reader.get() == 1 && issued,
          "remember-token issuance must commit after the blocked write resumes");
    if (!issued) return;

    fixture.storage->block_next_write(auth::AuthFile::remembered_logins);
    auth::AuthError logout_error = auth::AuthError::crypto_failure;
    std::thread logout([&] {
        logout_error = owner->logout_remember_token(
            auth::SecretBuffer{issued->token.bytes()}).error;
    });
    fixture.storage->wait_for_blocked_write();
    auto logout_reader = std::async(std::launch::async, [&] {
        return owner->active_session_count();
    });
    check(logout_reader.wait_for(500ms) == std::future_status::ready,
          "remember-token logout disk write must not hold the global session lock");
    fixture.storage->release_write();
    logout.join();
    check(logout_reader.get() == 1 && logout_error == auth::AuthError::none
              && owner->remembered_login_count() == 0,
          "remember-token logout must commit after the blocked write resumes");
}

void test_capacity_expiry_and_storage_failures()
{
    Fixture fixture;
    auth::AuthOwnerConfig config;
    config.max_password_utf8_bytes = 8;
    config.max_sessions = 1;
    config.session_ttl_seconds = 10;
    auto opened = fixture.open(config);
    auto owner = std::move(*opened.value);
    check(owner->initialize_password(secret("012345678")).error
              == auth::AuthError::password_too_large,
          "password byte limit must precede Argon2 work");
    check(owner->initialize_password(secret("pw")), "bounded password must initialize");
    const auto state = owner->password_state();
    auto hash = password_hash("pw", state.pwd_salt);
    const auto key = shared();
    const auto transcript_one = transcript("one");
    auto proof_one = password_proof(key, transcript_one, hash, state.pwd_epoch);
    auto first = owner->authenticate_control(handshake(key, transcript_one), proof_one);
    check(first, "first bounded session must open");
    const auto transcript_two = transcript("two");
    auto proof_two = password_proof(key, transcript_two, hash, state.pwd_epoch);
    check(owner->authenticate_control(handshake(key, transcript_two), proof_two).error
              == auth::AuthError::capacity_exceeded,
          "live session map must enforce its configured cap");
    fixture.clock->now += 11;
    check(owner->active_session_count() == 0,
          "expired sessions must be pruned and secrets destroyed");
    auto second = owner->authenticate_control(handshake(key, transcript_two), proof_two);
    check(second, "capacity must recover after expiry pruning");

    fixture.storage->fail_file = auth::AuthFile::remembered_logins;
    const auto before = owner->password_state().pwd_epoch;
    check(owner->change_password(second->session_id, secret("next")).error
              == auth::AuthError::storage_failure,
          "password rotation must fail closed when persistence fails");
    check(owner->password_state().pwd_epoch == before && owner->active_session_count() == 1,
          "failed rotation must not mutate live password/session state");
}

void test_argon_gate_and_corrupted_storage()
{
    Fixture fixture;
    auto blocker = std::make_shared<BlockingDeriver>();
    fixture.deriver = blocker;
    auto opened = fixture.open();
    auto owner = std::move(*opened.value);
    auth::AuthStatus first;
    std::thread worker([&] { first = owner->initialize_password(secret("first")); });
    blocker->wait_entered();
    const auto second = owner->initialize_password(secret("second"));
    check(second.error == auth::AuthError::password_derivation_busy,
          "Argon2 concurrency gate must reject excess work without blocking state lock");
    check(!owner->password_state().initialized,
          "owner state must remain readable while password derivation is blocked");
    blocker->release();
    worker.join();
    check(first && owner->password_state().initialized,
          "accepted derivation must commit after the gate releases");

    Fixture corrupt;
    const auto malformed = bytes("{}");
    corrupt.storage->files[auth::AuthFile::password_state] =
        auth::PublicBytes{malformed.begin(), malformed.end()};
    check(corrupt.open().error == auth::AuthError::corrupted_storage,
          "malformed persisted password state must fail closed");

    Fixture oversized;
    auth::AuthOwnerConfig tiny;
    tiny.max_file_bytes = 4;
    oversized.storage->files[auth::AuthFile::password_state] = auth::PublicBytes(5, std::byte{0});
    check(oversized.open(tiny).error == auth::AuthError::storage_failure,
          "oversized persisted input must be rejected at the storage boundary");
}

void test_python_float_timestamp_migration()
{
    Fixture fixture;
    auto opened = fixture.open();
    auto owner = std::move(*opened.value);
    check(owner->initialize_password(secret("migration")),
          "migration fixture password must initialize");
    owner.reset();

    const auth::PublicBytes zero_hash(auth::hmac_sha256_bytes, std::byte{0});
    auto encoded_hash = auth::encode_base64url_padded(zero_hash);
    const std::string legacy =
        std::string{"{\"logins\":[{\"token_id\":\"0123456789abcdef0123456789abcdef\","}
        + "\"token_hash\":\"" + *encoded_hash.value + "\","
        + "\"created_at\":1699999999.75,\"expires_at\":1700001000.25,"
          "\"pwd_epoch\":1}]}";
    const auto legacy_bytes = bytes(legacy);
    fixture.storage->files[auth::AuthFile::remembered_logins] =
        auth::PublicBytes{legacy_bytes.begin(), legacy_bytes.end()};
    auto migrated = fixture.open();
    check(migrated && (*migrated.value)->remembered_login_count() == 1,
          "C++ owner must migrate Python fractional remembered-login timestamps");
}

void test_file_storage_atomic_and_bounded()
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("baas-auth-owner-" + unique);
    auto storage = auth::make_file_auth_storage(root);
    const auth::PublicBytes value{std::byte{1}, std::byte{2}, std::byte{3}};
    check(storage->write_atomic(auth::AuthFile::ticket_key, value),
          "production storage must atomically create config files");
#if !defined(_WIN32)
    const auto directory_permissions =
        std::filesystem::status(root / "config").permissions();
    check((directory_permissions
              & (std::filesystem::perms::group_all
                 | std::filesystem::perms::others_all))
              == std::filesystem::perms::none,
          "production config directory must be private to its effective owner");
#endif
    auto read = storage->read(auth::AuthFile::ticket_key, value.size());
    check(read.status == auth::StorageReadStatus::value
              && auth::constant_time_equal(read.bytes.bytes(), value),
          "production storage must round-trip exact bytes");
    check(storage->read(auth::AuthFile::ticket_key, value.size() - 1).status
              == auth::StorageReadStatus::failure,
          "production storage must reject by file_size before payload allocation");

    auto contender = auth::make_file_auth_storage(root);
    check(contender->read(auth::AuthFile::ticket_key, value.size()).status
              == auth::StorageReadStatus::failure,
          "a second process owner must fail while the installation lock is held");
    contender.reset();

    const auto victim = root / "victim.bin";
    {
        std::ofstream output(victim, std::ios::binary);
        output << "do-not-follow";
    }
    const auto linked = root / "config" / "service_auth.json";
    std::error_code symlink_error;
    std::filesystem::create_symlink(victim, linked, symlink_error);
    if (!symlink_error) {
        check(storage->read(auth::AuthFile::password_state, 64).status
                  == auth::StorageReadStatus::failure,
              "production reads must reject a target symbolic link");
    }
    storage.reset();
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    const auto real = std::filesystem::temp_directory_path()
        / ("baas-auth-real-" + unique);
    const auto linked_root = std::filesystem::temp_directory_path()
        / ("baas-auth-link-" + unique);
    std::filesystem::create_directories(real, ignored);
    std::filesystem::create_directories(linked_root, ignored);
    std::error_code directory_link_error;
    std::filesystem::create_directory_symlink(
        real, linked_root / "config", directory_link_error);
    if (!directory_link_error) {
        auto linked_storage = auth::make_file_auth_storage(linked_root);
        check(!linked_storage->write_atomic(auth::AuthFile::ticket_key, value),
              "production storage must reject a symbolic-link config directory");
        linked_storage.reset();
    }
    std::filesystem::remove_all(linked_root, ignored);
    std::filesystem::remove_all(real, ignored);
}

void test_real_argon_owner_integration()
{
    Fixture fixture;
    fixture.deriver = auth::make_sodium_password_deriver();
    auto opened = fixture.open();
    auto owner = std::move(*opened.value);
    check(owner->initialize_password(secret("argon-owner-integration")),
          "production Argon2 deriver must initialize through AuthOwner");
    const auto state = owner->password_state();
    auto hash = auth::argon2id_v1(bytes("argon-owner-integration"), state.pwd_salt);
    check(hash, "production password verifier must be reproducible for proof construction");
    const auto key = shared(std::byte{0x61});
    const auto transcript_hash = transcript("argon-owner");
    const auto proof = password_proof(key, transcript_hash, *hash.value, state.pwd_epoch);
    check(owner->authenticate_control(handshake(key, transcript_hash), proof),
          "production Argon2 verifier must authenticate a control session");
}

}  // namespace

int main()
{
    try {
        test_open_initialization_and_restart();
        test_atomic_initialize_control_and_handshake_bounds();
        test_initialize_control_epoch_race();
        test_signing_compatibility_and_override();
        test_python_environment_signing_override();
        test_auth_ticket_remember_resume_logout_and_revocation();
        test_remember_persistence_does_not_block_session_reads();
        test_capacity_expiry_and_storage_failures();
        test_argon_gate_and_corrupted_storage();
        test_python_float_timestamp_migration();
        test_file_storage_atomic_and_bounded();
        test_real_argon_owner_integration();
    }
    catch (const std::exception& error) {
        std::cerr << "FAILED with exception: " << error.what() << '\n';
        ++failures;
    }
    if (failures != 0) {
        std::cerr << failures << " service auth owner test(s) failed\n";
        return 1;
    }
    std::cout << "service auth owner tests passed\n";
    return 0;
}
