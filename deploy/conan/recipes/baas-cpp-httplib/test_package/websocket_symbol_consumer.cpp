#include <httplib.h>

#include <string_view>

#if defined(BAAS_CPP_HTTPLIB_HAS_WEBSOCKET_INTERRUPT)
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

void consume_websocket_extension_symbols(
    RequestCloseMember request_close,
    InterruptMember interrupt,
    ClientRequestCloseMember client_request_close,
    ClientInterruptMember client_interrupt
) noexcept
{
    // Volatile stores are observable and keep both member-function addresses
    // materialized, turning declaration-only patches into link failures.
    static volatile RequestCloseMember retained_request_close;
    static volatile InterruptMember retained_interrupt;
    static volatile ClientRequestCloseMember retained_client_request_close;
    static volatile ClientInterruptMember retained_client_interrupt;
    retained_request_close = request_close;
    retained_interrupt = interrupt;
    retained_client_request_close = client_request_close;
    retained_client_interrupt = client_interrupt;
}
#endif
