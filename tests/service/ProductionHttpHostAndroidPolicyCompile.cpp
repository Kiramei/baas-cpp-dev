#include "service/http/ProductionHttpHost.h"

using baas::service::http::production_remote_handler_required;
using baas::service::websocket::RemoteChannelPolicy;

static_assert(!production_remote_handler_required(RemoteChannelPolicy::desktop_only));
static_assert(!production_remote_handler_required(RemoteChannelPolicy::disabled));
