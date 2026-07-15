#include <httplib.h>

#include <string_view>

#if defined(BAAS_CPP_HTTPLIB_HAS_WEBSOCKET_INTERRUPT)
using RequestCloseMember = bool (httplib::ws::WebSocket::*)(
    httplib::ws::CloseStatus,
    std::string_view
) noexcept;
using InterruptMember = void (httplib::ws::WebSocket::*)() noexcept;

void consume_websocket_extension_symbols(
    RequestCloseMember request_close,
    InterruptMember interrupt
) noexcept
{
    // Volatile stores are observable and keep both member-function addresses
    // materialized, turning declaration-only patches into link failures.
    static volatile RequestCloseMember retained_request_close;
    static volatile InterruptMember retained_interrupt;
    retained_request_close = request_close;
    retained_interrupt = interrupt;
}
#endif
