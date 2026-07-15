# Bounded trigger envelope codec

`BAAS_service_protocol` contains a dependency-free JSON codec for the v1
`trigger` business channel. It is the required boundary between untrusted
WebSocket/Pipe JSON and `TriggerSession`; it does not dispatch or execute a BAAS
command.

## Request decoding

`decode_command_envelope()` accepts the observed Python/Tauri request shape:

```json
{"type":"command","command":"status","timestamp":1700000000123,"config_id":null,"payload":{}}
```

`type`, `command`, and `timestamp` are required. `payload` defaults to `{}` and
must be an object when present; `config_id` defaults to null. Unknown top-level
fields are validated as JSON and ignored, matching the current Pydantic
`BaseModel` extra-field behavior. The stricter normative v1 requirements are
applied before admission: command names use lowercase ASCII/digits/underscore,
timestamps are unsigned integer JSON tokens no greater than
`9007199254740991`, and a present config ID is bounded UTF-8. Empty remains
present so the catalog can reject it only for commands that require an ID.

The parser validates the complete document, including ignored fields. It
rejects malformed scalar UTF-8, invalid escapes or surrogate pairs, trailing
data/commas, duplicate keys at every object depth, and depth/node/string/work
budget exhaustion. The returned `payload_json` is compact, duplicate-free
UTF-8 JSON. `declares_binary` reports only whether the exact JSON boolean
`payload.binary: true` was present. The codec deliberately does not name
`import_config`: `TriggerCommandCatalog` is the sole command-policy source and
`TriggerIngress` applies its required/forbidden binary rule.

After a transport receives the promised immediately-following frame,
`make_admission()` accepts an optional actual binary byte count and combines it
with decoded metadata for `TriggerSession::admit()`. Optional presence
distinguishes a received zero-byte frame from no frame. Missing declared input
and unexpected undeclared input both fail before admission. A transport must
not dispatch before both JSON and any declared binary input pass their
independent bounds.

The adjacent [`SERVICE_TRIGGER_INGRESS.md`](SERVICE_TRIGGER_INGRESS.md) now
implements that strict frame-order and ownership boundary without duplicating
this codec or claiming a live transport.

## Response encoding

`encode_command_response()` owns the outer response fields rather than
accepting caller-built envelope JSON. It copies `command`, `timestamp`, status,
and terminal state into one `OutboundBatch`, validates optional `data_json`
with the same bounded parser, JSON-escapes errors, and emits compact fields in a
deterministic order. Internal `cancelled` state uses the existing wire
`status:"error"` so Tauri terminates the callback as it does today.

For stream output, codec input carries the response mode. Successful progress
is nonterminal and cannot contain the reserved top-level `data.done` key;
successful terminal output must have object data and receives codec-owned
`done:true`. The opaque batch retains mode, and `TriggerSession::publish()`
rejects a batch whose mode differs from admission. Thus server correlation
release and Tauri callback cleanup cannot diverge. Error/cancelled output is
always terminal and uses Tauri's existing error cleanup path.

Publication still does not mean delivery. `TriggerSession::begin_send()` keeps
the immutable batch and correlation owned and byte-accounted until the
transport confirms the complete JSON-plus-optional-binary unit with
`complete_send()`. A write failure uses `fail_send()` and makes that connection
unusable for further frames.

When binary output is present, data must be an object and must not already
contain the reserved top-level `binary` key. The codec alone injects
`data.binary.size` from the attached vector. A present empty vector is distinct
from no binary: it declares size zero and `encode_pipe_batch()` still emits the
required zero-length BYTES frame after JSON. Conversely, caller data containing
`data.binary` without attached bytes is rejected, preventing Tauri's global
binary FIFO from waiting on a frame that will never arrive.

`OutboundBatch` exposes read-only accessors and only the codec can construct a
populated batch. `TriggerSession::publish()` therefore accepts validated,
immutable envelope metadata rather than caller-written JSON. The Pipe adapter
preserves the encoded batch in one owning write buffer. A future WebSocket
writer must hold its per-connection send lock across the corresponding JSON
and optional binary messages.

## Default limits

| Boundary | Default |
| --- | ---: |
| Input JSON bytes | 1 MiB |
| Output JSON bytes | 1 MiB |
| Binary bytes | 64 MiB |
| Command bytes | 128 |
| Config ID bytes | 256 |
| JSON depth | 64 |
| JSON value nodes | 65,536 |
| Decoded key/string bytes | 1 MiB |
| Consumed parser work bytes | 4 MiB |

The hard parser depth ceiling is 256 even if an embedder supplies a larger
configuration. These per-envelope limits complement, and do not replace, the
session correlation/queue limits or future connection/process budgets.

## Verification and remaining boundary

`BAAS_service_trigger_envelope_tests` covers compatibility defaults, ignored
fields, Unicode decoding, compact payload output, every required field,
timestamp/schema failures, hostile JSON and parser budgets, deterministic
success/error/cancellation responses, stream terminal wire binding, response
mode mismatch, inbound binary presence, reserved binary metadata, non-object
binary data, zero-byte frames, and codec-to-session correlation. The target is
part of the Debug/Release foundation CI matrix alongside framing, session, and
egress lease/race tests. Command authorization and mode derivation are covered
by the adjacent ingress integration suite, not duplicated in this codec suite.

Still required before Phase 4 task APIs are complete:

- command-specific payload/result schemas beyond the implemented inventory;
- actual runtime/executor ownership, deadlines, and stale-task cleanup;
- a coordinated cancellation envelope or exact legacy stop-command mapping;
- authenticated WebSocket and local Pipe trigger hosts using this codec/session;
- shared Python/C++/Tauri fixtures, load tests, and end-to-end execution.
