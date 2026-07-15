#include "service/auth/CanonicalJson.h"
#include "service/auth/SecretStream.h"
#include "service/auth/SecureEnvelope.h"
#include "service/websocket/BusinessSessionFactory.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
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
#include <vector>

namespace auth = baas::service::auth;
namespace ws = baas::service::websocket;
namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

void require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string{message});
}

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value) noexcept
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

[[nodiscard]] std::string encode(const auth::CanonicalJsonValue& value)
{
    auto result = auth::encode_canonical_json_value(value);
    require(static_cast<bool>(result), "canonical encode failed");
    return std::move(result.text);
}

[[nodiscard]] std::string b64(const std::span<const std::byte> value)
{
    auto result = auth::encode_base64url_padded(value);
    require(static_cast<bool>(result), "base64 encode failed");
    return std::move(*result.value);
}

[[nodiscard]] const auth::CanonicalJsonValue& member(
    const auth::CanonicalJsonValue& value, const std::string_view name)
{
    const auto* found = value.find(name);
    require(found != nullptr, "missing JSON member");
    return *found;
}

[[nodiscard]] const std::string& string_member(
    const auth::CanonicalJsonValue& value, const std::string_view name)
{
    const auto* found = member(value, name).as_string();
    require(found != nullptr, "JSON member is not string");
    return *found;
}

class MemoryStorage final : public auth::AuthStorage {
public:
    auth::StorageReadResult read(
        const auth::AuthFile file, const std::size_t maximum) noexcept override
    {
        std::lock_guard lock{mutex_};
        const auto found = files_.find(file);
        if (found == files_.end()) return {};
        if (found->second.size() > maximum) return {auth::StorageReadStatus::failure, {}};
        return {auth::StorageReadStatus::value, auth::SecretBuffer{found->second}};
    }
    bool write_atomic(
        const auth::AuthFile file, const std::span<const std::byte> value) noexcept override
    {
        try {
            std::lock_guard lock{mutex_};
            files_[file] = auth::PublicBytes{value.begin(), value.end()};
            return true;
        }
        catch (...) { return false; }
    }
private:
    std::mutex mutex_;
    std::unordered_map<auth::AuthFile, auth::PublicBytes> files_;
};

class FakeClock final : public auth::AuthClock {
public:
    std::int64_t now_unix_seconds() noexcept override { return now; }
    std::int64_t now{1'700'000'000};
};

class FakeRandom final : public auth::AuthRandom {
public:
    bool fill(const std::span<std::byte> output) noexcept override
    {
        std::lock_guard lock{mutex_};
        for (auto& item : output) item = static_cast<std::byte>(next_++);
        return true;
    }
private:
    std::mutex mutex_;
    unsigned int next_{1};
};

[[nodiscard]] auth::SecretBytesResult fake_derive(
    const std::span<const std::byte> password,
    const std::span<const std::byte> salt) noexcept
{
    try {
        auth::SecretBuffer combined{password.size() + salt.size()};
        std::copy(password.begin(), password.end(), combined.mutable_bytes().begin());
        std::copy(salt.begin(), salt.end(), combined.mutable_bytes().begin() + password.size());
        auto digest = auth::sha256(combined.bytes());
        if (!digest) return {std::nullopt, digest.error};
        return {std::optional<auth::SecretBuffer>{std::in_place, *digest.value},
                auth::CryptoError::none};
    }
    catch (...) { return {std::nullopt, auth::CryptoError::resource_exhausted}; }
}

class FakeDeriver final : public auth::PasswordDeriver {
public:
    auth::SecretBytesResult derive(
        const std::span<const std::byte> password,
        const std::span<const std::byte> salt) noexcept override
    {
        return fake_derive(password, salt);
    }
};

struct SessionBundle {
    std::string id;
    std::int64_t expires{};
    std::uint64_t epoch{};
    auth::SecretBuffer ticket;
    auth::SecretBuffer master;
    auth::SecretBuffer resume;
};

struct AuthFixture {
    std::shared_ptr<MemoryStorage> storage{std::make_shared<MemoryStorage>()};
    std::shared_ptr<FakeClock> clock{std::make_shared<FakeClock>()};
    std::shared_ptr<FakeRandom> random{std::make_shared<FakeRandom>()};
    std::shared_ptr<FakeDeriver> deriver{std::make_shared<FakeDeriver>()};
    std::shared_ptr<auth::AuthOwner> owner;

    AuthFixture()
    {
        auto opened = auth::AuthOwner::open({}, {storage, clock, random, deriver, {}, {}});
        require(static_cast<bool>(opened), "AuthOwner open failed");
        owner = std::shared_ptr<auth::AuthOwner>{std::move(*opened.value)};
    }

    [[nodiscard]] SessionBundle session()
    {
        require(static_cast<bool>(owner->initialize_password(auth::SecretBuffer{bytes("pw")})),
                "password initialize failed");
        const auto state = owner->password_state();
        auto password_hash = fake_derive(bytes("pw"), state.pwd_salt);
        auth::PublicBytes shared(auth::auth_key_bytes, std::byte{0x42});
        auto transcript = auth::sha256(bytes("control-business-fixture"));
        auto context = auth::hkdf_sha256(
            shared, *transcript.value, bytes("auth-proof:1"), auth::auth_key_bytes);
        auto proof = auth::hmac_sha256(password_hash.value->bytes(), context.value->bytes());
        auto authenticated = owner->authenticate_control(
            {auth::SecretBuffer{shared}, *transcript.value}, *proof.value);
        require(static_cast<bool>(authenticated), "control authentication failed");
        auth::SecretBuffer combined{shared.size() + password_hash.value->size()};
        std::copy(shared.begin(), shared.end(), combined.mutable_bytes().begin());
        std::copy(password_hash.value->bytes().begin(), password_hash.value->bytes().end(),
                  combined.mutable_bytes().begin() + shared.size());
        auto master = auth::hkdf_sha256(
            combined.bytes(), *transcript.value, bytes("master-secret"), auth::auth_key_bytes);
        auto resume = auth::hkdf_sha256(
            master.value->bytes(), *transcript.value, bytes("resume-secret"), auth::auth_key_bytes);
        return {authenticated->session_id, authenticated->expires_at, authenticated->pwd_epoch,
                auth::SecretBuffer{authenticated->resume_ticket.bytes()},
                std::move(*master.value), std::move(*resume.value)};
    }
};

class RecordingOutbound final : public ws::OutboundSink {
public:
    ws::EnqueueResult enqueue(
        ws::OutboundBatch batch,
        std::shared_ptr<ws::BatchCompletion> completion) override
    {
        std::lock_guard lock{mutex};
        if (reject_next) {
            reject_next = false;
            if (completion) completion->complete(ws::BatchWriteResult::failed);
            return ws::EnqueueResult::queue_full;
        }
        batches.push_back(std::move(batch));
        if (defer_next) {
            defer_next = false;
            pending.push_back(std::move(completion));
        }
        else if (completion) completion->complete(ws::BatchWriteResult::written);
        return ws::EnqueueResult::accepted;
    }

