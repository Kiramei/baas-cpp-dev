# Bounded trigger session core

`BAAS_service_protocol` now contains the transport-independent lifecycle core
for the v1 `trigger` business channel. It preserves the observed Python/Tauri
`command` and `command_response` model; it does not invent REST task routes.

This is a real bounded correlation and outbound-ordering layer, not a complete
command service. The adjacent bounded codec in `SERVICE_TRIGGER_ENVELOPE.md`
owns v1 JSON decoding/encoding, and `SERVICE_TRIGGER_COMMAND_CATALOG.md` freezes
the legacy command-selection metadata, while `SERVICE_TRIGGER_INGRESS.md` owns
strict inbound JSON/binary pairing and complete input lifetime. Catalog-to-
ingress integration, executor ownership, WebSocket authentication/secretstream,
and live BAAS task adapters remain separate required layers.

## Admission and correlation

`TriggerSession::admit()` is called only after a transport has decoded the v1
command envelope. The caller passes the exact command name, JavaScript-safe
integer timestamp, optional UTF-8 config ID, decoded payload/binary sizes, and
whether the server command produces one response or a stream.

Admission rejects before dispatch when:

- the session is closed;
- the command is not lowercase ASCII with digits/underscores after its first
  letter;
- the timestamp is greater than `9007199254740991`;
- the config ID is empty, invalid UTF-8, or oversized;
- decoded JSON/binary input exceeds its independent limit;
- the timestamp is already live; or
- the bounded correlation table is full.

A terminal response does not release its timestamp when it is merely queued or
leased to a writer. The timestamp remains reserved until the writer confirms
that the complete terminal batch was sent, preventing a new command from
racing a stale or partially written response into Tauri's callback map. Input
payloads are not retained by this core; their transport/dispatcher owner
remains responsible for lifetime and command-specific validation.

Successful admission also returns an opaque `AdmissionReceipt`. Every publish
must present that receipt; session instance ID, timestamp, and monotonic
generation must match the live entry. This prevents stale or cross-session
handlers from publishing into a reused correlation. `rollback()` releases an
admission only before any response is queued and rejects old, foreign, or
already-visible receipts without changing state.

## Streaming, binary FIFO, and backpressure

Single-response commands must publish one terminal batch. Stream commands may
publish ordered progress batches followed by exactly one terminal batch. Error
and cancellation responses are always terminal. Once cancellation is accepted,
ordinary progress/success is rejected and only an explicit terminal cancelled
result can complete the correlation.

One opaque `OutboundBatch` contains codec-produced UTF-8 `command_response` JSON,
response mode, and optional binary bytes. Read-only access prevents callers
from changing validated metadata. A `has_binary` bit distinguishes no binary
from a promised zero-byte frame. This is the indivisible send unit.
`encode_pipe_batch()` creates one owning BPIP write buffer containing
the JSON frame followed immediately by the optional BYTES frame. A Pipe writer
can therefore hold one connection send lock for the complete buffer; a future
WebSocket writer must apply the same batch boundary. This preserves Tauri's
global rule that the binary frame immediately follows the response declaring
`data.binary.size`.

The queue has independent per-JSON, per-binary, batch-count, and aggregate-byte
limits. A full queue returns `queue_full` or `queued_bytes_exceeded` without
marking a terminal response complete, so the task owner can retry after the
writer drains capacity. Accepted output is never silently dropped while the
connection remains open.

## Egress leases and send confirmation

`begin_send()` grants a read-only `SendLease` for the FIFO head. A session has
at most one active lease. Beginning a send does not remove or stop charging the
batch, does not increment `popped_batches`, and does not release its
correlation. The lease uses shared immutable ownership so a concurrent
`close()` cannot leave a writer with a dangling batch reference.

