# Remote business handler

`RemoteHandlerFactory` is the transport-independent production boundary between
an authenticated `remote` business session and an injected ADB/scrcpy backend.
The handler never opens sockets, starts ADB, or owns a device implementation.
`RemoteBackend::open` acquires those resources and returns one thread-safe
`RemoteSession`.

## Wire and configuration state

The first authenticated business plaintext must be one bounded JSON object.
`config_id` is required and is either a UTF-8 string or null; `decrypt` is an
optional Boolean and defaults to true. Duplicate keys, malformed UTF-8,
over-depth/over-node JSON, oversized identifiers, and a second configuration
are rejected. Unknown fields remain accepted to match the Python endpoint.

When `decrypt=false`, the handler enables the session sink's remote-only raw
outbound capability after configuration validation and before backend startup.
The switch is one-way, may occur only before the first business output, and is
unavailable to provider/sync/trigger handlers. It affects only ADB-to-WebSocket
binary payloads: all WebSocket-to-ADB input remains authenticated and decrypted
by the business secretstream driver. Pipe transports already carry raw BYTES
and do not need this WebSocket compatibility switch.

An internal empty server FINAL is not emitted as a zero-length scrcpy packet in
raw mode. Completion closes the WebSocket after every preceding raw frame is
reported written.

## Ownership and backpressure

The backend receives ordered `device_bytes` and `ended` callbacks. Each device
frame reserves configurable in-flight frame and byte budgets before output
admission. The reservation is released by the sink's exact-once write receipt,
including synchronous completion. Queue rejection, an oversized frame, budget
exhaustion, or a later failed write terminates the proxy; frames are never
silently dropped.

`RemoteSession::close` is idempotent, may interrupt a concurrent blocking
`send_to_device`, and has a strong send/callback barrier: after it returns no
send or callback can enter and every operation already entered has returned.
The handler deactivates its callback core before closing the session, contains
backend exceptions, closes partial sessions returned with failed opens, keeps a
shared session lifetime while sending outside its ownership mutex, and keeps
callbacks free of handler/session mutexes to avoid close inversion.

## Build and evidence

Configure `BUILD_SERVICE_REMOTE_HANDLER_TESTS=ON`. The native Debug and Release
tests cover strict configuration, encrypted/raw parity, byte-exact transfer in
both directions, synchronous startup callbacks, synchronous/asynchronous write
completion, queue and retained-byte backpressure, backend failures/end, channel
confusion, callback-versus-close, and blocked-send-versus-close races. The business-session suite also
proves raw mode cannot be enabled outside `remote`, cannot be repeated or
enabled after output, does not relax inbound secretstream authentication, and
does not expose the internal empty FINAL marker.

These are deterministic boundary tests. A concrete scrcpy/ADB backend,
captured scrcpy structure vectors, and live device/Tauri end-to-end tests remain
separate integration gates.
