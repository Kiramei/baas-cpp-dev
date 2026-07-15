#include "service/protocol/TriggerIngress.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace baas::service::protocol::trigger {
namespace {

namespace command_catalog = baas::service::trigger;

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

[[nodiscard]] ResponseMode response_mode_for(
    const command_catalog::TriggerCommandDescriptor& descriptor) noexcept
{
    using enum command_catalog::TriggerCommandResponseMode;
    return descriptor.response_mode == stream
        ? ResponseMode::stream : ResponseMode::single;
}

[[nodiscard]] TriggerIngressResult command_rejection_result(
    const TriggerIngressError error,
    std::string command,
    const Timestamp timestamp,
    const EnvelopeError envelope_error = EnvelopeError::none,
    const AdmissionError admission_error = AdmissionError::none
)
{
    TriggerIngressResult result;
    result.error = error;
    result.envelope_error = envelope_error;
    result.admission_error = admission_error;
    result.command_rejection.emplace(TriggerCommandRejection{
        std::move(command), timestamp, error,
    });
    return result;
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
        case json_too_large: return "json_too_large";
        case binary_too_large: return "binary_too_large";
        case aggregate_too_large: return "aggregate_too_large";
        case envelope_rejected: return "envelope_rejected";
        case unknown_command: return "unknown_command";
        case config_id_required: return "config_id_required";
        case binary_marker_required: return "binary_marker_required";
        case binary_marker_forbidden: return "binary_marker_forbidden";
        case admission_rejected: return "admission_rejected";
    }
    return "unknown";
}

TriggerIngressDisposition trigger_ingress_disposition(
    const TriggerIngressError error) noexcept
{
    using enum TriggerIngressError;
    switch (error) {
        case none: return TriggerIngressDisposition::none;
        case closed: return TriggerIngressDisposition::closed;
        case unknown_command:
        case config_id_required:
        case binary_marker_required:
        case binary_marker_forbidden:
        case admission_rejected:
            return TriggerIngressDisposition::command_rejection;
        case json_while_awaiting_binary:
        case item_pending:
        case binary_without_declaration:
        case json_too_large:
        case binary_too_large:
        case aggregate_too_large:
        case envelope_rejected:
            return TriggerIngressDisposition::fatal;
    }
    // Unknown future values fail closed instead of accidentally continuing a
    // potentially desynchronized frame stream.
    return TriggerIngressDisposition::fatal;
}

std::string_view trigger_ingress_disposition_name(
    const TriggerIngressDisposition disposition) noexcept
{
    using enum TriggerIngressDisposition;
    switch (disposition) {
        case none: return "none";
        case fatal: return "fatal";
        case recoverable: return "recoverable";
        case command_rejection: return "command_rejection";
        case closed: return "closed";
    }
    return "unknown";
}

std::string_view trigger_command_rejection_safe_message(
    const TriggerIngressError error) noexcept
{
    using enum TriggerIngressError;
    switch (error) {
        case unknown_command: return "Unsupported command";
        case config_id_required: return "config_id is required";
        case binary_marker_required:
            return "binary archive payload is required for import_config";
        case binary_marker_forbidden:
            return "binary payload is not allowed for this command";
        case admission_rejected: return "command admission rejected";
        default: return "command rejected";
    }
}

TriggerIngressItem::TriggerIngressItem(
    CommandEnvelope envelope,
    std::optional<std::vector<std::byte>> binary,
    BuildAdmissionResult build_admission,
    const command_catalog::TriggerCommandDescriptor* const descriptor
)
    : envelope_(std::move(envelope)),
      binary_(std::move(binary)),
      build_admission_(std::move(build_admission)),
      descriptor_(descriptor)
{}

TriggerIngressResult TriggerIngressItem::admit_to(TriggerSession& session) const
{
    const auto admitted = session.admit(admission());
    if (admitted) return {TriggerIngressOutcome::admitted};

    if (admitted.error == AdmissionError::closed) {
        TriggerIngressResult result;
        result.error = TriggerIngressError::closed;
        result.admission_error = admitted.error;
        return result;
    }
    return command_rejection_result(
        TriggerIngressError::admission_rejected,
        envelope_.command,
        envelope_.timestamp,
        EnvelopeError::none,
        admitted.error
    );
}

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

TriggerIngressResult TriggerIngress::receive_json_frame(const std::string_view json)
{
    if (closed_) return reject(TriggerIngressError::closed);
    if (ready_) return reject(TriggerIngressError::item_pending);
    if (pending_binary_) {
        pending_binary_.reset();
        return reject(TriggerIngressError::json_while_awaiting_binary);
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
    const auto lookup = command_catalog::TriggerCommandCatalog::lookup(
        decoded.envelope.command);
    if (!lookup) {
        return command_rejection_result(
            TriggerIngressError::unknown_command,
            std::move(decoded.envelope.command),
            decoded.envelope.timestamp
        );
    }
    const auto* const descriptor = lookup.descriptor;
    if (descriptor->config_id
            == command_catalog::TriggerConfigIdRequirement::required
        && (!decoded.envelope.config_id
            || decoded.envelope.config_id->empty())) {
        return command_rejection_result(
            TriggerIngressError::config_id_required,
            std::move(decoded.envelope.command),
            decoded.envelope.timestamp
        );
    }
    const bool requires_binary = descriptor->inbound_binary
        == command_catalog::TriggerInboundBinaryPolicy::required;
    if (requires_binary && !decoded.envelope.declares_binary) {
        return command_rejection_result(
            TriggerIngressError::binary_marker_required,
            std::move(decoded.envelope.command),
            decoded.envelope.timestamp
        );
    }
    if (!requires_binary && decoded.envelope.declares_binary) {
        return command_rejection_result(
            TriggerIngressError::binary_marker_forbidden,
            std::move(decoded.envelope.command),
            decoded.envelope.timestamp
        );
    }

    const auto response_mode = response_mode_for(*descriptor);
    if (requires_binary) {
        pending_binary_.emplace(PendingBinary{
            std::move(decoded.envelope), json.size(), response_mode, descriptor,
        });
        return {TriggerIngressOutcome::awaiting_binary};
    }
    return complete(
        std::move(decoded.envelope), std::nullopt, response_mode, descriptor);
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
        pending.response_mode,
        pending.descriptor
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
    const ResponseMode response_mode,
    const command_catalog::TriggerCommandDescriptor* const descriptor
)
{
    const std::optional<std::size_t> binary_bytes = binary
        ? std::optional<std::size_t>{binary->size()} : std::nullopt;
    auto build = make_admission(envelope, binary_bytes, response_mode);
    if (!build) {
        return command_rejection_result(
            TriggerIngressError::admission_rejected,
            std::move(envelope.command),
            envelope.timestamp,
            build.error
        );
    }

    TriggerIngressItem item{
        std::move(envelope), std::move(binary), std::move(build), descriptor,
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
    TriggerIngressResult result;
    result.error = error;
    result.envelope_error = envelope_error;
    result.error_offset = error_offset;
    if (result.disposition() == TriggerIngressDisposition::fatal) close();
    return result;
}

}  // namespace baas::service::protocol::trigger
