#include "service/pipe/BusinessPipeChannel.h"

#include "service/auth/Crypto.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace baas::service::pipe {
namespace {

namespace ws = baas::service::websocket;
namespace auth = baas::service::auth;

[[nodiscard]] auth::BusinessChannel business_channel(
    const PipeChannel channel) noexcept
{
    switch (channel) {
        case PipeChannel::provider: return auth::BusinessChannel::provider;
        case PipeChannel::sync: return auth::BusinessChannel::sync;
        case PipeChannel::remote: return auth::BusinessChannel::remote;
        case PipeChannel::trigger: return auth::BusinessChannel::trigger;
    }
    return auth::BusinessChannel::provider;
}

[[nodiscard]] bpip::FrameKind output_kind(const PipeChannel channel) noexcept
{
    return channel == PipeChannel::remote
        ? bpip::FrameKind::bytes : bpip::FrameKind::json;
}

[[nodiscard]] ws::BusinessEmitResult emit_error(
    const PipeHostError error) noexcept
{
    switch (error) {
        case PipeHostError::none: return ws::BusinessEmitResult::accepted;
        case PipeHostError::atomic_write_too_large:
            return ws::BusinessEmitResult::message_too_large;
        case PipeHostError::egress_budget_exhausted:
            return ws::BusinessEmitResult::queued_bytes_exceeded;
        case PipeHostError::invalid_limits:
        case PipeHostError::listener_closed:
        case PipeHostError::connection_limit:
        case PipeHostError::read_failed:
        case PipeHostError::truncated_frame:
        case PipeHostError::framing_error:
        case PipeHostError::first_frame_not_json:
        case PipeHostError::open_json_too_large:
        case PipeHostError::malformed_open_json:
        case PipeHostError::duplicate_open_field:
        case PipeHostError::invalid_open_type:
        case PipeHostError::unsupported_channel:
        case PipeHostError::invalid_open_name:
        case PipeHostError::unsupported_frame_kind:
        case PipeHostError::nonempty_close:
        case PipeHostError::channel_unavailable:
        case PipeHostError::handler_failed:
        case PipeHostError::ingress_budget_exhausted:
        case PipeHostError::write_failed:
        case PipeHostError::open_timeout:
        case PipeHostError::read_timeout:
            return ws::BusinessEmitResult::closed;
    }
    return ws::BusinessEmitResult::closed;
}

class PipeBusinessSink final : public ws::BusinessPlaintextSink {
public:
    explicit PipeBusinessSink(const PipeChannel channel)
        : channel_(channel), kind_(output_kind(channel))
    {}

    [[nodiscard]] bool attach(PipeConnectionWriter& writer) noexcept
    {
        std::lock_guard lock{mutex_};
        if (closing_ || writer_ != nullptr) return false;
        writer_ = &writer;
        return true;
    }

    ws::BusinessEmitResult emit(
        ws::BusinessOutboundMessage message) noexcept override
    {
        std::vector<ws::BusinessOutboundMessage> messages;
        try { messages.push_back(std::move(message)); }
        catch (...) { return ws::BusinessEmitResult::resource_exhausted; }
        return emit_impl(std::move(messages), {});
    }

    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage> messages) noexcept override
    {
        return emit_impl(std::move(messages), {});
    }

    ws::BusinessEmitResult emit(
        ws::BusinessOutboundMessage message,
        std::shared_ptr<ws::BusinessBatchCompletion> completion) noexcept override
    {
        std::vector<ws::BusinessOutboundMessage> messages;
        try { messages.push_back(std::move(message)); }
        catch (...) {
            complete(std::move(completion), false);
            return ws::BusinessEmitResult::resource_exhausted;
        }
        return emit_impl(std::move(messages), std::move(completion));
    }

    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage> messages,
        std::shared_ptr<ws::BusinessBatchCompletion> completion) noexcept override
    {
        return emit_impl(std::move(messages), std::move(completion));
    }

    bool enable_remote_raw_output() noexcept override
    {
        std::lock_guard lock{mutex_};
        if (channel_ != PipeChannel::remote || closing_ || output_started_
            || remote_raw_enabled_) return false;
        remote_raw_enabled_ = true;
        return true;
    }

    void begin_close() noexcept
    {
        PipeConnectionWriter* writer{};
        {
            std::lock_guard lock{mutex_};
            closing_ = true;
            writer = writer_;
        }
        // close_connection does not take the writer mutex and therefore can
        // interrupt an in-progress platform write before the callback barrier.
        if (writer) writer->close_connection();
    }

    void wait_idle() noexcept
    {
        std::unique_lock lock{mutex_};
        idle_.wait(lock, [this] { return active_calls_ == 0; });
        writer_ = nullptr;
    }

