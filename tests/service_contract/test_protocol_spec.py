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
HTTP_HOST_SPEC_PATH = ROOT / "docs" / "script-runtime" / "SERVICE_HTTPLIB_ADAPTER.md"
HTTP_HOST_SOURCE_PATH = ROOT / "src" / "service" / "http" / "HttpHost.cpp"
ORIGIN_POLICY_SPEC_PATH = ROOT / "docs" / "script-runtime" / "SERVICE_ORIGIN_POLICY.md"
HEALTH_READINESS_SPEC_PATH = ROOT / "docs" / "script-runtime" / "SERVICE_HEALTH_READINESS.md"


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
        cls.http_host_spec = HTTP_HOST_SPEC_PATH.read_text(encoding="utf-8")
        cls.http_host_source = HTTP_HOST_SOURCE_PATH.read_text(encoding="utf-8")
        cls.origin_policy_spec = ORIGIN_POLICY_SPEC_PATH.read_text(encoding="utf-8")
        cls.health_readiness_spec = HEALTH_READINESS_SPEC_PATH.read_text(encoding="utf-8")

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
        self.assertIn("body now follows the observed Python field shape", self.router_core_spec)
        self.assertIn("with_health_snapshot()", self.router_core_spec)
        self.assertIn("with_health_provider()", self.router_core_spec)
        self.assertIn("does not derive readiness", self.router_core_spec)
        for field in ("statuses", "auth.initialized", "auth.pwd_epoch", "auth.server_sign_public_key"):
            self.assertIn(field, self.router_core_spec)
        for state in ("starting", "ready", "failed"):
            self.assertIn(state, self.health_readiness_spec)
        for code in (
            "health_starting",
            "health_failed",
            "health_provider_failed",
            "invalid_health_snapshot",
            "response_too_large",
        ):
            self.assertIn(code, self.health_readiness_spec)

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

    def test_http_host_lifecycle_boundary_remains_explicit(self) -> None:
        for implemented in (
            "forced to IPv4 `127.0.0.1`",
            "max_queued_requests",
            "queue_rejections()",
            "stop/drain/join",
            "--repeat until-fail:20",
        ):
            self.assertIn(implemented, self.http_host_spec)
        for remaining in (
            "real runtime/auth subsystem owners",
            "authentication, cookie, TLS, WebSocket Origin, and LAN-exposure policy",
            "complete graceful in-flight",
            "Tauri probe and pipe-mode dynamic HTTP address",
        ):
            self.assertIn(remaining, self.http_host_spec)
        for boundary in (
            "exact, configured allowlist and fails closed",
            "http://localhost:8191",
            "http://127.0.0.1:8191",
            "http://tauri.localhost",
            "authentication control in the first place",
            "no browser/Tauri end-to-end test",
        ):
            self.assertIn(boundary, self.origin_policy_spec)

    def test_http_stop_latch_is_never_cleared_after_transport_failure(self) -> None:
        self.assertNotIn("clear_server_stop_request", self.http_host_source)
        catch_order = re.findall(
            r"catch \([^)]*\) \{\s*"
            r"(?:\/\/[^\n]*\n\s*\/\/[^\n]*\n\s*)?"
            r"server_stop_failed_ = true;\s*"
            r"record_server_stop_failure_noexcept\(\);",
            self.http_host_source,
        )
        self.assertEqual(len(catch_order), 2)
        self.assertRegex(
            self.http_host_source,
            r"state_ = HttpHostState::failed;\s*"
            r"port_ = 0;\s*"
            r"last_error_message_ = \"cpp-httplib stop failed; ownership retained\";",
        )
        self.assertIn("if (server_stop_failed_) return false", self.http_host_source)
        self.assertIn("ownership retained", self.http_host_source)
        self.assertIn("later stop/start calls fail without retrying", self.http_host_spec)
        self.assertIn("public port is cleared to zero", self.http_host_spec)

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
        self.assertEqual(
            workflow.count("docs/script-runtime/SERVICE_ORIGIN_POLICY.md"), 2
        )
        self.assertEqual(
            workflow.count("docs/script-runtime/SERVICE_HEALTH_READINESS.md"), 2
        )


if __name__ == "__main__":
    unittest.main()
