# BAAS Service Protocol v1

Status: normative migration specification, revision 1.0-draft

Protocol version: `1`
Baseline repository revision: `8333f75c1bf4c8dd750ab359fb97a935b2e94658`

This document turns the observed `baas-dev`/`baas-tauri` contract into a
versioned target for the C++ migration. It is narrower than a claim of product
parity: the wire requirements are normative, while the gate table records which
requirements still lack cross-implementation or end-to-end evidence.

## 1. Requirement language and status

The words **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**, and
**MAY** are normative.

- **[REQUIRED]** is part of a conforming v1 implementation.
- **[OPTIONAL]** is an interoperable extension that a peer cannot assume exists.
- **[MISSING]** is a required migration or verification gate without sufficient
  evidence. It does not make the corresponding behavior optional.

The protocol has three layers:

1. HTTP for health, remember-cookie management, logs, and Android helpers.
2. WebSocket for authenticated control and encrypted business channels.
3. BPIP over a Windows named pipe or Unix domain socket for native Tauri
   business channels without WebSocket encryption.

## 2. Evidence and authority

**[REQUIRED]** Implementations MUST match the checked-in deterministic bytes in
`tests/service_contract/v1_vectors.json`. The fixture anchors:

- `baas-dev` commit `75bbacb545bc87e9510d85cbe8034f9180397004`;
- `baas-tauri` commit `711a09d493b1d7e2fae9fc45b778a149977aa817`;
- canonical JSON, base64url, X25519, Ed25519, Argon2id, HMAC/HKDF, control
  envelopes, stream contexts, and BPIP frames;
- C++ BPIP implementation and tests introduced by commit `8b1ff52`.

`SERVICE_COMPATIBILITY.md` remains the observed-product inventory. This file is
the normative v1 target. If prose and a golden byte differ, the golden byte wins
for v1 and the prose MUST be corrected in the same change.

**[MISSING]** The vectors do not prove deterministic libsodium secretstream
header/ciphertext bytes, password/persistent-key lifecycle parity, complete
HTTP/WS behavior, live pipe behavior, or Tauri end-to-end operation.

## 3. Version selection and compatibility

### 3.1 Exact v1 selection

**[REQUIRED]** v1 has exact-version selection, not range negotiation:

- WebSocket `client_hello.version` MUST be the JSON number `1`.
- `server_hello.version` and encrypted `auth_ok.protocol_version` MUST be `1`.
- A server MUST reject a missing, non-integral, or non-`1` client version before
  authentication. The observed service reports `Unsupported protocol version`.
- A client MUST reject a non-`1` server version and MUST NOT continue under a
  best-effort downgrade.
- BPIP header byte 4 MUST be `0x01`; any other value is
  `unsupported_version`.

HTTP v1 paths are intentionally unversioned. `/health` exposes readiness and
the signing public key but is not a version negotiation endpoint.

**[MISSING]** There is no capability/range negotiation message and no verified
parallel v1/v2 server. A future incompatible protocol MUST use a new version and
coordinate the Tauri pin and rollout; it MUST NOT silently reinterpret v1.

### 3.2 Additive compatibility

**[REQUIRED]** Unknown fields inside extensible configuration, event, status,
and command payload objects MUST be preserved when data is round-tripped.
Unknown message types are not automatically supported. A new message type,
required field, cryptographic context, BPIP semantic kind, or ordering rule
requires explicit capability negotiation or a new protocol version.

**[OPTIONAL]** A peer MAY accept additional fields in known messages when doing
so does not alter a signed transcript, key context, or required state machine.

## 4. Common data representation

### 4.1 JSON

**[REQUIRED]** JSON text MUST be UTF-8. Protocol envelopes MUST be objects, not
top-level arrays or scalars. Non-finite numbers are forbidden. Sequence numbers
and timestamp correlation identifiers used as JavaScript numbers MUST remain in
the exact integer range `0..9007199254740991` (`2^53 - 1`).

Business JSON normally uses compact serialization but is not signed merely
because it is JSON. The following values use canonical JSON bytes:

- handshake transcript;
- ChaCha20-Poly1305 plaintext and AAD;
- resume/remember HMAC contexts;
- resume tickets;
- business key scope and secretstream AAD prefix.

### 4.2 Canonical JSON

**[REQUIRED]** Canonicalization recursively sorts object keys ascending, keeps
array order, emits no insignificant whitespace, emits Unicode text rather than
ASCII `\u` substitution where JSON escaping is not required, and UTF-8 encodes
the result. The golden vectors are the byte-level definition.

