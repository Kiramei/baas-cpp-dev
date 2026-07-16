# Bounded native Pipe host foundation

`BAAS_service_pipe_host` is the cross-platform local IPC boundary for BPIP v1.
It owns bounded accept/read/write lifecycle and open negotiation without using
cpp-httplib, WebSocket state, BAAS global objects, or runtime command handlers.
The target compiles a Windows named-pipe backend and a Unix-domain socket
backend; deterministic tests inject fake listeners and streams and never open a
real OS endpoint.

## Security boundary

Windows instances use `CreateNamedPipeW` with byte mode,
`PIPE_REJECT_REMOTE_CLIENTS`, `FILE_FLAG_OVERLAPPED`, an initial
`FILE_FLAG_FIRST_PIPE_INSTANCE` ownership check, and
`PIPE_UNLIMITED_INSTANCES`. The latter leaves one pending OS accept slot while
the host's pending-plus-active cap remains authoritative, so reaching the cap
rejects a connection without terminating the listener. A protected explicit
DACL grants read/write access only to the current process token user. Accept,
read, and write use cancellable overlapped operations; forced stop cancels I/O
and never calls blocking `FlushFileBuffers`.

Unix endpoints must be absolute children of an already existing, canonical,
current-user-owned directory with no group/other permission bits. The host does
not remove a pre-existing path. After a successful bind it records the socket
device/inode, sets mode `0600`, verifies owner/type/mode, and removes the path
only if it is still the same owned socket. Linux and supported BSD/macOS peers
must expose credentials matching the effective user; platforms without a
known credential API fail closed.
Listener and accepted descriptors are close-on-exec. Accepted streams are
nonblocking; stalled writes loop through `poll` and `EAGAIN` only until one
absolute write deadline. Stop signals a close-on-exec wakeup pipe, then the
accept owner closes descriptors and performs same-device/inode path cleanup
before the accept thread joins. macOS additionally fails a stream closed if
`SO_NOSIGPIPE` cannot be installed.

Endpoint construction only establishes this OS boundary. A hostile-process
audit, packaging-specific directory selection, and real cross-process tests are
still required before claiming production security readiness.

## BPIP and open state

Before authentication, a dedicated bounded pre-open reader inspects the first
ten-byte BPIP header and rejects any declared JSON payload above 4 KiB before
the generic decoder can allocate it. The first complete frame must be JSON
containing an object with exact `type:"open"`, one of
`provider|sync|trigger|remote`, and a non-empty bounded `name`. The parser
validates scalar UTF-8, JSON escapes/surrogates, nested extra values, duplicate
keys, depth, node count, and a 4 KiB default input limit. `control` remains
unsupported. A selected factory must exist before compact `open_ok` is written.
After open, each declared payload reserves its full size against one host-wide
ingress retained-byte budget before payload buffering begins.

Malformed framing or open input, unknown semantic frame kinds, handler
exceptions, and non-empty CLOSE payloads produce one atomic ERROR+CLOSE write
when the stream remains writable. CLOSE and peer ERROR are terminal. Complete
frames coalesced after open are delivered only after `open_ok` succeeds.
Any timeout, error, or incomplete `write_all` permanently poisons that
connection writer: the host closes directly and never appends ERROR or CLOSE
after bytes may already be visible.

## Bounded lifecycle and writes

The host uses one accept thread and a fixed worker pool equal to
`max_connections` (default 16, hard maximum 64). Pending plus active streams
cannot exceed that cap. Defaults include a 64 KiB read chunk, 5 second absolute
first-frame receive deadline that fragment progress cannot reset, 60 second
idle deadline, 10 second write deadline, a 72 MiB atomic write limit with a
128 MiB hard configuration ceiling, and 128 MiB host-wide ingress and egress
retained-byte budgets (256 MiB hard ceilings). The receive deadline ends once
the first frame is complete; bounded/cancellable factory work and the `open_ok`
write have their own cooperative-stop/write deadlines.

`PipeConnectionWriter::write_batch()` encodes all frames into one owning
buffer and invokes one logical `write_all`, preserving JSON+BYTES adjacency and
present zero-length BYTES frames. Platform streams finish ordinary partial OS
writes internally. Timeout, error, or incomplete logical `write_all` is fatal;
the batch is never retried after bytes may have become visible. Single-frame
writes reserve their wire size before allocating the owning output buffer and
do not first copy payload into a temporary `Frame`. Once poisoned, every later
writer call fails before reaching the platform stream, including when the
original `write_all` threw after I/O began. The writer marks itself poisoned
under its mutex before entering virtual I/O and clears that mark only after an
exact successful return, closing the exception-unwind concurrency window.

`stop()` closes the listener, cancels blocked reads, drops pending streams, and
wakes workers. Calling `stop()` (directly or from a handler) is a precondition
for an external `join()`; otherwise `join()` waits until another thread requests
stop. External `join()` waits for accept and all workers. A handler may call
`stop()+join()` from its own worker, but that self-join returns immediately. The
external owner MUST keep `PipeHost` alive and later call `join()` from a
non-worker thread before destruction. Destroying the host from its own factory
or handler callback is unsupported and terminates rather than allowing the
worker's captured `this` to become dangling. Thread entry points contain
allocation and handler exceptions and finish stream/accounting cleanup.
`PipeHost` is one-shot: every first `start()` attempt consumes the listener,
including partial thread-start failure, and later `start()` calls return false.
Workers never dequeue pending streams after state leaves `running`.

## Injection and remaining integration

`PipeChannelFactory` selects a per-connection `PipeChannelHandler` after open.
Both factory creation and handler callbacks receive the host `stop_token` and
MUST observe it around any potentially blocking production work. The host can
request cooperative cancellation but cannot forcibly unwind arbitrary user
callback code that ignores this contract.
After the factory returns a handler, the host first completes the `open_ok`
write and then invokes `PipeChannelHandler::on_open()` exactly once. Any
server-initiated initial state is emitted from that callback, so it cannot race
ahead of the negotiated response; coalesced client business frames are not
delivered until the callback succeeds. A failed, throwing, closing, or
write-poisoning callback follows the same terminal rules as `on_frame()`.
Handlers receive only validated business JSON/BYTES frames and a bounded
writer. The foundation target intentionally provides no built-in business
handler. `BAAS_service_trigger_pipe_channel` supplies the separately selectable
production Trigger adapter, while `BAAS_service_business_pipe_channel` adapts
the transport-independent provider, sync, and remote handlers and delegates
Trigger to that implementation. None of these targets starts a listener in
tests. Application ownership, a concrete remote backend, observability, and
live OS security/load tests remain pending.

`BAAS_service_pipe_host_tests` covers all four channels, bounded hostile open
JSON, fragmented/coalesced BPIP, open ordering, atomic JSON+zero-byte-BYTES,
ERROR+CLOSE, partial-write poisoning/no retry, fixed connection limits,
declared-oversized pre-open rejection, host-wide ingress/egress budgets,
blocked accept/read and cooperative callback cancellation, absolute drip-feed
receive timeout, handler exceptions, one-shot restart rejection, and both
self-first/external-first join orders using fakes only.
