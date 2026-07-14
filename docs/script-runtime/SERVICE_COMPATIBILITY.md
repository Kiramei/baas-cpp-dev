# BAAS Service Compatibility Contract

This is the migration compatibility baseline observed in `baas-dev` and
`baas-tauri`. It intentionally describes the existing product contract before a
new C++ service design is allowed to change it.

## Transport topology

- Native Tauri defaults to `pipe`; WebUI requires WebSocket.
- The service still exposes loopback HTTP in pipe mode for health, auth, logs,
  and Android helpers.
- Managed desktop WebSocket uses a random loopback port. Android uses port 8190
  and a fixed Unix socket in application storage.
- Normal WebSocket operation uses control, provider, sync, and trigger sockets,
  plus zero or more remote sockets.
- Pipe operation uses concurrent provider, sync, trigger, and remote connections.

## HTTP surface

| Method | Path | Compatibility purpose |
| --- | --- | --- |
| `GET` | `/health` | readiness and public auth state |
| `POST` | `/auth/remember` | remember-session proof and cookie |
| `POST` | `/auth/logout` | remembered-session logout |
| `GET` | `/system/logs` | structured log file/entry query |
| `POST` | `/system/logs/clear` | clear service logs |
| `POST` | `/android/reset-auth` | Android password reset flow |
| `POST` | `/android/active-config` | notification profile selection |
| `POST` | `/android/toggle` | foreground notification start/stop |
| `GET` | `/android/wiki` and `/android/wiki/proxy` | Android wiki proxy behavior |

The Python route inventory contains nine paths; Tauri additionally calls
`/android/reset-auth`, so that path must be classified against conditional or
missing backend behavior and retained as a compatibility requirement. CORS,
Origin, credentials, and cookie behavior must cover production Tauri origins
and both localhost/127.0.0.1 development origins; an Authorization-header
redesign is not wire compatible.

## WebSocket surface

- `/ws/control`: authenticated long-lived control channel.
- `/ws/provider`: logs, status, initialization state, and version events.
- `/ws/sync`: list/pull/snapshot/patch/patch-conflict configuration sync.
- `/ws/trigger`: timestamp-correlated commands, streaming responses, and paired
  binary payloads.
- `/ws/remote`: scrcpy-compatible binary stream/control proxy.

### Security compatibility gate

The existing protocol uses Ed25519 server identity, X25519 key exchange,
SHA-256/HKDF/HMAC, Argon2id password proof, ChaCha20-Poly1305 secure JSON
envelopes, and libsodium XChaCha20 secret streams for business channels.

Compatibility requires golden vectors for:

- canonical recursive-key-sorted JSON transcripts;
- `client_hello`/signed `server_hello` version 1;
- pre-auth and control HKDF direction labels;
- password initialization/authentication/resume and password epoch changes;
- sequence-number nonce/AAD construction;
- business resume proof, secret-stream headers, AAD, and binary framing;
- pinned signing identity and the existing persistent key migration path.

The default public signing key is compiled into the current frontend. A C++
service cannot silently generate a new identity without a coordinated pin
migration.

## Pipe wire format

- Endpoint is a Windows named pipe or Unix domain socket.
- Frame header is exactly 10 bytes: ASCII `BPIP`, version byte `1`, kind byte,
  and a little-endian `uint32` payload length.
- Payload limit is 64 MiB.
- Kinds: `1` UTF-8 JSON, `2` raw bytes, `3` close, `4` UTF-8 error.
- The first frame opens one logical channel with JSON
  `{type:"open",channel,name}`; service answers `{type:"open_ok",...}`.
- Pipe bypasses WebSocket control authentication/encryption and relies on OS IPC
  access control, but business JSON/binary semantics remain the same.

## Initialization and business gates

The frontend can remain indefinitely in loading state unless the service emits
all required initialization data. Contract tests must cover this order and
allow safe reordering where the frontend supports it:

1. provider and sync connected;
2. non-empty static snapshot;
3. `setup_toml/global` snapshot;
4. configuration list with at least one profile;
5. at least one event snapshot;
6. trigger connected;
7. all-data-initialized status.

Sync preserves unknown JSON fields and scalar types. Patch supports add,
replace, and remove with timestamp conflict snapshots and up to three client
retries.

Trigger includes scheduler start/stop, stop-all, task solve, ADB discovery,
configuration CRUD/import/export, update/CDK/SHA operations, and streaming
update tests. Responses correlate by request timestamp and may complete out of
order. A JSON response that declares binary data must be followed by its binary
frame in global FIFO order.

Remote is disabled on Android. Desktop remote starts with `{config_id,decrypt}`
and then follows the existing scrcpy binary structures and big-endian layouts;
it is not replaceable with a new JSON remote protocol.

## Configuration and persistence compatibility

- Profile `config.json` and scheduler events are extensible JSON objects;
  adapters must preserve unknown fields.
- `setup.toml` schema version 1 and legacy aliases remain part of updater
  compatibility.
- Transport serializes as `websocket` or `pipe` and currently defaults to pipe.
- Auth reset currently knows the filenames `service_auth.json`,
  `service_remembered_logins.json`, `service_signing_key.bin`, and
  `service_ticket.key`.

## Lifecycle blockers for C++ adoption

1. Desktop startup and process-stop validation currently hard-code
   `main.service.py`; executable discovery and PID identity must be migrated.
2. Android startup currently hard-codes the Chaquopy bootstrap; a native/JNI
   service must retain foreground-service, fixed-port, and fixed-socket behavior.
3. `cpp-httplib` can own HTTP endpoints but does not by itself implement the
   required WebSocket, secret-stream, Windows pipe, and Unix socket protocols.
4. The pipe-mode managed HTTP address has an observed persistence gap in the
   current Tauri path and needs an explicit compatibility fix.
5. Service queues must be bounded, cancellation-aware, and per-device
   serialized; existing global state and thread pools are not safe foundations.

## Required contract suites

- Cryptographic golden vectors and Python/C++/Tauri cross-implementation tests.
- Pipe malformed/truncated/oversize/concurrent-connection fuzz tests.
- Complete initialization fixtures and patch conflict/unknown-field tests.
- Table-driven trigger tests, including streaming and binary FIFO behavior.
- Backend kill/restart, transport switch, revocation, backoff, and stale-process
  lifecycle tests.
- Windows dynamic-port/pipe/multi-instance/tray exit smoke tests.
- Android foreground restart, port/socket, notification, accessibility, and
  process-recovery smoke tests.

## Primary source references

- Tauri state and business messages: `src/store/WebsocketStore.ts`.
- Secure WebSocket: `src/shared/SecureWebSocket.ts`.
- Pipe clients/bridge: `src/transport/pipe/TauriPipeConnection.ts` and
  `src-tauri/src/pipe_commands.rs`.
- Desktop lifecycle: `src-tauri/src/commands.rs` and
  `crates/baas-updater/src/environ.rs`.
- Android lifecycle: Kotlin backend/foreground service classes and
  `src-tauri/src/android_backend_service.rs`.
- Python routes/channels: `service/api`, `service/channels`, `service/auth`, and
  `service/transport`.
