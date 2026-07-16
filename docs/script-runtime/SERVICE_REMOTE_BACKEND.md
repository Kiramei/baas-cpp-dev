# Production ws-scrcpy remote backend

`BAAS_service_remote_backend` is the host-side implementation of
`channels::RemoteBackend`. It replaces the Python `ScrcpyClient` proxy with a
bounded C++ owner while preserving the service `remote` channel contract. It is
not composed into `ServiceApplication` in this unit; application and Tauri
wiring remain a separate integration change.

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
never killed. A newly launched PID is taken from the same shell command that
writes the private marker, then revalidated before use and again before kill.
Stale/reused PIDs fail closed. Startup failures reread the marker and only kill
a candidate whose PID relationship and exact cmdline can establish ownership.

Existing exact-serial `tcp:* -> tcp:8886` forwards are reused without ownership.
Otherwise the ADB server allocates a loopback port atomically with `tcp:0`.
Only a forward created by this session is removed, and removal first revalidates
the exact serial, local port, and remote endpoint. Partial opens unwind owned
forward and process state in reverse order.

## WebSocket proxy and close barrier

Production connects only to `ws://127.0.0.1:<owned-or-reused-port>/` through
the pinned cpp-httplib client. Connection attempts are bounded by the startup
deadline because the PID marker can precede listener readiness. cpp-httplib
assembles fragments, handles ping/pong and close frames, and applies its global
payload bound; the backend applies the tighter remote-frame limit after
assembly. Both text and binary device messages are forwarded byte-exactly in a
single reader's order.

Client-to-device messages are always binary. Concurrent sends are serialized so
WebSocket frames cannot interleave. Close admission is linearized: one caller
owns graceful-close, interrupt, reader join, active-send/callback drain, and ADB
cleanup while concurrent close callers wait for that sequence to complete.
After `close()` returns no send or callback can enter or remain active.

## Verification

Configure `BUILD_SERVICE_REMOTE_BACKEND_TESTS=ON`. Deterministic fake ADB,
resource, clock, and WebSocket boundaries cover strict configuration, no device
fallback, PID identity and ownership, marker failure cleanup, forward
reuse/ownership, listener retry, ordered text/binary reads, oversize/error
termination, serialized sends, concurrent close, per-serial admission, failed
open unwind, and the `stop()`/in-flight-open race. CI runs the native suite on
Windows, Linux, and macOS in Debug and Release. No test invokes an adb executable
or writes to an emulator.