    void terminate(const ws::TerminalAction action) noexcept override
    {
        ++terminate_calls;
        terminal.store(action);
    }

    void complete_one(const ws::BatchWriteResult result)
    {
        std::shared_ptr<ws::BatchCompletion> completion;
        {
            std::lock_guard lock{mutex};
            require(!pending.empty(), "missing deferred completion");
            completion = std::move(pending.front());
            pending.erase(pending.begin());
        }
        completion->complete(result);
    }

    std::mutex mutex;
    std::vector<ws::OutboundBatch> batches;
    std::vector<std::shared_ptr<ws::BatchCompletion>> pending;
    std::atomic<ws::TerminalAction> terminal{ws::TerminalAction::none};
    std::atomic_size_t terminate_calls{};
    bool reject_next{};
    bool defer_next{};
};

class RecordingBusinessCompletion final : public ws::BusinessBatchCompletion {
public:
    void complete(const ws::BusinessBatchWriteResult value) noexcept override
    {
        result.store(value);
        if (auto output = outbound.lock()) {
            callback_outside_outbound_lock.store(output->mutex.try_lock());
            if (callback_outside_outbound_lock.load()) output->mutex.unlock();
        }
        if (auto target = sink.lock()) {
            reentrant_emit.store(target->emit({"completion-reentry", false}));
        }
        calls.fetch_add(1);
    }

    std::weak_ptr<RecordingOutbound> outbound;
    std::weak_ptr<ws::BusinessPlaintextSink> sink;
    std::atomic<ws::BusinessBatchWriteResult> result{
        ws::BusinessBatchWriteResult::failed};
    std::atomic<ws::BusinessEmitResult> reentrant_emit{ws::BusinessEmitResult::closed};
    std::atomic_size_t calls{};
    std::atomic_bool callback_outside_outbound_lock{};
};

class LegacyPlaintextSink final : public ws::BusinessPlaintextSink {
public:
    using ws::BusinessPlaintextSink::emit;
    using ws::BusinessPlaintextSink::emit_batch;

    ws::BusinessEmitResult emit(ws::BusinessOutboundMessage) noexcept override
    {
        ++legacy_calls;
        return ws::BusinessEmitResult::accepted;
    }
    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage>) noexcept override
    {
        ++legacy_calls;
        return ws::BusinessEmitResult::accepted;
    }
    std::size_t legacy_calls{};
};

struct HandlerState {
    std::mutex mutex;
    std::shared_ptr<ws::BusinessPlaintextSink> sink;
    std::vector<std::string> inputs;
    std::optional<ws::BusinessCloseReason> closed;
    ws::BusinessEmitResult create_emit{ws::BusinessEmitResult::accepted};
    bool complete_on_ready{};
    std::size_t heartbeats{};
    std::atomic_size_t creates{};
    std::atomic_size_t ready_calls{};
};

class Handler final : public ws::BusinessChannelHandler {
public:
    explicit Handler(std::shared_ptr<HandlerState> state) : state_(std::move(state)) {}
    ws::BusinessHandlerResult ready(std::stop_token) override
    {
        ++state_->ready_calls;
        return {{}, state_->complete_on_ready
            ? ws::BusinessHandlerStatus::complete : ws::BusinessHandlerStatus::ok};
    }
    ws::BusinessHandlerResult input(
        auth::SecretBuffer plaintext, const bool, std::stop_token) override
    {
        std::lock_guard lock{state_->mutex};
        state_->inputs.push_back(text(plaintext.bytes()));
        return {};
    }
    ws::BusinessHandlerResult heartbeat(std::stop_token) override
    {
        std::lock_guard lock{state_->mutex};
        ++state_->heartbeats;
        return {};
    }
    void closed(const ws::BusinessCloseReason reason) noexcept override
    {
        std::lock_guard lock{state_->mutex};
        state_->closed = reason;
    }
private:
    std::shared_ptr<HandlerState> state_;
};

class HandlerFactory final : public ws::BusinessChannelHandlerFactory {
public:
    explicit HandlerFactory(std::shared_ptr<HandlerState> state) : state_(std::move(state)) {}
    ws::BusinessHandlerCreateResult create(
        ws::BusinessSessionContext context,
        std::shared_ptr<ws::BusinessPlaintextSink> output,
        std::stop_token) override
    {
        ++state_->creates;
        last_context = std::move(context);
        state_->sink = output;
        state_->create_emit = output->emit({"too-early", false});
        return {std::make_unique<Handler>(state_), ws::BusinessHandlerCreateError::none};
    }
    std::optional<ws::BusinessSessionContext> last_context;
private:
    std::shared_ptr<HandlerState> state_;
};

[[nodiscard]] ws::BusinessHandlerFactories handlers(
    const std::shared_ptr<HandlerFactory>& factory)
{
    return {factory, factory, factory, factory};
}

[[nodiscard]] std::string one_text(const ws::DriverResult& result)
{
    require(result.batches.size() == 1 && result.batches[0].frames.size() == 1,
            "expected one outbound frame");
    return result.batches[0].frames[0].payload;
}

struct OpenStream {
    std::unique_ptr<ws::SessionDriver> driver;
    std::shared_ptr<RecordingOutbound> outbound;
    std::shared_ptr<HandlerState> handler;
    auth::SecretStreamPush client_push;
    auth::SecretStreamPull client_pull;
    ws::TerminalAction ready_terminal{ws::TerminalAction::none};
};