All specified cryptographic-context keys are ASCII. A sender MUST NOT introduce
non-ASCII object keys into a signed or MACed v1 context. This restriction avoids
the unverified difference between Python Unicode-scalar sorting and JavaScript
UTF-16 sorting outside the current schema. Unicode values are allowed.

**[MISSING]** An exhaustive cross-language corpus for astral keys, edge-case
string escaping, negative zero, exponent formatting, and large numeric values
does not exist. Such values MUST NOT be added to cryptographic contexts until
that corpus passes in Python, Tauri, and C++.

### 4.3 Base64url

**[REQUIRED]** Binary fields in JSON use RFC 4648 URL-safe base64 with `-` and
`_` and retain `=` padding. Senders MUST emit padding. Receivers MUST accept
correctly padded values and reject malformed encodings.

**[OPTIONAL]** Tauri currently repairs omitted padding before decoding. This is
a receive-only tolerance; senders cannot rely on it because Python rejects
unpadded encodings when padding is required.

### 4.4 Identifiers and timestamps

**[REQUIRED]** `session_id` and `socket_id` are opaque non-empty strings.
`channel` is one of the values in section 8. Client `timestamp` fields are
correlation values, not authentication freshness proofs. A recipient MUST NOT
use a client timestamp as its only replay defense.

## 5. BPIP v1 byte stream

### 5.1 Header and frame kinds

**[REQUIRED]** Each frame is a 10-byte header followed immediately by exactly
`length` opaque payload bytes:

| Offset | Size | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 4 | magic | ASCII `BPIP` (`42 50 49 50`) |
| 4 | 1 | version | `01` |
| 5 | 1 | kind | unsigned byte |
| 6 | 4 | length | little-endian unsigned 32-bit integer |

Known kinds are:

| Kind | Name | Payload |
| ---: | --- | --- |
| 1 | JSON | UTF-8 JSON object |
| 2 | BYTES | opaque bytes |
| 3 | CLOSE | empty |
| 4 | ERROR | UTF-8 diagnostic text; not JSON |

**[REQUIRED]** Framing code MUST preserve unknown `kind` values. An unknown kind
with a valid header is a complete frame, not a malformed header. The channel
semantic layer MUST reject an unsupported kind, normally by sending ERROR and
closing. This distinction permits future framers to relay kinds they do not
interpret without claiming those kinds are supported.

### 5.2 Incremental decoding

**[REQUIRED]** BPIP runs over a byte stream. A decoder MUST:

- accept a header or payload split at any byte boundary;
- emit zero frames while an otherwise valid frame is incomplete;
- emit every complete frame when multiple frames are coalesced in one read;
- validate magic, version, decoded length, and the 64 MiB limit before reading
  or allocating the declared payload;
- preserve complete frames preceding a malformed next header;
- treat EOF with a partial header or payload as a truncated connection.

The standalone C++ decoder becomes sticky after `invalid_magic`,
`unsupported_version`, or `payload_too_large`: subsequent feeds consume nothing
and repeat the first error until explicit `reset()`. A network server MUST close
the faulty connection rather than reset and continue on untrusted residual
bytes.

### 5.3 Payload limit

**[REQUIRED]** `length <= 67108864` (64 MiB) is valid; `67108865` and above are
invalid. The exact maximum BYTES header is `42504950010200000004`. The limit is
per frame and does not authorize unbounded connection queues.

### 5.4 Open state machine

**[REQUIRED]** A new pipe connection starts in `EXPECT_OPEN`:

1. The first complete frame MUST be JSON.
2. Its payload MUST be an object with `type: "open"`, a supported `channel`, and
   a non-empty `name` used by Tauri to identify the logical connection.
3. The server MUST answer JSON `{"type":"open_ok","channel":"..."}` before
   business messages.
4. The connection then enters `OPEN` for exactly one logical channel.

```json protocol-example
{"type":"open","channel":"trigger","name":"trigger"}
```

```json protocol-example
{"type":"open_ok","channel":"trigger"}
```

The v1 Python server ignores `name` after opening, but clients MUST still send
it because the Tauri bridge keys connections as `channel:name`. Supported pipe
channels are `provider`, `sync`, `trigger`, and `remote`; `control` is not a pipe
channel.

**[REQUIRED]** A non-JSON first frame, malformed JSON, a non-object JSON value,
non-`open` type, or unsupported channel causes ERROR followed by connection
close. Extra open fields MAY be ignored.

