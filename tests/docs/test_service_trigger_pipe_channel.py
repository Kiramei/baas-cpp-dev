import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ServiceTriggerPipeChannelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "include/service/pipe/TriggerPipeChannel.h").read_text(
            encoding="utf-8"
        )
        cls.source = (ROOT / "src/service/pipe/TriggerPipeChannel.cpp").read_text(
            encoding="utf-8"
        )
        cls.tests = (ROOT / "tests/service/TriggerPipeChannelTests.cpp").read_text(
            encoding="utf-8"
        )
        cls.cmake = (ROOT / "cmake/ServiceTriggerPipeChannel.cmake").read_text(
            encoding="utf-8"
        )
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.spec = (
            ROOT / "docs/script-runtime/SERVICE_TRIGGER_PIPE_CHANNEL.md"
        ).read_text(encoding="utf-8")

    def test_factory_is_a_trigger_only_production_boundary(self) -> None:
        self.assertIn("class TriggerPipeChannelFactory", self.header)
        self.assertIn("request.channel != PipeChannel::trigger", self.source)
        self.assertIn("return nullptr", self.source)
        for unavailable in ("provider", "sync", "remote"):
            self.assertIn(unavailable, self.spec)

    def test_egress_transitions_are_explicit_and_atomic(self) -> None:
        for anchor in (
            "writer.write_batch", "owner_.complete_send(lease)",
            "owner_.fail_send(lease)", "writer->close_connection()",
            "pump_idle_.wait", "shutdown_complete_",
        ):
            self.assertIn(anchor, self.source)
        for phrase in ("same owning transport write", "complete_send", "fail_send", "strong output-callback barrier"):
            self.assertIn(phrase, self.spec)

    def test_fake_host_suite_covers_required_failure_boundaries(self) -> None:
        for name in (
            "test_factory_is_trigger_only",
            "test_binary_pair_and_atomic_output",
            "test_json_binary_response_is_one_write",
            "test_stream_backpressure_completes_all_leases",
            "test_peer_close_cancels_and_drains_running_task",
            "test_write_failure_fails_lease_and_closes_connection",
            "test_connection_task_limit_rejects_without_overcommit",
            "test_ingress_budget_is_strict",
            "test_stop_interrupts_write_and_waits_for_pump_barrier",
        ):
            self.assertIn(name, self.tests)

    def test_independent_target_is_in_debug_release_ci(self) -> None:
        self.assertIn("BAAS_service_trigger_pipe_channel", self.cmake)
        self.assertIn("BAAS_service_trigger_pipe_channel_tests", self.cmake)
        self.assertIn("BUILD_SERVICE_TRIGGER_PIPE_CHANNEL_TESTS", self.root_cmake)
        self.assertIn("BUILD_SERVICE_TRIGGER_PIPE_CHANNEL_TESTS=ON", self.workflow)
        self.assertIn("BAAS_service_trigger_pipe_channel_tests", self.workflow)
        self.assertEqual(
            self.workflow.count(
                "docs/script-runtime/SERVICE_TRIGGER_PIPE_CHANNEL.md"
            ),
            2,
        )


if __name__ == "__main__":
    unittest.main()