[[nodiscard]] OpenStream open_stream(
    AuthFixture& fixture,
    SessionBundle& session,
    const ws::Channel wire_channel = ws::Channel::trigger,
    const std::shared_ptr<RecordingOutbound>& outbound = std::make_shared<RecordingOutbound>(),
    const ws::BusinessSessionConfig config = {},
    std::string* const captured_proof = nullptr,
    std::function<void()> before_ready = {},
    const ws::TerminalAction expected_ready_terminal = ws::TerminalAction::none)
{
    auto state = std::make_shared<HandlerState>();
    auto handler_factory = std::make_shared<HandlerFactory>(state);
    ws::BusinessSessionFactory factory{
        fixture.owner, handlers(handler_factory), config};
    ws::RequestMetadata request;
    request.channel = wire_channel;
    request.path = wire_channel == ws::Channel::provider ? "/ws/provider"
        : wire_channel == ws::Channel::sync ? "/ws/sync"
        : wire_channel == ws::Channel::remote ? "/ws/remote" : "/ws/trigger";
    auto driver = factory.create(request, outbound, {});

    auth::SecretBuffer private_key{auth::x25519_key_bytes};
    std::fill(private_key.mutable_bytes().begin(), private_key.mutable_bytes().end(),
              std::byte{0x6A});
    auto public_key = auth::x25519_public_key(private_key.bytes());
    auth::PublicBytes nonce(auth::x25519_key_bytes, std::byte{0x21});
    const auto channel = wire_channel == ws::Channel::provider ? "provider"
        : wire_channel == ws::Channel::sync ? "sync"
        : wire_channel == ws::Channel::remote ? "remote" : "trigger";
    const auto ticket = text(session.ticket.bytes());
    auth::CanonicalJsonValue client{auth::CanonicalJsonValue::Object{
        {"type", json_string("client_hello")}, {"kind", json_string("resume")},
        {"channel", json_string(channel)},
        {"version", auth::CanonicalJsonValue{std::int64_t{1}}},
        {"timestamp", auth::CanonicalJsonValue{std::int64_t{1'700'000'000'123}}},
        {"client_nonce", json_string(b64(nonce))},
        {"client_kx_pub", json_string(b64(*public_key.value))},
        {"session_id", json_string(session.id)}, {"socket_id", json_string("socket-1")},
        {"resume_ticket", json_string(ticket)},
    }};
    auto hello_result = driver->input({ws::FrameKind::text, encode(client)}, {});
    auto server = auth::parse_canonical_json_value(one_text(hello_result));
    require(static_cast<bool>(server), "server hello parse failed");
    auth::CanonicalJsonValue::Object server_core;
    for (const auto& [key, value] : *server.value->as_object()) {
        if (key != "signature" && key != "server_sign_pub") server_core.emplace_back(key, value);
    }
    auto transcript_text = encode(auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
        {"kind", json_string("resume")}, {"channel", json_string(channel)},
        {"client", client}, {"server", auth::CanonicalJsonValue{std::move(server_core)}},
    }});
    auto transcript_hash = auth::sha256(bytes(transcript_text));
    auto server_public = auth::decode_base64url_canonical(
        string_member(*server.value, "server_kx_pub"), auth::x25519_key_bytes);
    auto shared = auth::x25519_shared_secret(private_key.bytes(), *server_public.value);
    auto preauth_tx = auth::hkdf_sha256(
        shared.value->bytes(), *transcript_hash.value,
        bytes("preauth:server-rx"), auth::auth_key_bytes);
    auto preauth_rx = auth::hkdf_sha256(
        shared.value->bytes(), *transcript_hash.value,
        bytes("preauth:server-tx"), auth::auth_key_bytes);
    auto preauth = auth::SecureEnvelopeCipher::create(
        preauth_tx.value->bytes(), preauth_rx.value->bytes());
    auto transcript_b64 = b64(*transcript_hash.value);
    auto resume_context = encode(auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
        {"channel", json_string(channel)},
        {"pwd_epoch", auth::CanonicalJsonValue{static_cast<std::int64_t>(session.epoch)}},
        {"session_id", json_string(session.id)}, {"socket_id", json_string("socket-1")},
        {"transcript_hash", json_string(transcript_b64)},
    }});
    auto resume_mac = auth::hmac_sha256(session.resume.bytes(), bytes(resume_context));
    auto proof = preauth.value->encrypt(auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
        {"type", json_string("resume_proof")}, {"resume_mac", json_string(b64(*resume_mac.value))},
    }});
    if (captured_proof != nullptr) *captured_proof = proof.envelope;
    auto resume_result = driver->input({ws::FrameKind::text, proof.envelope}, {});
    auto resume_ok = preauth.value->decrypt(one_text(resume_result));
    require(resume_ok && string_member(*resume_ok.plaintext, "type") == "resume_ok",
            "resume_ok failed");

    auto scope = encode(auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
        {"channel", json_string(channel)},
        {"pwd_epoch", auth::CanonicalJsonValue{static_cast<std::int64_t>(session.epoch)}},
        {"scope", json_string("ws")}, {"session_id", json_string(session.id)},
        {"socket_id", json_string("socket-1")},
    }});
    auto base = auth::hkdf_sha256(
        session.master.bytes(), *transcript_hash.value, bytes(scope), auth::auth_key_bytes * 2);
    auto server_tx = auth::hkdf_sha256(
        base.value->bytes().first(auth::auth_key_bytes), *transcript_hash.value,
        bytes("secretstream:server-tx"), auth::auth_key_bytes);
    auto server_rx = auth::hkdf_sha256(
        base.value->bytes().subspan(auth::auth_key_bytes), *transcript_hash.value,
        bytes("secretstream:server-rx"), auth::auth_key_bytes);
    auto aad = encode(auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
        {"channel", json_string(channel)},
        {"pwd_epoch", auth::CanonicalJsonValue{static_cast<std::int64_t>(session.epoch)}},
        {"session_id", json_string(session.id)}, {"socket_id", json_string("socket-1")},
    }});
    auto client_push = auth::SecretStreamPush::create(server_rx.value->bytes(), bytes(aad));
    auto server_header = auth::decode_base64url_canonical(
        string_member(*resume_ok.plaintext, "server_header"), auth::secretstream_header_bytes);
    auto client_pull = auth::SecretStreamPull::create(
        server_tx.value->bytes(), *server_header.value, bytes(aad));
    auto ready = preauth.value->encrypt(auth::CanonicalJsonValue{auth::CanonicalJsonValue::Object{
        {"type", json_string("stream_ready")},
        {"client_header", json_string(b64(client_push.value->header()))},
    }});
    if (before_ready) before_ready();
    auto ready_result = driver->input({ws::FrameKind::text, ready.envelope}, {});
    if (expected_ready_terminal == ws::TerminalAction::none) {
        require(ready_result.phase == ws::SessionPhase::streaming
                    && ready_result.terminal == ws::TerminalAction::none,
                "stream_ready failed");
        check(state->create_emit == ws::BusinessEmitResult::closed,
              "handler factory cannot emit before secure sink activation");
    }
    else {
        require(ready_result.terminal == expected_ready_terminal,
                "stream_ready terminal mismatch");
    }
    return {std::move(driver), outbound, state,
            std::move(*client_push.value), std::move(*client_pull.value),
            ready_result.terminal};
}