The C++ `PipeHost` foundation now implements this bounded open state over the
incremental BPIP decoder with an injected channel factory. A dedicated
pre-open header gate rejects declared payloads above the open limit before
generic decoder allocation; post-open declared payloads and outbound atomic
batches reserve host-wide retained-byte budgets. Its deterministic tests use
fake streams; real provider/sync/trigger/remote handler wiring and live
cross-process endpoint tests remain pending.

### 5.5 Close and error

**[REQUIRED]** CLOSE has a zero-length payload and means no more application
frames are expected from that sender. ERROR contains replace-safe UTF-8 text and
is terminal for the connection. Receivers MUST NOT parse ERROR as JSON or expose
its text as a stable machine error code.

**[MISSING]** Live named-pipe/Unix-socket tests for simultaneous close, partial
write followed by EOF, concurrent connections, and error delivery are pending.

## 6. HTTP surface

### 6.1 Inventory

| Class | Method and path | Access and v1 purpose |
| --- | --- | --- |
| [REQUIRED] | `GET /health` | Public readiness, runtime statuses, auth initialized/epoch, signing public key |
| [REQUIRED] | `POST /auth/remember` | Verify session resume proof and set remember cookie |
| [REQUIRED] | `POST /auth/logout` | Delete remember cookie |
| [REQUIRED] | `GET /system/logs` | Loopback-only entries/files; `limit` clamped to `1..10000` |
| [REQUIRED] | `POST /system/logs/clear` | Loopback-only log deletion |
| [REQUIRED] | `POST /android/active-config` | Android-and-loopback-only active profile selection |
| [REQUIRED] | `POST /android/toggle` | Android-and-loopback-only foreground task toggle |
| [REQUIRED] | `GET /android/wiki` | Android-and-loopback-only upstream wiki JSON wrapper |
| [REQUIRED] | `GET /android/wiki/proxy` | Android-and-loopback-only sanitized wiki proxy |
| [MISSING] | `POST /android/reset-auth` | Called by Tauri, absent from the anchored Python router; behavior must be classified and implemented or removed in a coordinated version change |

**[REQUIRED]** `/auth/remember` accepts `{session_id, proof}` and, on success,
returns `{ok:true, expires_at}` and sets `baas_remember` as HttpOnly,
`SameSite=Lax`, path `/`, and Secure when HTTPS or policy forces it.

**[REQUIRED]** `/health` MUST remain callable before authentication. Readiness
MUST be false or the endpoint unavailable until required persistent state is
loaded; a listening socket alone is not readiness. The C++ foundation now
models `starting | ready | failed` atomically with its public runtime/auth
snapshot: starting and failed are 503, and only ready retains the observed 200
body. Real runtime/auth owner wiring remains missing.

### 6.2 HTTP status and body compatibility

| Status | Meaning in observed v1 |
| ---: | --- |
| `200` | Successful operation; `/android/toggle` may still carry its legacy `{status:"error", type, error}` body |
| `400` | Invalid required input or unsupported wiki host |
| `401` | Remember proof/session failure |
| `404` | Non-loopback caller, non-Android conditional route, or missing resource; intentionally avoids exposing protected route existence |
| `502` | Wiki upstream failure |
| `500` | Unhandled internal failure |
| `503` | C++ foundation health is starting, failed, unavailable, or its provider failed |

FastAPI validation/error bodies use a `detail` field, while some business
responses use `status` and `error`. **[REQUIRED]** A v1 replacement MUST retain
the status and body shapes consumed by Tauri. It MUST escape diagnostics and
MUST NOT return secrets, keys, tokens, passwords, or filesystem internals.

**[MISSING]** A single structured HTTP error schema/code registry has not been
implemented or verified. Introducing one is additive only if existing fields
and statuses remain compatible; otherwise it requires v2.

### 6.3 HTTP security boundary

**[REQUIRED]** Logs and Android helpers MUST enforce loopback at the service,
not only in the UI. Android helpers MUST additionally require Android mode.
CORS credentials are required for Tauri/WebView cookie flows. Allowed origins
include configured origins, Tauri/localhost loopback origins, and same-host
development origins; arbitrary public origins MUST be rejected.

## 7. WebSocket authentication and cryptography

### 7.1 Transport state machine

**[REQUIRED]** `/ws/control` uses:

`CONNECT -> CLIENT_HELLO -> SERVER_HELLO_VERIFIED -> PREAUTH -> AUTHENTICATED`

Business WebSockets use:

`CONNECT -> CLIENT_HELLO(resume) -> SERVER_HELLO_VERIFIED -> RESUME_PROOF -> RESUME_OK -> STREAM_READY -> STREAMING`

