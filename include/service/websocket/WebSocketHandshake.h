#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace httplib {
struct Request;
}

namespace baas::service::websocket {

// These pure-function limits apply to decoded fields presented to
// validate_handshake(). The byte budget is the sum of name.size()+value.size()
// for every field. The patched cpp-httplib parser independently enforces a
// process-wide 32 KiB bound over the original header lines, including CRLF.
inline constexpr std::size_t websocket_handshake_max_header_fields = 64;
inline constexpr std::size_t websocket_handshake_max_header_bytes = 32U * 1'024U;
inline constexpr std::size_t websocket_handshake_max_host_bytes = 255;

struct HandshakeHeaderField {
    std::string_view name;
    std::string_view value;
};

enum class HandshakeChannel {
    control,
    provider,
    sync,
    trigger,
    remote,
};

enum class HeaderCardinality {
    absent,
    single,
    multiple,
};

struct SelectedHeader {
    HeaderCardinality cardinality{HeaderCardinality::absent};
    // Populated only for single. The view remains valid for as long as the
    // input header storage remains valid.
    std::string_view value;
};

enum class HandshakeDecision {
    not_websocket,
    accept,
    reject,
};

// These values are a stable boundary contract. Keep to_string() spellings
// stable so logs and transport-independent tests do not depend on prose.
enum class HandshakeError {
    none,
    unknown_websocket_path,
    remote_disabled,
    method_not_get,
    http_version_not_1_1,
    too_many_header_fields,
    header_bytes_exceeded,
    invalid_header_name,
    invalid_header_value,
    host_missing,
    host_duplicate,
    host_empty,
    host_too_large,
    host_invalid,
    non_canonical_request_target,
    upgrade_missing,
    upgrade_duplicate,
    upgrade_not_websocket,
    connection_missing,
    connection_duplicate,
    connection_invalid,
    connection_upgrade_missing,
    websocket_key_missing,
    websocket_key_duplicate,
    websocket_key_invalid,
    websocket_version_missing,
    websocket_version_duplicate,
    websocket_version_unsupported,
    content_length_duplicate,
    content_length_invalid,
    content_length_nonzero,
    transfer_encoding_forbidden,
    subprotocol_unsupported,
    extensions_unsupported,
};

struct HandshakeResult {
    HandshakeDecision decision{HandshakeDecision::not_websocket};
    HandshakeError error{HandshakeError::none};
    HandshakeChannel channel{HandshakeChannel::control};
    SelectedHeader origin;
    SelectedHeader cookie;

    [[nodiscard]] bool accepted() const noexcept
    {
        return decision == HandshakeDecision::accept;
    }
};

struct HttplibHandshakeEvaluation {
    HandshakeResult handshake;
    int rejection_status{};
    bool advertise_supported_version{};
};

// Unknown paths outside /ws/ are deliberately classified as not_websocket so
// the ordinary HTTP router can continue. Exact /ws/* routes are owned here;
// an unknown one is a rejected WebSocket route, never a prefix match.
[[nodiscard]] HandshakeResult validate_handshake(
    std::string_view method,
    std::string_view http_version,
    std::string_view raw_target,
    std::string_view decoded_path,
    std::span<const HandshakeHeaderField> headers,
    bool remote_enabled
) noexcept;

// This narrow adapter is shared by the real pre-routing handler and tests so
// request.target (raw) can never accidentally be replaced by request.path
// (decoded) at the transport boundary.
[[nodiscard]] HttplibHandshakeEvaluation evaluate_httplib_handshake_request(
    const httplib::Request& request,
    bool remote_enabled
);

[[nodiscard]] std::string_view to_string(HandshakeDecision decision) noexcept;
[[nodiscard]] std::string_view to_string(HandshakeError error) noexcept;
[[nodiscard]] std::string_view to_string(HandshakeChannel channel) noexcept;
[[nodiscard]] std::string_view to_string(HeaderCardinality cardinality) noexcept;

}  // namespace baas::service::websocket