private:
    static void complete(
        std::shared_ptr<ws::BusinessBatchCompletion> completion,
        const bool written) noexcept
    {
        if (!completion) return;
        try {
            completion->complete(written
                ? ws::BusinessBatchWriteResult::written
                : ws::BusinessBatchWriteResult::failed);
        } catch (...) {
        }
    }

    [[nodiscard]] ws::BusinessEmitResult emit_impl(
        std::vector<ws::BusinessOutboundMessage> messages,
        std::shared_ptr<ws::BusinessBatchCompletion> completion) noexcept
    {
        PipeConnectionWriter* writer{};
        bool unavailable{};
        {
            std::lock_guard lock{mutex_};
            if (closing_ || !writer_) {
                unavailable = true;
            } else {
                ++active_calls_;
                output_started_ = true;
                writer = writer_;
            }
        }
        if (unavailable) {
            complete(std::move(completion), false);
            return ws::BusinessEmitResult::closed;
        }

        PipeHostError write{PipeHostError::write_failed};
        bool allocation_failed{};
        try {
            std::vector<bpip::Frame> frames;
            frames.reserve(messages.size());
            for (auto& message : messages) {
                if (message.final) {
                    write = PipeHostError::handler_failed;
                    break;
                }
                auto payload = std::as_bytes(std::span{
                    message.payload.data(), message.payload.size()});
                frames.push_back({bpip::kind_value(kind_),
                    bpip::Bytes{payload.begin(), payload.end()}});
            }
            if (frames.size() == messages.size()) write = writer->write_batch(frames);
        } catch (...) {
            allocation_failed = true;
        }

        if (write != PipeHostError::none) writer->close_connection();
        const bool written = !allocation_failed && write == PipeHostError::none;
        complete(std::move(completion), written);
        {
            std::lock_guard lock{mutex_};
            if (active_calls_ != 0) --active_calls_;
        }
        idle_.notify_all();
        if (allocation_failed) return ws::BusinessEmitResult::resource_exhausted;
        return emit_error(write);
    }

    PipeChannel channel_;
    bpip::FrameKind kind_;
    std::mutex mutex_;
    std::condition_variable idle_;
    PipeConnectionWriter* writer_{};
    std::size_t active_calls_{};
    bool closing_{};
    bool output_started_{};
    bool remote_raw_enabled_{};
};

[[nodiscard]] PipeHandlerResult handler_result(
    ws::BusinessHandlerResult result,
    const std::shared_ptr<PipeBusinessSink>& sink) noexcept
{
    if (!result.messages.empty()) {
        const auto emitted = sink->emit_batch(std::move(result.messages));
        if (emitted != ws::BusinessEmitResult::accepted) {
            return {PipeHandlerAction::close_connection,
                    PipeHostError::write_failed};
        }
    }
    switch (result.status) {
        case ws::BusinessHandlerStatus::ok: return {};
        case ws::BusinessHandlerStatus::complete:
            return {PipeHandlerAction::close_connection, PipeHostError::none};
        case ws::BusinessHandlerStatus::protocol_failed:
        case ws::BusinessHandlerStatus::capacity:
        case ws::BusinessHandlerStatus::internal_error:
            return {PipeHandlerAction::close_connection,
                    PipeHostError::handler_failed};
    }
    return {PipeHandlerAction::close_connection, PipeHostError::handler_failed};
}

