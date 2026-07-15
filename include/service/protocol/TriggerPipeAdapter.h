#pragma once

#include "service/protocol/PipeFraming.h"
#include "service/protocol/TriggerSession.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace baas::service::protocol::trigger {

enum class PipeBatchError : std::uint8_t {
    empty_json,
    json_payload_too_large,
    binary_payload_too_large,
    batch_too_large,
};

[[nodiscard]] std::string_view pipe_batch_error_name(PipeBatchError error) noexcept;

struct PipeBatchEncodeResult {
    bpip::Bytes bytes;
    std::optional<PipeBatchError> error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error.has_value(); }
};

// Produces one owning write buffer containing a JSON BPIP frame followed
// immediately by its optional BYTES frame. Sending this buffer under one
// connection lock preserves the Tauri global binary FIFO contract.
[[nodiscard]] PipeBatchEncodeResult encode_pipe_batch(
    const OutboundBatch& batch,
    std::size_t max_wire_bytes = 72U * 1'024U * 1'024U + 2U * bpip::header_size
);

}  // namespace baas::service::protocol::trigger
