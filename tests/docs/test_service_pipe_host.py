import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ServicePipeHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "include/service/pipe/PipeHost.h").read_text(encoding="utf-8")
        cls.source = (ROOT / "src/service/pipe/PipeHost.cpp").read_text(encoding="utf-8")
        cls.native = (ROOT / "src/service/pipe/NativePipeListener.cpp").read_text(encoding="utf-8")
        cls.tests = (ROOT / "tests/service/PipeHostTests.cpp").read_text(encoding="utf-8")
        cls.cmake = (ROOT / "cmake/ServicePipeHost.cmake").read_text(encoding="utf-8")
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/foundation-runtime.yml").read_text(encoding="utf-8")
        cls.spec = (ROOT / "docs/script-runtime/SERVICE_PIPE_HOST.md").read_text(encoding="utf-8")
        cls.protocol = (ROOT / "docs/script-runtime/SERVICE_PROTOCOL_V1.md").read_text(encoding="utf-8")

    def test_open_codec_and_injected_boundary_are_bounded(self) -> None:
        for anchor in (
            "max_connections{16}", "max_open_json_bytes{4U * 1'024U}",
            "max_atomic_write_bytes{72U * 1'024U * 1'024U}",
            "open_timeout{5'000}", "class PipeListener", "class PipeStream",
            "class PipeChannelFactory", "class PipeChannelHandler",
            "write_batch(std::span<const bpip::Frame> frames)",
        ):
            self.assertIn(anchor, self.header)
        for anchor in (
            "OpenParser", "duplicate_open_field", "first_frame_not_json",
            "factory_->create", "encode_pipe_open_ok", "terminal_error",
            "128U * 1'024U * 1'024U", "active_worker_host == this",
            "std::terminate()",
        ):
            self.assertIn(anchor, self.source)

    def test_platform_security_backends_are_compile_owned(self) -> None:
        for anchor in (
            "CreateNamedPipeW", "PIPE_REJECT_REMOTE_CLIENTS",
            "FILE_FLAG_OVERLAPPED", "SetEntriesInAclW", "SE_DACL_PROTECTED",
            "CancelIoEx", "AF_UNIX", "chmod(endpoint_.c_str(), S_IRUSR | S_IWUSR)",
            "owned_device_", "owned_inode_", "SO_PEERCRED", "getpeereid",
            "pipe_listener_allocation_failed",
        ):
            self.assertIn(anchor, self.native)
        self.assertNotIn("FlushFileBuffers", self.native)
        self.assertIn("Advapi32", self.cmake)

    def test_fake_tests_cover_lifecycle_and_atomicity(self) -> None:
        for test_name in (
            "test_bounded_open_codec_and_inventory",
            "test_fragmented_open_coalesced_business_and_atomic_batch",
            "test_protocol_and_handler_failures_are_terminal",
            "test_partial_write_and_connection_limit_close_and_join",
            "test_open_timeout_and_hard_write_limit",
            "test_absolute_open_deadline_rejects_drip_feed",
            "test_handler_self_join_and_external_join_orders",
        ):
            self.assertIn(test_name, self.tests)
        self.assertIn("class FakeListener", self.tests)
        self.assertIn("class FakeStream", self.tests)

    def test_independent_target_ci_and_docs_do_not_overclaim(self) -> None:
        self.assertIn("BAAS_service_pipe_host", self.cmake)
        self.assertIn("BAAS_service_pipe_host_tests", self.cmake)
        self.assertIn("BUILD_SERVICE_PIPE_HOST_TESTS", self.root_cmake)
        self.assertIn("BUILD_SERVICE_PIPE_HOST_TESTS=ON", self.workflow)
        self.assertIn("BAAS_service_pipe_host_tests", self.workflow)
        self.assertEqual(self.workflow.count("docs/script-runtime/SERVICE_PIPE_HOST.md"), 2)
        for boundary in (
            "never open a\nreal OS endpoint", "no real provider, sync, trigger",
            "live OS security/load tests remain pending", "fake listeners and streams",
            "precondition\nfor an external `join()`", "MUST keep `PipeHost` alive",
            "Destroying the host from its own factory\nor handler callback is unsupported",
            "absolute\nopen deadline that fragment progress cannot reset",
        ):
            self.assertIn(boundary, self.spec)
        self.assertIn("real provider/sync/trigger/remote handler wiring", self.protocol)


if __name__ == "__main__":
    unittest.main()
