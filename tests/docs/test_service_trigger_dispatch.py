import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ServiceTriggerDispatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "include/service/trigger/TriggerDispatch.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src/service/trigger/TriggerDispatch.cpp"
        ).read_text(encoding="utf-8")
        cls.session_header = (
            ROOT / "include/service/protocol/TriggerSession.h"
        ).read_text(encoding="utf-8")
        cls.session_source = (
            ROOT / "src/service/protocol/TriggerSession.cpp"
        ).read_text(encoding="utf-8")
        cls.native_tests = (
            ROOT / "tests/service/TriggerDispatchTests.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (
            ROOT / "cmake/ServiceTriggerDispatch.cmake"
        ).read_text(encoding="utf-8")
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.spec = (
            ROOT / "docs/script-runtime/SERVICE_TRIGGER_DISPATCH.md"
        ).read_text(encoding="utf-8")

    def test_receipt_closes_owner_and_generation_aba(self) -> None:
        for anchor in (
            "class AdmissionReceipt final",
            "owner_id_",
            "generation_",
            "invalid_admission_receipt",
            "RollbackResult rollback",
            "response_already_queued",
        ):
            self.assertIn(anchor, self.session_header)
        self.assertIn("allocate_session_instance_id", self.session_source)
        self.assertIn("entry.generation != receipt.generation_", self.session_source)
        self.assertIn("entry.response_queued = true", self.session_source)
        self.assertIn("OutboundBatch&& batch", self.session_header)
        self.assertIn("same-address session reconstruction", self.spec)

    def test_dispatch_identity_and_registry_are_sealed(self) -> None:
        for anchor in (
            "class AdmittedTriggerRequest final",
            "class TriggerResponseSink final",
            "TriggerDispatcherBuildResult create",
            "duplicate_registration",
            "unregistered_command",
            "TriggerIngressItem item",
        ):
            self.assertIn(anchor, self.header)
        self.assertIn(
            "const auto* const handler = find_handler(item.descriptor())",
            self.source,
        )
        self.assertLess(
            self.source.index("find_handler(item.descriptor())"),
            self.source.index("admit_ingress_item(item, session)"),
        )
        self.assertIn("return session.admit(item.admission());", self.source)
        self.assertNotIn("TriggerSession& session()", self.header)

    def test_terminal_exception_and_backpressure_boundaries_are_explicit(self) -> None:
        for anchor in (
            "staged_terminal_",
            "discard_staged_terminal",
            "commit_staged_terminal",
            "PendingTriggerResponse",
            "retry_response",
            "close_session",
            "internal_failure",
        ):
            self.assertIn(anchor, self.header + self.source)
        self.assertIn("std::move(*batch_)", self.source)
        self.assertIn("std::move(*staged_terminal_)", self.source)
        self.assertIn("bounded_exception_message", self.source)
        self.assertIn("catch (...)", self.source)
        for test_name in (
            "test_success_then_throw_is_replaced_by_bounded_error",
            "test_binary_terminal_backpressure_has_no_copy_retry_contract",
            "test_ignored_progress_backpressure_cannot_orphan_correlation",
            "test_receipt_owner_generation_and_rollback",
            "test_dispatcher_concurrent_sessions",
        ):
            self.assertIn(test_name, self.native_tests)

    def test_independent_target_ci_and_docs_boundary(self) -> None:
        self.assertIn("BAAS_service_trigger_dispatch", self.cmake)
        self.assertIn("BAAS_service_trigger_dispatch_tests", self.cmake)
        self.assertIn("BUILD_SERVICE_TRIGGER_DISPATCH_TESTS", self.root_cmake)
        self.assertIn("BUILD_SERVICE_TRIGGER_DISPATCH_TESTS=ON", self.workflow)
        self.assertIn("BAAS_service_trigger_dispatch_tests", self.workflow)
        self.assertEqual(
            self.workflow.count("docs/script-runtime/SERVICE_TRIGGER_DISPATCH.md"),
            2,
        )
        self.assertIn("transport-independent foundation", self.spec)
        self.assertIn("does not implement BAAS runtime commands", self.spec)
        self.assertIn("real catalog handler implementations", self.spec)
        self.assertIn("authenticated WebSocket and local Pipe hosts", self.spec)


if __name__ == "__main__":
    unittest.main()
