import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ServiceTriggerExecutorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "include/service/trigger/TriggerExecutor.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src/service/trigger/TriggerExecutor.cpp"
        ).read_text(encoding="utf-8")
        cls.dispatch_header = (
            ROOT / "include/service/trigger/TriggerDispatch.h"
        ).read_text(encoding="utf-8")
        cls.tests = (
            ROOT / "tests/service/TriggerExecutorTests.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (
            ROOT / "cmake/ServiceTriggerExecutor.cmake"
        ).read_text(encoding="utf-8")
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.spec = (
            ROOT / "docs/script-runtime/SERVICE_TRIGGER_EXECUTOR.md"
        ).read_text(encoding="utf-8")

    def test_transaction_is_reserve_admit_register_commit(self) -> None:
        for anchor in (
            "global_task_limit",
            "connection_task_limit",
            "queue_full",
            "executor.reserve",
            "session->admit",
            "executor.commit",
            "session->rollback",
        ):
            self.assertIn(anchor, self.source + self.header)
        self.assertLess(
            self.source.index("executor.reserve"),
            self.source.index("session->admit"),
        )
        self.assertLess(
            self.source.index("session->admit"),
            self.source.index("executor.commit"),
        )
        self.assertNotIn("TriggerDispatchResult submit(", self.dispatch_header)

    def test_owner_cancellation_pending_and_shutdown_are_explicit(self) -> None:
        for anchor in (
            "std::stop_token",
            "replace_completed_with_cancelled",
            "PendingTriggerResponse",
            "cancel_handoff",
            "workers_remaining",
            "workers_finished",
            "registry_close_complete",
            "dispose_worker_handle",
            "request_stop_outside_locks",
            "shutdown_stop_sources",
            "slot.stop_source.get_token()",
            "current_trigger_owner_scan",
            "trigger_owner_scan_contains",
            "current_trigger_executor",
            "void shutdown() noexcept",
            "max_input_bytes",
            "max_pending_response_bytes",
            "shared_ptr<trigger_protocol::TriggerSession>",
        ):
            self.assertIn(anchor, self.source + self.header)
        for test_name in (
            "test_queued_cancel_skips_business_handler",
            "test_pending_terminal_is_owned_and_retried_without_rerun",
            "test_shutdown_and_executor_before_owner_destruction",
            "test_worker_initiated_shutdown_has_no_self_join",
            "test_pending_cancel_and_fatal_submission_linearization",
            "test_input_and_pending_byte_budgets",
            "test_external_first_and_cross_owner_worker_shutdown",
            "test_two_executor_workers_cross_shutdown_without_join_cycle",
            "test_concurrent_external_shutdown_waits_for_registry_close",
            "test_shutdown_race_cannot_publish_completed_after_registry_scan",
            "test_running_to_completed_cancel_window_uses_cancelled_fallback",
            "test_stop_callback_reenters_stats_cancel_and_owner_shutdown",
            "test_shutdown_stop_callback_reenters_executor_shutdown",
            "test_owner_shutdown_callback_scan_has_bounded_reentry_depth",
            "test_owner_close_callback_scan_has_bounded_reentry_depth",
            "test_cross_owner_callback_scan_cycle_is_bounded",
            "test_cancel_capability_does_not_cross_timestamp_reuse",
            "test_terminal_already_queued_never_requests_task_stop",
            "test_whole_service_teardown_from_worker_keeps_shared_session_alive",
        ):
            self.assertIn(test_name, self.tests)

    def test_previous_dispatch_safety_coverage_moved_to_owner_path(self) -> None:
        for test_name in (
            "test_prefix_identity_single_and_stream_rules",
            "test_ignored_progress_backpressure_cannot_orphan_correlation",
            "test_receipt_owner_generation_and_rollback_visibility",
            "test_concurrent_connections_respect_global_bound",
        ):
            self.assertIn(test_name, self.tests)
        for anchor in (
            "same-address reconstruction",
            "response_already_queued",
            "terminal_already_published",
            "progress_for_single",
        ):
            self.assertIn(anchor, self.tests)

    def test_independent_target_ci_and_remaining_boundary(self) -> None:
        self.assertIn("BAAS_service_trigger_executor", self.cmake)
        self.assertIn("BAAS_service_trigger_executor_tests", self.cmake)
        self.assertIn("TriggerExecutorOdrConsumer.cpp", self.cmake)
        self.assertIn("set_source_files_properties", self.cmake)
        self.assertIn("BUILD_SERVICE_TRIGGER_EXECUTOR_TESTS", self.root_cmake)
        self.assertIn("BUILD_SERVICE_TRIGGER_EXECUTOR_TESTS=ON", self.workflow)
        self.assertIn("BAAS_service_trigger_executor_tests", self.workflow)
        self.assertEqual(
            self.workflow.count("docs/script-runtime/SERVICE_TRIGGER_EXECUTOR.md"),
            2,
        )
        self.assertIn("no dependency on BAAS globals", self.spec)
        self.assertIn("real catalog handlers", self.spec)
        self.assertIn("local Pipe connection host", self.spec)
        self.assertIn("starts no service or application", self.spec)


if __name__ == "__main__":
    unittest.main()
