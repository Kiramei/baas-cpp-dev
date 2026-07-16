#include <httplib.h>

#include <cstdlib>
#include <concepts>
#include <string_view>

#if !defined(CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH)
#error "BAAS::httplib did not propagate its required configuration"
#endif

#if !defined(CPPHTTPLIB_OPENSSL_SUPPORT)
#error "BAAS::httplib did not propagate its required OpenSSL configuration"
#endif

#if defined(BAAS_CPP_HTTPLIB_HAS_WEBSOCKET_INTERRUPT)
static_assert(requires(httplib::ws::WebSocket& socket) {
    { socket.request_close() } noexcept -> std::same_as<bool>;
    { socket.interrupt() } noexcept;
});
static_assert(requires(httplib::ws::WebSocketClient& client) {
    { client.request_close() } noexcept -> std::same_as<bool>;
    { client.interrupt() } noexcept;
});
static_assert(CPPHTTPLIB_HEADER_MAX_TOTAL_LENGTH == 32'768);
static_assert(
    CPPHTTPLIB_WEBSOCKET_INTERRUPT_POLL_INTERVAL_MICROSECONDS == 100'000
);

using RequestCloseMember = bool (httplib::ws::WebSocket::*)(
    httplib::ws::CloseStatus,
    std::string_view
) noexcept;
using InterruptMember = void (httplib::ws::WebSocket::*)() noexcept;
using ClientRequestCloseMember = bool (httplib::ws::WebSocketClient::*)(
    httplib::ws::CloseStatus,
    std::string_view
) noexcept;
using ClientInterruptMember = void (httplib::ws::WebSocketClient::*)() noexcept;

// Kept in a separate translation unit so optimized builds must resolve both
// inline extension definitions from the packaged header.
void consume_websocket_extension_symbols(
    RequestCloseMember request_close,
    InterruptMember interrupt,
    ClientRequestCloseMember client_request_close,
    ClientInterruptMember client_interrupt
) noexcept;
#endif

int main()
{
    if (CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH != 67'108'864) return EXIT_FAILURE;

    httplib::Request request;
    request.method = "GET";
    request.path = "/";
    if (request.method != "GET" || request.path != "/") return EXIT_FAILURE;
    httplib::SSLClient tls_client("localhost", 443);
    tls_client.enable_server_certificate_verification(true);
    static_cast<void>(tls_client.is_valid());
#if defined(BAAS_CPP_HTTPLIB_HAS_WEBSOCKET_INTERRUPT)
    consume_websocket_extension_symbols(
        &httplib::ws::WebSocket::request_close,
        &httplib::ws::WebSocket::interrupt,
        &httplib::ws::WebSocketClient::request_close,
        &httplib::ws::WebSocketClient::interrupt
    );
#endif
    return EXIT_SUCCESS;
}