[[nodiscard]] std::string decrypt_last(
    OpenStream& stream, const std::size_t batch, const std::size_t frame = 0)
{
    std::lock_guard lock{stream.outbound->mutex};
    auto& payload = stream.outbound->batches.at(batch).frames.at(frame).payload;
    auto result = stream.client_pull.pull(bytes(payload));
    require(static_cast<bool>(result), "client secretstream pull failed");
    return text(result.plaintext.bytes());
}

void test_immediate_atomic_async_and_crypto_failures()
{
    AuthFixture fixture;
    auto session = fixture.session();
    auto stream = open_stream(fixture, session);
    check(stream.handler->sink->emit({"immediate", false}) == ws::BusinessEmitResult::accepted,
          "async handler output must enqueue immediately without heartbeat");
    check(decrypt_last(stream, 0) == "immediate", "immediate output must decrypt");

    check(stream.handler->sink->emit_batch({{"json", false}, {"binary", false}})
              == ws::BusinessEmitResult::accepted,
          "two-part trigger output must enqueue atomically");
    {
        std::lock_guard lock{stream.outbound->mutex};
        check(stream.outbound->batches.at(1).frames.size() == 2,
              "JSON and binary output must share one writer batch");
    }
    check(decrypt_last(stream, 1, 0) == "json" && decrypt_last(stream, 1, 1) == "binary",
          "atomic batch preserves secretstream order");

    auto input = stream.client_push.push(bytes("client"));
    auto accepted = stream.driver->input({ws::FrameKind::binary, text(input.ciphertext)}, {});
    check(accepted.terminal == ws::TerminalAction::none
              && stream.handler->inputs.back() == "client",
          "authenticated binary input reaches handler");
    auto replay = stream.driver->input({ws::FrameKind::binary, text(input.ciphertext)}, {});
    check(replay.terminal == ws::TerminalAction::authentication_failed,
          "secretstream replay closes as authentication failure");
    stream.driver->closed();
    check(stream.handler->closed == ws::BusinessCloseReason::authentication_failed,
          "authentication failure is reported to handler");

    auto stream2 = open_stream(fixture, session);
    auto corrupt = stream2.client_push.push(bytes("bad"));
    corrupt.ciphertext.back() ^= std::byte{1};
    auto rejected = stream2.driver->input(
        {ws::FrameKind::binary, text(corrupt.ciphertext)}, {});
    check(rejected.terminal == ws::TerminalAction::authentication_failed,
          "corrupted ciphertext closes as authentication failure");

    std::string captured_proof;
    auto source = open_stream(
        fixture, session, ws::Channel::trigger,
        std::make_shared<RecordingOutbound>(), {}, &captured_proof);
    static_cast<void>(source);
    auto state = std::make_shared<HandlerState>();
    auto handler_factory = std::make_shared<HandlerFactory>(state);
    ws::BusinessSessionFactory factory{fixture.owner, handlers(handler_factory)};
    ws::RequestMetadata request;
    request.channel = ws::Channel::trigger;
    request.path = "/ws/trigger";
    auto other = factory.create(request, std::make_shared<RecordingOutbound>(), {});
    auth::SecretBuffer other_private{auth::x25519_key_bytes};
    std::fill(other_private.mutable_bytes().begin(), other_private.mutable_bytes().end(),
              std::byte{0x6A});
    auto other_public = auth::x25519_public_key(other_private.bytes());
    auth::PublicBytes other_nonce(auth::x25519_key_bytes, std::byte{0x21});
    auth::CanonicalJsonValue other_hello{auth::CanonicalJsonValue::Object{
        {"type", json_string("client_hello")}, {"kind", json_string("resume")},
        {"channel", json_string("trigger")},
        {"version", auth::CanonicalJsonValue{std::int64_t{1}}},
        {"timestamp", auth::CanonicalJsonValue{std::int64_t{1'700'000'000'123}}},
        {"client_nonce", json_string(b64(other_nonce))},
        {"client_kx_pub", json_string(b64(*other_public.value))},
        {"session_id", json_string(session.id)}, {"socket_id", json_string("socket-1")},
        {"resume_ticket", json_string(text(session.ticket.bytes()))},
    }};
    auto other_server = other->input({ws::FrameKind::text, encode(other_hello)}, {});
    require(other_server.terminal == ws::TerminalAction::none,
            "second handshake hello failed");
    check(other->input({ws::FrameKind::text, captured_proof}, {}).terminal
              == ws::TerminalAction::authentication_failed,
          "preauth proof cannot be substituted across handshakes");
}

