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
        cls.native_tests = (
            ROOT / "tests/service/TriggerSessionTests.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (ROOT / "cmake/ServiceProtocol.cmake").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.spec = (
            ROOT / "docs/script-runtime/SERVICE_TRIGGER_SESSION.md"
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
        self.assertIn("if (result.terminal) entries_.erase(result.timestamp);", self.source)
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
        self.assertIn("PROPERTIES TIMEOUT 30", self.cmake)

    def test_docs_and_ci_preserve_the_remaining_service_boundary(self) -> None:
        self.assertEqual(
            self.workflow.count("docs/script-runtime/SERVICE_TRIGGER_SESSION.md"),
            2,
        )
        self.assertIn("BUILD_SERVICE_PROTOCOL_TESTS=ON", self.workflow)
        self.assertIn("- [~] Implement task submission", self.roadmap)
        for pending in (
            "JSON decoding/encoding",
            "complete table-driven command dispatcher",
            "WebSocket and live Pipe channel hosts",
            "shared Python/C++/Tauri fixtures",
        ):
            self.assertIn(pending, self.spec)
        self.assertIn("does not yet parse envelopes", self.protocol)


if __name__ == "__main__":
    unittest.main()
