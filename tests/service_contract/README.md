# BAAS service v1 golden vectors

`v1_vectors.json` freezes byte-exact behavior observed in the production Python
service and the Tauri clients.  It is deterministic, contains only published
defaults and synthetic test material, and must not be populated from a real
user config or live session.

## Source anchors

The fixture records source commits and SHA-256 hashes for every audited file.
The generator directly calls `baas-dev` implementations for canonical JSON,
base64url, HKDF, Argon2id, ChaCha control envelopes, nonce/AAD construction,
and pipe encoding/decoding.  The unittest independently implements the private
Tauri TypeScript/Rust algorithms and checks the same bytes.

Covered contract points:

- recursively key-sorted compact UTF-8 JSON;
- padded base64url encoding and the Python/Tauri unpadded decode distinction;
- 12-byte big-endian control nonces, canonical AAD, and deterministic control
  envelopes;
- fixed X25519, Ed25519, Argon2id, HMAC, and every v1 HKDF label/context;
- secretstream key derivation, context prefix, and 8-byte big-endian AAD suffix;
- the 10-byte little-endian `BPIP` header, JSON/BYTES/CLOSE/ERROR, `open` and
  `open_ok`, the inclusive 64 MiB boundary, malformed headers, truncation,
  wrong endian, oversize, and the currently accepted unknown-kind behavior.

## Verify or regenerate

From `baas-cpp-dev`, with sibling `baas-dev` and `baas-tauri` checkouts:

```powershell
python scripts/service_contract/generate_vectors.py --check
python -m unittest discover -s tests/service_contract -p "test_*.py" -v
```

Regeneration is intentional and reviewable:

```powershell
python scripts/service_contract/generate_vectors.py
git diff -- tests/service_contract/v1_vectors.json
```

## Missing gate

The fixture intentionally does not claim a deterministic libsodium secretstream
header/ciphertext vector.  Production `init_push` obtains its 24-byte header
from libsodium's internal RNG and neither current reference implementation
offers a deterministic injection point.  The fixture does freeze all keys and
AAD bytes leading into secretstream.  A complete stream vector requires a
test-only deterministic `randombytes` implementation or a captured fixture
verified by both production implementations; inventing one here would not be a
valid compatibility proof.
