# Service ADB SYNC

`BAAS_service_adb_sync` is the bounded production upload foundation for the
scrcpy server jar. It composes `ServiceAdbTransport` and its move-only native
socket ownership; it does not open, duplicate, or retain an OS socket itself.

## Audited Python compatibility slice

The current Python paths in `service/remote/scrcpy.py` and
`core/device/scrcpy/core.py` call `device.sync.push(...)` to deploy
`scrcpy-server.jar` to `/data/local/tmp/scrcpy-server.jar`. Process discovery,
shell start/kill, forwarding, and WebSocket proxying occur later and remain
outside this library. `ServiceAdbSync` therefore exposes only `stat`, bounded
in-memory `push`, and bounded streaming `push_file`. It has no shell, forward,
start, kill, remove, or application-lifecycle side effects.

Every operation requires an exact ADB serial. `open_sync` first sends
`host:transport:<serial>` and accepts its `OKAY`, then opens only `sync:` on
that selected connection. No first-device fallback exists.

## Wire contract

ADB smart-socket framing ends after `sync:` is accepted. The SYNC v1 binary
protocol then uses four-byte ASCII command IDs and unsigned little-endian
32-bit lengths or fields:

- `STAT` + path length + path; response `STAT` + mode + size + mtime;
- `SEND` + `path,mode`, then zero or more `DATA` + length + bytes;
- `DONE` + mtime, then `OKAY` + ignored 32-bit value, or `FAIL` + message length + message.

All fields are read exactly across arbitrary fragmentation. Unknown IDs,
truncated fields, oversized `FAIL`, and ADB `FAIL` responses fail closed. The
four bytes following `OKAY` are always consumed for alignment and ignored as
required by AOSP SYNC.TXT. A zero mode from legacy `STAT` is preserved as `exists() ==
false`. The implementation does not negotiate the newer `STA2`, `LST2`, or
`SND2` feature extensions.

Remote paths must be absolute, valid UTF-8 file paths without empty, dot, or
dot-dot components, controls, backslashes, commas, or a trailing slash. The
comma restriction makes `SEND`'s `path,mode` composition unambiguous. Upload
permission bits are limited to `0000..0777`; the regular-file type bit is
added on the wire. The composed `path,mode` must also fit the 1 KiB protocol
path cap and is rejected before opening ADB. A zero requested mtime means the bounded current Unix wall
time, matching the Python push behavior.

## Bounds, cancellation, and ownership

Defaults cap paths at 1 KiB, file content at 128 MiB, `DATA` chunks at the
protocol maximum of 64 KiB, and `FAIL` text at 64 KiB. The complete SYNC phase
uses one 30 second steady-clock deadline; configuration rejects non-positive
or greater-than-one-hour deadlines. The smart-socket connection and selection
remain bounded by `ServiceAdbTransport`'s connect and I/O deadlines.

`std::stop_token` is checked before connection and throughout reads and
writes. Every result path destroys its move-only `AdbServiceStream`; transport
`stop()` still strongly closes it. `ServiceAdbSync` is a non-copyable,
non-movable borrowed facade: its referenced `ServiceAdbTransport` must outlive
it and callers must serialize destruction against operations.

`push_file` checks cancellation before any local filesystem operation. It
opens once, validates regular-file type and size on that same anchored native
handle, and reads only from that handle, so path replacement cannot switch the
uploaded object after validation. Windows denies write/delete sharing, rejects
final reparse points, UNC/device paths, and mapped or unresolved network drives
before ADB is opened. POSIX uses `O_NOFOLLOW | O_NONBLOCK` and validates with
`fstat` on the opened descriptor. POSIX has no portable API that identifies
all network mounts, so callers requiring a hard deadline for an untrusted or
remote source must supply already-bounded memory to `push`; `push_file` is for
trusted local filesystems. Local files fail with `local_io_error` if opening,
validation, or reading fails. Live-device verification is deliberately
read-only: no CI or developer smoke test pushes to `emulator-5556`.

## Build and verification

Configure with `-DBUILD_SERVICE_ADB_SYNC_TESTS=ON` and build
`BAAS_service_adb_sync_tests`. The deterministic fake smart-socket/SYNC suite
covers exact serial selection, single-byte fragmentation, STAT success/FAIL,
SEND/DATA/DONE byte vectors, ignored OKAY values, FAIL handling,
malformed/truncated frames, caps, invalid paths and modes, pre-cancellation,
timeout, anchored regular-file checks, Windows network-path denial, and bounded
local-file streaming.

The foundation CI builds and runs this target on Windows, Linux, and macOS in
both Debug and Release. It never requires an ADB daemon or device.
