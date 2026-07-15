# Bounded native Pipe host foundation

`BAAS_service_pipe_host` is the cross-platform local IPC boundary for BPIP v1.
It owns bounded accept/read/write lifecycle and open negotiation without using
cpp-httplib, WebSocket state, BAAS global objects, or runtime command handlers.
The target compiles a Windows named-pipe backend and a Unix-domain socket
backend; deterministic tests inject fake listeners and streams and never open a
real OS endpoint.

## Security boundary

Windows instances use `CreateNamedPipeW` with byte mode,
`PIPE_REJECT_REMOTE_CLIENTS`, and `FILE_FLAG_OVERLAPPED`. A protected explicit
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

Endpoint construction only establishes this OS boundary. A hostile-process
audit, packaging-specific directory selection, and real cross-process tests are
still required before claiming production security readiness.

## BPIP and open state

Each connection has one incremental `bpip::Decoder`. The first complete frame
must be JSON containing an object with exact `type:"open"`, one of
`provider|sync|trigger|remote`, and a non-empty bounded `name`. The parser
validates scalar UTF-8, JSON escapes/surrogates, nested extra values, duplicate
keys, depth, node count, and a 4 KiB default input limit. `control` remains
unsupported. A selected factory must exist before compact `open_ok` is written.

Malformed framing or open input, unknown semantic frame kinds, handler
exceptions, and non-empty CLOSE payloads produce one atomic ERROR+CLOSE write
when the stream remains writable. CLOSE and peer ERROR are terminal. Complete
frames coalesced after open are delivered only after `open_ok` succeeds.

## Bounded lifecycle and writes

The host uses one accept thread and a fixed worker pool equal to
`max_connections` (default 16, hard maximum 64). Pending plus active streams
cannot exceed that cap. Defaults include a 64 KiB read chunk, 5 second absolute
open deadline that fragment progress cannot reset, 60 second idle deadline,
10 second write deadline, and a 72 MiB
atomic write limit with a 128 MiB hard configuration ceiling.

`PipeConnectionWriter::write_batch()` encodes all frames into one owning
buffer and invokes one logical `write_all`, preserving JSON+BYTES adjacency and
present zero-length BYTES frames. Platform streams finish ordinary partial OS
writes internally. Timeout, error, or incomplete logical `write_all` is fatal;
the batch is never retried after bytes may have become visible.

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

## Injection and remaining integration

`PipeChannelFactory` selects a per-connection `PipeChannelHandler` after open.
Handlers receive only validated business JSON/BYTES frames and a bounded
writer. This milestone intentionally provides no real provider, sync, trigger,
or remote handler and does not start a listener in tests. Wiring the existing
trigger dispatcher, provider/sync owners, remote proxy, application-selected
endpoint, observability, and live OS security/load tests remain pending.

`BAAS_service_pipe_host_tests` covers all four channels, bounded hostile open
JSON, fragmented/coalesced BPIP, open ordering, atomic JSON+zero-byte-BYTES,
ERROR+CLOSE, partial write failure, fixed connection limits, blocked accept and
read cancellation, absolute drip-feed/open slowloris timeout, handler exceptions, and both
self-first/external-first join orders using fakes only.
