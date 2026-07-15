#include "service/websocket/WebSocketHandshake.h"

namespace baas::service::websocket {
namespace {

constexpr bool ascii_iequal(const char left, const char right) noexcept
{
    const auto fold = [](const char value) constexpr noexcept {
        if (value >= 'A' && value <= 'Z') {
            return static_cast<char>(value + ('a' - 'A'));
        }
        return value;
    };
    return fold(left) == fold(right);
}

constexpr bool ascii_iequals(
    const std::string_view left,
    const std::string_view right
) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!ascii_iequal(left[index], right[index])) return false;
    }
    return true;
}

constexpr bool is_ows(const char value) noexcept
{
    return value == ' ' || value == '\t';
}

constexpr std::string_view trim_ows(std::string_view value) noexcept
{
    while (!value.empty() && is_ows(value.front())) value.remove_prefix(1);
    while (!value.empty() && is_ows(value.back())) value.remove_suffix(1);
    return value;
}

constexpr bool is_tchar(const unsigned char value) noexcept
{
    if ((value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z')
        || (value >= 'a' && value <= 'z')) {
        return true;
    }
    switch (value) {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '*':
        case '+':
        case '-':
        case '.':
        case '^':
        case '_':
        case '`':
        case '|':
        case '~': return true;
        default: return false;
    }
}

constexpr bool valid_header_name(const std::string_view name) noexcept
{
    if (name.empty()) return false;
    for (const unsigned char value : name) {
        if (!is_tchar(value)) return false;
    }
    return true;
}

constexpr bool valid_header_value(const std::string_view value) noexcept
{
    for (const unsigned char byte : value) {
        // RFC 9110 field-content permits HTAB, SP, visible ASCII and obs-text,
        // but never CR/LF, NUL, DEL, or another control byte.
        if (byte == '\t') continue;
        if (byte < 0x20U || byte == 0x7fU) return false;
    }
    return true;
}

struct Occurrence {
    std::size_t count{};
    std::string_view first;

    constexpr void add(const std::string_view value) noexcept
    {
        if (count == 0) first = value;
        ++count;
    }
};

constexpr SelectedHeader selected(const Occurrence& occurrence) noexcept
{
    if (occurrence.count == 0) return {};
    if (occurrence.count == 1) {
        return {HeaderCardinality::single, occurrence.first};
    }
    return {HeaderCardinality::multiple, {}};
}

struct RouteResult {
    HandshakeDecision decision{HandshakeDecision::reject};
    HandshakeError error{HandshakeError::unknown_websocket_path};
    HandshakeChannel channel{HandshakeChannel::control};
};

constexpr RouteResult classify_route(
    const std::string_view path,
    const bool remote_enabled
) noexcept
{
    if (path == "/ws/control") {
        return {HandshakeDecision::accept, HandshakeError::none, HandshakeChannel::control};
    }
    if (path == "/ws/provider") {
        return {HandshakeDecision::accept, HandshakeError::none, HandshakeChannel::provider};
    }
    if (path == "/ws/sync") {
        return {HandshakeDecision::accept, HandshakeError::none, HandshakeChannel::sync};
    }
    if (path == "/ws/trigger") {
        return {HandshakeDecision::accept, HandshakeError::none, HandshakeChannel::trigger};
    }
    if (path == "/ws/remote") {
        if (!remote_enabled) {
            return {HandshakeDecision::reject, HandshakeError::remote_disabled,
                    HandshakeChannel::remote};
        }
        return {HandshakeDecision::accept, HandshakeError::none, HandshakeChannel::remote};
    }
    if (!path.starts_with("/ws/")) {
        return {HandshakeDecision::not_websocket, HandshakeError::none,
                HandshakeChannel::control};
    }
    return {};
}

constexpr int base64_value(const unsigned char value) noexcept
{
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return 26 + value - 'a';
    if (value >= '0' && value <= '9') return 52 + value - '0';
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

constexpr bool valid_websocket_key(const std::string_view value) noexcept
{
    // A 16-byte value has exactly 24 canonical RFC 4648 characters and two
    // padding characters. The low four bits of sextet 21 are padding bits.
    if (value.size() != 24 || value[22] != '=' || value[23] != '=') return false;
    for (std::size_t index = 0; index < 22; ++index) {
        if (base64_value(static_cast<unsigned char>(value[index])) < 0) return false;
    }
    const auto last = base64_value(static_cast<unsigned char>(value[21]));
    return last >= 0 && (last & 0x0f) == 0;
}

enum class ConnectionValueResult { valid_with_upgrade, valid_without_upgrade, invalid };

constexpr ConnectionValueResult validate_connection_value(std::string_view value) noexcept
{
    bool found_upgrade = false;
    while (true) {
        const auto comma = value.find(',');
        const auto token = trim_ows(value.substr(0, comma));
        if (token.empty()) return ConnectionValueResult::invalid;
        for (const unsigned char byte : token) {
            if (!is_tchar(byte)) return ConnectionValueResult::invalid;
        }
        if (ascii_iequals(token, "upgrade")) found_upgrade = true;

        if (comma == std::string_view::npos) break;
        value.remove_prefix(comma + 1);
    }
    return found_upgrade ? ConnectionValueResult::valid_with_upgrade
                         : ConnectionValueResult::valid_without_upgrade;
}

enum class ContentLengthResult { zero, invalid, nonzero };

constexpr ContentLengthResult validate_content_length(std::string_view value) noexcept
{
    value = trim_ows(value);
    if (value.empty()) return ContentLengthResult::invalid;
    bool nonzero = false;
    for (const unsigned char byte : value) {
        if (byte < '0' || byte > '9') return ContentLengthResult::invalid;
        if (byte != '0') nonzero = true;
    }
    return nonzero ? ContentLengthResult::nonzero : ContentLengthResult::zero;
}

constexpr bool is_ascii_digit(const char value) noexcept
{
    return value >= '0' && value <= '9';
}

constexpr bool is_ascii_hex(const char value) noexcept
{
    return is_ascii_digit(value) || (value >= 'a' && value <= 'f')
        || (value >= 'A' && value <= 'F');
}

constexpr bool valid_decimal_port(const std::string_view value) noexcept
{
    if (value.empty()) return false;
    unsigned int port = 0;
    for (const char byte : value) {
        if (!is_ascii_digit(byte)) return false;
        const auto digit = static_cast<unsigned int>(byte - '0');
        if (port > (65'535U - digit) / 10U) return false;
        port = port * 10U + digit;
    }
    return true;
}

constexpr bool valid_ipv4(const std::string_view value) noexcept
{
    std::size_t start = 0;
    std::size_t components = 0;
    while (start <= value.size()) {
        const auto dot = value.find('.', start);
        const auto end = dot == std::string_view::npos ? value.size() : dot;
        const auto component = value.substr(start, end - start);
        if (component.empty() || component.size() > 3) return false;
        unsigned int number = 0;
        for (const char byte : component) {
            if (!is_ascii_digit(byte)) return false;
            number = number * 10U + static_cast<unsigned int>(byte - '0');
        }
        if (number > 255U) return false;
        ++components;
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return components == 4;
}

constexpr bool valid_dns_name(std::string_view value) noexcept
{
    if (value.empty()) return false;
    if (value.back() == '.') value.remove_suffix(1);
    if (value.empty() || value.size() > 253) return false;
    std::size_t start = 0;
    while (start < value.size()) {
        const auto dot = value.find('.', start);
        const auto end = dot == std::string_view::npos ? value.size() : dot;
        const auto label = value.substr(start, end - start);
        if (label.empty() || label.size() > 63) return false;
        const auto alnum = [](const char byte) constexpr noexcept {
            return is_ascii_digit(byte) || (byte >= 'a' && byte <= 'z')
                || (byte >= 'A' && byte <= 'Z');
        };
        if (!alnum(label.front()) || !alnum(label.back())) return false;
        for (const char byte : label) {
            if (!alnum(byte) && byte != '-') return false;
        }
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return true;
}

constexpr bool valid_ipv6_side(
    std::string_view side,
    std::size_t& groups,
    const bool may_end_in_ipv4
) noexcept
{
    if (side.empty()) return true;
    std::size_t start = 0;
    while (start <= side.size()) {
        const auto colon = side.find(':', start);
        const auto end = colon == std::string_view::npos ? side.size() : colon;
        const auto group = side.substr(start, end - start);
        if (group.empty()) return false;
        if (group.find('.') != std::string_view::npos) {
            if (!may_end_in_ipv4 || colon != std::string_view::npos
                || !valid_ipv4(group)) {
                return false;
            }
            groups += 2;
        } else {
            if (group.size() > 4) return false;
            for (const char byte : group) {
                if (!is_ascii_hex(byte)) return false;
            }
            ++groups;
        }
        if (colon == std::string_view::npos) break;
        start = colon + 1;
    }
    return true;
}

constexpr bool valid_ipv6(const std::string_view value) noexcept
{
    if (value.empty()) return false;
    const auto compression = value.find("::");
    if (compression != std::string_view::npos
        && value.find("::", compression + 2) != std::string_view::npos) {
        return false;
    }
    std::size_t groups = 0;
    if (compression == std::string_view::npos) {
        return valid_ipv6_side(value, groups, true) && groups == 8;
    }
    const auto left = value.substr(0, compression);
    const auto right = value.substr(compression + 2);
    if (!valid_ipv6_side(left, groups, false)
        || !valid_ipv6_side(right, groups, true)) {
        return false;
    }
    return groups < 8;
}

constexpr bool valid_authority(const std::string_view value) noexcept
{
    if (value.empty()) return false;
    for (const unsigned char byte : value) {
        if (byte <= 0x20U || byte >= 0x7fU || byte == ',' || byte == '@'
            || byte == '/' || byte == '\\' || byte == '?' || byte == '#') {
            return false;
        }
    }
    if (value.front() == '[') {
        const auto close = value.find(']');
        if (close == std::string_view::npos || close == 1
            || !valid_ipv6(value.substr(1, close - 1))) {
            return false;
        }
        const auto suffix = value.substr(close + 1);
        return suffix.empty()
            || (suffix.front() == ':' && valid_decimal_port(suffix.substr(1)));
    }

    const auto colon = value.find(':');
    if (colon != std::string_view::npos
        && value.find(':', colon + 1) != std::string_view::npos) {
        return false;
    }
    const auto host = value.substr(0, colon);
    const auto port = colon == std::string_view::npos
        ? std::string_view{} : value.substr(colon + 1);
    if (host.empty() || (colon != std::string_view::npos && !valid_decimal_port(port))) {
        return false;
    }
    const bool numeric_candidate = host.find_first_not_of("0123456789.")
        == std::string_view::npos;
    return numeric_candidate ? valid_ipv4(host) : valid_dns_name(host);
}

constexpr HandshakeResult reject(
    const HandshakeChannel channel,
    const HandshakeError error,
    const Occurrence& origin = {},
    const Occurrence& cookie = {}
) noexcept
{
    return {
        HandshakeDecision::reject,
        error,
        channel,
        selected(origin),
        selected(cookie),
    };
}

}  // namespace

HandshakeResult validate_handshake(
    const std::string_view method,
    const std::string_view http_version,
    const std::string_view raw_target,
    const std::string_view decoded_path,
    const std::span<const HandshakeHeaderField> headers,
    const bool remote_enabled
) noexcept
{
    const auto route = classify_route(decoded_path, remote_enabled);
    if (route.decision != HandshakeDecision::accept) {
        return {route.decision, route.error, route.channel, {}, {}};
    }
    if (raw_target != decoded_path) {
        return reject(route.channel, HandshakeError::non_canonical_request_target);
    }
    if (method != "GET") return reject(route.channel, HandshakeError::method_not_get);
    if (http_version != "HTTP/1.1") {
        return reject(route.channel, HandshakeError::http_version_not_1_1);
    }
    if (headers.size() > websocket_handshake_max_header_fields) {
        return reject(route.channel, HandshakeError::too_many_header_fields);
    }

    std::size_t header_bytes = 0;
    Occurrence host;
    Occurrence upgrade;
    Occurrence connection;
    Occurrence key;
    Occurrence version;
    Occurrence content_length;
    Occurrence transfer_encoding;
    Occurrence subprotocol;
    Occurrence extensions;
    Occurrence origin;
    Occurrence cookie;

    for (const auto& header : headers) {
        if (header.name.size() > websocket_handshake_max_header_bytes - header_bytes) {
            return reject(route.channel, HandshakeError::header_bytes_exceeded, origin, cookie);
        }
        header_bytes += header.name.size();
        if (header.value.size() > websocket_handshake_max_header_bytes - header_bytes) {
            return reject(route.channel, HandshakeError::header_bytes_exceeded, origin, cookie);
        }
        header_bytes += header.value.size();

        if (!valid_header_name(header.name)) {
            return reject(route.channel, HandshakeError::invalid_header_name, origin, cookie);
        }
        if (!valid_header_value(header.value)) {
            return reject(route.channel, HandshakeError::invalid_header_value, origin, cookie);
        }

        if (ascii_iequals(header.name, "Host")) host.add(header.value);
        else if (ascii_iequals(header.name, "Upgrade")) upgrade.add(header.value);
        else if (ascii_iequals(header.name, "Connection")) connection.add(header.value);
        else if (ascii_iequals(header.name, "Sec-WebSocket-Key")) key.add(header.value);
        else if (ascii_iequals(header.name, "Sec-WebSocket-Version")) version.add(header.value);
        else if (ascii_iequals(header.name, "Content-Length")) content_length.add(header.value);
        else if (ascii_iequals(header.name, "Transfer-Encoding")) transfer_encoding.add(header.value);
        else if (ascii_iequals(header.name, "Sec-WebSocket-Protocol")) subprotocol.add(header.value);
        else if (ascii_iequals(header.name, "Sec-WebSocket-Extensions")) extensions.add(header.value);
        else if (ascii_iequals(header.name, "Origin")) origin.add(header.value);
        else if (ascii_iequals(header.name, "Cookie")) cookie.add(header.value);
    }

    if (host.count == 0) return reject(route.channel, HandshakeError::host_missing, origin, cookie);
    if (host.count != 1) return reject(route.channel, HandshakeError::host_duplicate, origin, cookie);
    const auto host_value = trim_ows(host.first);
    if (host_value.empty()) return reject(route.channel, HandshakeError::host_empty, origin, cookie);
    if (host_value.size() > websocket_handshake_max_host_bytes) {
        return reject(route.channel, HandshakeError::host_too_large, origin, cookie);
    }
    if (host_value != host.first || !valid_authority(host_value)) {
        return reject(route.channel, HandshakeError::host_invalid, origin, cookie);
    }

    if (upgrade.count == 0) {
        return reject(route.channel, HandshakeError::upgrade_missing, origin, cookie);
    }
    if (upgrade.count != 1) {
        return reject(route.channel, HandshakeError::upgrade_duplicate, origin, cookie);
    }
    if (!ascii_iequals(trim_ows(upgrade.first), "websocket")) {
        return reject(route.channel, HandshakeError::upgrade_not_websocket, origin, cookie);
    }

    if (connection.count == 0) {
        return reject(route.channel, HandshakeError::connection_missing, origin, cookie);
    }
    if (connection.count != 1) {
        return reject(route.channel, HandshakeError::connection_duplicate, origin, cookie);
    }
    const auto connection_result = validate_connection_value(connection.first);
    if (connection_result == ConnectionValueResult::invalid) {
        return reject(route.channel, HandshakeError::connection_invalid, origin, cookie);
    }
    if (connection_result == ConnectionValueResult::valid_without_upgrade) {
        return reject(route.channel, HandshakeError::connection_upgrade_missing, origin, cookie);
    }

    if (key.count == 0) {
        return reject(route.channel, HandshakeError::websocket_key_missing, origin, cookie);
    }
    if (key.count != 1) {
        return reject(route.channel, HandshakeError::websocket_key_duplicate, origin, cookie);
    }
    if (!valid_websocket_key(trim_ows(key.first))) {
        return reject(route.channel, HandshakeError::websocket_key_invalid, origin, cookie);
    }

    if (version.count == 0) {
        return reject(route.channel, HandshakeError::websocket_version_missing, origin, cookie);
    }
    if (version.count != 1) {
        return reject(route.channel, HandshakeError::websocket_version_duplicate, origin, cookie);
    }
    if (trim_ows(version.first) != "13") {
        return reject(route.channel, HandshakeError::websocket_version_unsupported, origin, cookie);
    }

    if (content_length.count > 1) {
        return reject(route.channel, HandshakeError::content_length_duplicate, origin, cookie);
    }
    if (content_length.count == 1) {
        switch (validate_content_length(content_length.first)) {
            case ContentLengthResult::invalid:
                return reject(route.channel, HandshakeError::content_length_invalid, origin, cookie);
            case ContentLengthResult::nonzero:
                return reject(route.channel, HandshakeError::content_length_nonzero, origin, cookie);
            case ContentLengthResult::zero: break;
        }
    }

    if (transfer_encoding.count != 0) {
        return reject(route.channel, HandshakeError::transfer_encoding_forbidden, origin, cookie);
    }
    if (subprotocol.count != 0) {
        return reject(route.channel, HandshakeError::subprotocol_unsupported, origin, cookie);
    }
    if (extensions.count != 0) {
        return reject(route.channel, HandshakeError::extensions_unsupported, origin, cookie);
    }

    return {
        HandshakeDecision::accept,
        HandshakeError::none,
        route.channel,
        selected(origin),
        selected(cookie),
    };
}

std::string_view to_string(const HandshakeDecision decision) noexcept
{
    switch (decision) {
        case HandshakeDecision::not_websocket: return "not_websocket";
        case HandshakeDecision::accept: return "accept";
        case HandshakeDecision::reject: return "reject";
    }
    return "unknown";
}

std::string_view to_string(const HandshakeError error) noexcept
{
    switch (error) {
        case HandshakeError::none: return "none";
        case HandshakeError::unknown_websocket_path: return "unknown_websocket_path";
        case HandshakeError::remote_disabled: return "remote_disabled";
        case HandshakeError::method_not_get: return "method_not_get";
        case HandshakeError::http_version_not_1_1: return "http_version_not_1_1";
        case HandshakeError::too_many_header_fields: return "too_many_header_fields";
        case HandshakeError::header_bytes_exceeded: return "header_bytes_exceeded";
        case HandshakeError::invalid_header_name: return "invalid_header_name";
        case HandshakeError::invalid_header_value: return "invalid_header_value";
        case HandshakeError::host_missing: return "host_missing";
        case HandshakeError::host_duplicate: return "host_duplicate";
        case HandshakeError::host_empty: return "host_empty";
        case HandshakeError::host_too_large: return "host_too_large";
        case HandshakeError::host_invalid: return "host_invalid";
        case HandshakeError::non_canonical_request_target:
            return "non_canonical_request_target";
        case HandshakeError::upgrade_missing: return "upgrade_missing";
        case HandshakeError::upgrade_duplicate: return "upgrade_duplicate";
        case HandshakeError::upgrade_not_websocket: return "upgrade_not_websocket";
        case HandshakeError::connection_missing: return "connection_missing";
        case HandshakeError::connection_duplicate: return "connection_duplicate";
        case HandshakeError::connection_invalid: return "connection_invalid";
        case HandshakeError::connection_upgrade_missing: return "connection_upgrade_missing";
        case HandshakeError::websocket_key_missing: return "websocket_key_missing";
        case HandshakeError::websocket_key_duplicate: return "websocket_key_duplicate";
        case HandshakeError::websocket_key_invalid: return "websocket_key_invalid";
        case HandshakeError::websocket_version_missing: return "websocket_version_missing";
        case HandshakeError::websocket_version_duplicate: return "websocket_version_duplicate";
        case HandshakeError::websocket_version_unsupported:
            return "websocket_version_unsupported";
        case HandshakeError::content_length_duplicate: return "content_length_duplicate";
        case HandshakeError::content_length_invalid: return "content_length_invalid";
        case HandshakeError::content_length_nonzero: return "content_length_nonzero";
        case HandshakeError::transfer_encoding_forbidden: return "transfer_encoding_forbidden";
        case HandshakeError::subprotocol_unsupported: return "subprotocol_unsupported";
        case HandshakeError::extensions_unsupported: return "extensions_unsupported";
    }
    return "unknown";
}

std::string_view to_string(const HandshakeChannel channel) noexcept
{
    switch (channel) {
        case HandshakeChannel::control: return "control";
        case HandshakeChannel::provider: return "provider";
        case HandshakeChannel::sync: return "sync";
        case HandshakeChannel::trigger: return "trigger";
        case HandshakeChannel::remote: return "remote";
    }
    return "unknown";
}

std::string_view to_string(const HeaderCardinality cardinality) noexcept
{
    switch (cardinality) {
        case HeaderCardinality::absent: return "absent";
        case HeaderCardinality::single: return "single";
        case HeaderCardinality::multiple: return "multiple";
    }
    return "unknown";
}

}  // namespace baas::service::websocket
