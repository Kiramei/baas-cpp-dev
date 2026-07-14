# Transport-independent service router core

`BAAS_service_router` is a dependency-free C++20 routing foundation for the
future BAAS service backend. It accepts an in-memory `Request` and returns an
owning `Response`; it does not open a socket, parse HTTP bytes, run an event
loop, or terminate a process.

## Implemented boundary

The exact v1 routes are:

| Method | Path | Result |
| --- | --- | --- |
| `GET` | `/api/v1/health` | Stable health JSON with `api_version: 1` |
| `GET` | `/api/v1/version` | Injected service name/version plus API version |
| `POST` | `/api/v1/shutdown` | Requests an injected shutdown intent |

Paths and methods are matched exactly. The future transport adapter must pass
an absolute normalized path without query or fragment text and must preserve
the method token. Known paths with the wrong method return `405` and an `Allow`
header; unknown paths return `404`.

Every response is JSON with `Content-Type: application/json; charset=utf-8`.
Errors use one stable shape:

```json
{
  "error": {
    "code": "route_not_found",
    "message": "no route matches the request path",
    "status": 404
  },
  "ok": false
}
```

Service metadata is validated as UTF-8 and JSON escaped without a third-party
JSON dependency. Control characters are escaped while valid multi-byte text is
preserved byte-for-byte, so injected metadata cannot break the response JSON
or silently change Unicode meaning.

## Budgets

`SizeBudget` has independent limits for method, normalized path, request body,
and response body. The router rejects oversized input before route effects:

- method: `400 method_too_large`;
- path: `414 path_too_large`;
- body: `413 request_too_large`.

Generated output passes one final response-size gate. Oversized output becomes
a fixed `500 response_too_large` JSON error. The constructor requires at least
128 response bytes so this fallback is itself always within budget. Defaults
are 16 method bytes, 2,048 path bytes, and 1 MiB for each body.

These are router allocation/serialization budgets, not HTTP header, connection,
socket, BPIP, request-rate, or total-process memory budgets. A transport adapter
must enforce its own earlier limits before constructing `Request` views.

## Shutdown intent

The router never exits the process. `ShutdownIntent` is injected as a non-owning
interface and returns `accepted` or `rejected`. Missing, rejected, and accepted
intents produce `503`, `409`, and `202` respectively. Invalid methods and
oversized requests do not invoke it.

The owner must keep the intent alive longer than the router. Thread safety,
idempotence, authorization, draining, cancellation, deadlines, and the actual
shutdown sequence belong to the future service host. The core deliberately
does not claim graceful shutdown is implemented.

## Build and test

The standalone target has no Conan or application dependency:

```powershell
cmake -S . -B build/service-router `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTING=ON `
  -DBUILD_SERVICE_ROUTER_TESTS=ON `
  -DBAAS_FETCH_RESOURCES=OFF
cmake --build build/service-router --config Debug --target BAAS_service_router_tests
ctest --test-dir build/service-router --build-config Debug `
  --output-on-failure -R BAAS_service_router_tests
```

The tests cover versioned success routes, exact method/path matching, uniform
errors, JSON escaping, every size budget, and accepted/rejected/unavailable
shutdown intents.

## Explicitly not implemented

- the `cpp-httplib` adapter and any live HTTP listener;
- HTTP parsing, listen-address/origin/authentication policy, TLS, or CORS;
- bounded connection/worker concurrency, backpressure, or overload behavior;
- real process shutdown, task draining, restart, or port-conflict behavior;
- task/config/resource APIs, BPIP integration, or Tauri contract/E2E tests.

Consequently the Phase 4 `cpp-httplib` checklist item remains open. This core
is only the independently testable routing boundary that a later adapter can
call.
