#include "service/protocol/TriggerIngress.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace baas::service::protocol::trigger {
namespace {

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result
) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool valid_response_mode(const ResponseMode mode) noexcept
{
    return mode == ResponseMode::single || mode == ResponseMode::stream;
}

}  // namespace

std::string_view trigger_ingress_error_name(const TriggerIngressError error) noexcept
{
    using enum TriggerIngressError;
    switch (error) {
        case none: return "none";
        case closed: return "closed";
        case item_pending: return "item_pending";
        case json_while_awaiting_binary: return "json_while_awaiting_binary";
        case binary_without_declaration: return "binary_without_declaration";
        case invalid_response_mode: return "invalid_response_mode";
        case json_too_large: return "json_too_large";
        case binary_too_large: return "binary_too_large";
        case aggregate_too_large: return "aggregate_too_large";
        case envelope_rejected: return "envelope_rejected";
        case admission_rejected: return "admission_rejected";
    }
    return "unknown";
}

TriggerIngressItem::TriggerIngressItem(
    CommandEnvelope envelope,
    std::optional<std::vector<std::byte>> binary,
    BuildAdmissionResult build_admission
)
    : envelope_(std::move(envelope)),
      binary_(std::move(binary)),
      build_admission_(std::move(build_admission))
{}

TriggerIngress::TriggerIngress(const TriggerIngressLimits limits) : limits_(limits)
{
    if (!valid_trigger_envelope_limits(limits_.envelope)
        || limits_.max_json_frame_bytes == 0
        || limits_.max_binary_frame_bytes == 0
        || limits_.max_aggregate_bytes == 0
        || limits_.max_json_frame_bytes > limits_.envelope.max_input_json_bytes
        || limits_.max_binary_frame_bytes > limits_.envelope.max_binary_bytes) {
        throw std::invalid_argument("trigger ingress limits are invalid");
    }
}

TriggerIngressResult TriggerIngress::receive_json_frame(
    const std::string_view json,
    const ResponseMode response_mode
)
{
    if (closed_) return reject(TriggerIngressError::closed);
    if (ready_) return reject(TriggerIngressError::item_pending);
    if (pending_binary_) {
        pending_binary_.reset();
        return reject(TriggerIngressError::json_while_awaiting_binary);
    }
    if (!valid_response_mode(response_mode)) {
        return reject(TriggerIngressError::invalid_response_mode);
    }
    if (json.size() > limits_.max_json_frame_bytes) {
        return reject(TriggerIngressError::json_too_large);
    }
    if (json.size() > limits_.max_aggregate_bytes) {
        return reject(TriggerIngressError::aggregate_too_large);
    }

    auto decoded = decode_command_envelope(json, limits_.envelope);
    if (!decoded) {
        return reject(
            TriggerIngressError::envelope_rejected,
            decoded.error,
            decoded.error_offset
        );
    }
    if (decoded.envelope.expects_binary) {
        pending_binary_.emplace(PendingBinary{
            std::move(decoded.envelope), json.size(), response_mode,
        });
        return {TriggerIngressOutcome::awaiting_binary};
    }
    return complete(std::move(decoded.envelope), std::nullopt, response_mode);
}

TriggerIngressResult TriggerIngress::receive_binary_frame(
    const std::span<const std::byte> binary
)
{
    if (closed_) return reject(TriggerIngressError::closed);
    if (ready_) return reject(TriggerIngressError::item_pending);
    if (!pending_binary_) {
        return reject(TriggerIngressError::binary_without_declaration);
    }

    // Consuming the promised frame ends the partial state before any operation
    // that can reject or allocate. Every failure therefore returns to JSON input.
    auto pending = std::move(*pending_binary_);
    pending_binary_.reset();
    if (binary.size() > limits_.max_binary_frame_bytes) {
        return reject(TriggerIngressError::binary_too_large);
    }
    std::size_t aggregate_bytes = 0;
    if (!checked_add(pending.json_frame_bytes, binary.size(), aggregate_bytes)
        || aggregate_bytes > limits_.max_aggregate_bytes) {
        return reject(TriggerIngressError::aggregate_too_large);
    }

    std::vector<std::byte> owned_binary(binary.size());
    if (!binary.empty()) {
        std::copy(binary.begin(), binary.end(), owned_binary.begin());
    }
    return complete(
        std::move(pending.envelope),
        std::optional<std::vector<std::byte>>{std::move(owned_binary)},
        pending.response_mode
    );
}

std::optional<TriggerIngressItem> TriggerIngress::take_ready()
{
    if (!ready_) return std::nullopt;
    auto result = std::move(ready_);
    ready_.reset();
    return result;
}

void TriggerIngress::reset() noexcept
{
    if (closed_) return;
    pending_binary_.reset();
    ready_.reset();
}

void TriggerIngress::close() noexcept
{
    closed_ = true;
    pending_binary_.reset();
    ready_.reset();
}

TriggerIngressState TriggerIngress::state() const noexcept
{
    if (closed_) return TriggerIngressState::closed;
    if (ready_) return TriggerIngressState::ready;
    if (pending_binary_) return TriggerIngressState::awaiting_binary;
    return TriggerIngressState::accepting_json;
}

TriggerIngressResult TriggerIngress::complete(
    CommandEnvelope envelope,
    std::optional<std::vector<std::byte>> binary,
    const ResponseMode response_mode
)
{
    const std::optional<std::size_t> binary_bytes = binary
        ? std::optional<std::size_t>{binary->size()} : std::nullopt;
    auto build = make_admission(envelope, binary_bytes, response_mode);
    if (!build) {
        return reject(TriggerIngressError::admission_rejected, build.error);
    }

    TriggerIngressItem item{
        std::move(envelope), std::move(binary), std::move(build),
    };
    ready_.emplace(std::move(item));
    return {TriggerIngressOutcome::ready};
}

TriggerIngressResult TriggerIngress::reject(
    const TriggerIngressError error,
    const EnvelopeError envelope_error,
    const std::size_t error_offset
) noexcept
{
    return {
        TriggerIngressOutcome::rejected,
        error,
        envelope_error,
        error_offset,
    };
}

}  // namespace baas::service::protocol::trigger
