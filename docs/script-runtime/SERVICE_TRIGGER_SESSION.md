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

A terminal response does not release its timestamp when it is merely queued.
The timestamp remains reserved until that terminal batch is popped for sending,
preventing a new command from racing a stale response into Tauri's callback
map. Input payloads are not retained by this core; their transport/dispatcher
owner remains responsible for lifetime and command-specific validation.

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

## Cancellation and disconnect

`request_cancel()` is idempotent and records cancellation before any late
completion. It is an embedding API, not a newly claimed v1 wire message; the
current v1 protocol still lacks a general cancellation envelope.

`close()` permanently stops admission, drops output that can no longer be sent,
and returns every command without a terminal response in deterministic
timestamp order. The service owner must propagate that list into its executor
or runtime cancellation mechanism. A completion arriving after close is
rejected, satisfying the protocol rule that disconnected work cannot emit an
ordinary response.

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
encode/decode. The existing framing suite runs alongside it in Debug and
Release foundation builds.

Still required before the Phase 4 task API item is complete:

- catalog admission/dispatch integration and actual runtime/executor task ownership;
- a coordinated general cancellation message or exact legacy stop-command
  mapping;
- WebSocket and live Pipe channel hosts using this core;
- shared Python/C++/Tauri fixtures, deadlines, load tests, stale-task cleanup,
  and end-to-end execution.
