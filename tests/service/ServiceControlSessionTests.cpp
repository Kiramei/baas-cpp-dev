#include "service/auth/CanonicalJson.h"
#include "service/auth/SecureEnvelope.h"
#include "service/websocket/ControlSessionFactory.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

namespace auth = baas::service::auth;
namespace ws = baas::service::websocket;
namespace {

int failures = 0;

template <typename Condition>
void check(const Condition& condition, const std::string_view message)
{
    if (static_cast<bool>(condition)) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

void require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string{message});
}

[[nodiscard]] std::span<const std::byte> bytes(
    const std::string_view value) noexcept
{
    return std::as_bytes(std::span{value.data(), value.size()});
}

[[nodiscard]] std::string text(const std::span<const std::byte> value)
{
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] auth::CanonicalJsonValue json_string(std::string value)
{
    return auth::CanonicalJsonValue{std::move(value)};
}

[[nodiscard]] std::string b64(const std::span<const std::byte> value)
{
    auto encoded = auth::encode_base64url_padded(value);
    if (!encoded) throw std::runtime_error("base64 encoding failed");
    return std::move(*encoded.value);
}

[[nodiscard]] const auth::CanonicalJsonValue& member(
    const auth::CanonicalJsonValue& value, const std::string_view name)
{
    const auto* found = value.find(name);
    if (found == nullptr) throw std::runtime_error("missing JSON member");
    return *found;
}

[[nodiscard]] const std::string& string_member(
    const auth::CanonicalJsonValue& value, const std::string_view name)
{
    const auto* result = member(value, name).as_string();
    if (result == nullptr) throw std::runtime_error("JSON member is not a string");
    return *result;
}

[[nodiscard]] std::int64_t integer_member(
    const auth::CanonicalJsonValue& value, const std::string_view name)
{
    const auto* result = member(value, name).as_integer();
    if (result == nullptr) throw std::runtime_error("JSON member is not an integer");
    return *result;
}

[[nodiscard]] std::string encode(const auth::CanonicalJsonValue& value)
{
    auto encoded = auth::encode_canonical_json_value(value);
    if (!encoded) throw std::runtime_error("canonical encoding failed");
    return std::move(encoded.text);
}

class MemoryStorage final : public auth::AuthStorage {
public:
    [[nodiscard]] auth::StorageReadResult read(
        const auth::AuthFile file, const std::size_t maximum) noexcept override
    {
        std::lock_guard lock(mutex_);
        const auto found = files_.find(file);
        if (found == files_.end()) return {};
        if (found->second.size() > maximum) {
            return {auth::StorageReadStatus::failure, {}};
        }
        return {auth::StorageReadStatus::value, auth::SecretBuffer{found->second}};
    }

    [[nodiscard]] bool write_atomic(
        const auth::AuthFile file,
        const std::span<const std::byte> value) noexcept override
    {
        try {
            std::lock_guard lock(mutex_);
            files_[file] = auth::PublicBytes{value.begin(), value.end()};
            return true;
        }
        catch (...) {
            return false;
        }
    }

private:
    std::mutex mutex_;
    std::unordered_map<auth::AuthFile, auth::PublicBytes> files_;
};

class FakeClock final : public auth::AuthClock {
public:
    [[nodiscard]] std::int64_t now_unix_seconds() noexcept override
    {
        return now.load();
    }
    std::atomic<std::int64_t> now{1'700'000'000};
};

class FakeRandom final : public auth::AuthRandom {
public:
    [[nodiscard]] bool fill(const std::span<std::byte> output) noexcept override
    {
        std::lock_guard lock(mutex_);
        if (fail_) return false;
        for (auto& item : output) item = static_cast<std::byte>(next_++);
        return true;
    }
    void fail(const bool value) noexcept
    {
        std::lock_guard lock(mutex_);
        fail_ = value;
    }
private:
    std::mutex mutex_;
    unsigned int next_{1};
    bool fail_{};
};

[[nodiscard]] auth::SecretBytesResult fake_derive(
    const std::span<const std::byte> password,
    const std::span<const std::byte> salt) noexcept
{
    try {
        auth::SecretBuffer combined{password.size() + salt.size()};
        std::copy(password.begin(), password.end(), combined.mutable_bytes().begin());
        std::copy(salt.begin(), salt.end(),
                  combined.mutable_bytes().begin() + password.size());
        auto digest = auth::sha256(combined.bytes());
        if (!digest) return {std::nullopt, digest.error};
        return {
            std::optional<auth::SecretBuffer>{
                std::in_place, std::span<const std::byte>{*digest.value}},
            auth::CryptoError::none};
    }
    catch (...) {
        return {std::nullopt, auth::CryptoError::resource_exhausted};
    }
}

class FakeDeriver final : public auth::PasswordDeriver {
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
            std::unique_lock lock(mutex_);
            entered_ = true;
            changed_.notify_all();
            changed_.wait(lock, [&] { return released_; });
        }
        return fake_derive(password, salt);
    }

    void wait_entered()
    {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return entered_; });
    }

    void release()
    {
        std::lock_guard lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_{};
    bool released_{};
};

