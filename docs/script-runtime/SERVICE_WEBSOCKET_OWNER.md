# Bounded WebSocket handshake and owner

`BAAS_service_websocket` is the bounded cpp-httplib WebSocket transport
foundation used by `HttpHost`. It owns transport admission and lifecycle, but
delegates the channel protocol to an injected stateful `SessionFactory`.
`ProductionSessionFactory` now composes the control driver with encrypted
business-session drivers. Provider, sync, and trigger adapters are available as
injected transport-independent handler factories; remote and the final
composition root remain pending.

The owner and ordinary HTTP adapter are installed on the same
`httplib::Server`, so they share one IPv4 loopback listener and one bounded
worker pool. See [`SERVICE_HTTPLIB_ADAPTER.md`](SERVICE_HTTPLIB_ADAPTER.md) for
the host and HTTP contract and
[`SERVICE_ORIGIN_POLICY.md`](SERVICE_ORIGIN_POLICY.md) for the exact Origin
allowlist grammar.

## Routes and strict pre-routing

The owner installs these exact routes:

| Route | Channel | Platform |
| --- | --- | --- |
| `/ws/control` | control | all |
| `/ws/provider` | provider | all |
| `/ws/sync` | sync | all |
| `/ws/trigger` | trigger | all |
| `/ws/remote` | remote | desktop only; rejected on Android |

A cpp-httplib pre-routing handler validates an upgrade before its WebSocket
handler runs. Paths outside `/ws/` remain available to the ordinary HTTP
router. Unknown `/ws/*` paths fail closed. An accepted request must use GET and
HTTP/1.1, have the exact canonical request target (no query, fragment, or
percent-encoded alias), and satisfy the following header contract:

- at most 64 decoded header fields and 32 KiB total decoded name/value bytes;
- exactly one syntactically valid `Host`, no surrounding whitespace, with at
  most 255 bytes;
- exactly one `Upgrade: websocket` and one valid `Connection` field containing
  the `upgrade` token;
- exactly one canonical 16-byte base64 `Sec-WebSocket-Key` and one
  `Sec-WebSocket-Version: 13`;
- no `Transfer-Encoding`, `Sec-WebSocket-Protocol`, or
  `Sec-WebSocket-Extensions`;
- absent or unique, valid, zero `Content-Length`.

Malformed upgrades return a stable JSON 400 response. An unsupported
WebSocket version returns 426 and advertises `Sec-WebSocket-Version: 13`.
Origin and Cookie cardinality are retained for the post-upgrade owner checks;
they are not silently coalesced.

## Pinned transport patch

BAAS pins cpp-httplib 0.50.1 and applies the repository-owned
`deploy/conan/recipes/baas-cpp-httplib/patches/websocket-interrupt.patch` only
to that version. The Conan target publishes both process-wide header-only
definitions:

- `CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH=67108864`;
- `CPPHTTPLIB_HEADER_MAX_TOTAL_LENGTH=32768`;
- `CPPHTTPLIB_WEBSOCKET_INTERRUPT_POLL_INTERVAL_MICROSECONDS=100000`.

The raw 32 KiB parser limit counts original header-line bytes, including CRLF,
before decoded handshake validation. The patch also rejects a request-target
fragment, closes a rejected pre-upgrade request instead of interpreting an
unconsumed body as another keep-alive request, and adds `request_close()` plus
`interrupt()` to cpp-httplib's WebSocket. `request_close()` sends a close frame
without becoming a second reader. `interrupt()` shuts down the socket so an
owner stop can release the handler's blocked read or write. Because Windows
does not reliably wake an already-blocked Winsock `select` after `shutdown`,
WebSocket reads use a 100 ms interrupt poll. The patched exact-read loop retains
partial header, mask, and payload bytes across polls, so slow TCP delivery is
not mistaken for a failed WebSocket frame.

Because cpp-httplib is header-only, consumers must link `BAAS::httplib` rather
than include an independently configured header. The package test compiles the
extension consumer in a separate translation unit so optimized builds also
prove that the added member functions link.

## Session ownership and close policy

The handler is the only socket reader. Each admitted session has one bounded
writer queue, while one shared scheduler drives handshake deadlines and
heartbeats. Calls into one `SessionDriver` are serialized, and a driver cannot
transition from streaming back to handshaking. Outbound batches preserve
consecutive JSON-plus-binary writes and contain at most two frames. An enqueue
may carry a shared `BatchCompletion` observer. A non-null observer receives
exactly one `BatchWriteResult`: `written` only after every frame in the batch
has been written consecutively, or `failed` once the entire batch can no longer
be written. A partial JSON-plus-binary write is therefore `failed`, never
`written`.

