from __future__ import annotations

import base64
import json
import re
import struct
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = ROOT / "docs" / "script-runtime" / "SERVICE_PROTOCOL_V1.md"
VECTORS_PATH = ROOT / "tests" / "service_contract" / "v1_vectors.json"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "foundation-runtime.yml"
ROUTER_SOURCE_PATH = ROOT / "src" / "service" / "router" / "Router.cpp"
ROUTER_CORE_SPEC_PATH = ROOT / "docs" / "script-runtime" / "SERVICE_ROUTER_CORE.md"


class ServiceProtocolSpecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.spec = SPEC_PATH.read_text(encoding="utf-8")
        cls.vectors = json.loads(VECTORS_PATH.read_text(encoding="utf-8"))
        blocks = re.findall(
            r"```json protocol-example\n(.*?)\n```",
            cls.spec,
            flags=re.DOTALL,
        )
        cls.examples = [json.loads(block) for block in blocks]
        cls.router_source = ROUTER_SOURCE_PATH.read_text(encoding="utf-8")
        cls.router_core_spec = ROUTER_CORE_SPEC_PATH.read_text(encoding="utf-8")

    def test_every_tagged_json_example_is_valid_object(self) -> None:
        self.assertGreaterEqual(len(self.examples), 8)
        for example in self.examples:
            self.assertIsInstance(example, dict)
            self.assertIsInstance(example.get("type"), str)
        self.assertTrue(
            {
                "open",
                "open_ok",
                "client_hello",
                "secure",
                "pull",
                "snapshot",
                "command",
                "command_response",
            }.issubset({example["type"] for example in self.examples})
        )

    def test_open_examples_are_the_committed_golden_payloads(self) -> None:
        pipe_frames = {frame["name"]: frame for frame in self.vectors["pipe"]["frames"]}
        examples = {example["type"]: example for example in self.examples}
        for name in ("open", "open_ok"):
            encoded = json.dumps(examples[name], separators=(",", ":"), ensure_ascii=False)
            self.assertEqual(encoded, pipe_frames[name]["payload_utf8"])

    def test_client_hello_example_has_v1_binary_widths(self) -> None:
        hello = next(example for example in self.examples if example["type"] == "client_hello")
        self.assertEqual(hello["version"], 1)
        self.assertEqual(hello["kind"], "control")
        self.assertEqual(hello["channel"], "control")
        for field in ("client_nonce", "client_kx_pub"):
            self.assertTrue(hello[field].endswith("="))
            self.assertEqual(len(base64.urlsafe_b64decode(hello[field])), 32)

    def test_secure_example_is_a_golden_envelope(self) -> None:
        secure = next(example for example in self.examples if example["type"] == "secure")
        golden = self.vectors["control"]["chacha20poly1305_envelopes"][0]["envelope"]
        self.assertEqual(secure, golden)
        self.assertGreater(len(base64.urlsafe_b64decode(secure["ciphertext"])), 16)

    def test_bpip_constants_match_vectors(self) -> None:
        pipe = self.vectors["pipe"]
        self.assertIn("`42504950010200000004`", self.spec)
        self.assertIn("67108864", self.spec)
        self.assertIn("67108865", self.spec)
        header = bytes.fromhex(pipe["boundary"]["max_header_hex"])
        self.assertEqual(header[:4], b"BPIP")
        self.assertEqual(header[4], 1)
        self.assertEqual(header[5], pipe["kinds"]["bytes"])
        self.assertEqual(struct.unpack_from("<I", header, 6)[0], pipe["boundary"]["limit_bytes"])

    def test_route_and_channel_inventory_is_explicit(self) -> None:
        for route in (
            "/health",
            "/auth/remember",
            "/auth/logout",
            "/system/logs",
            "/system/logs/clear",
            "/android/reset-auth",
            "/android/active-config",
            "/android/toggle",
            "/android/wiki",
            "/android/wiki/proxy",
            "/ws/control",
            "/ws/provider",
            "/ws/sync",
            "/ws/trigger",
            "/ws/remote",
        ):
            self.assertIn(route, self.spec)
        for channel in ("control", "provider", "sync", "trigger", "remote"):
            self.assertRegex(self.spec, rf"`{channel}`")

    def test_router_uses_frozen_unversioned_health_path(self) -> None:
        self.assertIn('health_path = "/health"', self.router_source)
        self.assertNotIn('health_path = "/api/v1/health"', self.router_source)
        self.assertIn("HTTP v1 paths are intentionally unversioned", self.spec)
        self.assertIn("not yet payload-compatible", self.router_core_spec)
        for field in ("statuses", "auth.initialized", "auth.pwd_epoch", "auth.server_sign_public_key"):
            self.assertIn(field, self.router_core_spec)

    def test_required_optional_and_missing_classes_are_present(self) -> None:
        self.assertGreater(self.spec.count("[REQUIRED]"), 35)
        self.assertGreaterEqual(self.spec.count("[OPTIONAL]"), 5)
        self.assertGreater(self.spec.count("[MISSING]"), 20)
        for missing in (
            "secretstream",
            "reset-auth",
            "bounded",
            "end-to-end",
            "Android",
            "Windows named-pipe ACL",
        ):
            self.assertIn(missing, self.spec)

    def test_spec_does_not_claim_phase_or_e2e_completion(self) -> None:
        self.assertIn("does not satisfy the Phase 4 exit criterion", self.spec)
        self.assertNotIn("Phase 4 is complete", self.spec)
        self.assertIn("- [~] Define a versioned service protocol", (
            ROOT / "docs" / "script-runtime" / "ROADMAP.md"
        ).read_text(encoding="utf-8"))

    def test_ci_watches_normative_spec(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        watched_path = "docs/script-runtime/SERVICE_PROTOCOL_V1.md"
        self.assertEqual(workflow.count(watched_path), 2)


if __name__ == "__main__":
    unittest.main()