void test_sink_failures_final_and_weak_lifetime()
{
    AuthFixture fixture;
    auto session = fixture.session();
    auto outbound = std::make_shared<RecordingOutbound>();
    auto stream = open_stream(fixture, session, ws::Channel::trigger, outbound);
    outbound->reject_next = true;
    check(stream.handler->sink->emit({"reject", false}) != ws::BusinessEmitResult::accepted
              && outbound->terminal == ws::TerminalAction::internal_error,
          "synchronous queue rejection atomically fails secure sink");

    auto outbound2 = std::make_shared<RecordingOutbound>();
    auto stream2 = open_stream(fixture, session, ws::Channel::trigger, outbound2);
    outbound2->defer_next = true;
    check(stream2.handler->sink->emit({"later-fail", false})
              == ws::BusinessEmitResult::accepted,
          "deferred write is initially accepted");
    outbound2->complete_one(ws::BatchWriteResult::failed);
    check(outbound2->terminal == ws::TerminalAction::internal_error,
          "asynchronous write failure terminates channel");
    stream2.driver->closed();
    check(stream2.handler->closed == ws::BusinessCloseReason::internal_error
              && outbound2->terminal == ws::TerminalAction::internal_error
              && outbound2->terminate_calls == 1,
          "direct teardown preserves async write failure close reason and terminal");

    auto outbound3 = std::make_shared<RecordingOutbound>();
    auto weak_outbound = std::weak_ptr<RecordingOutbound>{outbound3};
    auto stream3 = open_stream(fixture, session, ws::Channel::trigger, outbound3);
    auto retained_sink = stream3.handler->sink;
    stream3.driver->closed();
    stream3.driver.reset();
    stream3.outbound.reset();
    outbound3.reset();
    check(weak_outbound.expired()
              && retained_sink->emit({"after-close", false}) == ws::BusinessEmitResult::closed,
          "retained handler sink cannot keep owner slot alive");

    auto stream4 = open_stream(fixture, session);
    auto peer_final = stream4.client_push.push({}, auth::SecretStreamTag::final);
    auto final_result = stream4.driver->input(
        {ws::FrameKind::binary, text(peer_final.ciphertext)}, {});
    check(final_result.terminal == ws::TerminalAction::none
              && stream4.outbound->terminal == ws::TerminalAction::complete,
          "authenticated peer FINAL plus written server FINAL completes cleanly");
    stream4.driver->closed();
    check(stream4.handler->closed == ws::BusinessCloseReason::clean_final,
          "clean close requires both authenticated and written FINAL latches");
}

void test_observed_batch_write_receipts()
{
    AuthFixture fixture;
    auto session = fixture.session();

    auto written_stream = open_stream(fixture, session);
    written_stream.outbound->defer_next = true;
    auto written = std::make_shared<RecordingBusinessCompletion>();
    written->outbound = written_stream.outbound;
    written->sink = written_stream.handler->sink;
    check(written_stream.handler->sink->emit_batch(
              {{"json", false}, {"binary", false}}, written)
              == ws::BusinessEmitResult::accepted
              && written->calls.load() == 0,
          "accepted observed batch must wait for whole-batch write completion");
    written_stream.outbound->complete_one(ws::BatchWriteResult::written);
    check(written->calls.load() == 1
              && written->result.load() == ws::BusinessBatchWriteResult::written
              && written->callback_outside_outbound_lock.load()
              && written->reentrant_emit.load() == ws::BusinessEmitResult::accepted,
          "written receipt must run once outside outbound and secure-writer locks");

    auto failed_stream = open_stream(fixture, session);
    failed_stream.outbound->defer_next = true;
    auto failed = std::make_shared<RecordingBusinessCompletion>();
    failed->outbound = failed_stream.outbound;
    failed->sink = failed_stream.handler->sink;
    check(failed_stream.handler->sink->emit({"later-fail", false}, failed)
              == ws::BusinessEmitResult::accepted,
          "failed observed write must first be admitted");
    failed_stream.outbound->complete_one(ws::BatchWriteResult::failed);
    check(failed->calls.load() == 1
              && failed->result.load() == ws::BusinessBatchWriteResult::failed
              && failed->callback_outside_outbound_lock.load()
              && failed->reentrant_emit.load() == ws::BusinessEmitResult::closed
              && failed_stream.outbound->terminal == ws::TerminalAction::internal_error,
          "asynchronous failed receipt must fail the sink before one unlocked callback");

    auto rejected_stream = open_stream(fixture, session);
    rejected_stream.outbound->reject_next = true;
    auto rejected = std::make_shared<RecordingBusinessCompletion>();
    rejected->outbound = rejected_stream.outbound;
    rejected->sink = rejected_stream.handler->sink;
    check(rejected_stream.handler->sink->emit({"reject", false}, rejected)
              == ws::BusinessEmitResult::queue_full
              && rejected->calls.load() == 1
              && rejected->result.load() == ws::BusinessBatchWriteResult::failed
              && rejected->callback_outside_outbound_lock.load()
              && rejected->reentrant_emit.load() == ws::BusinessEmitResult::closed,
          "rejected observed admission must synchronously fail outside writer locks");

    auto synchronous_stream = open_stream(fixture, session);
    auto synchronous = std::make_shared<RecordingBusinessCompletion>();
    synchronous->outbound = synchronous_stream.outbound;
    synchronous->sink = synchronous_stream.handler->sink;
    check(synchronous_stream.handler->sink->emit({"written-now", false}, synchronous)
              == ws::BusinessEmitResult::accepted
              && synchronous->calls.load() == 1
              && synchronous->result.load() == ws::BusinessBatchWriteResult::written,
          "synchronously written accepted batch must complete before emit returns");

    LegacyPlaintextSink legacy;
    auto unsupported = std::make_shared<RecordingBusinessCompletion>();
    check(legacy.emit({"legacy", false}, unsupported)
              == ws::BusinessEmitResult::completion_unsupported
              && unsupported->calls.load() == 1
              && unsupported->result.load() == ws::BusinessBatchWriteResult::failed
              && legacy.legacy_calls == 0,
          "legacy sink remains source-compatible and fails observed admission explicitly");
}