class BusinessPipeHandler final : public PipeChannelHandler {
public:
    BusinessPipeHandler(
        const PipeChannel channel,
        std::unique_ptr<ws::BusinessChannelHandler> handler,
        std::shared_ptr<PipeBusinessSink> sink)
        : channel_(channel), handler_(std::move(handler)), sink_(std::move(sink))
    {}

    ~BusinessPipeHandler() override { on_close({}); }

    PipeHandlerResult on_open(
        PipeConnectionWriter& writer,
        const std::stop_token stop) override
    {
        if (!sink_->attach(writer)) {
            return {PipeHandlerAction::close_connection,
                    PipeHostError::handler_failed};
        }
        try { return handler_result(handler_->ready(stop), sink_); }
        catch (...) {
            return {PipeHandlerAction::close_connection,
                    PipeHostError::handler_failed};
        }
    }

    PipeHandlerResult on_frame(
        const bpip::Frame& frame,
        PipeConnectionWriter&,
        const std::stop_token stop) override
    {
        const auto json = bpip::kind_value(bpip::FrameKind::json);
        const auto bytes = bpip::kind_value(bpip::FrameKind::bytes);
        if ((channel_ == PipeChannel::provider || channel_ == PipeChannel::sync)
            && frame.kind != json) {
            return {PipeHandlerAction::close_connection,
                    PipeHostError::handler_failed};
        }
        if (channel_ == PipeChannel::remote) {
            if ((!remote_configured_ && frame.kind != json)
                || (remote_configured_ && frame.kind != bytes)) {
                return {PipeHandlerAction::close_connection,
                        PipeHostError::handler_failed};
            }
            remote_configured_ = true;
        }
        try {
            auth::SecretBuffer plaintext{std::span<const std::byte>{
                frame.payload.data(), frame.payload.size()}};
            return handler_result(
                handler_->input(std::move(plaintext), false, stop), sink_);
        } catch (...) {
            return {PipeHandlerAction::close_connection,
                    PipeHostError::handler_failed};
        }
    }

    void on_close(const std::stop_token stop) noexcept override
    {
        std::call_once(close_once_, [this, stop] {
            sink_->begin_close();
            try {
                handler_->closed(stop.stop_requested()
                    ? ws::BusinessCloseReason::stopped
                    : ws::BusinessCloseReason::truncated);
            } catch (...) {
            }
            sink_->wait_idle();
            handler_.reset();
        });
    }

private:
    PipeChannel channel_;
    std::unique_ptr<ws::BusinessChannelHandler> handler_;
    std::shared_ptr<PipeBusinessSink> sink_;
    std::once_flag close_once_;
    bool remote_configured_{};
};

}  // namespace

BusinessPipeChannelFactory::BusinessPipeChannelFactory(
    BusinessPipeChannelFactories factories)
    : factories_(std::move(factories))
{
    if (!factories_.provider && !factories_.sync && !factories_.trigger
        && !factories_.remote) {
        throw std::invalid_argument("at least one Pipe channel factory is required");
    }
}

std::unique_ptr<PipeChannelHandler> BusinessPipeChannelFactory::create(
    const PipeOpenRequest& request,
    const std::stop_token stop)
{
    if (stop.stop_requested()) return nullptr;
    if (request.channel == PipeChannel::trigger) {
        return factories_.trigger
            ? factories_.trigger->create(request, stop) : nullptr;
    }

    std::shared_ptr<ws::BusinessChannelHandlerFactory> factory;
    switch (request.channel) {
        case PipeChannel::provider: factory = factories_.provider; break;
        case PipeChannel::sync: factory = factories_.sync; break;
        case PipeChannel::remote: factory = factories_.remote; break;
        case PipeChannel::trigger: break;
    }
    if (!factory) return nullptr;

    try {
        auto sink = std::make_shared<PipeBusinessSink>(request.channel);
        ws::BusinessSessionContext context;
        context.channel = business_channel(request.channel);
        context.session_id = "pipe";
        context.socket_id = request.name;
        auto created = factory->create(std::move(context), sink, stop);
        if (!created) return nullptr;
        return std::make_unique<BusinessPipeHandler>(
            request.channel, std::move(created.handler), std::move(sink));
    } catch (...) {
        return nullptr;
    }
}

}  // namespace baas::service::pipe
