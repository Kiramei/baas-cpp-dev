#include "service/channels/TriggerHandler.h"

#include "service/protocol/TriggerEnvelope.h"
#include "service/trigger/TriggerCommandCatalog.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace baas::service::channels {
namespace {

namespace protocol = baas::service::protocol::trigger;
namespace trigger_service = baas::service::trigger;
namespace ws = baas::service::websocket;

[[nodiscard]] ws::BusinessHandlerResult status_result(
    const ws::BusinessHandlerStatus status) noexcept
{
    return {{}, status};
}

[[nodiscard]] std::string byte_string(const std::span<const std::byte> bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

class TriggerHandlerCore;
thread_local TriggerHandlerCore* current_trigger_sink_call{};

class SendCompletion final : public ws::BusinessBatchCompletion {
public:
    SendCompletion(
        std::weak_ptr<TriggerHandlerCore> owner,
        protocol::SendLease lease) noexcept
        : owner_(std::move(owner)), lease_(std::move(lease))
    {}

    void complete(ws::BusinessBatchWriteResult result) noexcept override;

private:
    std::weak_ptr<TriggerHandlerCore> owner_;
    protocol::SendLease lease_;
    std::atomic_bool completed_{};
};

class TriggerHandlerCore final
    : public protocol::OutputReadyObserver,
      public std::enable_shared_from_this<TriggerHandlerCore> {
public:
    TriggerHandlerCore(
        std::shared_ptr<trigger_service::TriggerExecutor> executor,
        std::shared_ptr<ws::BusinessPlaintextSink> output,
        const TriggerHandlerLimits& limits)
        : session_(std::make_shared<protocol::TriggerSession>(limits.session)),
          owner_(executor->connect(session_, limits.max_tasks_per_connection)),
          output_(std::move(output)),
          ingress_(limits.ingress),
          response_limits_(limits.ingress.envelope)
    {}

    void arm()
    {
        subscription_ = owner_.observe_output_ready(weak_from_this());
        if (!subscription_) throw std::runtime_error("trigger output observer rejected");
    }

    void output_ready() noexcept override { pump(); }

    [[nodiscard]] ws::BusinessHandlerResult input(
        auth::SecretBuffer plaintext,
        const bool peer_final,
        const std::stop_token stop)
    {
        if (peer_final || stop.stop_requested()) {
            shutdown();
            return status_result(ws::BusinessHandlerStatus::complete);
        }
        {
            std::lock_guard lock{state_mutex_};
            if (closed_) return status_result(ws::BusinessHandlerStatus::complete);
            if (failed_) return status_result(ws::BusinessHandlerStatus::internal_error);
        }

        protocol::TriggerIngressResult received;
        std::optional<protocol::TriggerIngressItem> item;
        {
            std::lock_guard lock{ingress_mutex_};
            if (ingress_.state() == protocol::TriggerIngressState::awaiting_binary) {
                received = ingress_.receive_binary_frame(plaintext.bytes());
            }
            else {
                const std::string_view text{
                    reinterpret_cast<const char*>(plaintext.bytes().data()),
                    plaintext.size()};
                received = ingress_.receive_json_frame(text);
            }
            if (received.outcome == protocol::TriggerIngressOutcome::ready)
                item = ingress_.take_ready();
        }

        if (received.outcome == protocol::TriggerIngressOutcome::awaiting_binary)
            return {};
        if (received.disposition() == protocol::TriggerIngressDisposition::closed)
            return status_result(ws::BusinessHandlerStatus::complete);
        if (received.disposition() == protocol::TriggerIngressDisposition::fatal)
            return status_result(ws::BusinessHandlerStatus::protocol_failed);
        if (received.disposition()
                == protocol::TriggerIngressDisposition::command_rejection) {
            if (!received.command_rejection)
                return status_result(ws::BusinessHandlerStatus::internal_error);
            const auto lookup = trigger_service::TriggerCommandCatalog::lookup(
                received.command_rejection->command);
            const auto mode = lookup
                    && lookup.descriptor->response_mode
                        == trigger_service::TriggerCommandResponseMode::stream
                ? protocol::ResponseMode::stream
                : protocol::ResponseMode::single;
            return rejection(
                received.command_rejection->command,
                received.command_rejection->timestamp,
                received.command_rejection->safe_message(), mode);
        }
        if (!item) return status_result(ws::BusinessHandlerStatus::protocol_failed);

        const auto command = item->envelope().command;
        const auto timestamp = item->envelope().timestamp;
        const auto response_mode = item->admission().response_mode;
        const auto submitted = owner_.submit(std::move(*item));
        if (submitted) return {};
        if (submitted.error == trigger_service::TriggerSubmitError::connection_stopped
            || submitted.error == trigger_service::TriggerSubmitError::executor_stopped) {
            shutdown();
            return status_result(ws::BusinessHandlerStatus::complete);
        }
        if (submitted.error == trigger_service::TriggerSubmitError::transaction_failed) {
            mark_failed();
            return status_result(ws::BusinessHandlerStatus::internal_error);
        }
        return rejection(
            command, timestamp,
            trigger_service::trigger_submit_error_name(submitted.error),
            response_mode);
    }

    [[nodiscard]] ws::BusinessHandlerResult heartbeat(
        const std::stop_token stop)
    {
        if (stop.stop_requested()) {
            shutdown();
            return status_result(ws::BusinessHandlerStatus::complete);
        }
        std::lock_guard lock{state_mutex_};
        if (closed_) return status_result(ws::BusinessHandlerStatus::complete);
        return failed_ ? status_result(ws::BusinessHandlerStatus::internal_error)
                       : ws::BusinessHandlerResult{};
    }

    void send_completed(
        const protocol::SendLease& lease,
        const ws::BusinessBatchWriteResult result) noexcept
    {
        {
            std::lock_guard lock{state_mutex_};
            if (closed_) return;
        }
        bool ok{};
        try {
            if (result == ws::BusinessBatchWriteResult::written) {
                const auto completed = owner_.complete_send(lease);
                ok = static_cast<bool>(completed);
            }
            else {
                const auto failed = owner_.fail_send(lease);
                ok = static_cast<bool>(failed);
            }
        }
        catch (...) { ok = false; }
        {
            std::lock_guard lock{state_mutex_};
            completion_pending_ = false;
            if (!ok || result == ws::BusinessBatchWriteResult::failed)
                failed_ = true;
        }
        pump();
    }

    void shutdown() noexcept
    {
        {
            std::unique_lock lock{state_mutex_};
            if (closed_) return;
            closed_ = true;
            if (current_trigger_sink_call != this) {
                sink_idle_.wait(lock, [this] { return sink_calls_ == 0; });
            }
        }
        subscription_.reset();
        {
            std::lock_guard lock{ingress_mutex_};
            ingress_.close();
        }
        try { static_cast<void>(owner_.close()); } catch (...) {}
    }

private:
    [[nodiscard]] ws::BusinessHandlerResult rejection(
        std::string command,
        const protocol::Timestamp timestamp,
        const std::string_view message,
        const protocol::ResponseMode response_mode)
    {
        protocol::CommandResponse response;
        response.command = std::move(command);
        response.timestamp = timestamp;
        response.status = protocol::ResponseStatus::error;
        response.response_mode = response_mode;
        response.error = std::string{message};
        if (response_mode == protocol::ResponseMode::stream)
            response.data_json = std::string{"{\"done\":true}"};
        auto encoded = protocol::encode_command_response(
            std::move(response), response_limits_);
        if (!encoded)
            return status_result(ws::BusinessHandlerStatus::internal_error);
        return {{{encoded.batch.json(), false}}, ws::BusinessHandlerStatus::ok};
    }

    void mark_failed() noexcept
    {
        std::lock_guard lock{state_mutex_};
        if (!closed_) failed_ = true;
    }

    void pump() noexcept
    {
        {
            std::lock_guard lock{state_mutex_};
            if (closed_ || failed_) return;
            if (pump_active_) {
                pump_requested_ = true;
                return;
            }
            pump_active_ = true;
        }

        for (;;) {
            {
                std::lock_guard lock{state_mutex_};
                if (closed_ || failed_ || completion_pending_) {
                    pump_active_ = false;
                    return;
                }
                pump_requested_ = false;
            }

            protocol::BeginSendResult begun;
            try { begun = session_->begin_send(); }
            catch (...) {
                mark_failed();
                std::lock_guard lock{state_mutex_};
                pump_active_ = false;
                return;
            }
            if (!begun) {
                std::lock_guard lock{state_mutex_};
                if (pump_requested_ && !closed_ && !failed_) continue;
                if (begun.error != protocol::BeginSendError::queue_empty
                    && begun.error != protocol::BeginSendError::closed)
                    failed_ = true;
                pump_active_ = false;
                return;
            }

            auto lease = *begun.lease;
            std::vector<ws::BusinessOutboundMessage> messages;
            try {
                messages.push_back({lease.batch().json(), false});
                if (lease.batch().has_binary())
                    messages.push_back({byte_string(lease.batch().binary()), false});
            }
            catch (...) {
                try { static_cast<void>(owner_.fail_send(lease)); } catch (...) {}
                mark_failed();
                std::lock_guard lock{state_mutex_};
                pump_active_ = false;
                return;
            }
            auto completion = std::make_shared<SendCompletion>(
                weak_from_this(), lease);
            {
                std::lock_guard lock{state_mutex_};
                if (closed_) {
                    pump_active_ = false;
                    return;
                }
                completion_pending_ = true;
                ++sink_calls_;
            }

            ws::BusinessEmitResult emitted{ws::BusinessEmitResult::closed};
            auto* previous_sink_call = current_trigger_sink_call;
            current_trigger_sink_call = this;
            try {
                emitted = output_->emit_batch(
                    std::move(messages), completion);
            }
            catch (...) {}
            current_trigger_sink_call = previous_sink_call;
            {
                std::lock_guard lock{state_mutex_};
                --sink_calls_;
            }
            sink_idle_.notify_all();
            if (emitted != ws::BusinessEmitResult::accepted)
                completion->complete(ws::BusinessBatchWriteResult::failed);

            std::lock_guard lock{state_mutex_};
            if (closed_ || failed_ || completion_pending_) {
                pump_active_ = false;
                return;
            }
        }
    }

    std::shared_ptr<protocol::TriggerSession> session_;
    trigger_service::TriggerConnectionOwner owner_;
    std::shared_ptr<ws::BusinessPlaintextSink> output_;
    std::mutex ingress_mutex_;
    protocol::TriggerIngress ingress_;
    protocol::TriggerEnvelopeLimits response_limits_;
    std::mutex state_mutex_;
    std::condition_variable sink_idle_;
    protocol::OutputReadySubscription subscription_;
    bool closed_{};
    bool failed_{};
    bool pump_active_{};
    bool pump_requested_{};
    bool completion_pending_{};
    std::size_t sink_calls_{};
};

void SendCompletion::complete(const ws::BusinessBatchWriteResult result) noexcept
{
    bool expected = false;
    if (!completed_.compare_exchange_strong(expected, true)) return;
    if (auto owner = owner_.lock()) owner->send_completed(lease_, result);
}

class TriggerHandler final : public ws::BusinessChannelHandler {
public:
    TriggerHandler(
        std::shared_ptr<TriggerHandlerCore> core,
        const std::stop_token stop)
        : core_(std::move(core))
    {
        std::weak_ptr<TriggerHandlerCore> weak = core_;
        stop_callback_ = std::make_unique<StopCallback>(
            stop, [weak] {
                if (auto core = weak.lock()) core->shutdown();
            });
    }

    ~TriggerHandler() override { closed(ws::BusinessCloseReason::stopped); }

    [[nodiscard]] ws::BusinessHandlerResult ready(
        const std::stop_token stop) override
    {
        return core_->heartbeat(stop);
    }

    [[nodiscard]] ws::BusinessHandlerResult input(
        auth::SecretBuffer plaintext,
        const bool peer_final,
        const std::stop_token stop) override
    {
        try { return core_->input(std::move(plaintext), peer_final, stop); }
        catch (...) { return status_result(ws::BusinessHandlerStatus::internal_error); }
    }

    [[nodiscard]] ws::BusinessHandlerResult heartbeat(
        const std::stop_token stop) override
    {
        return core_->heartbeat(stop);
    }

    void closed(ws::BusinessCloseReason) noexcept override
    {
        std::call_once(close_once_, [this] {
            stop_callback_.reset();
            core_->shutdown();
        });
    }

private:
    using StopCallback = std::stop_callback<std::function<void()>>;
    std::shared_ptr<TriggerHandlerCore> core_;
    std::unique_ptr<StopCallback> stop_callback_;
    std::once_flag close_once_;
};

}  // namespace

TriggerHandlerFactory::TriggerHandlerFactory(
    std::shared_ptr<trigger::TriggerExecutor> executor,
    TriggerHandlerLimits limits)
    : executor_(std::move(executor)), limits_(std::move(limits))
{
    if (!executor_) throw std::invalid_argument("trigger executor is required");
    [[maybe_unused]] protocol::TriggerIngress validate_ingress{limits_.ingress};
    [[maybe_unused]] protocol::TriggerSession validate_session{limits_.session};
}

ws::BusinessHandlerCreateResult TriggerHandlerFactory::create(
    ws::BusinessSessionContext context,
    std::shared_ptr<ws::BusinessPlaintextSink> output,
    const std::stop_token stop)
{
    if (!output || stop.stop_requested()
        || context.channel != auth::BusinessChannel::trigger) {
        return {nullptr, ws::BusinessHandlerCreateError::internal_error};
    }
    try {
        auto core = std::make_shared<TriggerHandlerCore>(
            executor_, std::move(output), limits_);
        core->arm();
        return {std::make_unique<TriggerHandler>(std::move(core), stop),
                ws::BusinessHandlerCreateError::none};
    }
    catch (const std::bad_alloc&) {
        return {nullptr, ws::BusinessHandlerCreateError::capacity};
    }
    catch (const std::invalid_argument&) {
        return {nullptr, ws::BusinessHandlerCreateError::internal_error};
    }
    catch (const std::runtime_error&) {
        return {nullptr, executor_->stats().stopping
                ? ws::BusinessHandlerCreateError::internal_error
                : ws::BusinessHandlerCreateError::capacity};
    }
    catch (...) { return {nullptr, ws::BusinessHandlerCreateError::internal_error}; }
}

}  // namespace baas::service::channels
