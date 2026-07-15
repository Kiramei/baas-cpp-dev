#include "service/websocket/ControlSessionFactory.h"

#include "service/auth/CanonicalJson.h"
#include "service/auth/SecureEnvelope.h"

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace baas::service::websocket {
namespace {

using auth::CanonicalJsonValue;
using auth::SecretBuffer;
using SteadyClock = std::chrono::steady_clock;

inline constexpr std::int64_t protocol_version = 1;
inline constexpr std::string_view remember_cookie_name = "baas_remember";

[[nodiscard]] std::span<const std::byte> bytes_of(
    const std::string_view value) noexcept
{
    return std::as_bytes(std::span{value.data(), value.size()});
}

[[nodiscard]] std::string text_of(const std::span<const std::byte> value)
{
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] CanonicalJsonValue string_value(std::string value)
{
    return CanonicalJsonValue{std::move(value)};
}

[[nodiscard]] bool exact_fields(
    const CanonicalJsonValue& value,
    const std::initializer_list<std::string_view> fields) noexcept
{
    const auto* object = value.as_object();
    if (object == nullptr || object->size() != fields.size()) return false;
    return std::all_of(fields.begin(), fields.end(), [&](const auto field) {
        return value.find(field) != nullptr;
    });
}

[[nodiscard]] const std::string* member_string(
    const CanonicalJsonValue& value, const std::string_view name) noexcept
{
    const auto* member = value.find(name);
    return member == nullptr ? nullptr : member->as_string();
}

[[nodiscard]] const std::int64_t* member_integer(
    const CanonicalJsonValue& value, const std::string_view name) noexcept
{
    const auto* member = value.find(name);
    return member == nullptr ? nullptr : member->as_integer();
}

[[nodiscard]] std::optional<std::string> b64(
    const std::span<const std::byte> value)
{
    auto encoded = auth::encode_base64url_padded(value);
    if (!encoded) return std::nullopt;
    return std::move(*encoded.value);
}

[[nodiscard]] CanonicalJsonValue argon2_parameters()
{
    return CanonicalJsonValue{CanonicalJsonValue::Object{
        {"algorithm", string_value("argon2id")},
        {"hash_bytes", CanonicalJsonValue{
            static_cast<std::int64_t>(auth::argon2id_output_bytes)}},
        {"memlimit", CanonicalJsonValue{
            static_cast<std::int64_t>(auth::argon2id_v1_memlimit)}},
        {"opslimit", CanonicalJsonValue{
            static_cast<std::int64_t>(auth::argon2id_v1_opslimit)}},
        {"salt_bytes", CanonicalJsonValue{
            static_cast<std::int64_t>(auth::argon2id_salt_bytes)}},
    }};
}

[[nodiscard]] std::int64_t system_timestamp_ms() noexcept
{
    const auto value = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (value < 0) return 0;
    return std::min<std::int64_t>(value, auth::maximum_safe_json_integer);
}

[[nodiscard]] OutboundBatch text_batch(std::string payload)
{
    OutboundBatch batch;
    batch.frames.push_back({FrameKind::text, std::move(payload)});
    return batch;
}

[[nodiscard]] DriverResult terminal_result(
    const SessionPhase phase, const TerminalAction action) noexcept
{
    DriverResult result;
    result.phase = phase;
    result.terminal = action;
    return result;
}

[[nodiscard]] TerminalAction terminal_for_auth_error(
    const auth::AuthError error) noexcept
{
    switch (error) {
        case auth::AuthError::entropy_failure:
        case auth::AuthError::storage_failure:
        case auth::AuthError::corrupted_storage:
        case auth::AuthError::crypto_failure:
        case auth::AuthError::password_derivation_failed:
            return TerminalAction::internal_error;
        case auth::AuthError::capacity_exceeded:
        case auth::AuthError::password_derivation_busy:
            return TerminalAction::capacity;
        case auth::AuthError::none:
            return TerminalAction::none;
        default:
            return TerminalAction::authentication_failed;
    }
}

struct ParsedCookie {
    std::optional<SecretBuffer> remember_token;
    bool malformed{};
};

[[nodiscard]] std::string_view trim_cookie_space(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] ParsedCookie parse_remember_cookie(const std::string_view header)
{
    ParsedCookie result;
    std::size_t offset = 0;
    while (offset <= header.size()) {
        const auto separator = header.find(';', offset);
        const auto end = separator == std::string_view::npos ? header.size() : separator;
        const auto item = trim_cookie_space(header.substr(offset, end - offset));
        const auto equals = item.find('=');
        if (equals != std::string_view::npos
            && trim_cookie_space(item.substr(0, equals)) == remember_cookie_name) {
            if (result.remember_token) {
                result.malformed = true;
                result.remember_token.reset();
                return result;
            }
            const auto token = trim_cookie_space(item.substr(equals + 1));
            if (!token.empty()) result.remember_token.emplace(bytes_of(token));
        }
        if (separator == std::string_view::npos) break;
        offset = separator + 1;
    }
    return result;
}

class CanonicalStringWiper final {
public:
    explicit CanonicalStringWiper(CanonicalJsonValue& value) noexcept : value_(value) {}
    ~CanonicalStringWiper() { value_.wipe_strings(); }
private:
    CanonicalJsonValue& value_;
};

class WipingString final {
public:
    WipingString() = default;
    explicit WipingString(std::string value) : value(std::move(value)) {}
    ~WipingString()
    {
        auth::secure_zero(std::as_writable_bytes(
            std::span{value.data(), value.size()}));
        value.clear();
    }
    std::string value;
};

class UnsupportedSessionDriver final : public SessionDriver {
public:
    [[nodiscard]] DriverResult input(Frame, std::stop_token) override
    {
        return terminal_result(SessionPhase::handshaking, TerminalAction::protocol_failed);
    }

    [[nodiscard]] DriverResult heartbeat(std::stop_token) override
    {
        return terminal_result(SessionPhase::handshaking, TerminalAction::protocol_failed);
    }

    void closed() noexcept override {}
};

class ControlSessionDriver final : public SessionDriver {
public:
    ControlSessionDriver(
        std::shared_ptr<auth::AuthOwner> authentication,
        ControlSessionConfig config,
        ParsedCookie cookie,
        const SteadyClock::time_point handshake_deadline)
        : authentication_(std::move(authentication)),
          config_(config),
          remember_token_(std::move(cookie.remember_token)),
          malformed_cookie_(cookie.malformed),
          handshake_deadline_(handshake_deadline)
    {}

    ~ControlSessionDriver() override { closed(); }

    [[nodiscard]] DriverResult input(Frame frame, const std::stop_token stop) override
    {
        try {
            if (stop.stop_requested()) return terminal(TerminalAction::complete);
            if (step_ != Step::streaming && step_ != Step::closed
                && SteadyClock::now() >= handshake_deadline_) {
                return terminal(TerminalAction::authentication_failed);
            }
            switch (step_) {
                case Step::client_hello: return client_hello(std::move(frame));
                case Step::preauth:
                case Step::password_after_resume:
                    return preauth(std::move(frame));
                case Step::streaming: return control(std::move(frame));
                case Step::closed: return terminal(TerminalAction::complete);
            }
        }
        catch (...) {
            return terminal(TerminalAction::internal_error);
        }
        return terminal(TerminalAction::internal_error);
    }

    [[nodiscard]] DriverResult heartbeat(const std::stop_token stop) override
    {
        try {
            if (stop.stop_requested()) return terminal(TerminalAction::complete);
            if (step_ != Step::streaming) return current_result();
            auto revoked = revocation_result();
            if (revoked) return std::move(*revoked);
            if (!authentication_->validate_session(session_id_)) {
                return terminal(TerminalAction::authentication_failed);
            }
            return encrypted_control(CanonicalJsonValue{CanonicalJsonValue::Object{
                {"type", string_value("heartbeat")},
                {"timestamp", CanonicalJsonValue{system_timestamp_ms()}},
            }});
        }
        catch (...) {
            return terminal(TerminalAction::internal_error);
        }
    }

    void closed() noexcept override
    {
        if (step_ == Step::closed) return;
        if (subscription_) authentication_->unsubscribe_revocations(*subscription_);
        subscription_.reset();
        preauth_cipher_.reset();
        control_cipher_.reset();
        shared_key_.clear();
        auth::secure_zero(transcript_hash_);
        transcript_hash_.clear();
        remember_token_.reset();
        session_id_.clear();
        step_ = Step::closed;
    }

private:
    enum class Step : std::uint8_t {
        client_hello,
        preauth,
        password_after_resume,
        streaming,
        closed,
    };

    [[nodiscard]] SessionPhase phase() const noexcept
    {
        return step_ == Step::streaming ? SessionPhase::streaming
                                        : SessionPhase::handshaking;
    }

    [[nodiscard]] DriverResult current_result() const noexcept
    {
        DriverResult result;
        result.phase = phase();
        return result;
    }

    [[nodiscard]] DriverResult terminal(const TerminalAction action) const noexcept
    {
        return terminal_result(phase(), action);
    }

    [[nodiscard]] auth::HandshakeMaterial handshake_copy() const
    {
        return {SecretBuffer{shared_key_.bytes()}, transcript_hash_};
    }

    [[nodiscard]] DriverResult client_hello(Frame frame)
    {
        if (frame.kind != FrameKind::text
            || frame.payload.size() > config_.max_handshake_json_bytes) {
            return terminal(TerminalAction::protocol_failed);
        }
        auto parsed = auth::parse_canonical_json_value(
            frame.payload,
            {.max_input_bytes = config_.max_handshake_json_bytes,
             .max_output_bytes = config_.max_handshake_json_bytes});
        if (!parsed || !exact_fields(
                *parsed.value,
                {"type", "kind", "channel", "version", "timestamp",
                 "client_nonce", "client_kx_pub"})) {
            return terminal(TerminalAction::protocol_failed);
        }
        const auto* type = member_string(*parsed.value, "type");
        const auto* kind = member_string(*parsed.value, "kind");
        const auto* channel = member_string(*parsed.value, "channel");
        const auto* version = member_integer(*parsed.value, "version");
        const auto* timestamp = member_integer(*parsed.value, "timestamp");
        const auto* nonce_text = member_string(*parsed.value, "client_nonce");
        const auto* public_text = member_string(*parsed.value, "client_kx_pub");
        if (type == nullptr || *type != "client_hello"
            || kind == nullptr || *kind != "control"
            || channel == nullptr || *channel != "control"
            || version == nullptr || *version != protocol_version
            || timestamp == nullptr || *timestamp < 0
            || nonce_text == nullptr || public_text == nullptr) {
            return terminal(TerminalAction::protocol_failed);
        }
        auto client_nonce = auth::decode_base64url_canonical(
            *nonce_text, auth::x25519_key_bytes);
        auto client_public = auth::decode_base64url_canonical(
            *public_text, auth::x25519_key_bytes);
        if (!client_nonce || !client_public) {
            return terminal(TerminalAction::protocol_failed);
        }

        auto handshake = authentication_->begin_control_handshake({
            *timestamp,
            std::move(*client_nonce.value),
            std::move(*client_public.value),
        });
        if (!handshake) {
            return terminal(terminal_for_auth_error(handshake.error));
        }
        auto server_tx = auth::hkdf_sha256(
            handshake->authentication.shared_key.bytes(),
            handshake->authentication.transcript_hash,
            bytes_of("preauth:server-tx"), auth::auth_key_bytes);
        auto server_rx = auth::hkdf_sha256(
            handshake->authentication.shared_key.bytes(),
            handshake->authentication.transcript_hash,
            bytes_of("preauth:server-rx"), auth::auth_key_bytes);
        if (!server_tx || !server_rx) {
            return terminal(TerminalAction::internal_error);
        }
        auto cipher = auth::SecureEnvelopeCipher::create(
            server_tx.value->bytes(), server_rx.value->bytes());
        if (!cipher) return terminal(TerminalAction::internal_error);

        shared_key_ = std::move(handshake->authentication.shared_key);
        transcript_hash_ = std::move(handshake->authentication.transcript_hash);
        preauth_cipher_.emplace(std::move(*cipher.value));
        step_ = Step::preauth;
        DriverResult result;
        result.phase = SessionPhase::handshaking;
        result.batches.push_back(text_batch(std::move(handshake->server_hello_json)));
        return result;
    }

    [[nodiscard]] DriverResult preauth(Frame frame)
    {
        if (frame.kind != FrameKind::text
            || frame.payload.size() > config_.max_control_json_bytes
            || !preauth_cipher_) {
            return terminal(TerminalAction::protocol_failed);
        }
        auto decoded = preauth_cipher_->decrypt(frame.payload);
        if (!decoded) return terminal(TerminalAction::authentication_failed);
        CanonicalStringWiper wiper{*decoded.plaintext};
        const auto* type = member_string(*decoded.plaintext, "type");
        if (type == nullptr) return terminal(TerminalAction::protocol_failed);

        if (*type == "resume_control" && step_ == Step::preauth) {
            if (!exact_fields(*decoded.plaintext, {"type"}) || malformed_cookie_) {
                return terminal(TerminalAction::protocol_failed);
            }
            step_ = Step::password_after_resume;
            if (remember_token_) {
                auto resumed = authentication_->resume_control(
                    handshake_copy(), std::move(*remember_token_));
                remember_token_.reset();
                if (resumed) return establish(std::move(*resumed.value), true);
                if (resumed.error != auth::AuthError::invalid_remember_token
                    && resumed.error != auth::AuthError::stale_epoch) {
                    return terminal(terminal_for_auth_error(resumed.error));
                }
            }
            return encrypted_preauth(CanonicalJsonValue{CanonicalJsonValue::Object{
                {"type", string_value("resume_unavailable")},
            }});
        }

        auth::AuthResult<auth::ControlSessionMaterial> authenticated;
        if (*type == "initialize") {
            if (!exact_fields(*decoded.plaintext, {"type", "password"})) {
                return terminal(TerminalAction::protocol_failed);
            }
            const auto* password = member_string(*decoded.plaintext, "password");
            if (password == nullptr) return terminal(TerminalAction::protocol_failed);
            SecretBuffer password_secret{bytes_of(*password)};
            authenticated = authentication_->initialize_control(
                handshake_copy(), std::move(password_secret));
        }
        else if (*type == "authenticate") {
            if (!exact_fields(*decoded.plaintext, {"type", "proof"})) {
                return terminal(TerminalAction::protocol_failed);
            }
            const auto* proof_text = member_string(*decoded.plaintext, "proof");
            if (proof_text == nullptr) return terminal(TerminalAction::protocol_failed);
            auto proof = auth::decode_base64url_canonical(
                *proof_text, auth::hmac_sha256_bytes);
            if (!proof) return terminal(TerminalAction::authentication_failed);
            authenticated = authentication_->authenticate_control(
                handshake_copy(), *proof.value);
            auth::secure_zero(*proof.value);
        }
        else {
            return terminal(TerminalAction::protocol_failed);
        }
        remember_token_.reset();
        if (!authenticated) {
            return terminal(terminal_for_auth_error(authenticated.error));
        }
        return establish(std::move(*authenticated.value), false);
    }

    [[nodiscard]] DriverResult establish(
        auth::ControlSessionMaterial material,
        const bool disclose_secrets)
    {
        class SessionRollback final {
        public:
            SessionRollback(
                auth::AuthOwner& owner,
                const std::string_view session_id) noexcept
                : owner_(owner), session_id_(session_id)
            {}

            ~SessionRollback()
            {
                if (!active_) return;
                if (subscription_) owner_.unsubscribe_revocations(*subscription_);
                owner_.close_session(session_id_);
            }

            void subscribed(const auth::SubscriptionId value) noexcept
            {
                subscription_ = value;
            }

            void release() noexcept { active_ = false; }

        private:
            auth::AuthOwner& owner_;
            std::string_view session_id_;
            std::optional<auth::SubscriptionId> subscription_;
            bool active_{true};
        } rollback{*authentication_, material.session_id};

        if (SteadyClock::now() >= handshake_deadline_) {
            return terminal(TerminalAction::authentication_failed);
        }
        auto control = auth::SecureEnvelopeCipher::create(
            material.control_server_tx.bytes(), material.control_server_rx.bytes());
        if (!control) {
            return terminal(TerminalAction::internal_error);
        }
        auto subscription = authentication_->subscribe_revocations(material.session_id);
        if (!subscription) {
            return terminal(terminal_for_auth_error(subscription.error));
        }
        rollback.subscribed(*subscription.value);
        const auto& password = material.password_state;
        auto salt = b64(password.pwd_salt);
        WipingString ticket{text_of(material.resume_ticket.bytes())};
        if (!password.initialized || password.pwd_epoch != material.pwd_epoch
            || !salt || !auth::is_valid_utf8(ticket.value)) {
            return terminal(TerminalAction::authentication_failed);
        }

        CanonicalJsonValue::Object fields{
            {"type", string_value("auth_ok")},
            {"protocol_version", CanonicalJsonValue{protocol_version}},
            {"session_id", string_value(material.session_id)},
            {"resume_ticket", string_value(std::move(ticket.value))},
            {"expires_at", CanonicalJsonValue{material.expires_at}},
            {"pwd_epoch", CanonicalJsonValue{
                static_cast<std::int64_t>(material.pwd_epoch)}},
            {"pwd_salt", string_value(std::move(*salt))},
            {"argon2", argon2_parameters()},
        };
        if (disclose_secrets) {
            if (!material.disclosed_master_secret || !material.disclosed_resume_secret) {
                return terminal(TerminalAction::internal_error);
            }
            auto master_result = b64(material.disclosed_master_secret->bytes());
            auto resume_result = b64(material.disclosed_resume_secret->bytes());
            if (!master_result || !resume_result) {
                return terminal(TerminalAction::internal_error);
            }
            WipingString master;
            WipingString resume;
            master.value.swap(*master_result);
            resume.value.swap(*resume_result);
            fields.emplace_back(
                "master_secret", string_value(std::move(master.value)));
            fields.emplace_back(
                "resume_secret", string_value(std::move(resume.value)));
        }
        CanonicalJsonValue auth_ok{std::move(fields)};
        CanonicalStringWiper wiper{auth_ok};
        auto encrypted = preauth_cipher_->encrypt(auth_ok);
        if (!encrypted) {
            return terminal(TerminalAction::internal_error);
        }

        DriverResult result;
        result.phase = SessionPhase::streaming;
        result.batches.push_back(text_batch(std::move(encrypted.envelope)));
        session_id_ = material.session_id;
        control_cipher_.emplace(std::move(*control.value));
        subscription_ = *subscription.value;
        step_ = Step::streaming;
        rollback.release();
        return result;
    }

    [[nodiscard]] DriverResult control(Frame frame)
    {
        auto pending_revocation = revocation_result();
        if (pending_revocation) return std::move(*pending_revocation);
        if (frame.kind != FrameKind::text
            || frame.payload.size() > config_.max_control_json_bytes
            || !control_cipher_) {
            return terminal(TerminalAction::protocol_failed);
        }
        auto decoded = control_cipher_->decrypt(frame.payload);
        if (!decoded) return terminal(TerminalAction::authentication_failed);
        CanonicalStringWiper wiper{*decoded.plaintext};
        const auto* type = member_string(*decoded.plaintext, "type");
        if (type == nullptr) return terminal(TerminalAction::protocol_failed);
        if (*type == "ping") {
            if (!exact_fields(*decoded.plaintext, {"type"})) {
                return terminal(TerminalAction::protocol_failed);
            }
            return encrypted_control(CanonicalJsonValue{CanonicalJsonValue::Object{
                {"type", string_value("pong")},
                {"timestamp", CanonicalJsonValue{system_timestamp_ms()}},
            }});
        }
        if (*type == "change_password") {
            if (!exact_fields(*decoded.plaintext, {"type", "new_password"})) {
                return terminal(TerminalAction::protocol_failed);
            }
            const auto* password = member_string(*decoded.plaintext, "new_password");
            if (password == nullptr) return terminal(TerminalAction::protocol_failed);
            const auto changed = authentication_->change_password(
                session_id_, SecretBuffer{bytes_of(*password)});
            if (!changed) return terminal(terminal_for_auth_error(changed.error));
            auto revoked = revocation_result();
            return revoked ? std::move(*revoked)
                           : terminal(TerminalAction::authentication_failed);
        }
        return terminal(TerminalAction::protocol_failed);
    }

    [[nodiscard]] DriverResult encrypted_preauth(CanonicalJsonValue plaintext)
    {
        CanonicalStringWiper wiper{plaintext};
        auto encrypted = preauth_cipher_->encrypt(plaintext);
        if (!encrypted) return terminal(TerminalAction::internal_error);
        auto result = current_result();
        result.batches.push_back(text_batch(std::move(encrypted.envelope)));
        return result;
    }

    [[nodiscard]] DriverResult encrypted_control(CanonicalJsonValue plaintext)
    {
        CanonicalStringWiper wiper{plaintext};
        auto encrypted = control_cipher_->encrypt(plaintext);
        if (!encrypted) return terminal(TerminalAction::internal_error);
        auto result = current_result();
        result.batches.push_back(text_batch(std::move(encrypted.envelope)));
        return result;
    }

    [[nodiscard]] std::optional<DriverResult> revocation_result()
    {
        if (!subscription_) return terminal(TerminalAction::authentication_failed);
        auto events = authentication_->drain_revocations(
            *subscription_, 1);
        if (!events) return terminal(terminal_for_auth_error(events.error));
        if (events->empty()) return std::nullopt;
        const auto& event = events->front();
        auto result = encrypted_control(CanonicalJsonValue{CanonicalJsonValue::Object{
            {"type", string_value("auth_revoked")},
            {"reason", string_value(
                event.reason == auth::RevocationReason::password_reset
                    ? "password_reset" : "password_changed")},
            {"pwd_epoch", CanonicalJsonValue{
                static_cast<std::int64_t>(event.pwd_epoch)}},
        }});
        if (result.terminal == TerminalAction::none) {
            result.terminal = TerminalAction::authentication_failed;
        }
        return result;
    }

    std::shared_ptr<auth::AuthOwner> authentication_;
    ControlSessionConfig config_;
    std::optional<SecretBuffer> remember_token_;
    bool malformed_cookie_{};
    SteadyClock::time_point handshake_deadline_;
    Step step_{Step::client_hello};
    SecretBuffer shared_key_;
    auth::PublicBytes transcript_hash_;
    std::optional<auth::SecureEnvelopeCipher> preauth_cipher_;
    std::optional<auth::SecureEnvelopeCipher> control_cipher_;
    std::string session_id_;
    std::optional<auth::SubscriptionId> subscription_;
};

}  // namespace

ControlSessionFactory::ControlSessionFactory(
    std::shared_ptr<auth::AuthOwner> authentication,
    const ControlSessionConfig config)
    : authentication_(std::move(authentication)),
      config_(config)
{
    if (!authentication_
        || config_.max_handshake_json_bytes == 0
        || config_.max_handshake_json_bytes > websocket_max_frame_bytes
        || config_.max_control_json_bytes == 0
        || config_.max_control_json_bytes > websocket_max_frame_bytes) {
        throw std::invalid_argument("control session configuration is invalid");
    }
}

std::unique_ptr<SessionDriver> ControlSessionFactory::create(
    RequestMetadata request,
    std::shared_ptr<OutboundSink> outbound,
    const std::stop_token stop)
{
    static_cast<void>(outbound);
    if (stop.stop_requested() || request.channel != Channel::control
        || request.path != "/ws/control") {
        return std::make_unique<UnsupportedSessionDriver>();
    }
    auto cookie = parse_remember_cookie(request.cookie);
    auth::secure_zero(std::as_writable_bytes(
        std::span{request.cookie.data(), request.cookie.size()}));
    request.cookie.clear();
    return std::make_unique<ControlSessionDriver>(
        authentication_, config_, std::move(cookie),
        SteadyClock::now() + request.handshake_timeout);
}

}  // namespace baas::service::websocket
