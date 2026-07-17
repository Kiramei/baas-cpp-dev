# Optional cpp-httplib service adapter

`BAAS_service_http` contains the cpp-httplib transport adapter and an owned
local HTTP/WebSocket host over the dependency-free `BAAS_service_router`. The
bounded WebSocket handshake and owner are specified separately in
[`SERVICE_WEBSOCKET_OWNER.md`](SERVICE_WEBSOCKET_OWNER.md). The host is optional:
the normal foundation configuration does not find or include cpp-httplib, and
only `BUILD_SERVICE_HTTP` or `BUILD_SERVICE_HTTP_TESTS` requests the Conan
`BAAS::httplib` target. The public `HttpHost.h` uses a private implementation,
so cpp-httplib is not exposed through that header either.

This is a bounded loopback host foundation, not the complete production BAAS
service. Every listener is forced to IPv4 `127.0.0.1`; there is no bind-address
option that could expose it on a LAN or wildcard interface. Its exact
Origin/CORS boundary is specified in `SERVICE_ORIGIN_POLICY.md`; that policy is
not authentication or authorization.

## Mapping contract

`HttplibAdapter::handle` maps these fields without reinterpretation:

| cpp-httplib request | Router request |
| --- | --- |
| `Request::method` | `Request::method` |
| `Request::path` | normalized route `Request::path` |
| `Request::body` | `Request::body` |

The owning Router response status, headers, and body are copied back to
cpp-httplib. cpp-httplib may add wire-level headers such as `Content-Length` or
connection metadata when it serializes the response; those are transport
framing, not Router headers.

The mapped paths are intentionally unversioned. `/health` carries the Router's
explicitly injected `statuses` and auth snapshot. The host does not treat a
bound socket as readiness: an owned `HealthReadinessOwner` can return 503 while
the listener is running, and only its `ready` state returns 200. Without
injected health state the Router returns 503. `/version` and `/shutdown` are foundation
extensions rather than frozen required v1 routes. The adapter performs no
version negotiation, and `/api/v1/*` is not accepted.

The HTTP adapter's `install()` registers one shared catch-all handler for
cpp-httplib's GET/HEAD,
POST, PUT, PATCH, DELETE, and OPTIONS dispatch. This happens after httplib has
read and bounded the request body. A pre-routing handler is intentionally not
used for ordinary HTTP because cpp-httplib invokes it before content is read.
The WebSocket owner does install a strict pre-routing upgrade validator; it
handles only exact `/ws/*` upgrade ownership and otherwise leaves HTTP routing
untouched.

The adapter and its Router are non-owning dependencies captured by installed
handlers. Both objects must outlive the `httplib::Server` and all requests.
Installing the adapter replaces the server error handler and is therefore an
exclusive server-configuration operation.

`HttpHost` closes that lifetime gap for its installed server: it owns the
Router, adapter, WebSocket owner, cpp-httplib Server, listener thread, and
shutdown intent owner.
The Router directly retains the optional health provider shared owner. Member
destruction order keeps every dependency alive until all handlers are gone. A
caller cannot pass an already assembled Router with hidden non-owning provider
pointers into the host.

## Owned loopback lifecycle

`HttpHostConfig.port` accepts zero for an operating-system-selected ephemeral
port or a fixed 16-bit port. The bind host is always `127.0.0.1` and the socket
is forced to IPv4. Platform socket options prevent two hosts from silently
sharing one fixed port.

`start()` binds synchronously, launches `listen_after_bind()` on a background
thread, and waits at most `ready_timeout` for cpp-httplib to enter its running
state. It returns the selected port without blocking on the serving loop.
Repeated start while active returns `already_active`; bind failures, bounded
ready timeout, listener-thread allocation failures, and post-bind listener
failures remain observable through the result, `state()`,
`last_start_error()`, and `last_error_message()`. The listener thread is
created before bind and waits on a private launch gate, so thread creation
failure cannot leave a bound socket or a half-started listener.

`stop()` is idempotent. It first closes WebSocket admission and requests its
producers/scheduler to stop, closes the accept socket, interrupts remaining
upgraded sockets and waits for their bounded registry drain, then joins the
cpp-httplib task workers and listener before destroying the Server. The
destructor calls the same sequence. A cleanly stopped host can be started
again, including after a fixed-port conflict has been removed. This
stop/drain/join sequence is covered by the lifecycle tests.