Handshake JSON is text. Once a business channel reaches `STREAMING`, every
WebSocket application frame is binary. JSON business messages are UTF-8 JSON
inside secretstream; raw business bytes remain bytes inside secretstream unless
the remote channel explicitly disables outbound binary encryption.

### 7.2 Client and server hello

**[REQUIRED]** `client_hello` fields are:

- `type: "client_hello"`;
- `kind: "control" | "resume"`;
- endpoint-matching `channel`;
- `version: 1`;
- finite safe-integer `timestamp`;
- padded base64url 32-byte `client_nonce` and X25519 `client_kx_pub`;
- for resume: `session_id`, `socket_id`, and `resume_ticket`.

```json protocol-example
{"type":"client_hello","kind":"control","channel":"control","version":1,"timestamp":1700000000123,"client_nonce":"ICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj8=","client_kx_pub":"j0DFrbaPJWJK5bIU6nZ6bslNgp09e14a0bpvPiE4KF8="}
```

`server_hello` returns the matching type/kind/channel/version, password public
state, padded base64url 32-byte `server_nonce` and X25519 public key, Ed25519
`signature`, and `server_sign_pub`.

**[REQUIRED]** The signed transcript is canonical JSON of
`{kind,channel,client,server}` where `server` excludes `signature` and
`server_sign_pub`. The client MUST compare `server_sign_pub` to its pinned key
before signature verification. The current public pin is
`_GMKcfOCE-0_erXPJQRQv6mLiNBnT3tdHmAaXwWRis4=`. A server MUST NOT silently
replace this identity; migration requires a coordinated client release.

### 7.3 Algorithms and derivation labels

**[REQUIRED]** v1 uses Ed25519 signatures, X25519 key exchange, SHA-256,
HKDF-SHA256, HMAC-SHA256, Argon2id, IETF ChaCha20-Poly1305, and libsodium
XChaCha20-Poly1305 secretstream. Labels are case-sensitive UTF-8 bytes:

| Purpose | IKM / key | Salt | HKDF info or HMAC input |
| --- | --- | --- | --- |
| preauth server TX | X25519 shared | transcript SHA-256 | `preauth:server-tx` |
| preauth server RX | X25519 shared | transcript SHA-256 | `preauth:server-rx` |
| password proof context | X25519 shared | transcript SHA-256 | `auth-proof:{pwd_epoch}` |
| master secret | shared || Argon2 password key | transcript SHA-256 | `master-secret` |
| resume secret | master secret | transcript SHA-256 | `resume-secret` |
| control server TX/RX | master secret | SHA-256(UTF-8 session id) | `control:server-tx` / `control:server-rx` |
| business base, 64 bytes | master secret | transcript SHA-256 | canonical `{scope:"ws",session_id,socket_id,channel,pwd_epoch}` |
| stream server TX | first 32 base bytes | transcript SHA-256 | `secretstream:server-tx` |
| stream server RX | last 32 base bytes | transcript SHA-256 | `secretstream:server-rx` |

Argon2id public parameters are salt 16 bytes, output 32 bytes, operations 3,
memory 67108864 bytes. The password proof is HMAC-SHA256 keyed by the Argon2
password key over the derived auth-proof context.

### 7.4 Secure JSON envelope and replay rules

**[REQUIRED]** Preauth and control JSON is wrapped as:

```json protocol-example
{"type":"secure","seq":0,"ciphertext":"RiFCLXk-Ygobmj-deLIxa1VRQhire3Ls8ZUaAZZ3c5GUItaiEb_-id8tyFYLjn5Wng5VmPSeM7IIp1xAfczA"}
```

- Each direction has its own key and sequence starting at zero.
- Nonce is 12-byte big-endian unsigned `seq` (four leading zero bytes followed
  by a big-endian uint64 for the interoperable range).
- AAD is canonical JSON `{"seq":SEQ,"type":"secure"}`.
- Plaintext is canonical JSON of the inner object.
- Sender increments exactly once per envelope and MUST NOT reuse a nonce/key.
- The server MUST accept only the exact next client sequence. Replay,
  duplicate, backward, or skipped client values are authentication failures.
- The server MUST emit contiguous values. Clients MUST accept the exact next
  value and MUST reject replay or backward values.

The current Tauri receiver tolerates one forward gap (`expected + 1`), whereas
the Python receiver is strict. **[REQUIRED]** This tolerance does not permit a
server to skip a value. **[MISSING]** A coordinated Tauri hardening test is
needed before removing the receive tolerance.

### 7.5 Control authentication and sessions

**[REQUIRED]** In PREAUTH:

