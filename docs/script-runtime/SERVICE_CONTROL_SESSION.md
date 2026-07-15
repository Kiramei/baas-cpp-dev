# Service control session

`ControlSessionFactory` implements only the authenticated `/ws/control` v1
state machine. It is deliberately not named or wired as the production session
factory: provider, sync, trigger, and remote still require atomic business
resume plus XChaCha20 secretstream. Installing a control-only factory into the
owner that registers all five routes would turn those required routes into
authentication failures.

## State and ownership

Each driver is single-executor state owned by `WebSocketOwner`:

1. `client_hello` accepts one bounded text object with exact control fields.
2. AuthOwner atomically snapshots public password state, creates the ephemeral
   X25519 exchange, signs the typed canonical transcript internally, and returns
   the server hello plus move-only handshake material. The Ed25519 seed and a
   general-purpose signing operation never leave AuthOwner.
3. The preauth cipher accepts strict contiguous sequences. A valid remember
   cookie produces `auth_ok`; an absent, invalid, or stale token produces
   `resume_unavailable` and exactly one password fallback. Entropy, storage,
   crypto, and capacity failures are not disguised as an invalid cookie.
4. Initialization/authentication resets sequence ownership to the distinct
   control TX/RX keys, subscribes to bounded revocation events, and enters
   streaming only after `auth_ok` encryption succeeds.
5. Streaming accepts exact encrypted `ping` and `change_password` schemas.
   Every input and heartbeat drains revocation first. A password epoch change
   emits encrypted `auth_revoked` before status 4401.

The driver-local deadline is checked before every handshake step and after a
possibly slow Argon2 operation. A session that finishes after the deadline is
removed before `auth_ok` can be published. The shared WebSocket scheduler uses
non-blocking driver-lock acquisition, so one slow derivation cannot delay other
connections' heartbeat and deadline checks.

## Secret and transport boundaries

- `RequestMetadata` transfers by value into the factory. The copied Cookie
  header is overwritten immediately after extracting `baas_remember` into a
  move-only `SecretBuffer`; duplicate cookie names fail closed.
- Parsed password and bearer fields are recursively overwritten. Canonical
  plaintext copies inside `SecureEnvelopeCipher` are also wiped on every return
  path.
- Control disconnect unsubscribes revocation delivery but does not delete a
  published session, matching Python behavior needed by business resume.
- Capacity and derivation-busy outcomes use WebSocket 1013; authentication and
  protocol failures use 4401; internal storage/entropy/crypto failures use 1011.
- Business channels are explicitly rejected by this control-only factory and
  must not be exposed through it in a production host.

## Verification

Configure with `BUILD_SERVICE_CONTROL_SESSION_TESTS=ON`. The deterministic suite
reconstructs the Tauri transcript and verifies the pinned Ed25519 signature,
then covers no-cookie fallback, initialization, password proof, remember resume,
secret disclosure only on remember resume, strict preauth/control sequences,
ping/pong, heartbeat, password revocation, duplicate cookies, entropy failure
classification, late-Argon session rollback, and disconnect lifetime. The
WebSocket owner suite separately proves that a busy driver does not starve a
healthy connection's heartbeat.
