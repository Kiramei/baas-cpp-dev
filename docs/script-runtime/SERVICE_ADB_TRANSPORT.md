# Service ADB transport

`BAAS_service_adb_transport` is the standalone, production smart-socket
foundation for a later ws-scrcpy lease owner. It does not depend on
`BAASScrcpyClient`, deploy or start a scrcpy server, or own a browser-facing
WebSocket.

## Audited compatibility boundary

The Python `service/remote/scrcpy.py` path currently performs three distinct
classes of work:

1. ADB SYNC pushes the scrcpy server jar.
2. shell commands inspect, start, and kill the remote process.
3. forward-list/forward expose remote `tcp:8886` through a host port.

This foundation implements only smart-socket operations needed to compose
that chain safely:

- `host:devices-l` and exact-serial `get-state` diagnostics;
- `host:list-forward` and exact-serial forward creation;
- exact `host:transport:<serial>` selection followed by bounded legacy
  `shell:<command>`;
- exact transport selection followed by `tcp:<port>`, returned as a move-only
  raw service stream for a future lease owner.

The jar push is intentionally excluded because ADB SYNC is a separate binary
subprotocol. There are no dedicated start/kill helpers and no embedded command
strings: a future orchestrator must supply policy-approved shell commands.
Shell-v2 parsing is also outside this minimal compatibility slice; the audited
Python chain uses legacy shell output. No method in this library deploys a jar,
starts a process, kills a process, or creates a forward implicitly.

## Wire and ownership contract

Every request is encoded as four uppercase hexadecimal length bytes followed
by the request. Status parsing accepts only `OKAY` or `FAIL`. Length-bearing
responses and `FAIL` messages require exactly four hexadecimal bytes. Invalid,
truncated, oversized, timed-out, or cancelled input fails closed.

The default endpoint is `127.0.0.1:5037`; callers may inject an endpoint and
stream factory. Endpoint hosts must already be resolved numeric IPv4 or IPv6
literals. DNS names are rejected at construction, and the native connector
uses `AI_NUMERICHOST | AI_NUMERICSERV`, so name resolution cannot escape the
connect deadline or cancellation boundary. The exported
`open_native_adb_stream` factory re-checks cancellation and the absolute
deadline before returning either an immediate or asynchronously connected
socket. Each public operation opens an independent ADB connection.
Access to each connection is serialized, while unrelated connections can make
progress concurrently. `AdbServiceStream` is move-only. `stop()` and transport
destruction strongly close all active streams. Native close first rejects new
I/O, calls `shutdown` to wake active poll/recv/send operations, waits for every
native I/O lease to drain, and only then releases the OS socket handle. This
prevents a concurrent operation from observing a recycled descriptor or
`SOCKET` value.

Default caps are 255 endpoint-host bytes, 256 serial bytes, 65,535 request
bytes, 4 MiB per response, 8 MiB total protocol I/O, 64 KiB for `FAIL` and
shell command text, a 3 second connect timeout, and a 5 second absolute I/O
deadline for each handshake/query or complete legacy shell response. All caps
are configurable through `ServiceAdbTransportLimits` and validated at
construction.

## Build and verification

Configure with `-DBUILD_SERVICE_ADB_TRANSPORT_TESTS=ON` to build
`BAAS_service_adb_transport_tests`. The deterministic suite injects fake ADB
streams and runs a numeric-loopback fake ADB server. It covers fragmented
frames, malformed and oversized lengths, `FAIL`, timeouts, cancellation, wrong
serial selection, DNS-name rejection, connection independence, stop/destructor
close, and repeated close races against blocked native reads and writes.

An optional read-only local smoke may be run as:

```text
BAAS_service_adb_transport_tests --smoke emulator-5556
```

It sends only `host:devices-l` and exact-serial `get-state`. It never pushes,
shells, forwards, starts, or kills anything.

The smoke is an operator-run local verification, not a CTest or CI gate. Its
stdout may be recorded in the change handoff for auditability; no synthetic or
checked-in CI evidence should be created from a developer workstation result.
