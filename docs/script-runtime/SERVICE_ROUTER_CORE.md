# Transport-independent service router core

`BAAS_service_router` is a dependency-free C++20 routing foundation for the
future BAAS service backend. It accepts an in-memory `Request` and returns an
owning `Response`; it does not open a socket, parse HTTP bytes, run an event
loop, or terminate a process.

## Implemented boundary

The implemented foundation routes are:

| Method | Path | Result |
| --- | --- | --- |
| `GET` | `/health` | Frozen v1 status/auth payload from explicitly injected state |
| `GET` | `/version` | Foundation extension exposing injected metadata |
| `POST` | `/shutdown` | Foundation extension requesting an injected shutdown intent |

HTTP v1 paths are intentionally unversioned. `/api/v1/health` and other
invented version-prefixed aliases return `404`; protocol version negotiation
belongs to the authenticated channel handshake, not the HTTP path.

Only the `/health` path matches the frozen required v1 surface. Its successful
body now follows the observed Python field shape and ordering:

```json
{"ok":true,"statuses":{},"auth":{"initialized":false,"pwd_epoch":0,"server_sign_public_key":"..."}}
```

`statuses` is an object of JSON-safe nested values. The auth snapshot carries
the observed `auth.initialized`, `auth.pwd_epoch`, and
`auth.server_sign_public_key` fields. `/version` and `/shutdown` are foundation
extensions, not observed Python/Tauri v1 endpoints.

The router does not derive readiness from a listening socket, an empty status
object, or auth initialization. A caller may use `with_health_snapshot()` for
the static ready compatibility path or `with_health_provider()` with a non-null
shared provider. The Router retains that shared owner. With neither, `/health`
returns `503 health_unavailable`; provider exceptions return
`503 health_provider_failed`.

The production foundation provider is `HealthReadinessOwner`, documented in
[`SERVICE_HEALTH_READINESS.md`](SERVICE_HEALTH_READINESS.md). It publishes one
thread-safe state plus runtime/auth projection. `starting` and `failed` return
stable `503 health_starting` and `503 health_failed` errors; only `ready` can
produce the existing 200 body. The provider is called synchronously exactly
once per valid request. The Router copies, validates, canonicalizes, and bounds
the returned ready snapshot without exposing provider-owned state.

The current Tauri Android startup probe requests the unversioned `/health` and
only checks for a 2xx status. It does not parse these fields. Fixing the Tauri
probe and pipe-mode dynamic HTTP address, then wiring the real runtime/auth
owners, remain coordinated follow-up work before claiming integration parity.

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

Service metadata and health keys/string values are validated as UTF-8 and JSON
escaped without a third-party JSON dependency. Health objects reject duplicate
keys and non-finite numbers, limit nesting, and sort object keys recursively.
The top-level health/auth field order is fixed. Control characters are escaped
while valid multi-byte text is preserved byte-for-byte, so injected data cannot
break the response JSON or silently change Unicode meaning. Invalid static
snapshots fail construction; invalid dynamic snapshots become a stable
`500 invalid_health_snapshot` response.

## Budgets

`SizeBudget` has independent limits for method, normalized path, request body,
and response body. The router rejects oversized input before route effects:

- method: `400 method_too_large`;
- path: `414 path_too_large`;
- body: `413 request_too_large`.

Generated output passes one final response-size gate. Health serialization also
writes through the same limit and stops as soon as the body cannot fit, rather
than first constructing an unbounded encoded string. Oversized output becomes a
fixed `500 response_too_large` JSON error. The constructor requires at least
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
cmake --build build/service-router --config Debug `
  --target BAAS_service_router_tests BAAS_service_health_foundation_tests
ctest --test-dir build/service-router --build-config Debug `
  --output-on-failure -R "BAAS_service_(router|health_foundation)_tests"
```

The tests cover static and changing provider snapshots, starting/ready/failed
responses, exact frozen health fields, provider exceptions, missing state,
invalid UTF-8, duplicate keys, non-finite numbers, bounded oversized output,
concurrent stable runtime/auth publication, explicit restart transitions, the
unversioned path, extension routes, exact method/path matching, uniform errors,
JSON escaping, every size budget, and shutdown intents.

## Explicitly not implemented by the core

- any HTTP dependency or listener; the separately optional
  [`SERVICE_HTTPLIB_ADAPTER.md`](SERVICE_HTTPLIB_ADAPTER.md) maps cpp-httplib
  request/response objects and has a loopback-only lifecycle test;
- HTTP parsing, listen-address/origin/authentication policy, TLS, or CORS;
- bounded connection/worker concurrency, backpressure, or overload behavior;
- real process shutdown, task draining, restart, or port-conflict behavior;
- HTTP task/config/resource APIs or Tauri contract/E2E tests. The separate
  `SERVICE_TRIGGER_SESSION.md` foundation follows the observed trigger channel
  rather than adding a REST task route; it is not wired into this Router.
- real runtime/auth subsystem owners publishing into the readiness foundation,
  the remaining required unversioned HTTP routes, and shared Python/Tauri
  parity evidence.

Consequently the broad Phase 4 `cpp-httplib` checklist item remains partial.
This core remains the independently testable routing boundary; the optional
adapter does not complete production hosting, policy, concurrency, shutdown,
or integration.
