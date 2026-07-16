# Provider, sync, and remote Pipe channels

`BAAS_service_business_pipe_channel` adapts the existing transport-independent
provider, sync, and remote business handlers to bounded BPIP connections. It
also routes Trigger opens to an injected `TriggerPipeChannelFactory`; it does
not reimplement Trigger ingress, correlation, JSON/BYTES pairing, or send
leases.

This target is a channel factory, not a listener or application composition
root. `BAAS_service` does not yet install it, and this slice does not claim a
live native endpoint or Tauri end-to-end operation.

## Open and frame policy

`PipeHost` writes `open_ok` before invoking the adapter's `on_open`. Provider
starts its subscriptions and emits the initial `logs_full`/`status` snapshots
from that callback, so no initial state can precede negotiation. Sync and
remote are armed through the same callback but have no unsolicited initial
message. A coalesced client frame waits until `on_open` succeeds.

Provider and sync accept JSON frames only. Remote requires one JSON
configuration frame followed exclusively by BYTES frames. Remote output is
always BPIP BYTES; its WebSocket-only `decrypt=false` capability is accepted
before backend startup but does not add encryption to local Pipe traffic.
Trigger is delegated unchanged to the dedicated Pipe implementation. A
missing channel dependency, wrong semantic frame kind, failed handler status,
or exception fails closed.

## Output completion and close barrier

The per-connection plaintext sink maps provider/sync messages to JSON and
remote device messages to BYTES. One handler batch becomes one
`PipeConnectionWriter::write_batch`, preserving provider initial-message
adjacency. Observed remote output is reported `written` only after the complete
logical `write_all` succeeds. Allocation failure, capacity rejection,
timeout, partial write, exception, or a poisoned writer reports `failed`,
interrupts the stream, and is never retried.

Backend subscriptions and remote device callbacks may emit concurrently. The
sink admits each call under a small state lock, releases that lock before
entering the writer, and relies on the writer's serialization. Close first
stops new sink admission and interrupts platform I/O, then invokes the reused
handler's subscription/session close barrier, and finally waits for every sink
call that already entered, including its write-receipt callback. No adapter lock
is held while closing a backend, completing a write receipt, or performing
transport I/O.

The Pipe open `name` becomes the handler's opaque socket identity but is not an
authorization credential. BPIP business channels continue to rely on the
Windows named-pipe ACL or Unix peer-credential boundary documented in
`SERVICE_PIPE_HOST.md`.

## Build and evidence

Enable `BUILD_SERVICE_BUSINESS_PIPE_CHANNEL` for the adapter or
`BUILD_SERVICE_BUSINESS_PIPE_CHANNEL_TESTS` for
`BAAS_service_business_pipe_channel_tests`. The latter uses fake streams with
the real `ProviderHandlerFactory`, `SyncHandlerFactory`, and
`RemoteHandlerFactory`. It covers open ordering, provider initial state and
requests, sync list/pull/push, remote raw bytes and exact write completion,
write failure, strict JSON/BYTES policy, Trigger delegation, and a blocked
provider push interrupted through the close barrier. A separate receipt-barrier
race proves host join cannot overtake an observed completion callback.
`PipeHost` retains its separate foundation suite, including post-open callback
failure and writer poisoning.

Remaining integration gates are application ownership/start-stop ordering,
concrete provider/runtime publication, `setup.toml` projection, a concrete
remote backend, live Windows/Unix endpoints, Tauri recovery, and device smoke.
