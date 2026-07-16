#include "service/websocket/WebSocketHandshake.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace service_ws = baas::service::websocket;

namespace {

using Field = service_ws::HandshakeHeaderField;
using Error = service_ws::HandshakeError;
using Decision = service_ws::HandshakeDecision;
using Channel = service_ws::HandshakeChannel;
using Cardinality = service_ws::HeaderCardinality;

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<Field> valid_headers()
{
    return {
        {"Host", "127.0.0.1:8190"},
        {"Upgrade", "websocket"},
        {"Connection", "keep-alive, Upgrade"},
        {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="},
        {"Sec-WebSocket-Version", "13"},
    };
}

service_ws::HandshakeResult evaluate(
    const std::vector<Field>& headers,
    const std::string_view path = "/ws/control",
    const bool remote_enabled = true,
    const std::string_view method = "GET",
    const std::string_view version = "HTTP/1.1",
    const std::optional<std::string_view> raw_target = std::nullopt
)
{
    return service_ws::validate_handshake(
        method, version, raw_target.value_or(path), path, headers, remote_enabled);
}

void expect_error(
    const std::vector<Field>& headers,
    const Error expected,
    const std::string_view message
)
{
    const auto result = evaluate(headers);
    check(result.decision == Decision::reject && result.error == expected, message);
}

void remove_header(std::vector<Field>& headers, const std::string_view name)
{
    const auto position = std::find_if(headers.begin(), headers.end(), [&](const Field& field) {
        return field.name == name;
    });
    if (position != headers.end()) headers.erase(position);
}

void set_header(
    std::vector<Field>& headers,
    const std::string_view name,
    const std::string_view value
)
{
    const auto position = std::find_if(headers.begin(), headers.end(), [&](const Field& field) {
        return field.name == name;
    });
    if (position != headers.end()) position->value = value;
}

std::size_t header_bytes(const std::vector<Field>& headers)
{
    std::size_t result = 0;
    for (const auto& header : headers) result += header.name.size() + header.value.size();
    return result;
}

void test_routes_and_request_line()
{
    const auto headers = valid_headers();
    for (const auto [path, channel] : {
             std::pair{std::string_view{"/ws/control"}, Channel::control},
             std::pair{std::string_view{"/ws/provider"}, Channel::provider},
             std::pair{std::string_view{"/ws/sync"}, Channel::sync},
             std::pair{std::string_view{"/ws/trigger"}, Channel::trigger},
             std::pair{std::string_view{"/ws/remote"}, Channel::remote},
         }) {
        const auto result = evaluate(headers, path);
        check(result.accepted() && result.channel == channel,
              "each of the five exact WebSocket routes must be accepted");
    }

    for (const auto path : {"/health", "/ws", "/ws-control", "/api/ws/control"}) {
        const auto result = evaluate(headers, path);
        check(result.decision == Decision::not_websocket && result.error == Error::none,
              "non-WebSocket paths must continue through the HTTP router");
    }
    for (const auto path : {"/ws/", "/ws/unknown", "/ws/control/", "/ws/control?x=1"}) {
        const auto result = evaluate(headers, path);
        check(result.decision == Decision::reject
                  && result.error == Error::unknown_websocket_path,
              "unknown or non-exact /ws/ routes must fail closed");
    }

    auto result = evaluate(headers, "/ws/remote", false);
    check(result.decision == Decision::reject && result.error == Error::remote_disabled
              && result.channel == Channel::remote,
          "remote route must be rejected when the caller disables it");
    result = evaluate(headers, "/ws/control", true, "POST");
    check(result.error == Error::method_not_get, "only exact uppercase GET is allowed");
    result = evaluate(headers, "/ws/control", true, "get");
    check(result.error == Error::method_not_get, "method matching must be case-sensitive");
    result = evaluate(headers, "/ws/control", true, "GET", "HTTP/2");
    check(result.error == Error::http_version_not_1_1, "HTTP/2 must be rejected");
    result = evaluate(headers, "/ws/control", true, "GET", "1.1");
    check(result.error == Error::http_version_not_1_1,
          "only the complete HTTP/1.1 version token is accepted");

    for (const auto raw_target : {
             "/ws/control?x=1", "/ws/%63ontrol", "/ws/control#fragment"}) {
        result = evaluate(
            headers, "/ws/control", true, "GET", "HTTP/1.1", raw_target);
        check(result.error == Error::non_canonical_request_target,
              "owned decoded routes require an identical canonical raw target");
    }
    result = evaluate(
        headers, "/ws/control", true, "GET", "HTTP/1.1", "/ws/control");
    check(result.accepted(), "identical raw target and decoded path must be accepted");
}

void test_header_syntax_and_required_cardinality()
{
    {
        auto headers = valid_headers();
        headers.push_back({"Bad Header", "x"});
        expect_error(headers, Error::invalid_header_name, "spaces are forbidden in header names");
    }
    {
        auto headers = valid_headers();
        headers.push_back({"X-Test", "safe\r\nInjected: yes"});
        expect_error(headers, Error::invalid_header_value, "CRLF injection must be rejected");
    }
    {
        auto headers = valid_headers();
        headers.push_back({"X-Test", std::string_view{"bad\0value", 9}});
        expect_error(headers, Error::invalid_header_value, "NUL in a field value must be rejected");
    }

    struct RequiredCase {
        std::string_view name;
        Error missing;
        Error duplicate;
    };
    for (const auto& item : {
             RequiredCase{"Host", Error::host_missing, Error::host_duplicate},
             RequiredCase{"Upgrade", Error::upgrade_missing, Error::upgrade_duplicate},
             RequiredCase{"Connection", Error::connection_missing, Error::connection_duplicate},
             RequiredCase{"Sec-WebSocket-Key", Error::websocket_key_missing,
                          Error::websocket_key_duplicate},
             RequiredCase{"Sec-WebSocket-Version", Error::websocket_version_missing,
                          Error::websocket_version_duplicate},
         }) {
        auto headers = valid_headers();
        const auto original = *std::find_if(
            headers.begin(), headers.end(), [&](const Field& field) { return field.name == item.name; });
        remove_header(headers, item.name);
        expect_error(headers, item.missing, "each required handshake header must be present");
        headers.push_back(original);
        headers.push_back(original);
        expect_error(headers, item.duplicate, "required handshake headers must not be duplicated");
    }

    auto headers = valid_headers();
    headers[0].name = "hOsT";
    headers[1].name = "uPgRaDe";
    headers[2].name = "cOnNeCtIoN";
    headers[3].name = "sEc-WeBsOcKeT-kEy";
    headers[4].name = "SeC-wEbSoCkEt-VeRsIoN";
    check(evaluate(headers).accepted(), "HTTP field names must compare case-insensitively");
}

void test_host_upgrade_and_connection_values()
{
    {
        auto headers = valid_headers();
        set_header(headers, "Host", " \t ");
        expect_error(headers, Error::host_empty, "OWS-only Host must be rejected");
    }
    {
        const std::string host = std::string(63, 'a') + "."
            + std::string(63, 'b') + "." + std::string(63, 'c') + "."
            + std::string(61, 'd');
        auto headers = valid_headers();
        set_header(headers, "Host", host);
        check(host.size() == 253 && evaluate(headers).accepted(),
              "the longest strict DNS name must be accepted");
    }
    {
        const std::string host(service_ws::websocket_handshake_max_host_bytes + 1, 'h');
        auto headers = valid_headers();
        set_header(headers, "Host", host);
        expect_error(headers, Error::host_too_large, "Host maximum plus one must be rejected");
    }
    for (const auto value : {
             "localhost", "example.com", "example.com.", "127.0.0.1",
             "127.0.0.1:8190", "[::1]", "[2001:db8::1]:65535",
             "[::ffff:192.0.2.1]:0"}) {
        auto headers = valid_headers();
        set_header(headers, "Host", value);
        check(evaluate(headers).accepted(),
              "strict DNS, IPv4 and bracketed IPv6 authorities must be accepted");
    }
    const std::string obs_text_host{"example.com\x80", 12};
    for (const auto value : {
             std::string_view{" example.com"}, std::string_view{"example.com "},
             std::string_view{"example.com,evil"}, std::string_view{"user@example.com"},
             std::string_view{"example.com/path"}, std::string_view{"example.com\\path"},
             std::string_view{"example.com?x"}, std::string_view{"example.com#x"},
             std::string_view{"127.0.0.256"}, std::string_view{"[192.0.2.1::]"},
             std::string_view{"2001:db8::1"}, std::string_view{"[::1"},
             std::string_view{"[::1]x"}, std::string_view{"example.com:"},
             std::string_view{"example.com:-1"}, std::string_view{"example.com:+1"},
             std::string_view{"example.com:65536"}, std::string_view{"example.com:http"},
             std::string_view{obs_text_host}}) {
        auto headers = valid_headers();
        set_header(headers, "Host", value);
        expect_error(headers, Error::host_invalid,
                     "malformed or ambiguous Host authorities must be rejected");
    }
    for (const auto value : {"h2c", "websocket,h2c", "xwebsocket", "websocket-x"}) {
        auto headers = valid_headers();
        set_header(headers, "Upgrade", value);
        expect_error(headers, Error::upgrade_not_websocket,
                     "Upgrade must be exactly the websocket token");
    }
    {
        auto headers = valid_headers();
        set_header(headers, "Upgrade", " \tWebSocket\t ");
        check(evaluate(headers).accepted(), "Upgrade token permits surrounding OWS");
    }
    for (const auto value : {"keep-alive", "xupgrade", "upgrade-x", "upgrader"}) {
        auto headers = valid_headers();
        set_header(headers, "Connection", value);
        expect_error(headers, Error::connection_upgrade_missing,
                     "Connection substring attacks must not satisfy the upgrade token");
    }
    for (const auto value : {"", ",upgrade", "upgrade,", "keep-alive,,upgrade",
                             "keep alive,upgrade"}) {
        auto headers = valid_headers();
        set_header(headers, "Connection", value);
        expect_error(headers, Error::connection_invalid,
                     "Connection must be a non-empty comma-separated token list");
    }
    for (const auto value : {"Upgrade", " keep-alive , UpGrAdE ", "upgrade,close"}) {
        auto headers = valid_headers();
        set_header(headers, "Connection", value);
        check(evaluate(headers).accepted(), "a complete case-insensitive upgrade token is valid");
    }
}

void test_key_and_version_values()
{
    for (const auto value : {
             "dGhlIHNhbXBsZSBub25jZQ=",    // short/one padding byte
             "dGhlIHNhbXBsZSBub25jZQ===",  // too much padding
             "dGhlIHNhbXBsZSBub25jZQ=A",   // padding in data position
             "dGhlIHNhbXBsZSBub25jZQ--",   // URL-safe alphabet and no padding
             "dGhlIHNhbXBsZSBub25jZR==",   // non-zero canonical padding bits
             "dGhlIHNhbXBsZSBub25jZ!==",   // invalid alphabet
             "AAAAAAAAAAAAAAAAAAAAAA=A",   // malformed padding
         }) {
        auto headers = valid_headers();
        set_header(headers, "Sec-WebSocket-Key", value);
        expect_error(headers, Error::websocket_key_invalid,
                     "WebSocket key must be canonical base64 for exactly 16 bytes");
    }
    for (const auto value : {
             "dGhlIHNhbXBsZSBub25jZQ==",
             "AAAAAAAAAAAAAAAAAAAAAA==",
             "/////////////////////w==",
             " \tdGhlIHNhbXBsZSBub25jZQ==\t ",
         }) {
        auto headers = valid_headers();
        set_header(headers, "Sec-WebSocket-Key", value);
        check(evaluate(headers).accepted(), "canonical 16-byte WebSocket keys must be accepted");
    }

    for (const auto value : {"12", "14", "013", "13, 13", "+13", ""}) {
        auto headers = valid_headers();
        set_header(headers, "Sec-WebSocket-Version", value);
        expect_error(headers, Error::websocket_version_unsupported,
                     "only WebSocket version 13 is supported");
    }
    auto headers = valid_headers();
    set_header(headers, "Sec-WebSocket-Version", " \t13\t ");
    check(evaluate(headers).accepted(), "WebSocket version permits surrounding OWS");
}

void test_body_framing_and_unsupported_negotiation()
{
    check(evaluate(valid_headers()).accepted(), "Content-Length may be absent");
    for (const auto value : {"0", "00", "\t000 \t"}) {
        auto headers = valid_headers();
        headers.push_back({"Content-Length", value});
        check(evaluate(headers).accepted(), "strict decimal representations of zero are allowed");
    }
    for (const auto value : {"", "+0", "-0", "0x0", "0,0", "0 0", "\xEF\xBC\x90"}) {
        auto headers = valid_headers();
        headers.push_back({"Content-Length", value});
        expect_error(headers, Error::content_length_invalid,
                     "Content-Length must contain ASCII decimal digits only");
    }
    for (const auto value : {"1", "01", "999999999999999999999999999999999999"}) {
        auto headers = valid_headers();
        headers.push_back({"Content-Length", value});
        expect_error(headers, Error::content_length_nonzero,
                     "all non-zero Content-Length values must be rejected without overflow");
    }
    {
        auto headers = valid_headers();
        headers.push_back({"Content-Length", "0"});
        headers.push_back({"content-length", "0"});
        expect_error(headers, Error::content_length_duplicate,
                     "even identical duplicate Content-Length values must be rejected");
    }
    for (const auto value : {"chunked", "identity", ""}) {
        auto headers = valid_headers();
        headers.push_back({"Transfer-Encoding", value});
        expect_error(headers, Error::transfer_encoding_forbidden,
                     "any Transfer-Encoding occurrence must be rejected");
    }
    {
        auto headers = valid_headers();
        headers.push_back({"Sec-WebSocket-Protocol", "chat"});
        expect_error(headers, Error::subprotocol_unsupported,
                     "unimplemented subprotocol negotiation must be explicit");
    }
    for (const auto value : {
             "permessage-deflate",
             "permessage-deflate; client_max_window_bits",
             "permessage-deflate; server_max_window_bits=15; "
             "client_max_window_bits, x-example; mode=\"safe_value\"",
         }) {
        auto headers = valid_headers();
        headers.push_back({"Sec-WebSocket-Extensions", value});
        check(evaluate(headers).accepted(),
              "a valid client extension offer must not require server negotiation");
    }
    for (const auto value : {
             "",
             "   ",
             ",permessage-deflate",
             "permessage-deflate,",
             "permessage-deflate,,x-example",
             "; client_max_window_bits",
             "permessage-deflate;",
             "permessage-deflate; =15",
             "permessage-deflate; client_max_window_bits=",
             "permessage-deflate; mode=\"\"",
             "permessage-deflate; mode=\"two words\"",
             "permessage-deflate; mode=\"semi;colon\"",
             "permessage-deflate; mode=\"comma,value\"",
             "permessage-deflate; mode=\"safe\\\"value\"",
             "permessage-deflate; mode=\"slash\\\\value\"",
             "permessage-deflate; mode=\"\x80\"",
             "permessage-deflate; mode=\"unterminated",
             "permessage-deflate; mode=\"safe\"trailing",
         }) {
        auto headers = valid_headers();
        headers.push_back({"Sec-WebSocket-Extensions", value});
        expect_error(headers, Error::extensions_unsupported,
                     "a malformed client extension offer must fail closed");
    }
    {
        auto headers = valid_headers();
        headers.push_back({"Sec-WebSocket-Extensions", "permessage-deflate"});
        headers.push_back({"sec-websocket-extensions", "x-example"});
        check(evaluate(headers).accepted(),
              "multiple valid extension fields must retain list semantics");
    }
}

void test_origin_and_cookie_selection()
{
    auto result = evaluate(valid_headers());
    check(result.origin.cardinality == Cardinality::absent && result.origin.value.empty(),
          "missing Origin must be reported without applying an allowlist");
    check(result.cookie.cardinality == Cardinality::absent && result.cookie.value.empty(),
          "missing Cookie must be reported for later authentication");

    auto headers = valid_headers();
    headers.push_back({"Origin", "https://untrusted.example"});
    headers.push_back({"Cookie", "session=opaque"});
    result = evaluate(headers);
    check(result.accepted(), "Origin allowlisting and Cookie authentication occur after 101");
    check(result.origin.cardinality == Cardinality::single
              && result.origin.value == "https://untrusted.example",
          "single Origin must be returned without policy judgment");
    check(result.cookie.cardinality == Cardinality::single
              && result.cookie.value == "session=opaque",
          "single Cookie must be returned to the authentication driver");

    headers.push_back({"oRiGiN", "https://second.example"});
    headers.push_back({"cOoKiE", "session=second"});
    result = evaluate(headers);
    check(result.accepted(), "malformed Origin/Cookie cardinality must survive the HTTP upgrade");
    check(result.origin.cardinality == Cardinality::multiple && result.origin.value.empty(),
          "duplicate Origin must be flagged for a post-101 4403 close");
    check(result.cookie.cardinality == Cardinality::multiple && result.cookie.value.empty(),
          "duplicate Cookie must be flagged for a post-101 4401 close");
}

void test_global_header_limits()
{
    {
        auto headers = valid_headers();
        while (headers.size() < service_ws::websocket_handshake_max_header_fields) {
            headers.push_back({"X", ""});
        }
        check(evaluate(headers).accepted(), "maximum header field count must be inclusive");
        headers.push_back({"X", ""});
        expect_error(headers, Error::too_many_header_fields,
                     "maximum header field count plus one must be rejected");
    }
    {
        auto headers = valid_headers();
        const auto fixed_bytes = header_bytes(headers) + std::string_view{"X-Fill"}.size();
        check(fixed_bytes < service_ws::websocket_handshake_max_header_bytes,
              "test fixture must leave room for the byte-boundary field");
        const std::string exact(
            service_ws::websocket_handshake_max_header_bytes - fixed_bytes, 'a');
        headers.push_back({"X-Fill", exact});
        check(header_bytes(headers) == service_ws::websocket_handshake_max_header_bytes,
              "fixture must reach the exact documented byte budget");
        check(evaluate(headers).accepted(), "maximum aggregate header bytes must be inclusive");
    }
    {
        auto headers = valid_headers();
        const auto fixed_bytes = header_bytes(headers) + std::string_view{"X-Fill"}.size();
        const std::string over(
            service_ws::websocket_handshake_max_header_bytes - fixed_bytes + 1, 'a');
        headers.push_back({"X-Fill", over});
        expect_error(headers, Error::header_bytes_exceeded,
                     "maximum aggregate header bytes plus one must be rejected");
    }
}

void test_stable_strings()
{
    check(service_ws::to_string(Decision::not_websocket) == "not_websocket"
              && service_ws::to_string(Decision::accept) == "accept"
              && service_ws::to_string(Decision::reject) == "reject",
          "decision strings must remain stable");
    check(service_ws::to_string(Channel::control) == "control"
              && service_ws::to_string(Channel::provider) == "provider"
              && service_ws::to_string(Channel::sync) == "sync"
              && service_ws::to_string(Channel::trigger) == "trigger"
              && service_ws::to_string(Channel::remote) == "remote",
          "channel strings must remain stable");
    check(service_ws::to_string(Cardinality::absent) == "absent"
              && service_ws::to_string(Cardinality::single) == "single"
              && service_ws::to_string(Cardinality::multiple) == "multiple",
          "cardinality strings must remain stable");

    const std::vector<std::pair<Error, std::string_view>> errors{
        {Error::none, "none"},
        {Error::unknown_websocket_path, "unknown_websocket_path"},
        {Error::remote_disabled, "remote_disabled"},
        {Error::method_not_get, "method_not_get"},
        {Error::http_version_not_1_1, "http_version_not_1_1"},
        {Error::too_many_header_fields, "too_many_header_fields"},
        {Error::header_bytes_exceeded, "header_bytes_exceeded"},
        {Error::invalid_header_name, "invalid_header_name"},
        {Error::invalid_header_value, "invalid_header_value"},
        {Error::host_missing, "host_missing"},
        {Error::host_duplicate, "host_duplicate"},
        {Error::host_empty, "host_empty"},
        {Error::host_too_large, "host_too_large"},
        {Error::host_invalid, "host_invalid"},
        {Error::non_canonical_request_target, "non_canonical_request_target"},
        {Error::upgrade_missing, "upgrade_missing"},
        {Error::upgrade_duplicate, "upgrade_duplicate"},
        {Error::upgrade_not_websocket, "upgrade_not_websocket"},
        {Error::connection_missing, "connection_missing"},
        {Error::connection_duplicate, "connection_duplicate"},
        {Error::connection_invalid, "connection_invalid"},
        {Error::connection_upgrade_missing, "connection_upgrade_missing"},
        {Error::websocket_key_missing, "websocket_key_missing"},
        {Error::websocket_key_duplicate, "websocket_key_duplicate"},
        {Error::websocket_key_invalid, "websocket_key_invalid"},
        {Error::websocket_version_missing, "websocket_version_missing"},
        {Error::websocket_version_duplicate, "websocket_version_duplicate"},
        {Error::websocket_version_unsupported, "websocket_version_unsupported"},
        {Error::content_length_duplicate, "content_length_duplicate"},
        {Error::content_length_invalid, "content_length_invalid"},
        {Error::content_length_nonzero, "content_length_nonzero"},
        {Error::transfer_encoding_forbidden, "transfer_encoding_forbidden"},
        {Error::subprotocol_unsupported, "subprotocol_unsupported"},
        {Error::extensions_unsupported, "extensions_unsupported"},
    };
    for (const auto& [error, spelling] : errors) {
        check(service_ws::to_string(error) == spelling, "error strings must remain stable");
    }
}

}  // namespace

int main()
{
    test_routes_and_request_line();
    test_header_syntax_and_required_cardinality();
    test_host_upgrade_and_connection_values();
    test_key_and_version_values();
    test_body_framing_and_unsupported_negotiation();
    test_origin_and_cookie_selection();
    test_global_header_limits();
    test_stable_strings();

    if (failures != 0) {
        std::cerr << failures << " WebSocket handshake contract test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "WebSocket handshake contract tests passed\n";
    return EXIT_SUCCESS;
}
