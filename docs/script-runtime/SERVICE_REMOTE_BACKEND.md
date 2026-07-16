# Production ws-scrcpy remote backend

`BAAS_service_remote_backend` is the host-side implementation of
`channels::RemoteBackend`. It replaces the Python `ScrcpyClient` proxy with a
bounded C++ owner while preserving the service `remote` channel contract.
Desktop `ServiceApplication` composes it through `RemoteHandlerFactory` and
requires `<project-root>/service/remote/scrcpy-server.jar` before creating any
auth/config side effects. Android leaves this host-side route disabled.

## Exact device selection

An open requires a non-empty configuration id and pulls exactly
`config/<id>/config.json` through `ResourceStore`. The JSON must be bounded,
valid UTF-8, free of duplicate keys, and contain `adbIP` plus `adbPort`. The
serial follows the Python compatibility rule: either non-empty field alone is
the serial, otherwise it is `adbIP:adbPort`. Empty/automatic values, embedded
NUL, whitespace, shell separators, invalid ports, and malformed JSON are
rejected. The backend never lists devices and never falls back to the first
connected device.

Every ADB operation carries that exact serial. The device must report state
`device`. One in-flight or live lease is allowed per serial and total sessions
are bounded. `stop()` closes registered sessions, waits for opens that already
passed admission, rejects their late registration, and only then stops the
shared ADB transport.

## Server and forwarding ownership

The locked `ws-scrcpy` 1.19-ws7 artifact is uploaded as
`/data/local/tmp/baas-ws-scrcpy-server.jar`. This name is deliberately distinct
from the classic scrcpy jar used by the runtime. Process discovery first uses a
bounded `ps` candidate list, then validates the exact NUL-separated
`/proc/<pid>/cmdline` suffix:

`com.genymobile.scrcpy.Server 1.19-ws7 web ERROR 8886 true`

An existing matching server can be reused but is never considered owned and is
never killed. Before each launch, libsodium generates a fresh 256-bit lowercase
hexadecimal owner token; token generation or validation failure stops before
the upload or launch.

Each launch creates a token-named private owner directory and starts a small
device-side supervisor. Only after that supervisor has atomically written its
token, PID, and `/proc` start time may the launcher publish the global
`baas-ws-scrcpy.lease` symlink. Symlink creation is the cross-process compare-
and-set: if another live supervisor owns it, a second backend reports capacity
and cannot reuse that backend's child by cmdline. Process discovery is followed
by a second lease probe, closing the ADB round-trip window where an earlier
`NONE` result could otherwise misclassify a newly owned child as legacy. A
matching server with no lease after that second probe remains a legacy, unowned
server and may still be reused.

The supervisor starts the child behind a private gate. It records the child PID
and start time before atomically publishing the gate; any metadata or gate write
failure kills only the still-waiting shell, so `app_process` has not executed.
The child and supervisor both carry `BAAS_WS_SCRCPY_OWNER=TOKEN`. The resulting
ownership record follows the pending open into the live session and release.

Host cleanup never validates a PID in one ADB call and kills it in another. A
single guarded device command verifies the lease target shape, path token,
supervisor PID/start time, and exact environment field, then atomically writes a
token-bound stop request. The supervisor owns the unreaped child and, immediately
before killing it, rechecks the saved child start time and environment token.
PID reuse therefore fails closed before the stop action.
Natural child exit is reaped and removes the token directory and lease. A dead
supervisor makes the lease stale: the next probe guardedly removes only that
validated owner link/directory and leaves any uncertain orphan as a legacy
server rather than risking a kill. The only remaining PID race is the very small
in-device interval between the supervisor's final `/proc` check and its `kill`;
there is no ADB round trip in that interval, and an unreaped child PID cannot be
reused. Malformed or path-traversing lease targets fail closed and are never
read as deletion targets.

Existing exact-serial `tcp:* -> tcp:8886` forwards are reused without ownership.
Otherwise the ADB server allocates a loopback port atomically with `tcp:0`.
Only a forward created by this session is removed, and removal first revalidates
the exact serial, local port, and remote endpoint. Partial opens unwind owned
forward and process state in reverse order. If the `tcp:0` response itself is
lost, the allocated local port and therefore ownership are unknowable; the
backend does not guess at or remove a possibly unrelated forward.

## WebSocket proxy and close barrier

Production connects only to `ws://127.0.0.1:<owned-or-reused-port>/` through
the pinned cpp-httplib client. Connection attempts are bounded by the startup
deadline because lease publication can precede listener readiness. cpp-httplib
assembles fragments, handles ping/pong and close frames, and applies its global
payload bound; the backend applies the tighter remote-frame limit after
assembly. Both text and binary device messages are forwarded byte-exactly in a
single reader's order.

Client-to-device messages are always binary. Concurrent sends are serialized so
WebSocket frames cannot interleave. Close admission is linearized: one caller
owns graceful-close, interrupt, reader join, active-send/callback drain, and ADB
cleanup while concurrent close callers wait for that sequence to complete.
After `close()` returns no send or callback can enter or remain active.
Callbacks may reenter `close()`: the reader initiates shutdown without waiting
for itself, transfers its join handle to an allocation-free reaper on exit, and
external callers still wait for the complete join and cleanup barrier.

## Verification

Configure `BUILD_SERVICE_REMOTE_BACKEND_TESTS=ON`. Deterministic fake ADB,
resource, clock, and WebSocket boundaries cover strict configuration, no device
fallback, PID identity and ownership, cross-backend lease exclusion, gate
failure rollback, stale recovery, natural-exit cleanup,
forward reuse/ownership, listener retry, ordered text/binary reads, oversize/error
termination, serialized sends, concurrent close, per-serial admission, failed
open unwind, and the `stop()`/in-flight-open race. CI runs the native suite on
Windows, Linux, and macOS in Debug and Release. No test invokes an adb executable
or writes to an emulator.