struct Fixture {
    std::shared_ptr<MemoryStorage> storage{std::make_shared<MemoryStorage>()};
    std::shared_ptr<FakeClock> clock{std::make_shared<FakeClock>()};
    std::shared_ptr<FakeRandom> random{std::make_shared<FakeRandom>()};
    std::shared_ptr<FakeDeriver> deriver{std::make_shared<FakeDeriver>()};
    std::shared_ptr<auth::AuthOwner> owner;

    explicit Fixture(auth::AuthOwnerConfig config = {})
    {
        auto opened = auth::AuthOwner::open(
            config, {storage, clock, random, deriver, {}, {}});
        require(static_cast<bool>(opened), "AuthOwner fixture failed to open");
        owner = std::shared_ptr<auth::AuthOwner>{std::move(*opened.value)};
    }
};

class Sink final : public ws::OutboundSink {
public:
    [[nodiscard]] ws::EnqueueResult enqueue(ws::OutboundBatch) override
    {
        return ws::EnqueueResult::accepted;
    }
    void terminate(ws::TerminalAction) noexcept override {}
};

[[nodiscard]] ws::RequestMetadata control_request(std::string cookie = {})
{
    ws::RequestMetadata request;
    request.channel = ws::Channel::control;
    request.path = "/ws/control";
    request.cookie = std::move(cookie);
    request.handshake_timeout = std::chrono::seconds{10};
    return request;
}

[[nodiscard]] std::string single_payload(const ws::DriverResult& result)
{
    require(result.batches.size() == 1, "expected one outbound batch");
    require(result.batches.front().frames.size() == 1, "expected one outbound frame");
    require(result.batches.front().frames.front().kind == ws::FrameKind::text,
            "expected text frame");
    return result.batches.front().frames.front().payload;
}

struct ClientHandshake {
    auth::SecretBuffer shared;
    auth::PublicBytes transcript_hash;
    auth::CanonicalJsonValue server_hello;
    std::optional<auth::SecureEnvelopeCipher> preauth;
};

