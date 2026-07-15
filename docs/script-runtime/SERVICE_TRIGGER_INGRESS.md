# Bounded trigger ingress state machine

`TriggerIngress` is the dependency-free, transport-independent inbound frame
boundary between WebSocket/Pipe framing and future command dispatch. It accepts
already separated JSON and binary frames, delegates JSON schema parsing to
`TriggerEnvelope`, and produces one owned item suitable for later
`TriggerSession::admit()`. It performs catalog lookup and policy admission but
no network I/O, automatic session mutation, dispatch, or command execution.

## State and frame order

The serial state machine is intentionally one-outstanding:

| State | Accepted input | Result |
| --- | --- | --- |
| `accepting_json` | one JSON frame | ready item, or `awaiting_binary` only for declared input |
| `awaiting_binary` | the immediately adjacent binary frame | ready item |
| `ready` | `take_ready()` | returns the sole item and resumes JSON input |
| `closed` | none | permanent `closed` rejection |

Only the catalog descriptor whose inbound policy is `required`—currently
`command:"import_config"`—with the exact JSON boolean
`payload.binary:true` enters `awaiting_binary`. The codec reports the marker
without command knowledge and ingress applies the catalog rule. A JSON frame
while awaiting binary is discarded with `json_while_awaiting_binary` and clears the
partial command. A binary frame without that declaration is rejected with
`binary_without_declaration`. A frame received while a completed item is still
untaken returns `item_pending` without replacing the complete item.

The binary is represented as `optional<vector<byte>>`. `nullopt` means no
binary frame; an engaged empty vector proves a present zero-length frame. The
ready item owns the decoded `CommandEnvelope`, optional bytes, successful
`BuildAdmissionResult`/`CommandAdmission`, and stable catalog descriptor. Its
single/stream `ResponseMode` comes only from that descriptor; callers cannot
override it. `admit_to(TriggerSession&)` submits the immutable catalog-derived
admission without reconstructing policy.

Before frame state changes, ingress returns stable `unknown_command`,
`config_id_required`, `binary_marker_required`, or
`binary_marker_forbidden` errors. Required config IDs reject both absence and
the empty string; present-empty remains valid for commands without that
requirement. This fail-fast binary policy is a deliberate safety hardening over
Python: Python currently ignores a true marker on other commands and reports a
missing import payload later during execution.

## Transactional failure and lifecycle

JSON decode failures retain `EnvelopeError` and its byte offset. Oversized or
wrong-type frames, catalog rejection, codec rejection, and admission
rejection never leave a partial command. On receipt of the promised binary,
the pending envelope is removed before any limit check or allocation, so even
allocation exceptions cannot leave `awaiting_binary` state.

Ingress errors reset or preserve this local state exactly as documented, but
do not define whether a live transport must close its connection. Mapping
framing/schema failures to recoverable responses or connection-fatal closure
remains a responsibility of the pending authenticated adapter.

`reset()` explicitly drops pending or completed input and resumes
`accepting_json`. It cannot reopen an ingress after `close()`. `close()` is
idempotent, permanently rejects both frame types, and drops partial and ready
items. The class is a serial state machine; a connection owner must serialize
calls instead of concurrently invoking it.

## Limits

Defaults are independent and checked before ownership transfer:

| Boundary | Default |
| --- | ---: |
| JSON frame | 1 MiB |
| Binary frame | 64 MiB |
| JSON plus optional binary | 65 MiB |

The configured JSON and binary frame limits cannot exceed the adjacent
`TriggerEnvelopeLimits`; parser depth/node/string/work gates still apply.
Aggregate addition is overflow-checked. A rejected promised binary consumes and
clears that partial command, preventing late bytes from attaching to later JSON.

## Verification and remaining boundary

`BAAS_service_trigger_ingress_tests` covers owned JSON-only and binary items,
zero-length presence, one outstanding item, strict adjacency, required and
forbidden binary markers, malformed and duplicate-key JSON, unknown commands,
required/optional-empty config IDs, catalog-derived modes, direct session
admission, independent JSON/binary/aggregate limits, reset, close, late frames,
and clean recovery after every structured failure.

Still pending are authenticated WebSocket/Pipe hosts, frame decryption/decoding
adapters, command-specific payload schemas, connection-owned orchestration of
`admit_to()`, dispatch/runtime execution, cancellation, and shared
Python/C++/Tauri end-to-end fixtures.