`stop()` is `noexcept`: cpp-httplib and thread-join failures are caught and
recorded in failed state instead of escaping. A stop requested reentrantly from
a host request worker closes the listener but defers join to the owner, avoiding
a worker joining itself through cpp-httplib's pool shutdown. The host records
that accept-stop was requested or that `listen_after_bind()` already returned,
so a deferred/repeated owner stop never calls cpp-httplib `Server::stop()` twice
for the same listening socket. If `Server::stop()` throws, the latch remains
consumed: later stop/start calls fail without retrying that possibly partially
consumed socket, the public port is cleared to zero, and destruction retains
the ownership graph. A stop failure therefore never advertises its retained
transport as usable. The latch resets only when a fresh start is safe or a
normal stop has destroyed the Server. If
an exceptional cleanup cannot prove the listener is joined, destruction
retains the complete implementation ownership graph rather than destroying
live Router/adapter/provider references or invoking a joinable `std::thread`
destructor. This rare failure containment trades a bounded process-lifetime
leak for memory safety.

This is a drain/join boundary, not complete graceful request semantics.
cpp-httplib waits for queued and active handlers; the host cannot cancel an
arbitrary blocking runtime/provider call. Network read/write timeouts do not
turn business work into a cancellable operation, and there is no global stop
deadline or response-delivery guarantee after a peer disconnects.

## Layered input limits

`InputBudget` independently limits method, path, and body before Router
dispatch. Its limits must be positive and no larger than the corresponding
Router limits. Defaults are 16 method bytes, 2,048 path bytes, and 1 MiB body.

The body limit is also installed with `Server::set_payload_max_length`, so the
transport rejects oversized declared or streamed content before route effects.
cpp-httplib's resulting 413 is converted to the same JSON-safe error shape used
by direct adapter rejections. The Router still applies its own input and output
budgets as a second boundary.

`worker_count` is required in `1..256`. `max_queued_requests` is required in
`1..65536`; zero is rejected because cpp-httplib defines it as an unbounded
queue, while values above the explicit host bound (including `SIZE_MAX`) are
also rejected. BAAS pins cpp-httplib 0.50.1. Its `ThreadPool` constructor now
takes base threads, maximum threads, and maximum queued requests separately;
the host passes `(worker_count, worker_count, max_queued_requests)` so it cannot
dynamically exceed the configured worker bound. The queue bound counts waiting
tasks; active worker tasks are additional. When enqueue rejects an accepted
connection, cpp-httplib closes its socket without fabricating an HTTP response,
and `queue_rejections()` records the event.

The OCR server separately installs 8 fixed workers and a bounded queue of 16
waiting requests through `Server::new_task_queue`. This replaces cpp-httplib
0.50.1's dynamically expanding default pool and default unbounded queue for
requests whose transport cap is about 68 MiB each. Multipart OCR requests must
have exactly one valid `Content-Length`; chunked multipart requests omit that
header and fail closed before JSON parsing or image decoding.

The Conan `BAAS::httplib` target publishes `CPPHTTPLIB_OPENSSL_SUPPORT=1` and
links the pinned static OpenSSL 3.5.7 closure on every supported platform. It also publishes both
`CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH=67108864` and
`CPPHTTPLIB_HEADER_MAX_TOTAL_LENGTH=32768` to every 0.50.1 consumer. The target
also replaces cpp-httplib's five-connection listen default with
`CPPHTTPLIB_LISTEN_BACKLOG=65536`; the operating system may clamp that hint,
but ordinary bounded worker/queue bursts are no longer rejected before they
reach the host's explicit admission queue. Because
cpp-httplib is header-only, these process-wide target definitions prevent
different service/OCR translation units from compiling incompatible inline
definitions. The repository-owned 0.50.1 patch enforces the raw 32 KiB header
limit, rejects fragments and rejected-upgrade body reuse, and supplies the
bounded WebSocket close/interrupt API. The installed five-route owner, its
strict pre-routing contract, and its 64 MiB frame/queue/capacity limits are
specified in `SERVICE_WEBSOCKET_OWNER.md`.

The raw transport header limit and WebSocket outbound aggregate queue budget
are implemented. A shared cap of every open HTTP plus WebSocket connection,
per-client rate limits, aggregate HTTP/business memory, and global load
shedding are not implemented. They remain production-service requirements.

