# Service authentication owner

`AuthOwner` is the single process owner for service-v1 password state, control
sessions, resume tickets, remembered logins, and password-revocation events.
It is transport independent: the WebSocket control driver and HTTP auth routes
consume it, but cpp-httplib callbacks never own authentication state.

## Persistent state

Production storage maps a fixed `AuthFile` enum below `<project_root>/config`:

| File | Contents | Lifetime |
| --- | --- | --- |
| `service_auth.json` | Argon2id salt/verifier and password epoch | Until password rotation/reset |
| `service_ticket.key` | 32-byte HMAC key | Per installation |
| `service_remembered_logins.json` | Hashed remember tokens, expiry, epoch | At most 180 days by default |
| `service_signing_key.bin` | 32-byte Ed25519 seed | Per signing-identity policy |

Production storage holds an exclusive installation lock for its lifetime, so
two service processes cannot create divergent first-run keys. Reads receive
`max_file_bytes` and reject oversized regular files before allocating their
payload. Windows uses non-reparse handles and a protected service-file DACL;
POSIX uses an anchored directory descriptor, `openat(O_NOFOLLOW)`, and mode
`0600`. Writes use an unpredictable same-directory file opened with
create-exclusive semantics, a file flush, atomic replacement, and a best-effort
directory metadata flush. Password rotation clears the remember-token file
before replacing password state; a partial storage failure therefore fails
closed to fewer bearer tokens and does not mutate live state.

Python wrote remembered-login timestamps as JSON doubles. The C++ loader
accepts only the known non-negative `created_at` and `expires_at` decimal fields,
floors fractional seconds, then returns to the strict integer-only canonical
JSON domain. New C++ files contain integer Unix seconds.

## Secret and concurrency boundaries

- Passwords, password verifiers, signing/ticket keys, session master/resume
  secrets, and bearer-token bytes use move-only `SecretBuffer` storage and are
  overwritten before release.
- The production password is limited to 1,024 UTF-8 bytes and cannot be empty
  or Python-`str.strip()`-equivalent Unicode-whitespace-only.
- Argon2id uses the frozen v1 parameters (`opslimit=3`, `memlimit=64 MiB`,
  32-byte output). A non-blocking gate admits one derivation by default, and no
  owner state mutex remains held during derivation.
- Session, remembered-login, subscription, event-queue, token, identifier, and
  persistent-file sizes are independently bounded.
- Sessions expire after 12 hours and remembered logins after 180 days by
  default. Expired or stale-epoch entries are pruned before use.

The default Ed25519 seed deliberately preserves the public key pinned by the
current `baas-tauri` client. This is an explicit compatibility policy, not a
recommended multi-installation identity model. A random-per-install policy is
available, but deploying it requires rebuilding or configuring the frontend
with the matching public key. An incompatible legacy signing file is repaired
to the pinned seed under compatibility policy. Explicit dependency injection,
`BAAS_SERVICE_SIGN_SEED_B64`, and `BAAS_SERVICE_TICKET_KEY_B64` override files
with the same precedence as the Python service.

## Protocol ownership

Password authentication verifies
`HMAC-SHA256(password_verifier, HKDF(shared_key, transcript_hash,
"auth-proof:<epoch>"))`. A successful session derives `master-secret`,
`resume-secret`, and distinct `control:server-tx` / `control:server-rx` keys.
The only proof-free session path is `initialize_control`, which first wins the
one-shot uninitialized-to-initialized transition; there is no reusable public
"open after initialize" method.

Resume tickets are canonical JSON plus HMAC and remain valid only while their
in-memory session is live. Remember tokens use `v1.<token-id>.<secret>`; only an
HMAC of the token id and secret is persisted. `/auth/logout` must call
`logout_remember_token`, so logout revokes server-side state instead of merely
clearing a browser cookie.

`begin_business_handshake` accepts only the typed `provider`, `sync`, `trigger`,
and `remote` channels. It builds the signed v1 `kind=resume` transcript inside
the owner. The transcript includes every bearer-bound client field: timestamp,
nonces and KX key, session id, socket id, and resume ticket. The signing seed is
therefore never exposed as a general signing oracle. The result is a move-only,
non-default-constructible capability: only `AuthOwner` can set its private
channel/session/socket/transcript/ticket binding. Callers can inspect the
preauth `HandshakeMaterial` through a const accessor and move out the server
hello JSON, but cannot mutate or reconstruct the resume context.

`resume_business` is one mutex-linearized operation. It authenticates the
ticket, checks the live session epoch and expiry, verifies the resume MAC over
the canonical channel/session/socket/transcript context, derives the canonical
`scope=ws` business base, then derives only the final
`secretstream:server-tx` and `secretstream:server-rx` keys and the canonical AAD
prefix. It does not instantiate or depend on a secretstream transport class.
The same operation installs a bounded revocation subscription before returning.
If password rotation wins the mutex, resume cannot succeed; if resume wins,
the installed subscription receives the later revocation. Callers own the
returned subscription id and must unsubscribe it when setup fails or the
business connection closes. Business resume never creates a new control
session or extends its TTL. Its API consumes the capability plus only the
client's move-only resume MAC; it accepts no duplicate channel, identifier,
ticket, or transcript fields that could be substituted after the signed hello.

Password change/reset increments the epoch, removes all sessions and remembered
logins, and places a bounded `auth_revoked` event on every control subscription.
`ControlSessionFactory` drains these events before each authenticated input and
on its serialized heartbeat, emits encrypted `auth_revoked`, and closes with
service-v1 authentication status `4401`.

## Verification

Configure with `BUILD_SERVICE_AUTH_OWNER_TESTS=ON`. The owner suite covers
restart persistence, the Tauri-pinned signing identity, real Argon2 integration,
proof and ticket tampering, remember/resume/logout, password revocation,
capacity and expiry, injected storage/entropy failures, concurrent derivation
admission, Python timestamp migration, and production file-size enforcement.
It also loads the existing v1 vectors for the exact business resume MAC, scope,
final directional keys, and AAD prefix; covers all four typed handshakes; and
checks bad MAC/ticket/channel, subscription capacity, expiry, revocation, and
the resume-versus-password-rotation mutex boundary.
The service-auth workflow builds and tests Debug and Release on Windows, Linux,
and macOS, and cross-builds the owner for Android arm64-v8a and x86_64.