void test_multi_producer_and_server_final_first()
{
    AuthFixture fixture;
    auto session = fixture.session();
    ws::BusinessSessionConfig small_output;
    small_output.max_handler_output_bytes = 8;
    auto bounded = open_stream(
        fixture, session, ws::Channel::trigger,
        std::make_shared<RecordingOutbound>(), small_output);
    check(bounded.handler->sink->emit_batch({{"12345", false}, {"67890", false}})
              == ws::BusinessEmitResult::message_too_large,
          "async batch enforces the aggregate plaintext bound");
    check(bounded.handler->sink->emit({"ok", false})
              == ws::BusinessEmitResult::accepted
              && decrypt_last(bounded, 0) == "ok",
          "rejected aggregate batch consumes no secretstream sequence");

    auto stream = open_stream(fixture, session);
    constexpr std::size_t producer_count = 8;
    std::vector<ws::BusinessEmitResult> results(
        producer_count, ws::BusinessEmitResult::closed);
    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t index = 0; index < producer_count; ++index) {
        producers.emplace_back([&, index] {
            results[index] = stream.handler->sink->emit(
                {"producer-" + std::to_string(index), false});
        });
    }
    for (auto& producer : producers) producer.join();
    check(std::all_of(results.begin(), results.end(), [](const auto result) {
              return result == ws::BusinessEmitResult::accepted;
          }), "all concurrent producers are admitted");
    std::vector<std::string> plaintexts;
    plaintexts.reserve(producer_count);
    for (std::size_t index = 0; index < producer_count; ++index) {
        plaintexts.push_back(decrypt_last(stream, index));
    }
    std::sort(plaintexts.begin(), plaintexts.end());
    for (std::size_t index = 0; index < producer_count; ++index) {
        check(plaintexts[index] == "producer-" + std::to_string(index),
              "writer mutex keeps secretstream sequence and queue order aligned");
    }

    auto server_first = open_stream(fixture, session);
    check(server_first.handler->sink->emit({"done", true})
              == ws::BusinessEmitResult::accepted,
          "handler may initiate secure FINAL");
    check(server_first.outbound->terminal == ws::TerminalAction::none,
          "written server FINAL still waits for authenticated peer FINAL");
    const auto heartbeats_before = server_first.handler->heartbeats;
    auto waiting = server_first.driver->heartbeat({});
    check(waiting.terminal == ws::TerminalAction::none
              && server_first.handler->heartbeats == heartbeats_before,
          "business heartbeat is suppressed after server FINAL");
    auto peer_final = server_first.client_push.push({}, auth::SecretStreamTag::final);
    auto closed = server_first.driver->input(
        {ws::FrameKind::binary, text(peer_final.ciphertext)}, {});
    check(closed.terminal == ws::TerminalAction::none
              && server_first.outbound->terminal == ws::TerminalAction::complete,
          "server-FINAL-first closes only after peer FINAL");
    {
        std::lock_guard lock{server_first.outbound->mutex};
        check(server_first.outbound->batches.size() == 1,
              "peer FINAL does not append a second server FINAL");
    }

    auto after_final = open_stream(fixture, session);
    check(after_final.handler->sink->emit({"done", true})
              == ws::BusinessEmitResult::accepted,
          "second server FINAL setup succeeds");
    auto late_message = after_final.client_push.push(bytes("late"));
    auto late = after_final.driver->input(
        {ws::FrameKind::binary, text(late_message.ciphertext)}, {});
    check(late.terminal == ws::TerminalAction::protocol_failed,
          "MESSAGE after server FINAL is a protocol failure");

    ws::BusinessSessionConfig short_timeout;
    short_timeout.final_close_timeout = std::chrono::milliseconds{1};
    auto timed = open_stream(
        fixture, session, ws::Channel::trigger,
        std::make_shared<RecordingOutbound>(), short_timeout);
    check(timed.handler->sink->emit({"done", true})
              == ws::BusinessEmitResult::accepted,
          "timeout stream server FINAL succeeds");
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    auto timeout = timed.driver->heartbeat({});
    check(timeout.terminal == ws::TerminalAction::protocol_failed
              && timed.handler->closed == ws::BusinessCloseReason::truncated,
          "missing peer FINAL is bounded and reported as truncation");
}

void test_ready_and_async_authorization_gates()
{
    AuthFixture ready_fixture;
    auto ready_session = ready_fixture.session();
    auto rejected = open_stream(
        ready_fixture, ready_session, ws::Channel::trigger,
        std::make_shared<RecordingOutbound>(), {}, nullptr,
        [&] {
            require(static_cast<bool>(ready_fixture.owner->change_password(
                ready_session.id, auth::SecretBuffer{bytes("revoked")})),
                "pre-ready password change failed");
        }, ws::TerminalAction::authentication_failed);
    check(rejected.handler->creates == 0 && rejected.handler->ready_calls == 0
              && !rejected.handler->sink,
          "revocation between resume_ok and stream_ready has no handler side effects");
    {
        std::lock_guard lock{rejected.outbound->mutex};
        check(rejected.outbound->batches.empty(),
              "pre-ready revocation emits no secure business output");
    }

    AuthFixture expiry_fixture;
    auto expiry_session = expiry_fixture.session();
    auto expired = open_stream(
        expiry_fixture, expiry_session, ws::Channel::trigger,
        std::make_shared<RecordingOutbound>(), {}, nullptr,
        [&] { expiry_fixture.clock->now = expiry_session.expires + 1; },
        ws::TerminalAction::authentication_failed);
    check(expired.handler->creates == 0 && expired.handler->ready_calls == 0,
          "expiry between resume_ok and stream_ready has no handler side effects");

    AuthFixture async_fixture;
    auto async_session = async_fixture.session();
    auto stream = open_stream(async_fixture, async_session);
    require(static_cast<bool>(async_fixture.owner->change_password(
        async_session.id, auth::SecretBuffer{bytes("revoked")})),
        "async gate password change failed");
    check(stream.handler->sink->emit({"must-not-escape", false})
              == ws::BusinessEmitResult::closed
              && stream.outbound->terminal
                  == ws::TerminalAction::authentication_failed,
          "async output checks authorization immediately and selects auth failure");
    {
        std::lock_guard lock{stream.outbound->mutex};
        check(stream.outbound->batches.empty(),
              "revoked async output is rejected before encryption and enqueue");
    }
    stream.driver->closed();
    check(stream.handler->closed == ws::BusinessCloseReason::authentication_failed
              && stream.outbound->terminal
                  == ws::TerminalAction::authentication_failed
              && stream.outbound->terminate_calls == 1,
          "direct teardown preserves async authorization close reason and terminal");
}

