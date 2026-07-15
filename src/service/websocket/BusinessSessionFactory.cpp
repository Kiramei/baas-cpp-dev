#include "service/websocket/BusinessSessionFactory.h"

#include "service/auth/CanonicalJson.h"
#include "service/auth/SecretStream.h"
#include "service/auth/SecureEnvelope.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace baas::service::websocket {
namespace {

using auth::CanonicalJsonValue;
using Clock = std::chrono::steady_clock;

constexpr std::int64_t protocol_version = 1;

[[nodiscard]] std::span<const std::byte> bytes_of(const std::string_view value) noexcept
{
    return std::as_bytes(std::span{value.data(), value.size()});
}

[[nodiscard]] std::string byte_string(const std::span<const std::byte> value)
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
    return object != nullptr && object->size() == fields.size()
        && std::all_of(fields.begin(), fields.end(), [&](const auto field) {
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

class CanonicalStringWiper final {
public:
    explicit CanonicalStringWiper(CanonicalJsonValue& value) noexcept : value_(value) {}
    ~CanonicalStringWiper() { value_.wipe_strings(); }
private:
    CanonicalJsonValue& value_;
};

class OutboundMessageWiper final {
public:
    explicit OutboundMessageWiper(BusinessOutboundMessage& message) noexcept
        : message_(&message)
    {}
    explicit OutboundMessageWiper(
        std::vector<BusinessOutboundMessage>& messages) noexcept
        : messages_(&messages)
    {}
    ~OutboundMessageWiper()
    {
        if (message_ != nullptr) wipe(*message_);
        if (messages_ != nullptr) {
            for (auto& message : *messages_) wipe(message);
        }
    }
private:
    static void wipe(BusinessOutboundMessage& message) noexcept
    {
        auth::secure_zero(std::as_writable_bytes(
            std::span{message.payload.data(), message.payload.size()}));
        message.payload.clear();
    }
    BusinessOutboundMessage* message_{};
    std::vector<BusinessOutboundMessage>* messages_{};
};

[[nodiscard]] DriverResult terminal_result(
    const SessionPhase phase, const TerminalAction action) noexcept
{
    DriverResult result;
    result.phase = phase;
    result.terminal = action;
    return result;
}

[[nodiscard]] OutboundBatch text_batch(std::string payload)
{
    OutboundBatch batch;
    batch.frames.push_back({FrameKind::text, std::move(payload)});
    return batch;
}

[[nodiscard]] TerminalAction terminal_for_auth(const auth::AuthError error) noexcept
{
    switch (error) {
        case auth::AuthError::capacity_exceeded:
        case auth::AuthError::password_derivation_busy:
            return TerminalAction::capacity;
        case auth::AuthError::entropy_failure:
        case auth::AuthError::storage_failure:
        case auth::AuthError::corrupted_storage:
        case auth::AuthError::crypto_failure:
        case auth::AuthError::password_derivation_failed:
            return TerminalAction::internal_error;
        case auth::AuthError::none: return TerminalAction::none;
        default: return TerminalAction::authentication_failed;
    }
}

[[nodiscard]] TerminalAction terminal_for_secretstream(
    const auth::SecretStreamError error) noexcept
{
    switch (error) {
        case auth::SecretStreamError::none: return TerminalAction::none;
        case auth::SecretStreamError::authentication_failed:
            return TerminalAction::authentication_failed;
        case auth::SecretStreamError::initialization_failed:
        case auth::SecretStreamError::invalid_key:
        case auth::SecretStreamError::resource_exhausted:
            return TerminalAction::internal_error;
        case auth::SecretStreamError::invalid_header:
        case auth::SecretStreamError::invalid_input:
        case auth::SecretStreamError::message_too_large:
        case auth::SecretStreamError::unexpected_tag:
        case auth::SecretStreamError::sequence_exhausted:
        case auth::SecretStreamError::stream_closed:
        case auth::SecretStreamError::poisoned:
            return TerminalAction::protocol_failed;
    }
    return TerminalAction::internal_error;
}

[[nodiscard]] BusinessCloseReason close_reason_for_terminal(
    const TerminalAction terminal) noexcept
{
    switch (terminal) {
        case TerminalAction::authentication_failed:
            return BusinessCloseReason::authentication_failed;
        case TerminalAction::protocol_failed:
            return BusinessCloseReason::protocol_failed;
        case TerminalAction::none:
        case TerminalAction::capacity:
        case TerminalAction::internal_error:
        case TerminalAction::complete:
            return BusinessCloseReason::internal_error;
    }
    return BusinessCloseReason::internal_error;
}

#if defined(BAAS_BUSINESS_SESSION_TEST_HOOKS)
std::atomic<auth::SecretStreamError> injected_pull_create_error{
    auth::SecretStreamError::none};
std::atomic<auth::SecretStreamError> injected_pull_error{
    auth::SecretStreamError::none};
#endif

[[nodiscard]] auth::SecretStreamCreateResult<auth::SecretStreamPull> create_pull(
    const std::span<const std::byte> key,
    const std::span<const std::byte> header,
    const std::span<const std::byte> aad)
{
#if defined(BAAS_BUSINESS_SESSION_TEST_HOOKS)
    const auto injected = injected_pull_create_error.exchange(
        auth::SecretStreamError::none, std::memory_order_acq_rel);
    if (injected != auth::SecretStreamError::none) return {std::nullopt, injected};
#endif
    return auth::SecretStreamPull::create(key, header, aad);
}

[[nodiscard]] auth::SecretStreamDecryptResult pull_ciphertext(
    auth::SecretStreamPull& pull,
    const std::span<const std::byte> ciphertext)
{
#if defined(BAAS_BUSINESS_SESSION_TEST_HOOKS)
    const auto injected = injected_pull_error.exchange(
        auth::SecretStreamError::none, std::memory_order_acq_rel);
    if (injected != auth::SecretStreamError::none) {
        auth::SecretStreamDecryptResult result;
        result.error = injected;
        return result;
    }
#endif
    return pull.pull(ciphertext);
}

[[nodiscard]] TerminalAction terminal_for_handler(
    const BusinessHandlerStatus status) noexcept
{
    switch (status) {
        case BusinessHandlerStatus::ok: return TerminalAction::none;
        case BusinessHandlerStatus::protocol_failed: return TerminalAction::protocol_failed;
        case BusinessHandlerStatus::capacity: return TerminalAction::capacity;
        case BusinessHandlerStatus::internal_error: return TerminalAction::internal_error;
        case BusinessHandlerStatus::complete: return TerminalAction::complete;
    }
    return TerminalAction::internal_error;
}

[[nodiscard]] std::optional<auth::BusinessChannel> auth_channel(
    const Channel channel) noexcept
{
    switch (channel) {
        case Channel::provider: return auth::BusinessChannel::provider;
        case Channel::sync: return auth::BusinessChannel::sync;
        case Channel::trigger: return auth::BusinessChannel::trigger;
        case Channel::remote: return auth::BusinessChannel::remote;
        case Channel::control: return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view path_for(const Channel channel) noexcept
{
    switch (channel) {
        case Channel::control: return "/ws/control";
        case Channel::provider: return "/ws/provider";
        case Channel::sync: return "/ws/sync";
        case Channel::trigger: return "/ws/trigger";
        case Channel::remote: return "/ws/remote";
    }
    return {};
}

class UnsupportedDriver final : public SessionDriver {
public:
    DriverResult input(Frame, std::stop_token) override
    {
        return terminal_result(SessionPhase::handshaking, TerminalAction::protocol_failed);
    }
    DriverResult heartbeat(std::stop_token) override
    {
        return terminal_result(SessionPhase::handshaking, TerminalAction::protocol_failed);
    }
    void closed() noexcept override {}
};

// No driver or sink mutex is held while calling AuthOwner. Revocation and
// validity are linearized by AuthOwner itself; the atomic failure latch makes
// the first observed terminal decision sticky across concurrent producers.
class SessionAuthorizationGate final {
public:
    SessionAuthorizationGate(
        std::weak_ptr<auth::AuthOwner> authentication,
        const auth::SubscriptionId subscription,
        std::string session_id)
        : authentication_(std::move(authentication)),
          subscription_(subscription),
          session_id_(std::move(session_id))
    {}

    [[nodiscard]] TerminalAction authorize() noexcept
    {
        if (!active_.load(std::memory_order_acquire)) {
            return TerminalAction::complete;
        }
        const auto prior = terminal_.load(std::memory_order_acquire);
        if (prior != TerminalAction::none) return prior;
        auto authentication = authentication_.lock();
        if (!authentication) return publish(TerminalAction::internal_error);
        auto events = authentication->drain_revocations(subscription_, 1);
        if (!active_.load(std::memory_order_acquire)) {
            return TerminalAction::complete;
        }
        if (!events) return publish(terminal_for_auth(events.error));
        if (!events->empty()) {
            return publish(TerminalAction::authentication_failed);
        }
        const auto valid = authentication->validate_session(session_id_);
        if (!active_.load(std::memory_order_acquire)) {
            return TerminalAction::complete;
        }
        if (!valid) return publish(terminal_for_auth(valid.error));
        return terminal_.load(std::memory_order_acquire);
    }

    void deactivate() noexcept
    {
        active_.store(false, std::memory_order_release);
    }

private:
    [[nodiscard]] TerminalAction publish(const TerminalAction terminal) noexcept
    {
        auto expected = TerminalAction::none;
        terminal_.compare_exchange_strong(
            expected, terminal, std::memory_order_acq_rel,
            std::memory_order_acquire);
        return terminal_.load(std::memory_order_acquire);
    }

    std::weak_ptr<auth::AuthOwner> authentication_;
    auth::SubscriptionId subscription_{};
    std::string session_id_;
    std::atomic_bool active_{true};
    std::atomic<TerminalAction> terminal_{TerminalAction::none};
};

class SecureBusinessSink final
    : public BusinessPlaintextSink,
      public std::enable_shared_from_this<SecureBusinessSink> {
public:
    SecureBusinessSink(
        auth::SecretStreamPush push,
        std::weak_ptr<OutboundSink> outbound,
        std::shared_ptr<SessionAuthorizationGate> authorization,
        const std::size_t maximum_plaintext)
        : push_(std::move(push)),
          outbound_(outbound),
          authorization_(std::move(authorization)),
          maximum_plaintext_(maximum_plaintext)
    {}

    void activate() noexcept { active_.store(true, std::memory_order_release); }

    void close() noexcept
    {
        auto expected = SinkTerminal::open;
        terminal_.compare_exchange_strong(
            expected, SinkTerminal::silently_closed,
            std::memory_order_acq_rel, std::memory_order_acquire);
        active_.store(false, std::memory_order_release);
        try {
            std::lock_guard lock{writer_mutex_};
            push_.reset();
        }
        catch (...) {}
    }

    [[nodiscard]] TerminalAction terminal_action() const noexcept
    {
        switch (terminal_.load(std::memory_order_acquire)) {
            case SinkTerminal::authentication_failed:
                return TerminalAction::authentication_failed;
            case SinkTerminal::protocol_failed:
                return TerminalAction::protocol_failed;
            case SinkTerminal::capacity: return TerminalAction::capacity;
            case SinkTerminal::internal_error:
                return TerminalAction::internal_error;
            case SinkTerminal::complete: return TerminalAction::complete;
            case SinkTerminal::open:
            case SinkTerminal::silently_closed:
                return TerminalAction::none;
        }
        return TerminalAction::internal_error;
    }

    [[nodiscard]] bool server_final_sent() const noexcept
    {
        return server_final_enqueued_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<Clock::time_point> server_final_enqueued_at() const noexcept
    {
        if (!server_final_sent()) return std::nullopt;
        return Clock::time_point{Clock::duration{
            server_final_tick_.load(std::memory_order_acquire)}};
    }

    [[nodiscard]] bool clean_final() const noexcept
    {
        return peer_final_.load(std::memory_order_acquire)
            && server_final_written_.load(std::memory_order_acquire);
    }

    void authenticated_peer_final() noexcept
    {
        peer_final_.store(true, std::memory_order_release);
        maybe_complete();
    }

    [[nodiscard]] BusinessEmitResult emit(BusinessOutboundMessage message) noexcept override
    {
        return emit(std::move(message), {});
    }

    [[nodiscard]] BusinessEmitResult emit(
        BusinessOutboundMessage message,
        std::shared_ptr<BusinessBatchCompletion> completion) noexcept override
    {
        OutboundMessageWiper message_wiper{message};
        std::vector<BusinessOutboundMessage> batch;
        try {
            batch.push_back(std::move(message));
        }
        catch (...) {
            fail();
            if (completion) completion->complete(BusinessBatchWriteResult::failed);
            return BusinessEmitResult::resource_exhausted;
        }
        return emit_batch(std::move(batch), std::move(completion));
    }

    [[nodiscard]] BusinessEmitResult emit_batch(
        std::vector<BusinessOutboundMessage> messages) noexcept override
    {
        return emit_batch(std::move(messages), {});
    }

    [[nodiscard]] BusinessEmitResult emit_batch(
        std::vector<BusinessOutboundMessage> messages,
        std::shared_ptr<BusinessBatchCompletion> completion) noexcept override
    {
        OutboundMessageWiper messages_wiper{messages};
        const auto reject = [&completion](const BusinessEmitResult result) noexcept {
            if (completion) completion->complete(BusinessBatchWriteResult::failed);
            return result;
        };
        if (!active_.load(std::memory_order_acquire)
            || !is_open()) return reject(BusinessEmitResult::closed);
        if (messages.empty() || messages.size() > 2)
            return reject(BusinessEmitResult::queue_full);
        const auto authorized = authorization_->authorize();
        if (authorized == TerminalAction::complete)
            return reject(BusinessEmitResult::closed);
        if (authorized != TerminalAction::none) {
            terminate_once(authorized);
            return reject(BusinessEmitResult::closed);
        }
        try {
            std::unique_lock lock{writer_mutex_};
            if (!active_.load(std::memory_order_acquire)
                || !is_open()) {
                lock.unlock();
                return reject(BusinessEmitResult::closed);
            }
            if (server_final_enqueued_.load(std::memory_order_acquire)) {
                lock.unlock();
                return reject(BusinessEmitResult::closed);
            }
            OutboundBatch batch;
            batch.frames.reserve(messages.size());
            bool contains_final = false;
            std::size_t total_plaintext = 0;
            for (std::size_t index = 0; index < messages.size(); ++index) {
                const auto& message = messages[index];
                if (message.payload.size() > maximum_plaintext_ - total_plaintext
                    || (message.final && index + 1 != messages.size())) {
                    lock.unlock();
                    return reject(BusinessEmitResult::message_too_large);
                }
                total_plaintext += message.payload.size();
                contains_final = message.final;
            }
            for (auto& message : messages) {
                if (!push_) {
                    lock.unlock();
                    return reject(BusinessEmitResult::closed);
                }
                auto encrypted = push_->push(
                    bytes_of(message.payload),
                    message.final ? auth::SecretStreamTag::final
                                  : auth::SecretStreamTag::message);
                if (!encrypted) {
                    const auto result =
                        encrypted.error == auth::SecretStreamError::resource_exhausted
                        ? BusinessEmitResult::resource_exhausted
                        : BusinessEmitResult::closed;
                    lock.unlock();
                    fail();
                    return reject(result);
                }
                batch.frames.push_back({
                    FrameKind::binary, byte_string(encrypted.ciphertext)});
            }
            class Completion final : public BatchCompletion {
            public:
                Completion(
                    std::weak_ptr<SecureBusinessSink> owner,
                    const bool contains_final,
                    std::shared_ptr<BusinessBatchCompletion> observer) noexcept
                    : owner_(std::move(owner)), contains_final_(contains_final),
                      observer_(std::move(observer))
                {}
                void complete(const BatchWriteResult result) noexcept override
                {
                    bool deliver{};
                    {
                        std::lock_guard lock(mutex_);
                        if (completed_) return;
                        completed_ = true;
                        result_ = result;
                        if (armed_) {
                            delivered_ = true;
                            deliver = true;
                        }
                    }
                    if (deliver) deliver_result(result);
                }
                void reject_and_arm() noexcept
                {
                    complete(BatchWriteResult::failed);
                    arm();
                }
                void arm() noexcept
                {
                    bool deliver{};
                    BatchWriteResult result{BatchWriteResult::failed};
                    {
                        std::lock_guard lock(mutex_);
                        armed_ = true;
                        if (completed_ && !delivered_) {
                            delivered_ = true;
                            result = result_;
                            deliver = true;
                        }
                    }
                    if (deliver) deliver_result(result);
                }
            private:
                void deliver_result(const BatchWriteResult result) noexcept
                {
                    if (auto owner = owner_.lock()) {
                        if (result == BatchWriteResult::failed) owner->fail();
                        else if (contains_final_) owner->final_written();
                    }
                    if (observer_) {
                        observer_->complete(
                            result == BatchWriteResult::written
                                ? BusinessBatchWriteResult::written
                                : BusinessBatchWriteResult::failed);
                    }
                }
                std::mutex mutex_;
                std::weak_ptr<SecureBusinessSink> owner_;
                bool contains_final_{};
                std::shared_ptr<BusinessBatchCompletion> observer_;
                BatchWriteResult result_{BatchWriteResult::failed};
                bool armed_{};
                bool completed_{};
                bool delivered_{};
            };
            auto bridge = std::make_shared<Completion>(
                weak_from_this(), contains_final, completion);
            auto outbound = outbound_.lock();
            if (!outbound) {
                lock.unlock();
                bridge->reject_and_arm();
                fail();
                return BusinessEmitResult::closed;
            }
            const auto enqueued = outbound->enqueue(std::move(batch), bridge);
            if (enqueued == EnqueueResult::accepted) {
                if (contains_final) {
                    server_final_tick_.store(
                        Clock::now().time_since_epoch().count(),
                        std::memory_order_release);
                    server_final_enqueued_.store(true, std::memory_order_release);
                }
                lock.unlock();
                bridge->arm();
                return BusinessEmitResult::accepted;
            }
            lock.unlock();
            bridge->reject_and_arm();
            switch (enqueued) {
                case EnqueueResult::queue_full: return BusinessEmitResult::queue_full;
                case EnqueueResult::queued_bytes_exceeded:
                    return BusinessEmitResult::queued_bytes_exceeded;
                case EnqueueResult::frame_too_large:
                    return BusinessEmitResult::message_too_large;
                case EnqueueResult::resource_exhausted:
                    return BusinessEmitResult::resource_exhausted;
                default: return BusinessEmitResult::closed;
            }
        }
        catch (...) {
            fail();
            return reject(BusinessEmitResult::resource_exhausted);
        }
    }

private:
    enum class SinkTerminal : std::uint8_t {
        open,
        silently_closed,
        authentication_failed,
        protocol_failed,
        capacity,
        internal_error,
        complete,
    };

    [[nodiscard]] bool is_open() const noexcept
    {
        return terminal_.load(std::memory_order_acquire) == SinkTerminal::open;
    }

    [[nodiscard]] static SinkTerminal sink_terminal(
        const TerminalAction terminal) noexcept
    {
        switch (terminal) {
            case TerminalAction::authentication_failed:
                return SinkTerminal::authentication_failed;
            case TerminalAction::protocol_failed:
                return SinkTerminal::protocol_failed;
            case TerminalAction::capacity: return SinkTerminal::capacity;
            case TerminalAction::internal_error:
                return SinkTerminal::internal_error;
            case TerminalAction::complete: return SinkTerminal::complete;
            case TerminalAction::none: return SinkTerminal::open;
        }
        return SinkTerminal::internal_error;
    }

    void terminate_once(const TerminalAction action) noexcept
    {
        auto expected = SinkTerminal::open;
        if (!terminal_.compare_exchange_strong(
                expected, sink_terminal(action), std::memory_order_acq_rel,
                std::memory_order_acquire)) return;
        active_.store(false, std::memory_order_release);
        if (auto outbound = outbound_.lock()) outbound->terminate(action);
    }

    void fail() noexcept
    {
        terminate_once(TerminalAction::internal_error);
    }

    void final_written() noexcept
    {
        if (!is_open()) return;
        server_final_written_.store(true, std::memory_order_release);
        maybe_complete();
    }

    void maybe_complete() noexcept
    {
        if (!clean_final()) return;
        terminate_once(TerminalAction::complete);
    }

    std::mutex writer_mutex_;
    std::optional<auth::SecretStreamPush> push_;
    std::weak_ptr<OutboundSink> outbound_;
    std::shared_ptr<SessionAuthorizationGate> authorization_;
    std::size_t maximum_plaintext_{};
    std::atomic_bool active_{false};
    std::atomic<SinkTerminal> terminal_{SinkTerminal::open};
    std::atomic_bool peer_final_{false};
    std::atomic_bool server_final_enqueued_{false};
    std::atomic_bool server_final_written_{false};
    std::atomic<Clock::duration::rep> server_final_tick_{0};
};

class BusinessSessionDriver final : public SessionDriver {
public:
    BusinessSessionDriver(
        std::shared_ptr<auth::AuthOwner> authentication,
        std::shared_ptr<BusinessChannelHandlerFactory> handlers,
        std::shared_ptr<OutboundSink> outbound,
        BusinessSessionConfig config,
        const auth::BusinessChannel channel,
        const Clock::time_point deadline)
        : authentication_(std::move(authentication)),
          handlers_(std::move(handlers)),
          outbound_(std::move(outbound)),
          config_(config),
          channel_(channel),
          handshake_deadline_(deadline)
    {}

    ~BusinessSessionDriver() override { closed(); }

    DriverResult input(Frame frame, const std::stop_token stop) override
    {
        try {
            if (stop.stop_requested()) return finish(TerminalAction::complete,
                                                     BusinessCloseReason::stopped);
            if (step_ != Step::streaming && step_ != Step::closed
                && Clock::now() >= handshake_deadline_) {
                return finish(TerminalAction::authentication_failed,
                              BusinessCloseReason::authentication_failed);
            }
            switch (step_) {
                case Step::client_hello: return client_hello(std::move(frame));
                case Step::resume_proof: return resume_proof(std::move(frame), stop);
                case Step::stream_ready: return stream_ready(std::move(frame), stop);
                case Step::streaming: return streaming(std::move(frame), stop);
                case Step::closed: return terminal_result(
                    SessionPhase::handshaking, TerminalAction::complete);
            }
        }
        catch (...) {
            return finish(TerminalAction::internal_error,
                          BusinessCloseReason::internal_error);
        }
        return finish(TerminalAction::internal_error,
                      BusinessCloseReason::internal_error);
    }

    DriverResult heartbeat(const std::stop_token stop) override
    {
        try {
            if (stop.stop_requested()) return finish(TerminalAction::complete,
                                                     BusinessCloseReason::stopped);
            if (step_ != Step::streaming) {
                if (step_ != Step::closed && Clock::now() >= handshake_deadline_) {
                    return finish(TerminalAction::authentication_failed,
                                  BusinessCloseReason::authentication_failed);
                }
                return current();
            }
            if (auto terminal = sink_terminal()) return *terminal;
            if (secure_sink_ && secure_sink_->server_final_sent() && !final_deadline_) {
                const auto enqueued_at = secure_sink_->server_final_enqueued_at();
                final_deadline_ = enqueued_at.value_or(Clock::now())
                    + config_.final_close_timeout;
            }
            if (final_deadline_ && Clock::now() >= *final_deadline_
                && secure_sink_ && !secure_sink_->clean_final()) {
                return finish(TerminalAction::protocol_failed,
                              BusinessCloseReason::truncated);
            }
            if (auto unauthorized = authorization()) return *unauthorized;
            if (secure_sink_ && secure_sink_->server_final_sent()) return current();
            return handle(handler_->heartbeat(stop), false);
        }
        catch (...) {
            return finish(TerminalAction::internal_error,
                          BusinessCloseReason::internal_error);
        }
    }

    void closed() noexcept override
    {
        if (step_ == Step::closed) return;
        auto reason = BusinessCloseReason::truncated;
        if (secure_sink_ && secure_sink_->clean_final()) {
            reason = BusinessCloseReason::clean_final;
        }
        else if (secure_sink_) {
            const auto terminal = secure_sink_->terminal_action();
            if (terminal != TerminalAction::none) {
                reason = close_reason_for_terminal(terminal);
            }
        }
        cleanup(reason);
    }

private:
    enum class Step : std::uint8_t {
        client_hello,
        resume_proof,
        stream_ready,
        streaming,
        closed,
    };

    [[nodiscard]] SessionPhase phase() const noexcept
    {
        return step_ == Step::streaming ? SessionPhase::streaming
                                        : SessionPhase::handshaking;
    }

    [[nodiscard]] DriverResult current() const noexcept
    {
        DriverResult result;
        result.phase = phase();
        return result;
    }

    [[nodiscard]] DriverResult finish(
        const TerminalAction action, const BusinessCloseReason reason) noexcept
    {
        const auto current_phase = phase();
        cleanup(reason);
        return terminal_result(current_phase, action);
    }

    void cleanup(const BusinessCloseReason reason) noexcept
    {
        if (step_ == Step::closed) return;
        if (authorization_) authorization_->deactivate();
        if (secure_sink_) secure_sink_->close();
        if (handler_) handler_->closed(reason);
        handler_.reset();
        secure_sink_.reset();
        pull_.reset();
        pending_push_.reset();
        preauth_.reset();
        handshake_.reset();
        pending_rx_key_.clear();
        auth::secure_zero(pending_aad_);
        pending_aad_.clear();
        if (subscription_) authentication_->unsubscribe_revocations(*subscription_);
        subscription_.reset();
        authorization_.reset();
        session_id_.clear();
        socket_id_.clear();
        step_ = Step::closed;
    }

    [[nodiscard]] DriverResult client_hello(Frame frame)
    {
        if (frame.kind != FrameKind::text
            || frame.payload.size() > config_.max_handshake_json_bytes) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        auto parsed = auth::parse_canonical_json_value(
            frame.payload,
            {.max_input_bytes = config_.max_handshake_json_bytes,
             .max_output_bytes = config_.max_handshake_json_bytes});
        if (!parsed || !exact_fields(*parsed.value, {
                "type", "kind", "channel", "version", "timestamp",
                "client_nonce", "client_kx_pub", "session_id", "socket_id",
                "resume_ticket"})) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        CanonicalStringWiper wiper{*parsed.value};
        const auto* type = member_string(*parsed.value, "type");
        const auto* kind = member_string(*parsed.value, "kind");
        const auto* channel = member_string(*parsed.value, "channel");
        const auto* version = member_integer(*parsed.value, "version");
        const auto* timestamp = member_integer(*parsed.value, "timestamp");
        const auto* nonce = member_string(*parsed.value, "client_nonce");
        const auto* public_key = member_string(*parsed.value, "client_kx_pub");
        const auto* session = member_string(*parsed.value, "session_id");
        const auto* socket = member_string(*parsed.value, "socket_id");
        const auto* ticket = member_string(*parsed.value, "resume_ticket");
        const auto expected_channel = auth::business_channel_name(channel_);
        if (type == nullptr || *type != "client_hello"
            || kind == nullptr || *kind != "resume"
            || channel == nullptr || *channel != expected_channel
            || version == nullptr || *version != protocol_version
            || timestamp == nullptr || *timestamp < 0
            || *timestamp > auth::maximum_safe_json_integer
            || nonce == nullptr || public_key == nullptr || session == nullptr
            || socket == nullptr || ticket == nullptr || ticket->empty()) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        auto nonce_bytes = auth::decode_base64url_canonical(*nonce, auth::x25519_key_bytes);
        auto public_bytes = auth::decode_base64url_canonical(
            *public_key, auth::x25519_key_bytes);
        if (!nonce_bytes || !public_bytes || !auth::is_valid_utf8(*ticket)) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        auto begun = authentication_->begin_business_handshake(auth::BusinessClientHello{
            channel_, *timestamp, std::move(*nonce_bytes.value),
            std::move(*public_bytes.value), *session, *socket,
            auth::SecretBuffer{bytes_of(*ticket)}});
        if (!begun) {
            return finish(terminal_for_auth(begun.error),
                          BusinessCloseReason::authentication_failed);
        }
        const auto& authentication = begun->authentication();
        auto tx = auth::hkdf_sha256(
            authentication.shared_key.bytes(), authentication.transcript_hash,
            bytes_of("preauth:server-tx"), auth::auth_key_bytes);
        auto rx = auth::hkdf_sha256(
            authentication.shared_key.bytes(), authentication.transcript_hash,
            bytes_of("preauth:server-rx"), auth::auth_key_bytes);
        if (!tx || !rx) {
            return finish(TerminalAction::internal_error,
                          BusinessCloseReason::internal_error);
        }
        auto cipher = auth::SecureEnvelopeCipher::create(tx.value->bytes(), rx.value->bytes());
        if (!cipher) return finish(TerminalAction::internal_error,
                                   BusinessCloseReason::internal_error);
        auto hello = begun->take_server_hello_json();
        handshake_.emplace(std::move(*begun.value));
        preauth_.emplace(std::move(*cipher.value));
        step_ = Step::resume_proof;
        auto result = current();
        result.batches.push_back(text_batch(std::move(hello)));
        return result;
    }

    [[nodiscard]] DriverResult resume_proof(Frame frame, const std::stop_token)
    {
        if (frame.kind != FrameKind::text
            || frame.payload.size() > config_.max_preauth_json_bytes
            || !preauth_ || !handshake_) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        auto decoded = preauth_->decrypt(frame.payload);
        if (!decoded) return finish(TerminalAction::authentication_failed,
                                    BusinessCloseReason::authentication_failed);
        CanonicalStringWiper wiper{*decoded.plaintext};
        if (!exact_fields(*decoded.plaintext, {"type", "resume_mac"})) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        const auto* type = member_string(*decoded.plaintext, "type");
        const auto* mac_text = member_string(*decoded.plaintext, "resume_mac");
        if (type == nullptr || *type != "resume_proof" || mac_text == nullptr) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        auto mac = auth::decode_base64url_canonical(*mac_text, auth::hmac_sha256_bytes);
        if (!mac) return finish(TerminalAction::authentication_failed,
                                BusinessCloseReason::authentication_failed);
        auto resumed = authentication_->resume_business(
            std::move(*handshake_), auth::SecretBuffer{*mac.value});
        auth::secure_zero(*mac.value);
        handshake_.reset();
        if (!resumed) return finish(terminal_for_auth(resumed.error),
                                    BusinessCloseReason::authentication_failed);

        subscription_ = resumed->revocation_subscription;
        session_id_ = resumed->session_id;
        socket_id_ = resumed->socket_id;
        expires_at_ = resumed->expires_at;
        pwd_epoch_ = resumed->pwd_epoch;
        authorization_ = std::make_shared<SessionAuthorizationGate>(
            authentication_, *subscription_, session_id_);
        pending_rx_key_ = std::move(resumed->stream_server_rx_key);
        pending_aad_ = std::move(resumed->stream_aad_prefix);
        auto push = auth::SecretStreamPush::create(
            resumed->stream_server_tx_key.bytes(), pending_aad_);
        if (!push) return finish(TerminalAction::internal_error,
                                 BusinessCloseReason::internal_error);
        auto header = auth::encode_base64url_padded(push.value->header());
        if (!header) return finish(TerminalAction::internal_error,
                                   BusinessCloseReason::internal_error);
        pending_push_.emplace(std::move(*push.value));
        CanonicalJsonValue response{CanonicalJsonValue::Object{
            {"type", string_value("resume_ok")},
            {"session_id", string_value(session_id_)},
            {"pwd_epoch", CanonicalJsonValue{static_cast<std::int64_t>(pwd_epoch_)}},
            {"server_header", string_value(std::move(*header.value))},
        }};
        CanonicalStringWiper response_wiper{response};
        auto encrypted = preauth_->encrypt(response);
        if (!encrypted) return finish(TerminalAction::internal_error,
                                      BusinessCloseReason::internal_error);
        step_ = Step::stream_ready;
        auto result = current();
        result.batches.push_back(text_batch(std::move(encrypted.envelope)));
        return result;
    }

    [[nodiscard]] DriverResult stream_ready(Frame frame, const std::stop_token stop)
    {
        if (frame.kind != FrameKind::text
            || frame.payload.size() > config_.max_preauth_json_bytes
            || !preauth_ || !pending_push_ || pending_rx_key_.empty()) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        auto decoded = preauth_->decrypt(frame.payload);
        if (!decoded) return finish(TerminalAction::authentication_failed,
                                    BusinessCloseReason::authentication_failed);
        CanonicalStringWiper wiper{*decoded.plaintext};
        if (!exact_fields(*decoded.plaintext, {"type", "client_header"})) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        const auto* type = member_string(*decoded.plaintext, "type");
        const auto* header_text = member_string(*decoded.plaintext, "client_header");
        if (type == nullptr || *type != "stream_ready" || header_text == nullptr) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        auto header = auth::decode_base64url_canonical(
            *header_text, auth::secretstream_header_bytes);
        if (!header) return finish(TerminalAction::protocol_failed,
                                   BusinessCloseReason::protocol_failed);
        if (auto unauthorized = authorization()) return *unauthorized;
        auto pull = create_pull(
            pending_rx_key_.bytes(), *header.value, pending_aad_);
        if (!pull) {
            const auto terminal = terminal_for_secretstream(pull.error);
            return finish(terminal, close_reason_for_terminal(terminal));
        }
        pull_.emplace(std::move(*pull.value));
        secure_sink_ = std::make_shared<SecureBusinessSink>(
            std::move(*pending_push_), outbound_, authorization_, std::min(
                config_.max_handler_output_bytes,
                config_.max_ciphertext_bytes - auth::secretstream_overhead_bytes));
        pending_push_.reset();
        auto created = handlers_->create(BusinessSessionContext{
            channel_, session_id_, socket_id_, expires_at_, pwd_epoch_}, secure_sink_, stop);
        if (!created) {
            return finish(
                created.error == BusinessHandlerCreateError::capacity
                    ? TerminalAction::capacity : TerminalAction::internal_error,
                BusinessCloseReason::internal_error);
        }
        handler_ = std::move(created.handler);
        secure_sink_->activate();
        preauth_.reset();
        pending_rx_key_.clear();
        auth::secure_zero(pending_aad_);
        pending_aad_.clear();
        auto ready = handler_->ready(stop);
        step_ = Step::streaming;
        return handle(std::move(ready), false);
    }

    [[nodiscard]] DriverResult streaming(Frame frame, const std::stop_token stop)
    {
        if (auto terminal = sink_terminal()) return *terminal;
        if (auto unauthorized = authorization()) return *unauthorized;
        if (frame.kind != FrameKind::binary
            || frame.payload.size() > config_.max_ciphertext_bytes || !pull_) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        auto decrypted = pull_ciphertext(*pull_, bytes_of(frame.payload));
        if (!decrypted) {
            const auto terminal = terminal_for_secretstream(decrypted.error);
            return finish(terminal, close_reason_for_terminal(terminal));
        }
        const bool final = decrypted.tag == auth::SecretStreamTag::final;
        if (!final && secure_sink_ && secure_sink_->server_final_sent()) {
            return finish(TerminalAction::protocol_failed,
                          BusinessCloseReason::protocol_failed);
        }
        return handle(handler_->input(std::move(decrypted.plaintext), final, stop), final);
    }

    [[nodiscard]] DriverResult handle(
        BusinessHandlerResult handled, const bool peer_final)
    {
        OutboundMessageWiper handled_wiper{handled.messages};
        if (handled.messages.size() > config_.max_handler_messages) {
            return finish(TerminalAction::capacity, BusinessCloseReason::internal_error);
        }
        std::size_t total = 0;
        bool emitted_final = false;
        for (std::size_t index = 0; index < handled.messages.size(); ++index) {
            auto& message = handled.messages[index];
            if (message.payload.size() > config_.max_handler_output_bytes - total
                || (message.final && index + 1 != handled.messages.size())) {
                return finish(TerminalAction::capacity, BusinessCloseReason::internal_error);
            }
            total += message.payload.size();
            emitted_final = message.final;
        }
        auto terminal = terminal_for_handler(handled.status);
        if (terminal != TerminalAction::none && terminal != TerminalAction::complete) {
            return finish(terminal,
                terminal == TerminalAction::protocol_failed
                    ? BusinessCloseReason::protocol_failed
                    : BusinessCloseReason::internal_error);
        }
        if (peer_final && secure_sink_ && secure_sink_->server_final_sent()) {
            if (!handled.messages.empty()) {
                return finish(TerminalAction::protocol_failed,
                              BusinessCloseReason::protocol_failed);
            }
            secure_sink_->authenticated_peer_final();
            return current();
        }
        if (peer_final && secure_sink_) secure_sink_->authenticated_peer_final();
        if (peer_final || terminal == TerminalAction::complete) {
            if (!emitted_final) {
                if (handled.messages.size() >= config_.max_handler_messages) {
                    return finish(TerminalAction::capacity,
                                  BusinessCloseReason::internal_error);
                }
                handled.messages.push_back({{}, true});
            }
            if (!final_deadline_) final_deadline_ = Clock::now() + config_.final_close_timeout;
        }
        if (!handled.messages.empty()
            && (!secure_sink_ || secure_sink_->emit_batch(std::move(handled.messages))
                    != BusinessEmitResult::accepted)) {
            const auto sink_action = secure_sink_
                ? secure_sink_->terminal_action() : TerminalAction::internal_error;
            const auto selected = sink_action == TerminalAction::none
                ? TerminalAction::internal_error : sink_action;
            return finish(selected, close_reason_for_terminal(selected));
        }
        if (secure_sink_ && secure_sink_->server_final_sent() && !final_deadline_) {
            const auto enqueued_at = secure_sink_->server_final_enqueued_at();
            final_deadline_ = enqueued_at.value_or(Clock::now())
                + config_.final_close_timeout;
        }
        return current();
    }

    [[nodiscard]] std::optional<DriverResult> authorization()
    {
        if (!authorization_) {
            return finish(TerminalAction::authentication_failed,
                          BusinessCloseReason::authentication_failed);
        }
        const auto terminal = authorization_->authorize();
        if (terminal == TerminalAction::none) return std::nullopt;
        if (terminal == TerminalAction::complete) {
            return finish(TerminalAction::internal_error,
                          BusinessCloseReason::internal_error);
        }
        return finish(terminal, close_reason_for_terminal(terminal));
    }

    [[nodiscard]] std::optional<DriverResult> sink_terminal()
    {
        if (!secure_sink_) return std::nullopt;
        const auto terminal = secure_sink_->terminal_action();
        if (terminal == TerminalAction::none) return std::nullopt;
        if (terminal == TerminalAction::complete && secure_sink_->clean_final()) {
            return finish(TerminalAction::complete,
                          BusinessCloseReason::clean_final);
        }
        return finish(terminal, close_reason_for_terminal(terminal));
    }

    std::shared_ptr<auth::AuthOwner> authentication_;
    std::shared_ptr<BusinessChannelHandlerFactory> handlers_;
    std::weak_ptr<OutboundSink> outbound_;
    BusinessSessionConfig config_;
    auth::BusinessChannel channel_;
    Clock::time_point handshake_deadline_;
    Step step_{Step::client_hello};
    std::optional<auth::BusinessHandshakeMaterial> handshake_;
    std::optional<auth::SecureEnvelopeCipher> preauth_;
    std::optional<auth::SecretStreamPush> pending_push_;
    auth::SecretBuffer pending_rx_key_;
    auth::PublicBytes pending_aad_;
    std::optional<auth::SecretStreamPull> pull_;
    std::shared_ptr<SecureBusinessSink> secure_sink_;
    std::unique_ptr<BusinessChannelHandler> handler_;
    std::optional<auth::SubscriptionId> subscription_;
    std::shared_ptr<SessionAuthorizationGate> authorization_;
    std::string session_id_;
    std::string socket_id_;
    std::int64_t expires_at_{};
    std::uint64_t pwd_epoch_{};
    std::optional<Clock::time_point> final_deadline_;
};

[[nodiscard]] bool remote_available(const RemoteChannelPolicy policy) noexcept
{
#if defined(__ANDROID__)
    static_cast<void>(policy);
    return false;
#else
    return policy == RemoteChannelPolicy::desktop_only;
#endif
}

}  // namespace

#if defined(BAAS_BUSINESS_SESSION_TEST_HOOKS)
namespace detail {

void fail_next_business_pull_create_for_test(
    const auth::SecretStreamError error) noexcept
{
    injected_pull_create_error.store(error, std::memory_order_release);
}

void fail_next_business_pull_for_test(
    const auth::SecretStreamError error) noexcept
{
    injected_pull_error.store(error, std::memory_order_release);
}

}  // namespace detail
#endif

BusinessSessionFactory::BusinessSessionFactory(
    std::shared_ptr<auth::AuthOwner> authentication,
    BusinessHandlerFactories handlers,
    const BusinessSessionConfig config,
    const RemoteChannelPolicy remote)
    : authentication_(std::move(authentication)),
      handlers_(std::move(handlers)),
      config_(config),
      remote_policy_(remote)
{
    if (!authentication_ || !handlers_.provider || !handlers_.sync || !handlers_.trigger
        || config_.max_handshake_json_bytes == 0
        || config_.max_handshake_json_bytes > websocket_max_frame_bytes
        || config_.max_preauth_json_bytes == 0
        || config_.max_preauth_json_bytes > websocket_max_frame_bytes
        || config_.max_ciphertext_bytes < auth::secretstream_overhead_bytes
        || config_.max_ciphertext_bytes > websocket_max_frame_bytes
        || config_.max_handler_messages == 0 || config_.max_handler_messages > 2
        || config_.max_handler_output_bytes == 0
        || config_.max_handler_output_bytes
            > config_.max_ciphertext_bytes - auth::secretstream_overhead_bytes
        || config_.final_close_timeout <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("invalid business session dependencies or bounds");
    }
    if (remote_available(remote_policy_) && !handlers_.remote) {
        throw std::invalid_argument("desktop remote business handler is required");
    }
}

std::unique_ptr<SessionDriver> BusinessSessionFactory::create(
    RequestMetadata request,
    std::shared_ptr<OutboundSink> outbound,
    const std::stop_token stop)
{
    auth::secure_zero(std::as_writable_bytes(
        std::span{request.cookie.data(), request.cookie.size()}));
    request.cookie.clear();
    if (stop.stop_requested() || !outbound || request.path != path_for(request.channel)) {
        return std::make_unique<UnsupportedDriver>();
    }
    const auto channel = auth_channel(request.channel);
    if (!channel || (request.channel == Channel::remote && !remote_available(remote_policy_))) {
        return std::make_unique<UnsupportedDriver>();
    }
    std::shared_ptr<BusinessChannelHandlerFactory> handlers;
    switch (request.channel) {
        case Channel::provider: handlers = handlers_.provider; break;
        case Channel::sync: handlers = handlers_.sync; break;
        case Channel::trigger: handlers = handlers_.trigger; break;
        case Channel::remote: handlers = handlers_.remote; break;
        case Channel::control: break;
    }
    if (!handlers) return std::make_unique<UnsupportedDriver>();
    return std::make_unique<BusinessSessionDriver>(
        authentication_, std::move(handlers), std::move(outbound), config_, *channel,
        Clock::now() + request.handshake_timeout);
}

ProductionSessionFactory::ProductionSessionFactory(
    std::shared_ptr<auth::AuthOwner> authentication,
    BusinessHandlerFactories handlers,
    const ControlSessionConfig control_config,
    const BusinessSessionConfig business_config,
    const RemoteChannelPolicy remote)
{
    if (!authentication) throw std::invalid_argument("authentication owner is required");
    control_ = std::make_shared<ControlSessionFactory>(authentication, control_config);
    business_ = std::make_shared<BusinessSessionFactory>(
        std::move(authentication), std::move(handlers), business_config, remote);
}

std::unique_ptr<SessionDriver> ProductionSessionFactory::create(
    RequestMetadata request,
    std::shared_ptr<OutboundSink> outbound,
    const std::stop_token stop)
{
    if (request.channel == Channel::control && request.path == "/ws/control") {
        return control_->create(std::move(request), std::move(outbound), stop);
    }
    if (request.channel != Channel::control && request.path == path_for(request.channel)) {
        return business_->create(std::move(request), std::move(outbound), stop);
    }
    auth::secure_zero(std::as_writable_bytes(
        std::span{request.cookie.data(), request.cookie.size()}));
    request.cookie.clear();
    return std::make_unique<UnsupportedDriver>();
}

}  // namespace baas::service::websocket