After the transport has successfully written the complete JSON frame and its
optional immediately-following binary frame, it calls `complete_send()` with
the same lease. That is the only operation that removes the head, relieves
queue backpressure, increments `popped_batches`, and releases a terminal
correlation. Duplicate acknowledgements return `no_active_lease`; an old lease
presented while a newer send is active returns `lease_mismatch`. Neither case
changes queue state or counters.

`observe_output_ready()` installs one weak, cancellable observer for transport
wakeup. It is invoked, without the session mutex held, when the output queue
changes from empty to non-empty. Registration while output is already queued
performs one immediate level-recovery notification, avoiding a lost edge during
host installation. The move-only subscription unregisters by generation;
session close/destruction cancels future delivery, while an already-running
callback owns a strong observer reference and may finish safely. A rejected
publish (including queue backpressure) does not create a false ready edge.

Any write error, including failure after only one frame of a JSON/binary batch,
is reported through `fail_send()`. A matching failure permanently closes the
session, invalidates the active send, drops every queued batch, and returns all
still-running commands that the executor owner must cancel. The transport MUST
close that connection and MUST NOT attempt the remaining binary frame or any
later batch on it. Pipe hosts should first coalesce with `encode_pipe_batch()`;
future WebSocket hosts must serialize both writes and failure/close on one
connection send strand.

`complete_send()`, `fail_send()`, and `close()` are mutex-linearized. In a
close race exactly one transition owns the cancellation list: a winning send
failure or explicit close returns it, and later close/failure calls return an
empty list or `closed`. A successful completion racing close either completes
fully before close or returns `closed`; it can never consume output after
close. Queue statistics retain leased batches. `send_leases_started`,
`send_failures`, `dropped_batches`, and `dropped_bytes` expose the resulting
outcome without double counting.

## Cancellation and disconnect

`request_cancel()` is idempotent and records cancellation before any late
completion. It is an embedding API, not a newly claimed v1 wire message; the
current v1 protocol still lacks a general cancellation envelope.

`close()` permanently stops admission, invalidates any send lease, drops output
that can no longer be sent, and returns every command without a terminal
response in deterministic timestamp order. A command with a queued terminal is
already complete and therefore is dropped but not returned for cancellation.
The service owner must propagate the returned list into its executor or runtime
cancellation mechanism. A completion arriving after close is rejected,
satisfying the protocol rule that disconnected work cannot emit an ordinary
response.

## Default limits

| Boundary | Default |
| --- | ---: |
| Live correlations | 256 |
| Command bytes | 128 |
| Config ID bytes | 256 |
| Decoded request JSON | 1 MiB |
| Request binary | 64 MiB |
| Response JSON | 1 MiB |
| Response binary | 64 MiB |
| Queued batches | 256 |
| Aggregate queued bytes | 72 MiB |

The 64 MiB binary limit matches BPIP v1. These are per-session foundation
defaults and do not replace future global connection, process-memory, command
deadline, rate, or executor limits.

## Verification and remaining boundary

`BAAS_service_trigger_session_tests` covers stable errors, input bounds,
duplicate timestamps, in-flight saturation, terminal reservation, streaming
order, JSON+binary pairing, retryable queue backpressure, cancellation
precedence, disconnect cleanup, concurrent publication, and BPIP
encode/decode. `BAAS_service_trigger_egress_tests` additionally covers retained
lease budgets/correlation, one-lease concurrency, duplicate and stale ack/fail,
connection-fatal send failure, deterministic cancellation handoff, exact
counters, and complete/fail/close races. The framing and envelope suites run
alongside them in Debug and Release foundation builds on Windows, Linux, and
macOS.

Still required before the Phase 4 task API item is complete:

- catalog-selected admissions and a sealed dispatch/response bridge are
  integrated; actual command handlers and runtime/executor ownership remain
  pending;
- a coordinated general cancellation message or exact legacy stop-command
  mapping;
- WebSocket and live Pipe channel hosts using this core;
- shared Python/C++/Tauri fixtures, deadlines, load tests, stale-task cleanup,
  and end-to-end execution.