Synchronous enqueue rejection completes the observer with `failed` before
`enqueue()` returns. Accepted batches are completed later by the writer or
teardown path; acceptance alone is not a transport acknowledgement. Normal
terminal handling still drains batches accepted before the terminal action,
while shutdown, transport interruption, write failure, and any discarded queue
complete their unsendable batches with `failed`. Active bytes remain charged
until the write attempt ends, and every queued or active charge is released
before its callback. Callbacks can run on the enqueue, writer, or teardown
thread and are invoked without the owner queue, registry, transport, or writer
mutex held. Observer implementations must be non-blocking and `noexcept` and
must tolerate callback re-entry into the outbound sink.

Origin is evaluated immediately after the HTTP 101 upgrade because
cpp-httplib's WebSocket callback owns the upgraded socket. A denied or
duplicate Origin closes with 4403 before a session driver is created. A
malformed/oversized Cookie or driver authentication/protocol rejection closes
with 4401; capacity exhaustion closes with 1013; missing driver state and
internal failures close with 1011; normal driver completion closes with 1000.
The close frame is followed by the configured close grace period and a socket
interrupt, so the implementation never waits indefinitely for a peer close
acknowledgement.

The default limits are:

| Resource | Default |
| --- | ---: |
| Concurrent WebSocket sessions | 16 |
| HTTP workers reserved outside that cap | 2 |
| Maximum frame | 64 MiB |
| Frames per outbound batch | 2 |
| Queued batches per session | 256 |
| Queued payload bytes per session | 72 MiB |
| Queued payload bytes across sessions | 256 MiB |
| Retained Cookie | 4 KiB |
| Handshake deadline | 10 s |
| Heartbeat interval | 3 s |
| Close grace | 250 ms |
| Owner shutdown deadline | 2 s |

The connection cap is validated in `5..256` on desktop and `4..256` on
Android, matching the installed steady-state routes. The HTTP worker reserve
is at least two. When WebSocket support is enabled, `HttpHost` requires
`worker_count >= max_connections + http_worker_reserve`; its default is
therefore 18. This preserves workers for `/health` and ordinary HTTP even when
all 16 WebSocket slots are occupied. Queue admission is transactional: a
rejected or allocation-failed enqueue releases any global byte reservation.

`WebSocketOwnerStats` exposes active/peak/accepted sessions, rejection and
failure counters, handshake timeouts, shutdown interrupts/timeouts, heartbeat
ticks, global queued bytes, the configured cap/reserve, and whether admission
is open.

## Host shutdown and restart

`HttpHost::stop()` first closes WebSocket admission and requests producer and
scheduler stop, then calls `httplib::Server::stop()`. It next interrupts every
remaining upgraded socket and waits at most the owner's `shutdown_timeout` for
the session registry to drain before joining the listener. This ordering lets
blocked handlers leave the same worker pool that the HTTP host owns.

Successful shutdown leaves no registered session or scheduler and permits
`prepare_start()` to reopen admission on a host restart. A shutdown deadline or
scheduler/join failure is observable as host failure; an abandoned scheduler
cannot be restarted. The implementation retains its shared ownership graph in
that exceptional case rather than destroying objects still reachable by a
detached transport thread.

The transport contract requires `interrupt()` to make concurrent read, write,
and close operations return promptly. The owner can bound its own registry
wait, but it cannot make a broken third-party transport implementation obey
that contract or cancel arbitrary work inside a production session driver.

## Build and verification

Configure `BUILD_SERVICE_WEBSOCKET_TESTS=ON`; this also enables the WebSocket
library and its Origin/httplib dependencies. The current deterministic targets
are:

- `BAAS_service_websocket_handshake_tests` for exact route, target, authority,
  cardinality, token, key, version, and body-framing validation;
- `BAAS_service_websocket_owner_tests` for driver serialization, frame/batch
  limits, backpressure, exact-once batch completion, partial-write failure,
  callback re-entry, close codes, capacity, deadlines, heartbeats, interrupt,
  shutdown, and restart behavior through a fake transport;
- `BAAS_service_websocket_wire_tests` for real 101/426 responses, strict target
  rejection, rejected-body connection closure, post-upgrade close codes, slow
  partial masked frames, full-capacity `/health` reserve, and bounded idle stop
  over an in-process IPv4 loopback server.

The `httplib-http.yml` workflow builds and runs all three targets together with the
HTTP compatibility suite on Windows, Linux, and macOS in Debug and Release. It
also repeats the HTTP and WebSocket lifecycle tests five times. These tests do
not launch an external service, device, emulator, OCR process, Python service,
or Tauri process.

## Still incomplete

- production host wiring, concrete remote handling, and real trigger runtime
  command registrations above the authenticated session-driver boundary;
- higher-volume real-wire load, malformed-frame fuzzing, and teardown-race
  evidence beyond the deterministic transport and lifecycle gates;
- TLS, authenticated LAN exposure, per-principal rate limits, and policy above
  the existing loopback-only listener;
- runtime-wide memory/load shedding beyond outbound WebSocket queue bytes, and
  measured production load evidence;
- Tauri protocol sharing and end-to-end host/client/device smoke tests.

This foundation and its session drivers are not a claim that production host
wiring, concrete business handlers, or the authentication migration is
end-to-end complete.