- uninitialized service: client sends encrypted `{type:"initialize",password}`;
- initialized service: client derives and sends `{type:"authenticate",proof}`;
- cookie resume: client first sends `{type:"resume_control"}`; server replies
  `resume_unavailable` or encrypted `auth_ok` containing session secrets.

`auth_ok` carries protocol version, session id, resume ticket, expiry, password
epoch/salt, and Argon2 parameters. Password-created sessions independently
derive the master/resume secrets. Remember-cookie sessions receive freshly
random master/resume material inside the preauth channel.

Sessions expire after 12 hours in the observed default. Password change/reset
increments `pwd_epoch`, clears remembered logins, publishes encrypted
`auth_revoked`, and closes control with code 4401. Tickets, sessions, and
remember tokens from an older epoch MUST be rejected.

**[MISSING]** Full initialization, remember, expiration, password change/reset,
persistent signing-key migration, and restart vectors are pending.

### 7.6 Business resume and secretstream

**[REQUIRED]** Resume proof is HMAC-SHA256 keyed by `resume_secret` over
canonical `{transcript_hash,session_id,socket_id,channel,pwd_epoch}`. The server
verifies ticket HMAC, session id, expiry, current password epoch, endpoint
channel, and resume MAC before sending encrypted `resume_ok` with its 24-byte
secretstream header. Client then sends encrypted sequence 1 `stream_ready` with
its 24-byte header.

Secretstream AAD is canonical
`{session_id,socket_id,channel,pwd_epoch}` followed by an 8-byte big-endian
per-direction stream sequence. Sequences start at zero. Authentication failure,
unexpected tag, replay, or ordering failure terminates the channel.

**[MISSING]** Keys and AAD are deterministic vectors, but secretstream headers
are generated by libsodium internal randomness. No deterministic header and
ciphertext vector is claimed. A deterministic randombytes test provider or a
captured fixture verified in both Python and Tauri is required.

## 8. Channel and message inventory

### 8.1 Transport availability

| Channel | WebSocket | Pipe | Purpose |
| --- | --- | --- | --- |
| `control` | `/ws/control`, REQUIRED | not supported | auth, heartbeat, revocation, password change |
| `provider` | `/ws/provider`, REQUIRED | REQUIRED | logs, runtime status, static snapshot |
| `sync` | `/ws/sync`, REQUIRED | REQUIRED | configuration/resource list, pull, patch, push |
| `trigger` | `/ws/trigger`, REQUIRED | REQUIRED | correlated commands and binary results |
| `remote` | `/ws/remote`, desktop REQUIRED | desktop REQUIRED | scrcpy-compatible binary proxy; disabled on Android |

WebSocket business channels require an authenticated control session and
secretstream resume. Pipe business channels bypass control authentication and
encryption and rely on OS IPC access control.

### 8.2 Control

**[REQUIRED]** Client messages are `ping` and `change_password`. Server messages
are `pong`, heartbeat every observed 3 seconds, and `auth_revoked`. Unknown
control types are protocol errors. Heartbeat timestamp is informational and not
a replay primitive.

### 8.3 Provider

**[REQUIRED]** Server initial/push types are `logs_full`, `log`, and `status`.
Client requests are `static_request` and `status_request`; responses are
`static_snapshot` and `status`. Initialization is not complete until the client
has a non-empty static snapshot and receives
`status.is_all_data_initialized=true` in addition to sync requirements.

### 8.4 Sync

**[REQUIRED]** Client request types are:

- `list` -> `config_list`;
- `pull` with resource `config|event|gui|static|setup_toml` -> `snapshot`;
- `patch` with mutable resource `config|event|gui|setup_toml`, timestamp, and
  JSON Patch operations `add|remove|replace` -> `patch_ack` or
  `patch_conflict` containing the current snapshot.

Backend push uses type `patch` and direction `push`. Unknown object fields and
scalar types in persisted resources MUST survive pull/patch/push. JSON Pointer
paths are empty or begin `/`. Tauri currently retries a conflict at most three
times with a fresh correlation timestamp.

```json protocol-example
{"type":"pull","resource":"config","resource_id":"example"}
```

```json protocol-example
{"type":"snapshot","resource":"config","resource_id":"example","timestamp":1700000000123,"data":{"unknown_field":true}}
```

**[MISSING]** Shared fixtures for unknown-field retention, every patch operation,
conflicts, retries, filesystem-origin pushes, and initialization ordering are
pending.

### 8.5 Trigger

**[REQUIRED]** Request envelope is:

```json protocol-example
{"type":"command","command":"status","timestamp":1700000000123,"config_id":null,"payload":{}}
```

