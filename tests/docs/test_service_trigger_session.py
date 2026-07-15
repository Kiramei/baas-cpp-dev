import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ServiceTriggerSessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "include/service/protocol/TriggerSession.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src/service/protocol/TriggerSession.cpp"
        ).read_text(encoding="utf-8")
        cls.pipe_header = (
            ROOT / "include/service/protocol/TriggerPipeAdapter.h"
        ).read_text(encoding="utf-8")
        cls.pipe_source = (
            ROOT / "src/service/protocol/TriggerPipeAdapter.cpp"
        ).read_text(encoding="utf-8")
        cls.envelope_header = (
            ROOT / "include/service/protocol/TriggerEnvelope.h"
        ).read_text(encoding="utf-8")
        cls.envelope_source = (
            ROOT / "src/service/protocol/TriggerEnvelope.cpp"
        ).read_text(encoding="utf-8")
        cls.ingress_header = (
            ROOT / "include/service/protocol/TriggerIngress.h"
        ).read_text(encoding="utf-8")
        cls.ingress_source = (
            ROOT / "src/service/protocol/TriggerIngress.cpp"
        ).read_text(encoding="utf-8")
        cls.native_tests = (
            ROOT / "tests/service/TriggerSessionTests.cpp"
        ).read_text(encoding="utf-8")
        cls.envelope_tests = (
            ROOT / "tests/service/TriggerEnvelopeTests.cpp"
        ).read_text(encoding="utf-8")
        cls.ingress_tests = (
            ROOT / "tests/service/TriggerIngressTests.cpp"
        ).read_text(encoding="utf-8")
        cls.egress_tests = (
            ROOT / "tests/service/TriggerEgressTests.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (ROOT / "cmake/ServiceProtocol.cmake").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.spec = (
            ROOT / "docs/script-runtime/SERVICE_TRIGGER_SESSION.md"
        ).read_text(encoding="utf-8")
        cls.catalog_spec = (
            ROOT / "docs/script-runtime/SERVICE_TRIGGER_COMMAND_CATALOG.md"
        ).read_text(encoding="utf-8")
        cls.envelope_spec = (
            ROOT / "docs/script-runtime/SERVICE_TRIGGER_ENVELOPE.md"
        ).read_text(encoding="utf-8")
        cls.ingress_spec = (
            ROOT / "docs/script-runtime/SERVICE_TRIGGER_INGRESS.md"
        ).read_text(encoding="utf-8")
        cls.protocol = (
            ROOT / "docs/script-runtime/SERVICE_PROTOCOL_V1.md"
        ).read_text(encoding="utf-8")
        cls.roadmap = (ROOT / "docs/script-runtime/ROADMAP.md").read_text(encoding="utf-8")

    def test_session_has_bounded_protocol_compatible_admission(self) -> None:
        for anchor in (
            "maximum_safe_timestamp = 9'007'199'254'740'991ULL",
            "max_in_flight{256}",
            "max_request_payload_bytes{1U * 1'024U * 1'024U}",
            "max_request_binary_bytes{64U * 1'024U * 1'024U}",
            "duplicate_timestamp",
            "in_flight_limit",
        ):
            self.assertIn(anchor, self.header)
        self.assertIn("entries_.contains(command.timestamp)", self.source)
        self.assertIn("is_command_name", self.source)
        self.assertIn("is_valid_utf8", self.source)

    def test_stream_terminal_cancellation_and_backpressure_are_enforced(self) -> None:
        for anchor in (
            "single_response_must_be_terminal",
            "error_response_must_be_terminal",
            "cancellation_response_required",
            "queue_full",
            "queued_bytes_exceeded",
        ):
            self.assertIn(anchor, self.header + self.source)
        self.assertIn("BeginSendResult TriggerSession::begin_send()", self.source)
        self.assertIn("CompleteSendResult TriggerSession::complete_send", self.source)
        self.assertIn("if (batch.terminal()) entries_.erase(batch.timestamp());", self.source)
        self.assertIn("FailSendResult TriggerSession::fail_send", self.source)
        self.assertIn("return {SendTransitionError::none, close_locked()};", self.source)
        self.assertIn("response_mode_mismatch", self.header + self.source)
        self.assertIn("if (!entry.terminal_queued)", self.source)
        self.assertIn("std::lock_guard lock(mutex_)", self.source)

    def test_pipe_adapter_keeps_json_and_binary_in_one_write_batch(self) -> None:
        self.assertIn("encode_pipe_batch", self.pipe_header)
        self.assertIn("one owning write buffer", self.pipe_header)
        self.assertIn("bpip::FrameKind::json", self.pipe_source)
        self.assertIn("bpip::FrameKind::bytes", self.pipe_source)
        self.assertIn("result.bytes.insert", self.pipe_source)
        self.assertIn("one coalesced trigger write", self.native_tests)
        self.assertIn("binary frame payload must remain byte exact", self.native_tests)
        self.assertIn("has_binary", self.header + self.pipe_source)
        self.assertIn("declared zero-byte binary", self.native_tests)

    def test_bounded_envelope_codec_owns_schema_and_binary_metadata(self) -> None:
        for anchor in (
            "decode_command_envelope",
            "make_admission",
            "encode_command_response",
            "max_depth{64}",
            "max_nodes{65'536}",
            "max_work{4U * 1'024U * 1'024U}",
            "reserved_binary_field",
        ):
            self.assertIn(anchor, self.envelope_header)
        self.assertIn("std::unordered_set<std::string> keys", self.envelope_source)
        self.assertIn('data->object.emplace_back("binary"', self.envelope_source)
        self.assertIn("response.binary->size()", self.envelope_source)
        self.assertIn("BAAS_service_trigger_envelope_tests", self.cmake)
        for test_name in (
            "test_command_decode_and_compatibility",
            "test_schema_and_json_rejections",
            "test_parser_budgets",
            "test_deterministic_response_encoding",
            "test_stream_terminal_wire_binding",
            "test_binary_metadata_and_zero_length_frame",
            "test_response_rejections_and_session_integration",
        ):
            self.assertIn(test_name, self.envelope_tests)

    def test_native_tests_cover_lifecycle_and_concurrency(self) -> None:
        for test_name in (
            "test_admission_validation_and_correlation_reservation",
            "test_streaming_order_and_atomic_binary_batch",
            "test_publish_validation_backpressure_and_retry",
            "test_cancellation_and_disconnect_cleanup",
            "test_concurrent_admission_and_publication",
            "test_bpip_json_binary_batch_encoding",
        ):
            self.assertIn(test_name, self.native_tests)
        self.assertIn("BAAS_service_trigger_session_tests", self.cmake)
        self.assertIn("BAAS_service_trigger_egress_tests", self.cmake)
        self.assertIn("test_lease_retains_queue_budget_and_correlation", self.egress_tests)
        self.assertIn("test_send_failure_is_connection_fatal_and_deterministic", self.egress_tests)
        self.assertIn("test_complete_close_race_is_linearizable", self.egress_tests)
        self.assertIn("test_fail_close_race_returns_cancellation_once", self.egress_tests)
        self.assertIn("PROPERTIES TIMEOUT 30", self.cmake)

    def test_ingress_owns_strict_bounded_input_state(self) -> None:
        for anchor in (
            "accepting_json",
            "awaiting_binary",
            "json_while_awaiting_binary",
            "binary_without_declaration",
            "max_aggregate_bytes",
            "std::optional<std::vector<std::byte>>",
            "BuildAdmissionResult",
            "take_ready",
        ):
            self.assertIn(anchor, self.ingress_header)
        self.assertIn("decode_command_envelope", self.ingress_source)
        self.assertIn("make_admission", self.ingress_source)
        self.assertIn("pending_binary_.reset();", self.ingress_source)
        self.assertIn("checked_add", self.ingress_source)
        for test_name in (
            "test_json_only_ready_item_is_owned_and_single_outstanding",
            "test_declared_binary_is_adjacent_owned_and_zero_length_distinct",
            "test_binary_marker_gate_and_strict_frame_order",
            "test_frame_and_aggregate_limits_clear_partial_state",
            "test_envelope_failures_modes_and_limit_validation_recover",
            "test_reset_and_close_are_explicit_and_terminal",
        ):
            self.assertIn(test_name, self.ingress_tests)
        self.assertIn("BAAS_service_trigger_ingress_tests", self.cmake)
        self.assertIn("BAAS_service_trigger_ingress_tests", self.workflow)

    def test_docs_and_ci_preserve_the_remaining_service_boundary(self) -> None:
        self.assertEqual(
            self.workflow.count("docs/script-runtime/SERVICE_TRIGGER_SESSION.md"),
            2,
        )
        self.assertEqual(
            self.workflow.count("docs/script-runtime/SERVICE_TRIGGER_ENVELOPE.md"),
            2,
        )
        self.assertEqual(
            self.workflow.count(
                "docs/script-runtime/SERVICE_TRIGGER_COMMAND_CATALOG.md"
            ),
            2,
        )
        self.assertEqual(
            self.workflow.count("docs/script-runtime/SERVICE_TRIGGER_INGRESS.md"),
            2,
        )
        self.assertIn("BUILD_SERVICE_PROTOCOL_TESTS=ON", self.workflow)
        self.assertIn("BUILD_SERVICE_TRIGGER_CATALOG_TESTS=ON", self.workflow)
        self.assertIn("BAAS_service_trigger_envelope_tests", self.workflow)
        self.assertIn("BAAS_service_trigger_catalog_tests", self.workflow)
        self.assertIn("- [~] Implement task submission", self.roadmap)
        for pending in (
            "catalog admission/dispatch integration",
            "WebSocket and live Pipe channel hosts",
            "shared Python/C++/Tauri fixtures",
        ):
            self.assertIn(pending, self.spec)
        self.assertIn("TriggerEnvelope", self.protocol)
        self.assertIn("does not yet dispatch commands", self.protocol)
        self.assertIn("dependency-free JSON codec", self.envelope_spec)
        self.assertIn("zero-length BYTES frame", self.envelope_spec)
        self.assertIn("does not execute commands", self.catalog_spec)
        self.assertIn("Only `import_config`", self.catalog_spec)
        for boundary in (
            "dependency-free, transport-independent",
            "one-outstanding",
            "immediately adjacent binary frame",
            "present zero-length frame",
            "performs no network I/O",
        ):
            self.assertIn(boundary, self.ingress_spec)


if __name__ == "__main__":
    unittest.main()