[[nodiscard]] ClientHandshake perform_client_hello(
    ws::SessionDriver& driver,
    const std::shared_ptr<auth::AuthOwner>& owner,
    const std::byte private_fill)
{
    auth::SecretBuffer client_private{auth::x25519_key_bytes};
    std::fill(client_private.mutable_bytes().begin(),
              client_private.mutable_bytes().end(), private_fill);
    auto client_public = auth::x25519_public_key(client_private.bytes());
    require(static_cast<bool>(client_public), "client public key failed");
    auth::PublicBytes nonce(auth::x25519_key_bytes, std::byte{0x21});
    auth::CanonicalJsonValue client{auth::CanonicalJsonValue::Object{
        {"type", json_string("client_hello")},
        {"kind", json_string("control")},
        {"channel", json_string("control")},
        {"version", auth::CanonicalJsonValue{std::int64_t{1}}},
        {"timestamp", auth::CanonicalJsonValue{std::int64_t{1'700'000'000'123}}},
        {"client_nonce", json_string(b64(nonce))},
        {"client_kx_pub", json_string(b64(*client_public.value))},
    }};
    auto response = driver.input(
        {ws::FrameKind::text, encode(client)}, std::stop_token{});
    require(response.terminal == ws::TerminalAction::none
                && response.phase == ws::SessionPhase::handshaking,
            "server hello was rejected");
    auto parsed = auth::parse_canonical_json_value(single_payload(response));
    require(static_cast<bool>(parsed), "server hello JSON failed to parse");
    require(string_member(*parsed.value, "type") == "server_hello"
                && string_member(*parsed.value, "kind") == "control"
                && string_member(*parsed.value, "channel") == "control"
                && integer_member(*parsed.value, "version") == 1,
            "server hello identity fields mismatch");

    auth::CanonicalJsonValue server_core{auth::CanonicalJsonValue::Object{
        {"type", member(*parsed.value, "type")},
        {"kind", member(*parsed.value, "kind")},
        {"channel", member(*parsed.value, "channel")},
        {"version", member(*parsed.value, "version")},
        {"initialized", member(*parsed.value, "initialized")},
        {"pwd_epoch", member(*parsed.value, "pwd_epoch")},
        {"pwd_salt", member(*parsed.value, "pwd_salt")},
        {"argon2", member(*parsed.value, "argon2")},
        {"server_nonce", member(*parsed.value, "server_nonce")},
        {"server_kx_pub", member(*parsed.value, "server_kx_pub")},
    }};
    auth::CanonicalJsonValue transcript{auth::CanonicalJsonValue::Object{
        {"kind", json_string("control")},
        {"channel", json_string("control")},
        {"client", client},
        {"server", server_core},
    }};
    const auto transcript_text = encode(transcript);
    auto transcript_hash = auth::sha256(bytes(transcript_text));
    auto server_public = auth::decode_base64url_canonical(
        string_member(*parsed.value, "server_kx_pub"), auth::x25519_key_bytes);
    auto signature = auth::decode_base64url_canonical(
        string_member(*parsed.value, "signature"), auth::ed25519_signature_bytes);
    auto sign_public = auth::decode_base64url_canonical(
        string_member(*parsed.value, "server_sign_pub"), auth::ed25519_public_key_bytes);
    require(transcript_hash && server_public && signature && sign_public,
            "server hello cryptographic fields failed to decode");
    check(auth::constant_time_equal(*sign_public.value, owner->signing_public_key()),
          "server hello must expose the AuthOwner signing identity");
    check(auth::ed25519_verify(
              *sign_public.value, bytes(transcript_text), *signature.value)
              == auth::CryptoError::none,
          "server hello signature must cover the exact Tauri transcript");
    auto shared = auth::x25519_shared_secret(
        client_private.bytes(), *server_public.value);
    require(static_cast<bool>(shared), "client shared key failed");
    auto client_tx = auth::hkdf_sha256(
        shared.value->bytes(), *transcript_hash.value,
        bytes("preauth:server-rx"), auth::auth_key_bytes);
    auto client_rx = auth::hkdf_sha256(
        shared.value->bytes(), *transcript_hash.value,
        bytes("preauth:server-tx"), auth::auth_key_bytes);
    require(client_tx && client_rx, "client preauth keys failed");
    auto preauth = auth::SecureEnvelopeCipher::create(
        client_tx.value->bytes(), client_rx.value->bytes());
    require(static_cast<bool>(preauth), "client preauth cipher failed");
    ClientHandshake result;
    result.shared = std::move(*shared.value);
    result.transcript_hash = std::move(*transcript_hash.value);
    result.server_hello = std::move(*parsed.value);
    result.preauth.emplace(std::move(*preauth.value));
    return result;
}

[[nodiscard]] std::string encrypt(
    auth::SecureEnvelopeCipher& cipher, const auth::CanonicalJsonValue& value)
{
    auto encrypted = cipher.encrypt(value);
    if (!encrypted) throw std::runtime_error("secure envelope encryption failed");
    return std::move(encrypted.envelope);
}

[[nodiscard]] auth::CanonicalJsonValue decrypt(
    auth::SecureEnvelopeCipher& cipher, const ws::DriverResult& result)
{
    auto decrypted = cipher.decrypt(single_payload(result));
    if (!decrypted) throw std::runtime_error("secure envelope decryption failed");
    return std::move(*decrypted.plaintext);
}

[[nodiscard]] auth::SecretBuffer password_hash(
    const std::string_view password, const std::span<const std::byte> salt)
{
    auto result = fake_derive(bytes(password), salt);
    if (!result) throw std::runtime_error("password hash failed");
    return std::move(*result.value);
}