Response echoes `command` and exact `timestamp`:

```json protocol-example
{"type":"command_response","command":"status","status":"ok","data":{},"timestamp":1700000000123}
```

Observed command inventory is `start_scheduler`, `stop_scheduler`, `solve`, the
`start_*` task family, `add_config*`, `remove_config*`, `copy_config`,
`export_config`, `import_config`, `detect_adb`, `valid_cdk`, `test_all_sha`,
`test_all_sha_stream`, `check_for_update`, `update_setup_toml`,
`update_to_latest`, `update_to_latest_stream`, `restart_backend`,
`stop_all_tasks`, `control_device`, and `status`.

Responses MAY complete out of request order and MUST correlate by timestamp.
Streaming commands emit zero or more responses with the same timestamp and end
with `data.done=true` or error. If a response declares `data.binary.size`, the
next global binary frame belongs to that response; JSON declaration and bytes
MUST be serialized together under the channel send lock. `import_config` with
`payload.binary=true` similarly consumes the immediately following binary
frame. Declared size MUST equal actual size.

**[MISSING]** Exhaustive table-driven command execution and cross-language load
tests remain pending. The C++ `TriggerSession` foundation now bounds live
correlations and output, rejects duplicate timestamps, enforces stream terminal
state, and keeps JSON/binary output in one indivisible batch. A single
transport-neutral send lease retains queue ownership and correlation until the
whole batch is confirmed; failure closes the session and deterministically
hands still-running commands back for cancellation. Its BPIP adapter emits the
two frames in one owning write buffer. `TriggerEnvelope` now parses
bounded duplicate-free command JSON, creates session admissions, builds the
exact response envelope, and exclusively injects verified binary sizes,
including zero-byte frames. `TriggerIngress` now enforces one-outstanding input,
strict adjacent JSON/binary ordering, owned zero-byte-aware input, and
independent frame/aggregate bounds. It resolves the immutable command catalog,
rejects unknown/config/binary-policy violations, derives response mode, and
produces an item directly admissible to `TriggerSession`. `TriggerDispatcher`
now provides sealed descriptor registration, receipt-bound response identity,
staged terminal exception containment, and explicit retry/close outcomes. The
C++ service does not yet execute real commands or run behind a live
WebSocket/Pipe trigger channel; live adapters also still own
fatal-versus-recoverable error mapping.

### 8.6 Remote

**[REQUIRED]** First business message is an object containing `config_id` and
`decrypt` (default true). Subsequent data follows the existing scrcpy binary
protocol and its big-endian structures; it is not replaced by new JSON.
Remote is unavailable on Android.

**[OPTIONAL]** When `decrypt=false`, the observed WebSocket endpoint may send
outbound remote bytes without secretstream encryption after the authenticated
setup message. This is a compatibility mode, not a general channel option.

**[MISSING]** Scrcpy structure vectors, raw/encrypted mode parity, disconnect,
and device lifecycle tests are pending.

## 9. Errors and close codes

### 9.1 Stable classifications

| Layer | Code/value | Required action |
| --- | --- | --- |
| C++ BPIP local | `invalid_magic` | poison decoder; network peer closes |
| C++ BPIP local | `unsupported_version` | poison decoder; network peer closes |
| C++ BPIP local | `payload_too_large` | reject before payload allocation; close |
| BPIP wire | kind 4 ERROR | terminal UTF-8 diagnostic, then close |
| WebSocket | `4403` | origin rejected before authentication |
| WebSocket | `4401` | authentication, resume, sequence, or protocol failure; also revocation |
| WebSocket | `1011` | internal channel failure |
| HTTP | `400/401/404/502/500` | meanings in section 6.2 |

**[REQUIRED]** Diagnostics are not stable codes. Clients MUST branch on the
layer code/status/message type, not English text. Servers SHOULD log a detailed
internal correlation record while returning a non-sensitive reason.

**[MISSING]** Business-level structured error codes are not standardized; the
current trigger channel uses `{status:"error",error}`. A v1-compatible registry
must retain these fields.

## 10. Limits, backpressure, timeouts, and cancellation

