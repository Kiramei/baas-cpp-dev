#include "service/protocol/TriggerPipeAdapter.h"

#include <limits>
#include <span>

namespace baas::service::protocol::trigger {
namespace {

[[nodiscard]] bool checked_add(
    const std::size_t left, const std::size_t right, std::size_t& result
) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    result = left + right;
    return true;
}

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value) noexcept
{
    return {
        reinterpret_cast<const std::byte*>(value.data()),
        value.size(),
    };
}

}  // namespace

std::string_view pipe_batch_error_name(const PipeBatchError error) noexcept
{
    using enum PipeBatchError;
    switch (error) {
        case empty_json: return "empty_json";
        case json_payload_too_large: return "json_payload_too_large";
        case binary_payload_too_large: return "binary_payload_too_large";
        case batch_too_large: return "batch_too_large";
    }
    return "unknown";
}

PipeBatchEncodeResult encode_pipe_batch(
    const OutboundBatch& batch, const std::size_t max_wire_bytes)
{
    const bool has_binary = batch.has_binary();
    if (batch.json().empty()) return {{}, PipeBatchError::empty_json};
    if (batch.json().size() > bpip::max_payload_size)
        return {{}, PipeBatchError::json_payload_too_large};
    if (batch.binary().size() > bpip::max_payload_size)
        return {{}, PipeBatchError::binary_payload_too_large};

    std::size_t wire_bytes{};
    if (!checked_add(bpip::header_size, batch.json().size(), wire_bytes))
        return {{}, PipeBatchError::batch_too_large};
    if (has_binary) {
        std::size_t binary_frame_bytes{};
        if (!checked_add(bpip::header_size, batch.binary().size(), binary_frame_bytes)
            || !checked_add(wire_bytes, binary_frame_bytes, wire_bytes)) {
            return {{}, PipeBatchError::batch_too_large};
        }
    }
    if (wire_bytes > max_wire_bytes) return {{}, PipeBatchError::batch_too_large};

    auto json_frame = bpip::encode_frame(bpip::FrameKind::json, bytes(batch.json()));
    if (!json_frame) return {{}, PipeBatchError::json_payload_too_large};

    PipeBatchEncodeResult result;
    result.bytes.reserve(wire_bytes);
    result.bytes.insert(
        result.bytes.end(), json_frame.bytes.begin(), json_frame.bytes.end());
    if (has_binary) {
        const auto binary_frame = bpip::encode_frame(
            bpip::FrameKind::bytes,
            std::span<const std::byte>{batch.binary().data(), batch.binary().size()});
        if (!binary_frame) return {{}, PipeBatchError::binary_payload_too_large};
        result.bytes.insert(
            result.bytes.end(), binary_frame.bytes.begin(), binary_frame.bytes.end());
    }
    return result;
}

}  // namespace baas::service::protocol::trigger
