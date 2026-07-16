from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ServiceBusinessPipeChannelDocsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "include/service/pipe/BusinessPipeChannel.h").read_text(
            encoding="utf-8"
        )
        cls.source = (ROOT / "src/service/pipe/BusinessPipeChannel.cpp").read_text(
            encoding="utf-8"
        )
        cls.tests = (ROOT / "tests/service/BusinessPipeChannelTests.cpp").read_text(
            encoding="utf-8"
        )
        cls.spec = (
            ROOT / "docs/script-runtime/SERVICE_BUSINESS_PIPE_CHANNEL.md"
        ).read_text(encoding="utf-8")
        cls.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.module = (
            ROOT / "cmake/ServiceBusinessPipeChannel.cmake"
        ).read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/service-auth.yml"
        ).read_text(encoding="utf-8")

    def test_factory_routes_transport_neutral_handlers_and_trigger(self) -> None:
        for token in (
            "BusinessPipeChannelFactories",
            "BusinessPipeChannelFactory",
            "BusinessChannelHandlerFactory",
            "factories_.trigger->create",
            "PipeChannel::provider",
            "PipeChannel::sync",
            "PipeChannel::remote",
        ):
            self.assertIn(token, self.header + self.source)

    def test_output_and_close_contract_is_explicit(self) -> None:
        for token in (
            "emit_batch",
            "BusinessBatchWriteResult::written",
            "BusinessBatchWriteResult::failed",
            "writer->close_connection()",
            "begin_close",
            "wait_idle",
            "remote_configured_",
        ):
            self.assertIn(token, self.source)
        for statement in (
            "Provider and sync accept JSON frames only",
            "Remote requires one JSON",
            "reported `written` only after the complete",
            "does not yet install it",
            "live Windows/Unix endpoints",
        ):
            self.assertIn(statement, self.spec)

    def test_native_suite_and_ci_are_selectable(self) -> None:
        for test_name in (
            "test_provider_initial_output_after_open_ok_and_json_requests",
            "test_sync_list_pull_and_push_are_json",
            "test_remote_raw_bytes_and_observed_write_completion",
            "test_remote_write_failure_is_observed_and_closes_session",
            "test_trigger_delegation_and_strict_frame_kinds",
            "test_close_barrier_waits_for_write_receipt_callback",
            "test_stop_interrupts_blocked_push_and_waits_for_close_barrier",
        ):
            self.assertIn(test_name, self.tests)
        for token in (
            "BUILD_SERVICE_BUSINESS_PIPE_CHANNEL_TESTS",
            "BAAS_service_business_pipe_channel_tests",
        ):
            self.assertIn(token, self.cmake + self.module)
            self.assertIn(token, self.workflow)
        self.assertEqual(
            self.workflow.count(
                "docs/script-runtime/SERVICE_BUSINESS_PIPE_CHANNEL.md"
            ),
            2,
        )


if __name__ == "__main__":
    unittest.main()