void test_late_completions_cannot_override_terminal()
{
    AuthFixture auth_fixture;
    auto auth_session = auth_fixture.session();
    auto auth_outbound = std::make_shared<RecordingOutbound>();
    auto auth_stream = open_stream(
        auth_fixture, auth_session, ws::Channel::trigger, auth_outbound);
    auth_outbound->defer_next = true;
    require(auth_stream.handler->sink->emit({"pending", false})
                == ws::BusinessEmitResult::accepted,
            "pending auth batch setup failed");
    auto corrupt = auth_stream.client_push.push(bytes("corrupt"));
    corrupt.ciphertext.back() ^= std::byte{1};
    auto auth_terminal = auth_stream.driver->input(
        {ws::FrameKind::binary, text(corrupt.ciphertext)}, {});
    require(auth_terminal.terminal == ws::TerminalAction::authentication_failed,
            "auth cleanup setup failed");
    auth_outbound->terminal.store(ws::TerminalAction::authentication_failed);
    auth_outbound->complete_one(ws::BatchWriteResult::failed);
    check(auth_outbound->terminal == ws::TerminalAction::authentication_failed,
          "late failed completion cannot overwrite authentication failure");

    AuthFixture protocol_fixture;
    auto protocol_session = protocol_fixture.session();
    auto protocol_outbound = std::make_shared<RecordingOutbound>();
    auto protocol_stream = open_stream(
        protocol_fixture, protocol_session, ws::Channel::trigger,
        protocol_outbound);
    protocol_outbound->defer_next = true;
    require(protocol_stream.handler->sink->emit({"final", true})
                == ws::BusinessEmitResult::accepted,
            "pending protocol FINAL setup failed");
    auto late = protocol_stream.client_push.push(bytes("late"));
    auto protocol_terminal = protocol_stream.driver->input(
        {ws::FrameKind::binary, text(late.ciphertext)}, {});
    require(protocol_terminal.terminal == ws::TerminalAction::protocol_failed,
            "protocol cleanup setup failed");
    protocol_outbound->terminal.store(ws::TerminalAction::protocol_failed);
    protocol_outbound->complete_one(ws::BatchWriteResult::written);
    check(protocol_outbound->terminal == ws::TerminalAction::protocol_failed,
          "late FINAL-written completion cannot overwrite protocol failure");

    AuthFixture timeout_fixture;
    auto timeout_session = timeout_fixture.session();
    auto timeout_outbound = std::make_shared<RecordingOutbound>();
    ws::BusinessSessionConfig config;
    config.final_close_timeout = std::chrono::milliseconds{1};
    auto timeout_stream = open_stream(
        timeout_fixture, timeout_session, ws::Channel::trigger,
        timeout_outbound, config);
    timeout_outbound->defer_next = true;
    require(timeout_stream.handler->sink->emit({"final", true})
                == ws::BusinessEmitResult::accepted,
            "pending timeout FINAL setup failed");
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    auto timeout_terminal = timeout_stream.driver->heartbeat({});
    require(timeout_terminal.terminal == ws::TerminalAction::protocol_failed
                && timeout_stream.handler->closed
                    == ws::BusinessCloseReason::truncated,
            "timeout cleanup setup failed");
    timeout_outbound->terminal.store(ws::TerminalAction::protocol_failed);
    timeout_outbound->complete_one(ws::BatchWriteResult::written);
    check(timeout_outbound->terminal == ws::TerminalAction::protocol_failed,
          "late FINAL-written completion cannot overwrite timeout terminal");
}

void test_secretstream_typed_error_mapping()
{
#if defined(BAAS_BUSINESS_SESSION_TEST_HOOKS)
    const auto create_case = [](const auth::SecretStreamError error,
                                const ws::TerminalAction expected) {
        AuthFixture fixture;
        auto session = fixture.session();
        auto stream = open_stream(
            fixture, session, ws::Channel::trigger,
            std::make_shared<RecordingOutbound>(), {}, nullptr,
            [=] { ws::detail::fail_next_business_pull_create_for_test(error); },
            expected);
        check(stream.handler->creates == 0 && stream.handler->ready_calls == 0,
              "pull-create error occurs before handler construction");
    };
    create_case(auth::SecretStreamError::resource_exhausted,
                ws::TerminalAction::internal_error);
    create_case(auth::SecretStreamError::initialization_failed,
                ws::TerminalAction::internal_error);
    create_case(auth::SecretStreamError::invalid_header,
                ws::TerminalAction::protocol_failed);

    const std::vector<std::pair<auth::SecretStreamError, ws::TerminalAction>> cases{
        {auth::SecretStreamError::resource_exhausted,
         ws::TerminalAction::internal_error},
        {auth::SecretStreamError::invalid_input,
         ws::TerminalAction::protocol_failed},
        {auth::SecretStreamError::unexpected_tag,
         ws::TerminalAction::protocol_failed},
        {auth::SecretStreamError::sequence_exhausted,
         ws::TerminalAction::protocol_failed},
        {auth::SecretStreamError::stream_closed,
         ws::TerminalAction::protocol_failed},
        {auth::SecretStreamError::authentication_failed,
         ws::TerminalAction::authentication_failed},
    };
    for (const auto [error, expected] : cases) {
        AuthFixture fixture;
        auto session = fixture.session();
        auto stream = open_stream(fixture, session);
        auto ciphertext = stream.client_push.push(bytes("typed-error"));
        ws::detail::fail_next_business_pull_for_test(error);
        auto result = stream.driver->input(
            {ws::FrameKind::binary, text(ciphertext.ciphertext)}, {});
        check(result.terminal == expected,
              std::string{"typed pull error mapping: "}
                  + std::string{auth::secretstream_error_name(error)});
    }
    AuthFixture short_fixture;
    auto short_session = short_fixture.session();
    auto short_stream = open_stream(short_fixture, short_session);
    check(short_stream.driver->input(
              {ws::FrameKind::binary, std::string(1, 'x')}, {}).terminal
              == ws::TerminalAction::protocol_failed,
          "real short ciphertext maps typed invalid_input to protocol failure");
#endif
}

