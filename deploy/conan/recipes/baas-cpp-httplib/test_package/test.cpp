#include <httplib.h>

#include <cstdlib>

#if !defined(CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH)
#error "BAAS::httplib did not propagate its required configuration"
#endif

int main()
{
    if (CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH != 67'108'864) return EXIT_FAILURE;

    httplib::Request request;
    request.method = "GET";
    request.path = "/";
    if (request.method != "GET" || request.path != "/") return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