[[nodiscard]] auth::PublicBytes password_proof(
    const ClientHandshake& handshake,
    const auth::SecretBuffer& hash,
    const std::uint64_t epoch)
{
    const auto label = std::string{"auth-proof:"} + std::to_string(epoch);
    auto context = auth::hkdf_sha256(
        handshake.shared.bytes(), handshake.transcript_hash,
        bytes(label), auth::auth_key_bytes);
    require(static_cast<bool>(context), "password context failed");
    auto proof = auth::hmac_sha256(hash.bytes(), context.value->bytes());
    require(static_cast<bool>(proof), "password proof failed");
    return std::move(*proof.value);
}

[[nodiscard]] auth::SecretBuffer password_master(
    const ClientHandshake& handshake, const auth::SecretBuffer& hash)
{
    auth::SecretBuffer input{handshake.shared.size() + hash.size()};
    std::copy(handshake.shared.bytes().begin(), handshake.shared.bytes().end(),
              input.mutable_bytes().begin());
    std::copy(hash.bytes().begin(), hash.bytes().end(),
              input.mutable_bytes().begin() + handshake.shared.size());
    auto master = auth::hkdf_sha256(
        input.bytes(), handshake.transcript_hash,
        bytes("master-secret"), auth::auth_key_bytes);
    require(static_cast<bool>(master), "master secret failed");
    return std::move(*master.value);
}

[[nodiscard]] auth::SecureEnvelopeCipher control_cipher(
    const std::string_view session_id, const auth::SecretBuffer& master)
{
    auto salt = auth::sha256(bytes(session_id));
    require(static_cast<bool>(salt), "control salt failed");
    auto client_tx = auth::hkdf_sha256(
        master.bytes(), *salt.value,
        bytes("control:server-rx"), auth::auth_key_bytes);
    auto client_rx = auth::hkdf_sha256(
        master.bytes(), *salt.value,
        bytes("control:server-tx"), auth::auth_key_bytes);
    require(client_tx && client_rx, "control keys failed");
    auto cipher = auth::SecureEnvelopeCipher::create(
        client_tx.value->bytes(), client_rx.value->bytes());
    require(static_cast<bool>(cipher), "control cipher failed");
    return std::move(*cipher.value);
}

[[nodiscard]] auth::PublicBytes remember_proof(
    const std::string_view session_id,
    const std::uint64_t epoch,
    const auth::SecretBuffer& resume)
{
    const auto payload = encode(auth::CanonicalJsonValue{
        auth::CanonicalJsonValue::Object{
            {"type", json_string("remember_session")},
            {"session_id", json_string(std::string{session_id})},
            {"pwd_epoch", auth::CanonicalJsonValue{
                static_cast<std::int64_t>(epoch)}},
        }});
    auto proof = auth::hmac_sha256(resume.bytes(), bytes(payload));
    require(static_cast<bool>(proof), "remember proof failed");
    return std::move(*proof.value);
}

void test_initialize_fallback_ping_heartbeat_and_replay()
{
    Fixture fixture;
    ws::ControlSessionFactory factory{fixture.owner};
    auto driver = factory.create(
        control_request(), std::make_shared<Sink>(), std::stop_token{});
    auto handshake = perform_client_hello(
        *driver, fixture.owner, std::byte{0x42});
    check(member(handshake.server_hello, "initialized").as_boolean()
              && !*member(handshake.server_hello, "initialized").as_boolean(),
          "fresh server hello must advertise uninitialized state");

    auto unavailable = driver->input(
        {ws::FrameKind::text, encrypt(*handshake.preauth,
            auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
                {"type", json_string("resume_control")},
            }})},
        std::stop_token{});
    auto unavailable_plain = decrypt(*handshake.preauth, unavailable);
    check(string_member(unavailable_plain, "type") == "resume_unavailable",
          "missing remember cookie must fall back without closing");

    constexpr std::string_view password = "control-password";
    auto authenticated = driver->input(
        {ws::FrameKind::text, encrypt(*handshake.preauth,
            auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
                {"type", json_string("initialize")},
                {"password", json_string(std::string{password})},
            }})},
        std::stop_token{});
    check(authenticated.phase == ws::SessionPhase::streaming
              && authenticated.terminal == ws::TerminalAction::none,
          "initialize after resume fallback must authenticate");
    auto auth_ok = decrypt(*handshake.preauth, authenticated);
    check(string_member(auth_ok, "type") == "auth_ok"
              && integer_member(auth_ok, "protocol_version") == 1,
          "initialization must emit v1 auth_ok");
    const auto salt = auth::decode_base64url_canonical(
        string_member(auth_ok, "pwd_salt"), auth::argon2id_salt_bytes);
    require(static_cast<bool>(salt), "auth_ok salt failed");
    auto hash = password_hash(password, *salt.value);
    auto master = password_master(handshake, hash);
    auto control = control_cipher(string_member(auth_ok, "session_id"), master);

    const auto ping_envelope = encrypt(control, auth::CanonicalJsonValue{
        auth::CanonicalJsonValue::Object{{"type", json_string("ping")}}});
    auto pong = driver->input(
        {ws::FrameKind::text, ping_envelope}, std::stop_token{});
    auto pong_plain = decrypt(control, pong);
    check(string_member(pong_plain, "type") == "pong"
              && integer_member(pong_plain, "timestamp") > 0,
          "control ping must produce encrypted pong");
    auto heartbeat = driver->heartbeat(std::stop_token{});
    auto heartbeat_plain = decrypt(control, heartbeat);
    check(string_member(heartbeat_plain, "type") == "heartbeat",
          "streaming heartbeat must be encrypted and contiguous");

    auto replay = driver->input(
        {ws::FrameKind::text, ping_envelope}, std::stop_token{});
    check(replay.terminal == ws::TerminalAction::authentication_failed,
          "replayed control sequence must fail authentication");
    driver->closed();
    check(fixture.owner->active_session_count() == 1,
          "control disconnect must retain the published session for business resume");
}