| Class | Requirement or observed compatibility value |
| --- | --- |
| [REQUIRED] | BPIP frame payload maximum: 64 MiB inclusive |
| [REQUIRED] | Tauri WebSocket handshake response timeout: 10 seconds |
| [REQUIRED] | Tauri pipe connection retry window: 10 seconds |
| [REQUIRED] | Wiki upstream request timeout: 15 seconds |
| [REQUIRED] | Default active session TTL: 12 hours |
| [REQUIRED] | Default remember TTL: 180 days, configurable by existing environment policy |
| [REQUIRED] | Per-connection writes are serialized; JSON/binary response pairs cannot interleave |
| [REQUIRED] | Pipe writers honor transport pause/resume rather than continuing unbounded writes |
| [OPTIONAL] | Status/log producers may coalesce superseded updates only when observable ordering requirements are retained |

**[REQUIRED]** A production C++ service MUST bound connection count, inbound
bytes, per-channel message queues, outstanding commands, worker queues, and log
fan-out. On saturation it MUST apply backpressure or return a scoped overload
error; it MUST NOT grow without bound or silently drop trigger/sync data.

**[MISSING]** Exact queue capacities, overload wire code, per-command deadlines,
server-side handshake deadline, heartbeat liveness threshold, and performance
budgets have not been selected or parity-tested. The anchored Python service
uses unbounded `asyncio.Queue` in several paths and therefore is not evidence
that this requirement is complete.

**[REQUIRED]** Connection loss cancels connection-owned sender/receiver tasks.
Shutdown stops admission before canceling work. A response MUST NOT be emitted
after its owning request/session is canceled unless the response is an explicit
terminal cancellation result.

**[MISSING]** v1 has stop commands but no general request cancellation envelope
and no verified stale-task cleanup contract. The C++ trigger core provides an
idempotent embedding cancellation request, cancellation-wins publication, and
disconnect enumeration for its future executor owner; this is not a new wire
message and is not yet connected to live tasks.

## 11. Lifecycle and persistence

### 11.1 Desktop

**[REQUIRED]** Native Tauri defaults to pipe. Managed desktop startup creates a
loopback HTTP service plus a per-instance Windows named pipe or Unix socket.
WebSocket mode uses a loopback service address and `/ws/*`. Multiple instances
MUST NOT accidentally share session, PID, port, or pipe ownership.

### 11.2 Android

**[REQUIRED]** Current Android compatibility uses port 8190 and an app-private
Unix socket, foreground-service/notification ownership, and no remote channel.
Android-only HTTP endpoints are enabled only in Android mode.

**[MISSING]** Current startup embeds Python through Chaquopy. Native C++/JNI
startup, foreground recovery, accessibility, socket/port reuse, and process
restart are not implemented or smoke-tested.

### 11.3 Startup and shutdown

**[REQUIRED]** Startup loads auth/config state before reporting ready, binds
only configured local interfaces, and creates pipe/socket endpoints with
current-user/app-private access. Shutdown stops admission, closes pipe and WS
connections, cancels tasks, flushes required atomic persistence, and removes
owned Unix socket/PID artifacts.

Auth compatibility recognizes `service_auth.json`,
`service_remembered_logins.json`, `service_signing_key.bin`, and
`service_ticket.key`. These files and the signing pin cannot be renamed or
regenerated silently.

**[MISSING]** Windows ACL verification, dynamic-port persistence, stale PID,
crash/restart, port conflict, multi-instance, tray exit, and graceful-shutdown
tests are pending. The C++ health foundation covers its owner plus HTTP
stop/restart/port lifecycle, but the Tauri probe and pipe-mode dynamic HTTP
address are not wired. Desktop launch paths still include Python-specific
process assumptions.

## 12. Security boundaries

**[REQUIRED]** WebSocket mode authenticates the server with the pinned Ed25519
identity, authenticates the user/session, enforces Origin, separates TX/RX keys,
and binds every resume/key/AAD context to session, socket, channel, epoch, and
transcript as specified.

**[REQUIRED]** Pipe mode deliberately bypasses WebSocket control authentication
and encryption. Its security boundary is OS IPC access control. Unix sockets
MUST be app-private (`0600` observed); Windows pipes MUST grant access only to
the intended user/app principals. Pipe names are capabilities only when ACLs
make them so; secrecy of the name alone is insufficient.

**[REQUIRED]** HTTP privileged helpers enforce loopback server-side. Services
MUST default to loopback and MUST require explicit policy before LAN exposure.
Secrets MUST use cryptographic randomness, constant-time MAC/signature checks,
and protected persistence appropriate to the platform.

The compiled Windows Pipe backend installs a protected current-user DACL,
checks first-instance ownership, rejects remote clients, and leaves instance
capacity to the host-wide connection cap. The compiled Unix backend requires a
canonical private parent, mode `0600`, close-on-exec/nonblocking descriptors,
owned-inode cleanup at listener stop, and same-user peer credentials where the
platform exposes them. **[MISSING]** Windows named-pipe ACL audit, local hostile-process tests, key-file
permission tests, origin/CORS matrix, LAN threat model, rate limiting, and
security review are pending.