void test_revocation_routing_and_platform_policy()
{
    AuthFixture fixture;
    auto session = fixture.session();
    auto default_state = std::make_shared<HandlerState>();
    auto default_factory = std::make_shared<HandlerFactory>(default_state);
    ws::BusinessSessionFactory default_config_factory{
        fixture.owner, handlers(default_factory)};
    ws::RequestMetadata expired_request;
    expired_request.channel = ws::Channel::trigger;
    expired_request.path = "/ws/trigger";
    expired_request.handshake_timeout = std::chrono::milliseconds::zero();
    auto expired_driver = default_config_factory.create(
        expired_request, std::make_shared<RecordingOutbound>(), {});
    check(expired_driver->heartbeat({}).terminal
              == ws::TerminalAction::authentication_failed,
          "scheduler heartbeat enforces the business handshake deadline");
    auto stream = open_stream(fixture, session, ws::Channel::provider);
    require(static_cast<bool>(fixture.owner->change_password(
        session.id, auth::SecretBuffer{bytes("next")})), "password change failed");
    auto heartbeat = stream.driver->heartbeat({});
    check(heartbeat.terminal == ws::TerminalAction::authentication_failed,
          "business heartbeat observes atomic revocation");

    AuthFixture input_fixture;
    auto input_session = input_fixture.session();
    auto input_stream = open_stream(
        input_fixture, input_session, ws::Channel::sync);
    require(static_cast<bool>(input_fixture.owner->change_password(
        input_session.id, auth::SecretBuffer{bytes("next")})),
        "input revocation password change failed");
    auto ciphertext = input_stream.client_push.push(bytes("after-revoke"));
    check(input_stream.driver->input(
              {ws::FrameKind::binary, text(ciphertext.ciphertext)}, {}).terminal
              == ws::TerminalAction::authentication_failed,
          "every streaming input observes atomic revocation");

    auto state = std::make_shared<HandlerState>();
    auto handler_factory = std::make_shared<HandlerFactory>(state);
    ws::ProductionSessionFactory production{
        fixture.owner, handlers(handler_factory), {}, {}, ws::RemoteChannelPolicy::disabled};
    auto sink = std::make_shared<RecordingOutbound>();
    ws::RequestMetadata unknown;
    unknown.channel = ws::Channel::trigger;
    unknown.path = "/ws/provider";
    auto rejected = production.create(unknown, sink, {});
    check(rejected->input({ws::FrameKind::text, "{}"}, {}).terminal
              == ws::TerminalAction::protocol_failed,
          "production multiplexer rejects channel/path confusion");
    ws::RequestMetadata remote;
    remote.channel = ws::Channel::remote;
    remote.path = "/ws/remote";
    auto remote_rejected = production.create(remote, sink, {});
    check(remote_rejected->input({ws::FrameKind::text, "{}"}, {}).terminal
              == ws::TerminalAction::protocol_failed,
          "explicit remote-disabled policy rejects desktop route");
}

void test_remote_raw_output_is_one_way_and_outbound_only()
{
    {
        AuthFixture fixture;
        auto session = fixture.session();
        auto stream = open_stream(fixture, session, ws::Channel::trigger);
        check(!stream.handler->sink->enable_remote_raw_output(),
              "non-remote channels must reject raw output");
    }
    {
        AuthFixture fixture;
        auto session = fixture.session();
        auto stream = open_stream(fixture, session, ws::Channel::remote);
        check(stream.handler->sink->emit({"encrypted-remote", false})
                  == ws::BusinessEmitResult::accepted,
              "remote output remains encrypted by default");
        check(decrypt_last(stream, 0) == "encrypted-remote",
              "default remote output must preserve secretstream parity");
        check(!stream.handler->sink->enable_remote_raw_output(),
              "raw output cannot be enabled after business output starts");
    }
    {
        AuthFixture fixture;
        auto session = fixture.session();
        auto stream = open_stream(fixture, session, ws::Channel::remote);
        check(stream.handler->sink->enable_remote_raw_output(),
              "remote channel may enable compatibility raw output once");
        check(!stream.handler->sink->enable_remote_raw_output(),
              "remote raw output switch is one-way and non-repeatable");
        const std::string raw{"\0scrcpy\xFF", 8};
        check(stream.handler->sink->emit({raw, false})
                  == ws::BusinessEmitResult::accepted,
              "raw remote output must be admitted");
        {
            std::lock_guard lock{stream.outbound->mutex};
            check(stream.outbound->batches.size() == 1
                      && stream.outbound->batches.front().frames.size() == 1
                      && stream.outbound->batches.front().frames.front().kind
                          == ws::FrameKind::binary
                      && stream.outbound->batches.front().frames.front().payload == raw,
                  "raw mode must preserve ADB bytes exactly");
        }

        auto encrypted_input = stream.client_push.push(bytes("authenticated-adb"));
        const auto accepted = stream.driver->input(
            {ws::FrameKind::binary, text(encrypted_input.ciphertext)}, {});
        check(accepted.terminal == ws::TerminalAction::none
                  && stream.handler->inputs.back() == "authenticated-adb",
              "raw outbound mode must keep inbound secretstream authentication");

        auto peer_final = stream.client_push.push(
            {}, auth::SecretStreamTag::final);
        const auto finished = stream.driver->input(
            {ws::FrameKind::binary, text(peer_final.ciphertext)}, {});
        const auto settled = finished.terminal == ws::TerminalAction::complete
            ? finished : stream.driver->heartbeat({});
        check(settled.terminal == ws::TerminalAction::complete,
              "raw remote peer FINAL must close cleanly");
        {
            std::lock_guard lock{stream.outbound->mutex};
            check(stream.outbound->batches.size() == 1,
                  "raw close must not expose an internal empty FINAL packet");
        }
    }
    {
        AuthFixture fixture;
        auto session = fixture.session();
        auto stream = open_stream(fixture, session, ws::Channel::remote);
        require(stream.handler->sink->enable_remote_raw_output(),
                "remote raw setup failed");
        check(stream.driver->input(
                  {ws::FrameKind::binary, "unauthenticated-adb"}, {}).terminal
                  != ws::TerminalAction::none,
              "raw client bytes must never bypass inbound secretstream");
    }
}

}  // namespace

int main()
{
    try {
        test_immediate_atomic_async_and_crypto_failures();
        test_sink_failures_final_and_weak_lifetime();
        test_observed_batch_write_receipts();
        test_multi_producer_and_server_final_first();
        test_ready_and_async_authorization_gates();
        test_late_completions_cannot_override_terminal();
        test_secretstream_typed_error_mapping();
        test_revocation_routing_and_platform_policy();
        test_remote_raw_output_is_one_way_and_outbound_only();
    }
    catch (const std::exception& error) {
        std::cerr << "FAILED with exception: " << error.what() << '\n';
        ++failures;
    }
    if (failures != 0) {
        std::cerr << failures << " business session test(s) failed\n";
        return 1;
    }
    std::cout << "service business session tests passed\n";
    return 0;
}