void test_remember_resume_password_fallback_and_revocation()
{
    Fixture fixture;
    constexpr std::string_view password = "remember-password";
    check(fixture.owner->initialize_password(auth::SecretBuffer{bytes(password)}),
          "remember fixture must initialize");
    const auto state = fixture.owner->password_state();
    auto hash = password_hash(password, state.pwd_salt);

    auth::SecretBuffer shared{auth::auth_key_bytes};
    std::fill(shared.mutable_bytes().begin(), shared.mutable_bytes().end(),
              std::byte{0x33});
    auto transcript_hash = auth::sha256(bytes("remember-seed-session"));
    require(static_cast<bool>(transcript_hash), "seed transcript failed");
    ClientHandshake seed_handshake;
    seed_handshake.shared = auth::SecretBuffer{shared.bytes()};
    seed_handshake.transcript_hash = *transcript_hash.value;
    const auto proof = password_proof(seed_handshake, hash, state.pwd_epoch);
    auto seed_session = fixture.owner->authenticate_control(
        {auth::SecretBuffer{shared.bytes()}, *transcript_hash.value}, proof);
    require(static_cast<bool>(seed_session), "seed control session failed");
    auto master = password_master(seed_handshake, hash);
    auto resume_result = auth::hkdf_sha256(
        master.bytes(), seed_handshake.transcript_hash,
        bytes("resume-secret"), auth::auth_key_bytes);
    require(static_cast<bool>(resume_result), "seed resume secret failed");
    auto remember = fixture.owner->issue_remember_token(
        seed_session->session_id,
        remember_proof(seed_session->session_id, state.pwd_epoch,
                       *resume_result.value));
    require(static_cast<bool>(remember), "remember token issuance failed");
    const auto token_text = text(remember->token.bytes());

    ws::ControlSessionFactory factory{fixture.owner};
    auto failed_resume_driver = factory.create(
        control_request("baas_remember=" + token_text),
        std::make_shared<Sink>(), std::stop_token{});
    auto failed_resume_handshake = perform_client_hello(
        *failed_resume_driver, fixture.owner, std::byte{0x47});
    fixture.random->fail(true);
    auto failed_resume = failed_resume_driver->input(
        {ws::FrameKind::text, encrypt(*failed_resume_handshake.preauth,
            auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
                {"type", json_string("resume_control")},
            }})},
        std::stop_token{});
    fixture.random->fail(false);
    check(failed_resume.terminal == ws::TerminalAction::internal_error
              && failed_resume.batches.empty(),
          "resume entropy failure must not masquerade as resume_unavailable");

    auto resumed_driver = factory.create(
        control_request("other=1; baas_remember=" + token_text),
        std::make_shared<Sink>(), std::stop_token{});
    auto resumed_handshake = perform_client_hello(
        *resumed_driver, fixture.owner, std::byte{0x51});
    auto resumed_result = resumed_driver->input(
        {ws::FrameKind::text, encrypt(*resumed_handshake.preauth,
            auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
                {"type", json_string("resume_control")},
            }})},
        std::stop_token{});
    check(resumed_result.phase == ws::SessionPhase::streaming,
          "valid remember cookie must resume control");
    auto resumed_auth = decrypt(*resumed_handshake.preauth, resumed_result);
    auto disclosed_master = auth::decode_base64url_canonical(
        string_member(resumed_auth, "master_secret"), auth::auth_key_bytes);
    auto disclosed_resume = auth::decode_base64url_canonical(
        string_member(resumed_auth, "resume_secret"), auth::auth_key_bytes);
    require(disclosed_master && disclosed_resume,
            "remember auth_ok must disclose fresh session secrets");
    auth::SecretBuffer resumed_master{*disclosed_master.value};
    auto resumed_control = control_cipher(
        string_member(resumed_auth, "session_id"), resumed_master);

    auto fallback_driver = factory.create(
        control_request(), std::make_shared<Sink>(), std::stop_token{});
    auto fallback_handshake = perform_client_hello(
        *fallback_driver, fixture.owner, std::byte{0x62});
    auto unavailable = fallback_driver->input(
        {ws::FrameKind::text, encrypt(*fallback_handshake.preauth,
            auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
                {"type", json_string("resume_control")},
            }})},
        std::stop_token{});
    check(string_member(decrypt(*fallback_handshake.preauth, unavailable), "type")
              == "resume_unavailable",
          "cookie-less initialized connection must offer password fallback");
    const auto fallback_proof = password_proof(
        fallback_handshake, hash, state.pwd_epoch);
    auto fallback_auth_result = fallback_driver->input(
        {ws::FrameKind::text, encrypt(*fallback_handshake.preauth,
            auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
                {"type", json_string("authenticate")},
                {"proof", json_string(b64(fallback_proof))},
            }})},
        std::stop_token{});
    auto fallback_auth = decrypt(
        *fallback_handshake.preauth, fallback_auth_result);
    auto fallback_master = password_master(fallback_handshake, hash);
    auto fallback_control = control_cipher(
        string_member(fallback_auth, "session_id"), fallback_master);

    auto duplicate_driver = factory.create(
        control_request("baas_remember=" + token_text
                        + "; baas_remember=" + token_text),
        std::make_shared<Sink>(), std::stop_token{});
    auto duplicate_handshake = perform_client_hello(
        *duplicate_driver, fixture.owner, std::byte{0x73});
    auto duplicate = duplicate_driver->input(
        {ws::FrameKind::text, encrypt(*duplicate_handshake.preauth,
            auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
                {"type", json_string("resume_control")},
            }})},
        std::stop_token{});
    check(duplicate.terminal == ws::TerminalAction::protocol_failed,
          "duplicate remember-cookie names must fail closed");

    auto changed = fallback_driver->input(
        {ws::FrameKind::text, encrypt(fallback_control,
            auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
                {"type", json_string("change_password")},
                {"new_password", json_string("next-password")},
            }})},
        std::stop_token{});
    check(changed.terminal == ws::TerminalAction::authentication_failed,
          "password change must close the control channel with 4401 semantics");
    auto changed_plain = decrypt(fallback_control, changed);
    check(string_member(changed_plain, "type") == "auth_revoked"
              && string_member(changed_plain, "reason") == "password_changed"
              && integer_member(changed_plain, "pwd_epoch") == 2,
          "password change must emit its encrypted revocation before close");

    auto peer_revoked = resumed_driver->heartbeat(std::stop_token{});
    check(peer_revoked.terminal == ws::TerminalAction::authentication_failed,
          "peer control session must close after epoch rotation");
    auto peer_plain = decrypt(resumed_control, peer_revoked);
    check(string_member(peer_plain, "type") == "auth_revoked"
              && integer_member(peer_plain, "pwd_epoch") == 2,
          "peer revocation must preserve the new epoch");
    check(fixture.owner->remembered_login_count() == 0,
          "password rotation must revoke the persisted remember token");
}

