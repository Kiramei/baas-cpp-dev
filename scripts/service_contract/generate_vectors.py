#!/usr/bin/env python3
"""Generate deterministic BAAS service v1 wire-contract vectors.

The generator deliberately imports the production Python implementation from a
neighbouring baas-dev checkout.  Tauri's private TypeScript/Rust helpers are
cross-checked by the repository-local unittest using independent equivalents.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import json
import struct
import subprocess
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
FIXTURE = REPO_ROOT / "tests" / "service_contract" / "v1_vectors.json"


def _find_checkout(name: str) -> Path:
    for parent in (REPO_ROOT, *REPO_ROOT.parents):
        candidate = parent / name
        if candidate.is_dir():
            return candidate
    raise FileNotFoundError(f"could not locate neighbouring {name} checkout")


def _commit(path: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
    ).strip()


def _sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("ascii")


def _hex(data: bytes) -> str:
    return data.hex()


def generate(baas_dev: Path, baas_tauri: Path) -> dict[str, Any]:
    # Import the actual v1 production functions.  These imports do not create a
    # ServiceAuthManager, touch config, or start service threads.
    sys.path.insert(0, str(baas_dev))
    try:
        from service.auth.channels import JsonChaChaChannel
        from service.auth.constants import (
            ARGON2_HASH_BYTES,
            ARGON2_MEMLIMIT,
            ARGON2_OPSLIMIT,
            DEFAULT_SERVER_SIGN_PUBLIC_KEY_B64,
        )
        from service.auth.crypto import (
            argon2,
            b64d,
            b64e,
            canonical_dumps,
            hkdf_sha256,
            hmac_sha256,
            session_nonce,
        )
        from service.transport.framing import (
            HEADER,
            KIND_BYTES,
            KIND_CLOSE,
            KIND_ERROR,
            KIND_JSON,
            MAGIC,
            MAX_PAYLOAD_BYTES,
            VERSION,
            FrameDecoder,
            encode_frame,
            encode_json,
        )
    finally:
        sys.path.pop(0)

    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey

    canonical_inputs = [
        {
            "name": "recursive_unicode",
            "value": {
                "z": 0,
                "a": {"beta": [3, {"z": False, "a": "蓝色档案"}], "alpha": None},
                "m": "BAAS/v1",
            },
        },
        {
            "name": "arrays_preserve_order",
            "value": {"outer": [{"z": 1, "a": 2}, {"d": 4, "c": 3}], "empty": {}},
        },
    ]
    canonical = [
        {
            **item,
            "utf8": canonical_dumps(item["value"]).decode("utf-8"),
            "hex": _hex(canonical_dumps(item["value"])),
        }
        for item in canonical_inputs
    ]

    base64_cases = []
    for raw in (b"", b"\xff", b"\xfb\xef", b"\xfb\xef\xff", b"\x00\x01\x02\x03"):
        padded = b64e(raw)
        unpadded = padded.rstrip("=")
        try:
            b64d(unpadded)
            python_unpadded = True
        except Exception:  # production intentionally delegates strict padding to stdlib
            python_unpadded = False
        base64_cases.append(
            {
                "raw_hex": _hex(raw),
                "encoded": padded,
                "unpadded": unpadded,
                "padding_chars": len(padded) - len(unpadded),
                "python_decoder_accepts_unpadded": python_unpadded,
                "tauri_decoder_accepts_unpadded": True,
            }
        )

    sequences = []
    for seq in (0, 1, 0x010203040506, 0x001FFFFFFFFFFFFF):
        aad = canonical_dumps({"seq": seq, "type": "secure"})
        sequences.append({"seq": seq, "nonce_hex": _hex(session_nonce(seq)), "aad_utf8": aad.decode()})

    client_private = X25519PrivateKey.from_private_bytes(bytes(range(32)))
    server_private = X25519PrivateKey.from_private_bytes(bytes(range(31, -1, -1)))
    client_public = client_private.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw
    )
    server_public = server_private.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw
    )
    shared = client_private.exchange(server_private.public_key())
    assert shared == server_private.exchange(client_private.public_key())

    client_hello = {
        "type": "client_hello",
        "kind": "control",
        "channel": "control",
        "version": 1,
        "timestamp": 1700000000123,
        "client_nonce": _b64(bytes(range(32, 64))),
        "client_kx_pub": _b64(client_public),
    }
    server_core = {
        "type": "server_hello",
        "kind": "control",
        "channel": "control",
        "version": 1,
        "server_nonce": _b64(bytes(range(64, 96))),
        "server_kx_pub": _b64(server_public),
        "initialized": True,
        "pwd_epoch": 7,
        "pwd_salt": _b64(bytes(range(16))),
        "argon2": {
            "algorithm": "argon2id",
            "opslimit": ARGON2_OPSLIMIT,
            "memlimit": ARGON2_MEMLIMIT,
            "salt_bytes": 16,
            "hash_bytes": ARGON2_HASH_BYTES,
        },
    }
    transcript_obj = {
        "kind": "control",
        "channel": "control",
        "client": client_hello,
        "server": server_core,
    }
    transcript = canonical_dumps(transcript_obj)
    transcript_hash = hashlib.sha256(transcript).digest()

    # Never copy a configured or production signing seed into a fixture.  This
    # synthetic seed is conspicuous test material; the real compatibility pin
    # is represented only by its already-public public key below.
    signing_seed = bytes(range(0xA0, 0xC0))
    signing_key = Ed25519PrivateKey.from_private_bytes(signing_seed)
    signing_public = signing_key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw
    )
    signature = signing_key.sign(transcript)
    signing_key.public_key().verify(signature, transcript)

    password = "BAAS-v1-vector"
    password_salt = bytes(range(16))
    password_key = argon2(password, password_salt)

    session_id = "00000000-1111-4222-8333-444444444444"
    socket_id = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"
    channel = "trigger"
    pwd_epoch = 7
    control_salt = hashlib.sha256(session_id.encode()).digest()

    derivations: list[dict[str, Any]] = []

    def derive(name: str, ikm: bytes, info: bytes, length: int, salt: bytes) -> bytes:
        output = hkdf_sha256(ikm, info, length, salt)
        derivations.append(
            {
                "name": name,
                "ikm_hex": _hex(ikm),
                "salt_hex": _hex(salt),
                "info_utf8": info.decode("utf-8"),
                "length": length,
                "output_hex": _hex(output),
            }
        )
        return output

    preauth_tx = derive("preauth_server_tx", shared, b"preauth:server-tx", 32, transcript_hash)
    preauth_rx = derive("preauth_server_rx", shared, b"preauth:server-rx", 32, transcript_hash)
    derive("auth_proof_epoch_7", shared, b"auth-proof:7", 32, transcript_hash)
    master_secret = derive(
        "master_secret", shared + password_key, b"master-secret", 32, transcript_hash
    )
    resume_secret = derive("resume_secret", master_secret, b"resume-secret", 32, transcript_hash)
    derive("control_server_tx", master_secret, b"control:server-tx", 32, control_salt)
    derive("control_server_rx", master_secret, b"control:server-rx", 32, control_salt)

    resume_context_obj = {
        "transcript_hash": _b64(transcript_hash),
        "session_id": session_id,
        "socket_id": socket_id,
        "channel": channel,
        "pwd_epoch": pwd_epoch,
    }
    scope_obj = {
        "scope": "ws",
        "session_id": session_id,
        "socket_id": socket_id,
        "channel": channel,
        "pwd_epoch": pwd_epoch,
    }
    stream_context_obj = {
        "session_id": session_id,
        "socket_id": socket_id,
        "channel": channel,
        "pwd_epoch": pwd_epoch,
    }
    business_base = derive(
        "business_base", master_secret, canonical_dumps(scope_obj), 64, transcript_hash
    )
    derive(
        "secretstream_server_tx",
        business_base[:32],
        b"secretstream:server-tx",
        32,
        transcript_hash,
    )
    derive(
        "secretstream_server_rx",
        business_base[32:],
        b"secretstream:server-rx",
        32,
        transcript_hash,
    )

    remember_context = canonical_dumps(
        {"type": "remember_session", "session_id": session_id, "pwd_epoch": pwd_epoch}
    )
    resume_context = canonical_dumps(resume_context_obj)
    stream_prefix = canonical_dumps(stream_context_obj)

    control_channel = JsonChaChaChannel(tx_key=preauth_tx, rx_key=preauth_tx)
    control_envelopes = []
    for payload in (
        {"type": "initialize", "password": "example-only"},
        {"type": "nested", "value": {"z": 9, "a": "档案"}},
    ):
        envelope = control_channel.encrypt(payload)
        control_envelopes.append({"payload": payload, "envelope": envelope})

    pipe_messages = [
        ("open", KIND_JSON, encode_json({"type": "open", "channel": "trigger", "name": "trigger"})),
        ("open_ok", KIND_JSON, encode_json({"type": "open_ok", "channel": "trigger"})),
        ("json", KIND_JSON, encode_json({"type": "ping", "value": "蓝"})),
        ("bytes", KIND_BYTES, encode_frame(KIND_BYTES, bytes.fromhex("0001feff"))),
        ("close", KIND_CLOSE, encode_frame(KIND_CLOSE)),
        ("error", KIND_ERROR, encode_frame(KIND_ERROR, b"invalid request")),
    ]
    pipe_frames = []
    for name, kind, frame in pipe_messages:
        decoded = FrameDecoder().feed(frame)
        assert decoded == [(kind, frame[HEADER.size :])]
        pipe_frames.append(
            {
                "name": name,
                "kind": kind,
                "payload_utf8": frame[HEADER.size :].decode("utf-8") if kind in (KIND_JSON, KIND_ERROR) else None,
                "payload_hex": _hex(frame[HEADER.size :]),
                "header_hex": _hex(frame[: HEADER.size]),
                "frame_hex": _hex(frame),
            }
        )

    max_payload = b"\0" * MAX_PAYLOAD_BYTES
    max_frame = encode_frame(KIND_BYTES, max_payload)
    max_boundary = {
        "limit_bytes": MAX_PAYLOAD_BYTES,
        "max_header_hex": _hex(max_frame[: HEADER.size]),
        "max_payload_sha256": hashlib.sha256(max_payload).hexdigest(),
        "max_frame_sha256": hashlib.sha256(max_frame).hexdigest(),
        "oversize_bytes": MAX_PAYLOAD_BYTES + 1,
        "oversize_error": "Pipe payload exceeds the 64 MiB limit",
    }
    try:
        encode_frame(KIND_BYTES, b"\0" * (MAX_PAYLOAD_BYTES + 1))
        raise AssertionError("production encoder accepted an oversized payload")
    except ValueError as exc:
        assert str(exc) == max_boundary["oversize_error"]

    malformed = [
        {
            "name": "truncated_header",
            "hex": _hex(b"BPIP\x01\x01\x00\x00\x00"),
            "expected": "pending",
        },
        {
            "name": "truncated_payload",
            "hex": _hex(HEADER.pack(MAGIC, VERSION, KIND_BYTES, 4) + b"\x00\x01"),
            "expected": "pending",
        },
        {
            "name": "invalid_magic",
            "hex": _hex(HEADER.pack(b"NOPE", VERSION, KIND_JSON, 0)),
            "expected_error": "Invalid pipe frame magic",
        },
        {
            "name": "invalid_version",
            "hex": _hex(HEADER.pack(MAGIC, 2, KIND_JSON, 0)),
            "expected_error": "Unsupported pipe protocol version: 2",
        },
        {
            "name": "big_endian_length_0x01020304",
            "hex": _hex(MAGIC + bytes((VERSION, KIND_JSON)) + struct.pack(">I", 0x01020304)),
            "decoded_little_endian_length": 0x04030201,
            "expected_error": "Pipe payload exceeds the 64 MiB limit",
        },
        {
            "name": "unknown_kind_is_transport_accepted",
            "hex": _hex(HEADER.pack(MAGIC, VERSION, 255, 0)),
            "expected_kind": 255,
            "expected_payload_hex": "",
        },
    ]
    for case in malformed:
        decoder = FrameDecoder()
        try:
            output = decoder.feed(bytes.fromhex(case["hex"]))
        except ValueError as exc:
            assert str(exc) == case["expected_error"]
        else:
            if case.get("expected") == "pending":
                assert output == []
            elif "expected_kind" in case:
                assert output == [(case["expected_kind"], bytes.fromhex(case["expected_payload_hex"]))]

    source_files = {
        "baas-dev": [
            "service/auth/crypto.py",
            "service/auth/channels.py",
            "service/auth/constants.py",
            "service/auth/manager.py",
            "service/transport/framing.py",
            "service/transport/pipe_server.py",
        ],
        "baas-tauri": [
            "src/shared/SecureWebSocket.ts",
            "src-tauri/src/pipe_commands.rs",
            "src/transport/pipe/TauriPipeConnection.ts",
        ],
    }
    sources = {}
    for name, root in (("baas-dev", baas_dev), ("baas-tauri", baas_tauri)):
        sources[name] = {
            "commit": _commit(root),
            "files": [
                {"path": path, "sha256": _sha256_file(root / path)} for path in source_files[name]
            ],
        }

    return {
        "schema": "baas.service-contract.v1",
        "deterministic": True,
        "contains_secrets": False,
        "sources": sources,
        "canonical_json": canonical,
        "base64url": {
            "encoder_retains_rfc4648_padding": True,
            "cases": base64_cases,
        },
        "control": {
            "nonce_layout": "12-byte big-endian unsigned sequence",
            "aad_shape": {"seq": "number", "type": "secure"},
            "sequences": sequences,
            "chacha20poly1305_envelopes": control_envelopes,
        },
        "handshake_crypto": {
            "client_private_hex": _hex(bytes(range(32))),
            "client_public_hex": _hex(client_public),
            "server_private_hex": _hex(bytes(range(31, -1, -1))),
            "server_public_hex": _hex(server_public),
            "shared_secret_hex": _hex(shared),
            "transcript_object": transcript_obj,
            "transcript_utf8": transcript.decode("utf-8"),
            "transcript_sha256": _hex(transcript_hash),
            "synthetic_test_signing_seed_hex": _hex(signing_seed),
            "synthetic_test_signing_public_b64url": _b64(signing_public),
            "production_pinned_signing_public_b64url": DEFAULT_SERVER_SIGN_PUBLIC_KEY_B64,
            "ed25519_signature_b64url": _b64(signature),
            "argon2id": {
                "password": password,
                "salt_hex": _hex(password_salt),
                "opslimit": ARGON2_OPSLIMIT,
                "memlimit": ARGON2_MEMLIMIT,
                "output_bytes": ARGON2_HASH_BYTES,
                "output_hex": _hex(password_key),
            },
        },
        "hkdf_sha256": {
            "derivations": derivations,
            "control_session_id": session_id,
            "control_salt_sha256_hex": _hex(control_salt),
        },
        "contexts": {
            "remember_session_utf8": remember_context.decode(),
            "remember_proof_hmac_sha256_hex": _hex(hmac_sha256(resume_secret, remember_context)),
            "business_resume_utf8": resume_context.decode(),
            "business_resume_hmac_sha256_hex": _hex(hmac_sha256(resume_secret, resume_context)),
            "business_scope_utf8": canonical_dumps(scope_obj).decode(),
            "stream_aad_prefix_utf8": stream_prefix.decode(),
            "stream_aad": [
                {"seq": seq, "hex": _hex(stream_prefix + seq.to_bytes(8, "big"))}
                for seq in (0, 1, 0x0102030405060708)
            ],
        },
        "pipe": {
            "magic_ascii": "BPIP",
            "version": VERSION,
            "header_bytes": HEADER.size,
            "length_byte_order": "little",
            "kinds": {"json": KIND_JSON, "bytes": KIND_BYTES, "close": KIND_CLOSE, "error": KIND_ERROR},
            "frames": pipe_frames,
            "boundary": max_boundary,
            "malformed": malformed,
        },
        "missing_gates": [
            {
                "name": "secretstream_header_and_ciphertext",
                "reason": "Production crypto_secretstream_xchacha20poly1305_init_push obtains a random 24-byte header internally; the current APIs expose no deterministic RNG/header injection. Key derivation and exact AAD bytes are fixed above, but a purported reproducible header/ciphertext vector would be fabricated.",
                "required_future_gate": "Add a test-only deterministic libsodium randombytes implementation or an independently captured cross-language fixture with a verifier in both Python and Tauri.",
            }
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baas-dev", type=Path, default=None)
    parser.add_argument("--baas-tauri", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=FIXTURE)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    baas_dev = (args.baas_dev or _find_checkout("baas-dev")).resolve()
    baas_tauri = (args.baas_tauri or _find_checkout("baas-tauri")).resolve()
    rendered = json.dumps(generate(baas_dev, baas_tauri), ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.check:
        current = args.output.read_text(encoding="utf-8")
        if current != rendered:
            print(f"stale service-contract fixture: {args.output}", file=sys.stderr)
            return 1
        print(f"verified {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8", newline="\n")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