Origin, `Access-Control-Request-Method`, and
`Access-Control-Request-Headers` have independent strict grammar, cardinality,
and byte/count gates. cpp-httplib's pre-handler payload 413 path reuses the same
Origin decision as normal dispatch: an allowed actual Origin retains 413 plus
credentialed exact-origin headers, while a denied Origin returns the stable
fail-closed 403 response. See `SERVICE_ORIGIN_POLICY.md` for the full matrix.

## Build and tests

The dependency must first be available through the repository's Conan
generators. For the configured Windows release profile:

```powershell
cmake -S . -B build\service-http-release -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=build\conan\windows-msvc-release-baas\generators\conan_toolchain.cmake `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON `
  -DBUILD_SERVICE_HTTP_TESTS=ON `
  -DBUILD_SERVICE_WEBSOCKET_TESTS=ON `
  -DBAAS_FETCH_RESOURCES=OFF
cmake --build build\service-http-release --parallel 4 `
  --target BAAS_httplib_upgrade_contract_tests `
           BAAS_service_origin_policy_tests `
           BAAS_service_httplib_adapter_tests BAAS_service_http_host_tests `
           BAAS_service_websocket_handshake_tests `
           BAAS_service_websocket_owner_tests `
           BAAS_service_websocket_wire_tests
ctest --test-dir build\service-http-release --output-on-failure `
  -R "BAAS_(httplib_upgrade_contract|service_(origin_policy|httplib_adapter|http_host|websocket_.*))_tests"
ctest --test-dir build\service-http-release --output-on-failure `
  --repeat until-fail:20 `
  -R "BAAS_service_(http_host|websocket_.*)_tests"
```

The tests have four layers:

- the package/upgrade contract checks the exact 0.50.1 header, both
  process-wide WebSocket/header macros, the separately linked close/interrupt
  extension, v0.50 multipart field/file split, zero-copy OCR file access, and
  fixed-worker/one-waiting-task queue semantics without opening a socket;
- direct cpp-httplib request/response objects verify exact field mapping,
  headers, errors, and transport-before-Router budgets;
- a real server binds only `127.0.0.1:0`, waits at most two seconds for ready,
  uses two-second client/server I/O timeouts, exercises health/method/body-limit
  responses (including cpp-httplib's wire-level `set_payload_max_length` 413
  path), then calls `stop()` and joins the listener thread. CTest has a
  15-second outer timeout;
- the owned-host suite covers repeated start/stop, ephemeral and fixed ports,
  fixed-port conflict and recovery, starting/ready/failed readiness across
  stop/restart and the current ephemeral port, concurrent health bounded by workers,
  transactional listener-thread failure, stop during an in-flight request,
  deferred reentrant stop, destructor/provider lifetime, oversized queue
  configuration, and a deterministic one-worker/one-waiting-request queue
  overflow. Its CTest outer timeout is 30 seconds and the lifecycle gate is
  repeated 20 times;
- the WebSocket suites exercise strict handshake decisions and the Owner's
  stateful-driver serialization, bounded queues, capacity, heartbeat,
  deadline, close, interrupt, shutdown, and restart contracts with an in-memory
  fake transport, plus real loopback 101/426, close-code, rejected-body,
  partial-frame, capacity/health-reserve, and idle-stop behavior.

No hostname resolution, external address, remote network, device, emulator,
OCR, Python service, or Tauri process is used by this test.

## Still incomplete

- real runtime/auth subsystem owners wired into `HealthReadinessOwner`;
- production WebSocket authentication/cookie protocol drivers, TLS, and
  authenticated LAN-exposure policy (strict WebSocket Origin enforcement is
  implemented for the loopback host);
- complete graceful in-flight cancellation/deadline/response semantics;
- bounded total HTTP connections, runtime-wide memory/rate backpressure, and
  load evidence beyond the WebSocket connection/outbound-byte budgets;
- higher-volume WebSocket load, malformed-frame fuzzing, and teardown-race
  evidence beyond the deterministic loopback gates;
- logging, metrics, task/config/resource routes, and BPIP integration;
- the Tauri probe and pipe-mode dynamic HTTP address, contract sharing, and
  end-to-end testing.

The existing injected Router shutdown intent is mapped like any other route;
it does not automatically call `HttpHost::stop()` or terminate the process.
Therefore this is not a claim of production authentication, complete graceful
shutdown, real runtime/auth integration, high-load capacity, or Tauri
integration.
