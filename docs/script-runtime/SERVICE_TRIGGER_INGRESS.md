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
while awaiting binary is discarded with `json_while_awaiting_binary`, clears the
partial command, and closes ingress. A binary frame without that declaration is
connection-fatal as `binary_without_declaration`. A frame received while a
completed item is still untaken is also fatal: ingress does not own that second
frame and therefore cannot safely claim it is retryable. These paths close
before a late binary can attach to a new command.

The binary is represented as `optional<vector<byte>>`. `nullopt` means no
binary frame; an engaged empty vector proves a present zero-length frame. The
ready item owns the decoded `CommandEnvelope`, optional bytes, successful
`BuildAdmissionResult`/`CommandAdmission`, and stable catalog descriptor. Its
single/stream `ResponseMode` comes only from that descriptor; callers cannot
override it. `admit_to(TriggerSession&)` submits the immutable catalog-derived
admission without reconstructing policy and returns the session-minted
`AdmissionReceipt` on success. A low-level caller must retain that capability
for publication or rollback; it must never admit and discard the receipt.
Duplicate/saturated session admission is a correlated command
rejection; a closed session yields the explicit `closed` disposition.

The intended host execution path move-consumes the ready item through
`TriggerConnectionOwner::submit()`. It resolves the dispatcher registration and
reserves global/per-connection executor capacity before admission, then registers
the task owner before committing a worker. Direct `admit_to()` remains a
low-level integration/testing API and must not be combined with owner submission
of the same logical item.

Before frame state changes, ingress returns stable `unknown_command`,
`config_id_required`, `binary_marker_required`, or
`binary_marker_forbidden` errors. Required config IDs reject both absence and
the empty string; present-empty remains valid for commands without that
requirement. This fail-fast binary policy is a deliberate safety hardening over
the current Python handler, which ignores a true marker on other commands and reports a
missing import payload later during execution. C++ rejects both cases before
dispatch, but classifies them as correlated `command_rejection`, so a future
adapter may send one safe error response and continue without waiting for bytes.

## Executable live-adapter policy

`trigger_ingress_disposition()` is a total, allocation-free matrix. Unknown
future error enum values fail closed.

| Error class | Disposition | Correlation | Required adapter action |
| --- | --- | --- | --- |
| no error | `none` | no | continue the success path |
| already closed | `closed` | no | stop reads; close remains permanent |
| item already pending | `fatal` | no | close; the newly supplied frame is not owned |
| malformed/schema-invalid JSON | `fatal` | no | close without a response |
| frame order/presence violation | `fatal` | no | close without a response |
| JSON, binary, or aggregate limit | `fatal` | no | close without a response |
| unknown command | `command_rejection` | yes | send one command error, then continue |
| missing required config ID | `command_rejection` | yes | send one command error, then continue |
| required/forbidden binary marker | `command_rejection` | yes | send one command error, then continue |
| non-closed session admission failure | `command_rejection` | yes | send one command error, then continue |

`recoverable` is reserved in the public vocabulary for a future operation that
can prove the caller still owns all retry input. No current ingress error maps
to it.

Every command rejection owns the already decoded, bounded command name and
timestamp, its stable ingress error code, and a static safe message. Fatal,
recoverable, and closed results never invent command correlation. This is
enough to construct a future `command_response` without reparsing the original
JSON, but this layer deliberately does not own a generic response sink.

## Transactional failure and lifecycle

JSON decode failures retain `EnvelopeError` and its byte offset. Oversized or
wrong-type frames and codec rejection transition ingress to `closed` without a
correlation identity. Catalog and non-closed session admission rejection leave
ingress able to accept the next JSON command and carry bounded identity.

On receipt of the promised binary, the pending envelope is removed before any
limit check or allocation. A binary/aggregate limit failure then closes
ingress; even allocation exceptions cannot leave `awaiting_binary` state.

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
admission, every error-to-disposition mapping, correlated rejection identity,
static safe messages, independent limits, fatal closure, command-level
continuation, reset, explicit close, and late frames.

Still pending are authenticated WebSocket/Pipe hosts, frame decryption/decoding
adapters, command-specific payload schemas, real runtime execution,
cancellation propagation, and shared
Python/C++/Tauri end-to-end fixtures.
