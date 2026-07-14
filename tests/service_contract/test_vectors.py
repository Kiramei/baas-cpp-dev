from __future__ import annotations

import base64
import hashlib
import hmac
import json
import struct
import subprocess
import sys
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
VECTORS = json.loads((HERE / "v1_vectors.json").read_text(encoding="utf-8"))


def canonical(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()


def hkdf_sha256(ikm: bytes, info: bytes, length: int, salt: bytes) -> bytes:
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    output = bytearray()
    previous = b""
    counter = 1
    while len(output) < length:
        previous = hmac.new(prk, previous + info + bytes((counter,)), hashlib.sha256).digest()
        output.extend(previous)
        counter += 1
    return bytes(output[:length])


def decode_pipe(frame: bytes) -> tuple[int, bytes] | None:
    if len(frame) < 10:
        return None
    if frame[:4] != b"BPIP":
        raise ValueError("Invalid pipe frame magic")
    if frame[4] != 1:
        raise ValueError(f"Unsupported pipe protocol version: {frame[4]}")
    length = struct.unpack_from("<I", frame, 6)[0]
    if length > 64 * 1024 * 1024:
        raise ValueError("Pipe payload exceeds the 64 MiB limit")
    if len(frame) < 10 + length:
        return None
    if len(frame) != 10 + length:
        raise ValueError("trailing bytes")
    return frame[5], frame[10:]


class VectorShapeTests(unittest.TestCase):
    def test_manifest_and_missing_gate_are_explicit(self) -> None:
        self.assertEqual(VECTORS["schema"], "baas.service-contract.v1")
        self.assertTrue(VECTORS["deterministic"])
        self.assertFalse(VECTORS["contains_secrets"])
        self.assertEqual(
            [gate["name"] for gate in VECTORS["missing_gates"]],
            ["secretstream_header_and_ciphertext"],
        )
        for source in VECTORS["sources"].values():
            self.assertRegex(source["commit"], r"^[0-9a-f]{40}$")
            for item in source["files"]:
                self.assertRegex(item["sha256"], r"^[0-9a-f]{64}$")

    def test_canonical_recursive_key_sorting(self) -> None:
        for case in VECTORS["canonical_json"]:
            encoded = canonical(case["value"])
            self.assertEqual(encoded.decode(), case["utf8"], case["name"])
            self.assertEqual(encoded.hex(), case["hex"], case["name"])

    def test_base64url_padding_contract(self) -> None:
        self.assertTrue(VECTORS["base64url"]["encoder_retains_rfc4648_padding"])
        for case in VECTORS["base64url"]["cases"]:
            raw = bytes.fromhex(case["raw_hex"])
            encoded = base64.urlsafe_b64encode(raw).decode("ascii")
            self.assertEqual(encoded, case["encoded"])
            self.assertEqual(encoded.rstrip("="), case["unpadded"])
            self.assertEqual(encoded.count("="), case["padding_chars"])
            padded = case["unpadded"] + "=" * ((4 - len(case["unpadded"]) % 4) % 4)
            self.assertEqual(base64.urlsafe_b64decode(padded), raw)

    def test_control_nonce_and_aad(self) -> None:
        for case in VECTORS["control"]["sequences"]:
            seq = case["seq"]
            self.assertEqual(seq.to_bytes(12, "big").hex(), case["nonce_hex"])
            self.assertEqual(canonical({"seq": seq, "type": "secure"}).decode(), case["aad_utf8"])

    def test_all_hkdf_derivations(self) -> None:
        names = set()
        for case in VECTORS["hkdf_sha256"]["derivations"]:
            names.add(case["name"])
            actual = hkdf_sha256(
                bytes.fromhex(case["ikm_hex"]),
                case["info_utf8"].encode(),
                case["length"],
                bytes.fromhex(case["salt_hex"]),
            )
            self.assertEqual(actual.hex(), case["output_hex"], case["name"])
        self.assertEqual(
            names,
            {
                "preauth_server_tx",
                "preauth_server_rx",
                "auth_proof_epoch_7",
                "master_secret",
                "resume_secret",
                "control_server_tx",
                "control_server_rx",
                "business_base",
                "secretstream_server_tx",
                "secretstream_server_rx",
            },
        )

    def test_contexts_and_big_endian_stream_sequence(self) -> None:
        contexts = VECTORS["contexts"]
        derivations = {
            item["name"]: bytes.fromhex(item["output_hex"])
            for item in VECTORS["hkdf_sha256"]["derivations"]
        }
        self.assertEqual(
            hmac.new(
                derivations["resume_secret"], contexts["remember_session_utf8"].encode(), hashlib.sha256
            ).hexdigest(),
            contexts["remember_proof_hmac_sha256_hex"],
        )
        self.assertEqual(
            hmac.new(
                derivations["resume_secret"], contexts["business_resume_utf8"].encode(), hashlib.sha256
            ).hexdigest(),
            contexts["business_resume_hmac_sha256_hex"],
        )
        prefix = contexts["stream_aad_prefix_utf8"].encode()
        for case in contexts["stream_aad"]:
            self.assertEqual((prefix + case["seq"].to_bytes(8, "big")).hex(), case["hex"])


class CryptoVectorTests(unittest.TestCase):
    def test_x25519_shared_secret_and_ed25519_signature(self) -> None:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey

        vector = VECTORS["handshake_crypto"]
        client = X25519PrivateKey.from_private_bytes(bytes.fromhex(vector["client_private_hex"]))
        server = X25519PrivateKey.from_private_bytes(bytes.fromhex(vector["server_private_hex"]))
        self.assertEqual(
            client.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex(),
            vector["client_public_hex"],
        )
        self.assertEqual(client.exchange(server.public_key()).hex(), vector["shared_secret_hex"])

        transcript = vector["transcript_utf8"].encode()
        self.assertEqual(canonical(vector["transcript_object"]), transcript)
        self.assertEqual(hashlib.sha256(transcript).hexdigest(), vector["transcript_sha256"])
        signing = Ed25519PrivateKey.from_private_bytes(
            bytes.fromhex(vector["synthetic_test_signing_seed_hex"])
        )
        signature = base64.urlsafe_b64decode(vector["ed25519_signature_b64url"])
        signing.public_key().verify(signature, transcript)

    def test_argon2id(self) -> None:
        from nacl import pwhash

        case = VECTORS["handshake_crypto"]["argon2id"]
        output = pwhash.argon2id.kdf(
            case["output_bytes"],
            case["password"].encode(),
            bytes.fromhex(case["salt_hex"]),
            opslimit=case["opslimit"],
            memlimit=case["memlimit"],
        )
        self.assertEqual(output.hex(), case["output_hex"])

    def test_control_envelopes_decrypt(self) -> None:
        from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

        key = next(
            bytes.fromhex(item["output_hex"])
            for item in VECTORS["hkdf_sha256"]["derivations"]
            if item["name"] == "preauth_server_tx"
        )
        cipher = ChaCha20Poly1305(key)
        for case in VECTORS["control"]["chacha20poly1305_envelopes"]:
            envelope = case["envelope"]
            seq = envelope["seq"]
            plaintext = cipher.decrypt(
                seq.to_bytes(12, "big"),
                base64.urlsafe_b64decode(envelope["ciphertext"]),
                canonical({"seq": seq, "type": "secure"}),
            )
            self.assertEqual(json.loads(plaintext), case["payload"])


class PipeVectorTests(unittest.TestCase):
    def test_header_and_all_frame_kinds(self) -> None:
        pipe = VECTORS["pipe"]
        self.assertEqual(pipe["header_bytes"], 10)
        self.assertEqual(pipe["length_byte_order"], "little")
        self.assertEqual(pipe["kinds"], {"json": 1, "bytes": 2, "close": 3, "error": 4})
        for case in pipe["frames"]:
            frame = bytes.fromhex(case["frame_hex"])
            self.assertEqual(frame[:10].hex(), case["header_hex"])
            decoded = decode_pipe(frame)
            self.assertIsNotNone(decoded)
            kind, payload = decoded  # type: ignore[misc]
            self.assertEqual(kind, case["kind"])
            self.assertEqual(payload.hex(), case["payload_hex"])

    def test_open_and_open_ok_payloads(self) -> None:
        frames = {item["name"]: item for item in VECTORS["pipe"]["frames"]}
        self.assertEqual(
            json.loads(frames["open"]["payload_utf8"]),
            {"type": "open", "channel": "trigger", "name": "trigger"},
        )
        self.assertEqual(
            json.loads(frames["open_ok"]["payload_utf8"]),
            {"type": "open_ok", "channel": "trigger"},
        )

    def test_64_mib_boundary_without_large_fixture_blob(self) -> None:
        case = VECTORS["pipe"]["boundary"]
        self.assertEqual(case["limit_bytes"], 64 * 1024 * 1024)
        header = bytes.fromhex(case["max_header_hex"])
        self.assertEqual(header, b"BPIP" + bytes((1, 2)) + struct.pack("<I", case["limit_bytes"]))
        payload_hash = hashlib.sha256()
        frame_hash = hashlib.sha256(header)
        chunk = b"\0" * (1024 * 1024)
        for _ in range(64):
            payload_hash.update(chunk)
            frame_hash.update(chunk)
        self.assertEqual(payload_hash.hexdigest(), case["max_payload_sha256"])
        self.assertEqual(frame_hash.hexdigest(), case["max_frame_sha256"])
        with self.assertRaisesRegex(ValueError, "64 MiB"):
            decode_pipe(b"BPIP" + bytes((1, 2)) + struct.pack("<I", case["oversize_bytes"]))

    def test_malformed_truncated_endian_and_unknown_kind(self) -> None:
        for case in VECTORS["pipe"]["malformed"]:
            raw = bytes.fromhex(case["hex"])
            if "expected_error" in case:
                with self.assertRaisesRegex(ValueError, case["expected_error"]):
                    decode_pipe(raw)
            elif case.get("expected") == "pending":
                self.assertIsNone(decode_pipe(raw))
            else:
                self.assertEqual(
                    decode_pipe(raw),
                    (case["expected_kind"], bytes.fromhex(case["expected_payload_hex"])),
                )


class ProductionParityTests(unittest.TestCase):
    def test_checked_in_fixture_regenerates_from_production_source(self) -> None:
        command = [sys.executable, str(ROOT / "scripts" / "service_contract" / "generate_vectors.py"), "--check"]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if result.returncode != 0 and "could not locate neighbouring" in result.stderr:
            self.skipTest("neighbouring baas-dev/baas-tauri checkouts are unavailable")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
