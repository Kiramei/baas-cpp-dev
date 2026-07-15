# Service v1 cryptographic foundation

`BAAS_service_auth_crypto` is the transport-independent C++ foundation for the
BAAS service v1 control handshake, secure JSON envelopes, and authenticated
XChaCha20-Poly1305 streams. It links the
private `BAAS::sodium` target and fails every operation closed when
`sodium_init()` cannot initialize the process-wide runtime.

This target owns cryptographic primitives and deterministic wire bytes. It does
not own password persistence, sessions, cookies, WebSocket state, or business
channel policy.

## Protocol-safe canonical JSON

`CanonicalJsonValue` deliberately accepts a strict subset of JSON:

- null, boolean, UTF-8 string, array, object, and exact integer;
- integers only in `[-9007199254740991, 9007199254740991]`;
- no floating point or exponent spellings;
- decoded object keys must be unique;
- object keys are recursively sorted and output is compact UTF-8;
- input/output bytes, nesting depth, and total value count are bounded.

Invalid UTF-8, unpaired UTF-16 escapes, duplicate escaped keys, unsafe
integers, allocation failure, and output exhaustion have stable fail-closed
results. This value domain is suitable for v1 handshake, AAD, HMAC, and secure
envelope schemas; it is not a general application JSON replacement.

## Encoding and primitive contract

Base64 uses the RFC 4648 URL-safe alphabet and retains canonical `=` padding.
Decode validates the alphabet, padding position, unused bits through canonical
re-encoding, and an optional exact decoded width before returning bytes.

The library exposes the v1 operations required by the checked-in contract:

- SHA-256 and HMAC-SHA256;
- RFC 5869 HKDF-SHA256 with the 255-block output bound;
- X25519 public-key derivation and shared-secret agreement, including
  low-order/all-zero peer rejection;
- Ed25519 public-key derivation, deterministic signing, and verification;
- the exact v1 Argon2id parameters: 32-byte output, 16-byte salt, operations 3,
  and 64 MiB memory;
- IETF ChaCha20-Poly1305 with exact 32-byte key and 12-byte nonce widths;
- constant-time equal-length comparison and explicit secret zeroization.

Derived keys and decrypted raw plaintext use move-only `SecretBuffer` storage,
whose live bytes are overwritten before release. Public API operations convert
allocation failure to `resource_exhausted` rather than continuing with partial
state.

## Secure control envelope

`SecureEnvelopeCipher` requires explicit transmit and receive key arguments and
rejects equal key bytes. It owns independent monotonic send and receive
sequences. For each
direction:

- nonce is the 12-byte big-endian sequence;
- AAD is canonical `{"seq":SEQ,"type":"secure"}`;
- plaintext must be canonical protocol-safe JSON;
- ciphertext is canonical padded base64url in
  `{"ciphertext":...,"seq":SEQ,"type":"secure"}`;
- duplicate, backward, skipped, unsafe, or exhausted sequences fail closed;
- receive sequence advances only after AEAD authentication, JSON validation,
  and byte-for-byte canonical verification all succeed.

Transmit and receive keys remain separate because v1 derives
`preauth:server-tx` and `preauth:server-rx` independently. A production driver
must not substitute one bidirectional key.

## Authenticated secret streams

`SecretStreamPush` and `SecretStreamPull` are move-only, single-direction
owners around libsodium XChaCha20-Poly1305 secretstream state. The public
header does not expose the libsodium state layout. A push owner generates one
public 24-byte header; the peer constructs a pull owner with that header and
the matching 32-byte directional key.

For each record the owner authenticates `aad_prefix || uint64_be(sequence)`.
The sequence begins at zero, belongs to the owner, and advances exactly once
after a successful native operation. Ciphertext is plaintext plus the fixed
17-byte secretstream overhead. Protocol v1 accepts only `MESSAGE` and `FINAL`:

- `FINAL` authenticates and returns its plaintext, then permanently closes the
  direction;
- authenticated `PUSH` or `REKEY` tags are protocol errors and poison the
  receive owner;
- a wrong key, header, prefix, sequence, truncated or modified record, replay,
  reorder, and every other authentication failure permanently poison the
  receive owner;
- a poisoned, closed, exhausted, or moved-from owner never calls libsodium
  again and returns a stable typed error;
- the native state is wiped on destruction and decrypted plaintext uses
  `SecretBuffer`.

Allocation and size validation occur before a native state transition. The
wrapper checks libsodium's message bound before calling it, so oversized input
cannot enter the library's misuse/abort path. Calls are not internally locked;
the owning session executor must serialize each direction.
Output allocation failure leaves the native state and sequence unchanged, so
the owning driver may retry the same operation or terminate the channel.

A graceful protocol close must exchange `FINAL`. Transport EOF or WebSocket
close without an authenticated `FINAL` is an unclean/truncated stream, because
the crypto layer cannot distinguish a deliberately shortened stream by
itself.

## Verification

Configure `BUILD_SERVICE_AUTH_CRYPTO_TESTS=ON` and build
`BAAS_service_auth_crypto_tests`. The C++ executable directly reads
`tests/service_contract/v1_vectors.json` and checks:

- every canonical JSON, base64url, nonce, AAD, SHA/HMAC/HKDF, X25519, Ed25519,
  Argon2id, and deterministic ChaCha envelope byte;
- exact nonce/key/signature widths;
- invalid padding/alphabet/unused bits, wrong key/signature/tag, replay/gap,
  duplicate JSON keys, invalid UTF-8, unsafe integers, and noncanonical secure
  plaintext;
- secretstream widths, empty/multi-record roundtrips, native AAD cross-check,
  wrong key/header/prefix, tamper/truncation, replay/reorder, unexpected tags,
  FINAL closure, poisoning, and move-only state transfer.

The fixture contains one secretstream uint64 metadata value above the JSON safe
integer range. The test loader tags only that non-protocol metadata value; the
production canonical parser remains strict.

## Still incomplete

- persistent signing/password state, epoch changes, tickets, remember cookies,
  TTL, revocation, and an `AuthOwner`;
- control `SessionDriver` and production `SessionFactory` integration;
- deterministic secretstream header/ciphertext cross-language evidence;
- password/secret-bearing schema types with field-specific lifetime policy;
- Tauri/Python live interoperability, restart, hostile-input, and platform
  smoke tests.

Consequently this target proves the reusable cryptographic byte contract, not a
complete authentication or WebSocket protocol migration.