void test_factory_and_handshake_fail_closed()
{
    Fixture fixture;
    ws::ControlSessionFactory factory{fixture.owner};
    auto unsupported_request = control_request();
    unsupported_request.channel = ws::Channel::provider;
    unsupported_request.path = "/ws/provider";
    auto unsupported = factory.create(
        std::move(unsupported_request), std::make_shared<Sink>(), std::stop_token{});
    check(unsupported->input(
              {ws::FrameKind::text, "{}"}, std::stop_token{}).terminal
              == ws::TerminalAction::protocol_failed,
          "control-only factory must explicitly reject business channels");

    auto binary = factory.create(
        control_request(), std::make_shared<Sink>(), std::stop_token{});
    check(binary->input(
              {ws::FrameKind::binary, "{}"}, std::stop_token{}).terminal
              == ws::TerminalAction::protocol_failed,
          "binary control hello must fail closed");

    auth::SecretBuffer client_private{auth::x25519_key_bytes};
    std::fill(client_private.mutable_bytes().begin(),
              client_private.mutable_bytes().end(), std::byte{0x38});
    auto client_public = auth::x25519_public_key(client_private.bytes());
    require(static_cast<bool>(client_public), "malformed hello public key failed");
    const auth::PublicBytes nonce(auth::x25519_key_bytes, std::byte{0x29});
    auto negative = factory.create(
        control_request(), std::make_shared<Sink>(), std::stop_token{});
    const auto negative_hello = encode(auth::CanonicalJsonValue{
        auth::CanonicalJsonValue::Object{
            {"type", json_string("client_hello")},
            {"kind", json_string("control")},
            {"channel", json_string("control")},
            {"version", auth::CanonicalJsonValue{std::int64_t{1}}},
            {"timestamp", auth::CanonicalJsonValue{std::int64_t{-1}}},
            {"client_nonce", json_string(b64(nonce))},
            {"client_kx_pub", json_string(b64(*client_public.value))},
        }});
    check(negative->input(
              {ws::FrameKind::text, negative_hello}, std::stop_token{}).terminal
              == ws::TerminalAction::protocol_failed,
          "negative client timestamp must fail schema validation");

    auto expired_request = control_request();
    expired_request.handshake_timeout = std::chrono::milliseconds{1};
    auto expired = factory.create(
        std::move(expired_request), std::make_shared<Sink>(), std::stop_token{});
    std::this_thread::sleep_for(std::chrono::milliseconds{3});
    check(expired->input(
              {ws::FrameKind::text, "{}"}, std::stop_token{}).terminal
              == ws::TerminalAction::authentication_failed,
          "driver-local handshake deadline must reject late input");
}

