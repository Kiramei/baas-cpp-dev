#include "service/pipe/TriggerPipeChannel.h"

#include "service/protocol/TriggerEnvelope.h"
#include "service/trigger/TriggerCommandCatalog.h"

#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace baas::service::pipe {
namespace {

namespace protocol = baas::service::protocol::trigger;
namespace trigger_service = baas::service::trigger;

[[nodiscard]] std::span<const std::byte> as_bytes(
    const std::string_view value) noexcept
{
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

class TriggerPipeCore;
thread_local TriggerPipeCore* active_pipe_pump{};

class TriggerPipeCore final
    : public protocol::OutputReadyObserver,
      public std::enable_shared_from_this<TriggerPipeCore> {
public:
    TriggerPipeCore(
        std::shared_ptr<trigger_service::TriggerExecutor> executor,
        const TriggerPipeChannelLimits& limits)
        : session_(std::make_shared<protocol::TriggerSession>(limits.session)),
          owner_(executor->connect(session_, limits.max_tasks_per_connection)),
          ingress_(limits.ingress), response_limits_(limits.ingress.envelope)
    {}

    void arm()
    {
        subscription_ = owner_.observe_output_ready(weak_from_this());
        if (!subscription_) throw std::runtime_error("trigger output observer rejected");
    }

    void output_ready() noexcept override { pump(); }

    [[nodiscard]] PipeHandlerResult input(
        const bpip::Frame& frame,
        PipeConnectionWriter& writer,
        const std::stop_token stop)
    {
        {
            std::lock_guard lock{state_mutex_};
            if (!writer_) writer_ = &writer;
            if (writer_ != &writer || closed_ || failed_ || stop.stop_requested())
                return close_result();
        }

        protocol::TriggerIngressResult received;
        std::optional<protocol::TriggerIngressItem> item;
        {
            std::lock_guard lock{ingress_mutex_};
            if (frame.kind == bpip::kind_value(bpip::FrameKind::json)) {
                received = ingress_.receive_json_frame(std::string_view{
                    reinterpret_cast<const char*>(frame.payload.data()),
                    frame.payload.size()});
            } else {
                received = ingress_.receive_binary_frame(frame.payload);
            }
            if (received.outcome == protocol::TriggerIngressOutcome::ready)
                item = ingress_.take_ready();
        }

        if (received.outcome == protocol::TriggerIngressOutcome::awaiting_binary)
            return {};
        if (received.disposition() == protocol::TriggerIngressDisposition::closed)
            return close_result();
        if (received.disposition() == protocol::TriggerIngressDisposition::fatal)
            return fail(PipeHostError::handler_failed);
        if (received.disposition()
            == protocol::TriggerIngressDisposition::command_rejection) {
            if (!received.command_rejection)
                return fail(PipeHostError::handler_failed);
            const auto lookup = trigger_service::TriggerCommandCatalog::lookup(
                received.command_rejection->command);
            const auto mode = lookup
                    && lookup.descriptor->response_mode
                        == trigger_service::TriggerCommandResponseMode::stream
                ? protocol::ResponseMode::stream
                : protocol::ResponseMode::single;
            return write_rejection(writer, received.command_rejection->command,
                received.command_rejection->timestamp,
                received.command_rejection->safe_message(), mode);
        }
        if (!item) return fail(PipeHostError::handler_failed);

        const auto command = item->envelope().command;
        const auto timestamp = item->envelope().timestamp;
        const auto mode = item->admission().response_mode;
        trigger_service::TriggerSubmitResult submitted;
        {
            std::lock_guard admission_lock{admission_mutex_};
            {
                std::lock_guard state_lock{state_mutex_};
                if (closed_ || failed_) return close_result();
            }
            submitted = owner_.submit(std::move(*item));
        }
        if (submitted) return {};
        if (submitted.error == trigger_service::TriggerSubmitError::connection_stopped
            || submitted.error == trigger_service::TriggerSubmitError::executor_stopped)
            return close_result();
        if (submitted.error == trigger_service::TriggerSubmitError::transaction_failed)
            return fail(PipeHostError::handler_failed);
        return write_rejection(writer, command, timestamp,
            trigger_service::trigger_submit_error_name(submitted.error), mode);
    }

    void shutdown() noexcept
    {
        PipeConnectionWriter* writer{};
        bool perform_shutdown{};
        {
            std::lock_guard admission_lock{admission_mutex_};
            std::lock_guard state_lock{state_mutex_};
            if (!shutdown_started_) {
                shutdown_started_ = true;
                closed_ = true;
                perform_shutdown = true;
            }
            writer = writer_;
        }
        if (!perform_shutdown) {
            std::unique_lock lock{state_mutex_};
            if (active_pipe_pump != this)
                pump_idle_.wait(lock, [this] { return shutdown_complete_; });
            return;
        }
        subscription_.reset();
        {
            std::lock_guard lock{ingress_mutex_};
            ingress_.close();
        }
        // Interrupt a blocking write/read before waiting for the pump barrier.
        if (writer) writer->close_connection();
        try { static_cast<void>(owner_.close()); } catch (...) {}
        std::unique_lock lock{state_mutex_};
        if (active_pipe_pump != this)
            pump_idle_.wait(lock, [this] { return pump_calls_ == 0; });
        shutdown_complete_ = true;
        lock.unlock();
        pump_idle_.notify_all();
    }

private:
    [[nodiscard]] static PipeHandlerResult close_result() noexcept
    {
        return {PipeHandlerAction::close_connection, PipeHostError::none};
    }

    [[nodiscard]] PipeHandlerResult fail(const PipeHostError error) noexcept
    {
        PipeConnectionWriter* writer{};
        {
            std::lock_guard lock{state_mutex_};
            failed_ = true;
            writer = writer_;
        }
        if (writer) writer->close_connection();
        return {PipeHandlerAction::close_connection, error};
    }

    [[nodiscard]] PipeHostError write_batch(
        PipeConnectionWriter& writer, const protocol::OutboundBatch& batch) noexcept
    {
        try {
            bpip::Frame json{
                bpip::kind_value(bpip::FrameKind::json),
                bpip::Bytes{as_bytes(batch.json()).begin(), as_bytes(batch.json()).end()}};
            if (!batch.has_binary())
                return writer.write_batch(std::span<const bpip::Frame>{&json, 1});
            bpip::Frame binary{
                bpip::kind_value(bpip::FrameKind::bytes), batch.binary()};
            const std::array frames{std::move(json), std::move(binary)};
            return writer.write_batch(frames);
        } catch (...) {
            return PipeHostError::write_failed;
        }
    }

    [[nodiscard]] PipeHandlerResult write_rejection(
        PipeConnectionWriter& writer, std::string command,
        const protocol::Timestamp timestamp, const std::string_view message,
        const protocol::ResponseMode mode)
    {
        protocol::CommandResponse response;
        response.command = std::move(command);
        response.timestamp = timestamp;
        response.status = protocol::ResponseStatus::error;
        response.response_mode = mode;
        response.error = std::string{message};
        if (mode == protocol::ResponseMode::stream)
            response.data_json = std::string{"{\"done\":true}"};
        auto encoded = protocol::encode_command_response(
            std::move(response), response_limits_);
        if (!encoded) return fail(PipeHostError::handler_failed);
        const auto error = write_batch(writer, encoded.batch);
        return error == PipeHostError::none ? PipeHandlerResult{} : fail(error);
    }

    void pump() noexcept
    {
        PipeConnectionWriter* writer{};
        {
            std::lock_guard lock{state_mutex_};
            if (closed_ || failed_ || !writer_) return;
            if (pump_active_) {
                pump_requested_ = true;
                return;
            }
            pump_active_ = true;
            ++pump_calls_;
            writer = writer_;
        }

        auto* previous = active_pipe_pump;
        active_pipe_pump = this;
        for (;;) {
            {
                std::lock_guard lock{state_mutex_};
                if (closed_ || failed_) break;
                pump_requested_ = false;
            }
            protocol::BeginSendResult begun;
            try { begun = session_->begin_send(); }
            catch (...) { break_with_failure(); break; }
            if (!begun) {
                std::lock_guard lock{state_mutex_};
                if (pump_requested_ && !closed_ && !failed_) continue;
                if (begun.error != protocol::BeginSendError::queue_empty
                    && begun.error != protocol::BeginSendError::closed)
                    failed_ = true;
                break;
            }
            const auto lease = *begun.lease;
            const auto write = write_batch(*writer, lease.batch());
            bool ok{};
            try {
                if (write == PipeHostError::none)
                    ok = static_cast<bool>(owner_.complete_send(lease));
                else
                    ok = static_cast<bool>(owner_.fail_send(lease));
            } catch (...) { ok = false; }
            if (write != PipeHostError::none || !ok) {
                break_with_failure();
                break;
            }
        }
        active_pipe_pump = previous;
        {
            std::lock_guard lock{state_mutex_};
            pump_active_ = false;
            --pump_calls_;
        }
        pump_idle_.notify_all();
    }

    void break_with_failure() noexcept
    {
        PipeConnectionWriter* writer{};
        {
            std::lock_guard lock{state_mutex_};
            failed_ = true;
            writer = writer_;
        }
        if (writer) writer->close_connection();
    }

    std::shared_ptr<protocol::TriggerSession> session_;
    trigger_service::TriggerConnectionOwner owner_;
    std::mutex admission_mutex_;
    std::mutex ingress_mutex_;
    protocol::TriggerIngress ingress_;
    protocol::TriggerEnvelopeLimits response_limits_;
    std::mutex state_mutex_;
    std::condition_variable pump_idle_;
    protocol::OutputReadySubscription subscription_;
    PipeConnectionWriter* writer_{};
    bool closed_{};
    bool failed_{};
    bool pump_active_{};
    bool pump_requested_{};
    bool shutdown_started_{};
    bool shutdown_complete_{};
    std::size_t pump_calls_{};
};

class TriggerPipeHandler final : public PipeChannelHandler {
public:
    TriggerPipeHandler(std::shared_ptr<TriggerPipeCore> core, std::stop_token stop)
        : core_(std::move(core))
    {
        std::weak_ptr<TriggerPipeCore> weak = core_;
        stop_callback_ = std::make_unique<StopCallback>(stop, [weak] {
            if (auto core = weak.lock()) core->shutdown();
        });
    }

    ~TriggerPipeHandler() override { on_close({}); }

    [[nodiscard]] PipeHandlerResult on_frame(
        const bpip::Frame& frame, PipeConnectionWriter& writer,
        const std::stop_token stop) override
    {
        try { return core_->input(frame, writer, stop); }
        catch (...) {
            writer.close_connection();
            return {PipeHandlerAction::close_connection, PipeHostError::handler_failed};
        }
    }

    void on_close(std::stop_token) noexcept override
    {
        std::call_once(close_once_, [this] {
            stop_callback_.reset();
            core_->shutdown();
        });
    }

private:
    using StopCallback = std::stop_callback<std::function<void()>>;
    std::shared_ptr<TriggerPipeCore> core_;
    std::unique_ptr<StopCallback> stop_callback_;
    std::once_flag close_once_;
};

}  // namespace

TriggerPipeChannelFactory::TriggerPipeChannelFactory(
    std::shared_ptr<trigger::TriggerExecutor> executor,
    TriggerPipeChannelLimits limits)
    : executor_(std::move(executor)), limits_(std::move(limits))
{
    if (!executor_) throw std::invalid_argument("trigger executor is required");
    [[maybe_unused]] protocol::TriggerIngress validate_ingress{limits_.ingress};
    [[maybe_unused]] protocol::TriggerSession validate_session{limits_.session};
}

std::unique_ptr<PipeChannelHandler> TriggerPipeChannelFactory::create(
    const PipeOpenRequest& request, const std::stop_token stop)
{
    if (request.channel != PipeChannel::trigger || stop.stop_requested())
        return nullptr;
    try {
        auto core = std::make_shared<TriggerPipeCore>(executor_, limits_);
        core->arm();
        return std::make_unique<TriggerPipeHandler>(std::move(core), stop);
    } catch (...) {
        return nullptr;
    }
}

}  // namespace baas::service::pipe
