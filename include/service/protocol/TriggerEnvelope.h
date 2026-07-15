#pragma once

#include "service/protocol/TriggerSession.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::protocol::trigger {

// Independent limits keep hostile JSON bounded before it reaches a command
// dispatcher. max_work counts consumed input bytes, including whitespace.
struct TriggerEnvelopeLimits {
    std::size_t max_input_json_bytes{1U * 1'024U * 1'024U};
    std::size_t max_output_json_bytes{1U * 1'024U * 1'024U};
    std::size_t max_binary_bytes{64U * 1'024U * 1'024U};
    std::size_t max_command_bytes{128};
    std::size_t max_config_id_bytes{256};
    std::size_t max_depth{64};
    std::size_t max_nodes{65'536};
    std::size_t max_string_bytes{1U * 1'024U * 1'024U};
    std::size_t max_work{4U * 1'024U * 1'024U};
};

[[nodiscard]] bool valid_trigger_envelope_limits(
    const TriggerEnvelopeLimits& limits
) noexcept;

enum class EnvelopeError : std::uint8_t {
    none,
    invalid_limits,
    input_too_large,
    invalid_utf8,
    invalid_json,
    depth_limit,
    node_limit,
    string_limit,
    work_limit,
    duplicate_key,
    root_not_object,
    missing_type,
    invalid_type,
    missing_command,
    invalid_command,
    missing_timestamp,
    invalid_timestamp,
    invalid_config_id,
    invalid_payload,
    binary_presence_mismatch,
    invalid_data,
    invalid_error,
    invalid_terminal,
    reserved_binary_field,
    binary_too_large,
    output_too_large,
};

[[nodiscard]] std::string_view envelope_error_name(EnvelopeError error) noexcept;

// payload_json is validated, duplicate-free, compact UTF-8 JSON. Unknown
// top-level fields are ignored to retain Pydantic v1's existing extra-field
// compatibility. Only import_config with payload.binary === true declares an
// inbound BYTES frame, matching the Python channel handler.
struct CommandEnvelope {
    std::string command;
    Timestamp timestamp{};
    std::optional<std::string> config_id;
    std::string payload_json{"{}"};
    bool expects_binary{};
};

struct DecodeCommandResult {
    CommandEnvelope envelope;
    EnvelopeError error{EnvelopeError::none};
    std::size_t error_offset{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == EnvelopeError::none;
    }
};

[[nodiscard]] DecodeCommandResult decode_command_envelope(
    std::string_view json,
    TriggerEnvelopeLimits limits = {}
);

// Builds the lifecycle admission only after the transport has received and
// bounded the optional immediately-following binary frame.
struct BuildAdmissionResult {
    CommandAdmission admission;
    EnvelopeError error{EnvelopeError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == EnvelopeError::none;
    }
};

[[nodiscard]] BuildAdmissionResult make_admission(
    const CommandEnvelope& envelope,
    std::optional<std::size_t> binary_frame_bytes,
    ResponseMode mode = ResponseMode::single
);

// data_json, when present, may contain any JSON value for wire compatibility.
// Binary output requires object data so the codec can exclusively own and
// inject data.binary.size. A present empty vector still means "send a zero-byte
// binary frame" and is distinct from std::nullopt.
struct CommandResponse {
    std::string command;
    Timestamp timestamp{};
    ResponseStatus status{ResponseStatus::ok};
    ResponseMode response_mode{ResponseMode::single};
    bool terminal{true};
    std::optional<std::string> data_json;
    std::string error;
    std::optional<std::vector<std::byte>> binary;
};

struct EncodeResponseResult {
    OutboundBatch batch;
    EnvelopeError error{EnvelopeError::none};
    std::size_t error_offset{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == EnvelopeError::none;
    }
};

// Produces the exact outer command_response envelope and an indivisible binary
// batch. Callers publish only the returned batch into TriggerSession.
[[nodiscard]] EncodeResponseResult encode_command_response(
    CommandResponse response,
    TriggerEnvelopeLimits limits = {}
);

}  // namespace baas::service::protocol::trigger