## 13. Tauri mapping

| Tauri surface | Protocol mapping |
| --- | --- |
| `src/shared/SecureWebSocket.ts` | canonical/base64url, pinned hello, preauth/control envelopes, business resume, secretstream |
| `src/transport/factory.ts` | selects `pipe` or WebSocket channel implementation |
| `src-tauri/src/pipe_commands.rs` | native pipe connect, open/open_ok, BPIP read/write, close/error forwarding |
| `src/transport/pipe/TauriPipeConnection.ts` | decodes forwarded BPIP and maps JSON/BYTES/CLOSE/ERROR to frontend callbacks |
| `src/store/WebsocketStore.ts` | channel naming, initialization ordering, timestamp callbacks, patch retries, binary FIFO, recovery |
| `src-tauri/src/commands.rs` | desktop backend process, HTTP address, pipe endpoint lifecycle |
| Android service/bootstrap files | fixed port/socket and foreground lifecycle |

**[REQUIRED]** In WebSocket mode Tauri MUST complete control authentication
before business channels. In pipe mode it starts the managed backend, marks the
local IPC boundary authenticated, and opens provider/sync/trigger/remote pipes
without a control socket. A C++ replacement MUST preserve this distinction or
coordinate a new Tauri security flow.

**[MISSING]** No shared generated schema binds these mappings, no Tauri test
executes the C++ service, and the complete initialization/recovery state machine
has not passed end-to-end.

## 14. Deprecation policy

**[REQUIRED]** A change is breaking when it alters canonical bytes, labels,
key/AAD direction, sequence rules, BPIP header/kind meaning, route/method,
required field, close/status behavior, security boundary, or required message
ordering. Breaking changes require a new protocol version, new golden vectors,
and coordinated Python/C++/Tauri tests.

**[REQUIRED]** A server that supports v1 during migration MUST keep the v1
signing identity/pin and exact v1 behavior. Removal of v1 requires evidence that
all supported Tauri releases have migrated and that rollback is defined.

**[OPTIONAL]** Additive fields and endpoints may ship in v1 when old clients
ignore them safely and cryptographic transcripts remain byte-identical.

**[MISSING]** Supported-version advertisement, dual-version tests, minimum
client policy, release overlap duration, and downgrade/rollback drills are not
defined.

## 15. Compliance gates

| Class | Gate | Current evidence |
| --- | --- | --- |
| [REQUIRED] | Normative v1 document and machine-validated examples | This file and `test_protocol_spec.py` |
| [REQUIRED] | BPIP exact bytes, fragmentation, coalescing, malformed/endian/oversize/unknown kind | Golden vectors and C++ CTest |
| [REQUIRED] | Canonical JSON/base64url/control/HKDF/X25519/Ed25519/Argon2 vectors | Python-generated checked-in fixture; C++ crypto not implemented |
| [MISSING] | Deterministic secretstream header/ciphertext cross-language vector | RNG injection/captured fixture absent |
| [MISSING] | Password, remember, epoch, persistent key, expiry, restart vectors | Not covered by current fixture |
| [MISSING] | Complete HTTP route/status/body parity including reset-auth | C++ has an owned readiness provider and `/health` lifecycle, but no real runtime/auth owner wiring or shared route suite, and one route is absent in Python |
| [MISSING] | Provider/sync/trigger/remote shared contract suite | Inventoried only; focused tests incomplete |
| [MISSING] | Bounded queues, overload, timeout, cancellation, load gates | Trigger correlation/output constants, leased-send backpressure, cancellation precedence, send-failure/disconnect cleanup, close-race tests, and BPIP batching exist; transport-wide deadlines, global load policy, live executor propagation, and cross-language load remain absent |
| [MISSING] | Live Windows pipe and Unix socket interoperability/fuzz | Framing unit tests only |
| [MISSING] | C++ WebSocket/auth/secretstream implementation | Not implemented |
| [MISSING] | Windows desktop Tauri end-to-end | Not run |
| [MISSING] | Android native/JNI foreground and emulator smoke | Not implemented/run |
| [MISSING] | Lifecycle, multi-instance, crash/restart, rollback | Not run |
| [MISSING] | Security review and hostile-local-process tests | Not run |

Phase 4 service-protocol work is not complete until every REQUIRED behavior has
an implementation and every MISSING gate required for the supported release is
closed. This specification alone does not satisfy the Phase 4 exit criterion.