void test_late_password_derivation_rolls_back_session()
{
    auto storage = std::make_shared<MemoryStorage>();
    auto clock = std::make_shared<FakeClock>();
    auto random = std::make_shared<FakeRandom>();
    auto deriver = std::make_shared<BlockingDeriver>();
    auto opened = auth::AuthOwner::open(
        {}, {storage, clock, random, deriver, {}, {}});
    require(static_cast<bool>(opened), "blocking AuthOwner failed to open");
    auto owner = std::shared_ptr<auth::AuthOwner>{std::move(*opened.value)};
    ws::ControlSessionFactory factory{owner};
    auto request = control_request();
    request.handshake_timeout = std::chrono::milliseconds{20};
    auto driver = factory.create(
        std::move(request), std::make_shared<Sink>(), std::stop_token{});
    auto handshake = perform_client_hello(*driver, owner, std::byte{0x5A});
    const auto initialization = encrypt(
        *handshake.preauth,
        auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
            {"type", json_string("initialize")},
            {"password", json_string("slow-password")},
        }});
    ws::DriverResult result;
    std::thread worker([&] {
        result = driver->input(
            {ws::FrameKind::text, initialization}, std::stop_token{});
    });
    deriver->wait_entered();
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    deriver->release();
    worker.join();
    check(result.terminal == ws::TerminalAction::authentication_failed
              && result.batches.empty(),
          "authentication completing after its deadline must not publish auth_ok");
    check(owner->active_session_count() == 0,
          "late authentication must roll back its unpublished session");
    check(owner->password_state().initialized,
          "successful first-run persistence remains recoverable after client timeout");
}

}  // namespace

int main()
{
    try {
        test_initialize_fallback_ping_heartbeat_and_replay();
        test_remember_resume_password_fallback_and_revocation();
        test_factory_and_handshake_fail_closed();
        test_late_password_derivation_rolls_back_session();
    }
    catch (const std::exception& error) {
        std::cerr << "FAILED with exception: " << error.what() << '\n';
        ++failures;
    }
    if (failures != 0) {
        std::cerr << failures << " control session test(s) failed\n";
        return 1;
    }
    std::cout << "control session tests passed\n";
    return 0;
}
