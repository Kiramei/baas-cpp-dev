import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ProductionRemoteBackendContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "include" / "service" / "app" / "ProductionRemoteBackend.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src" / "service" / "app" / "ProductionRemoteBackend.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (ROOT / "cmake" / "ServiceRemoteBackend.cmake").read_text(
            encoding="utf-8"
        )
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github" / "workflows" / "service-auth.yml"
        ).read_text(encoding="utf-8")
        cls.docs = (
            ROOT / "docs" / "script-runtime" / "SERVICE_REMOTE_BACKEND.md"
        ).read_text(encoding="utf-8")

    def test_exact_device_and_independent_server_identity_are_owned(self) -> None:
        for anchor in (
            'SyncResource::config',
            '"adbIP"',
            '"adbPort"',
            'get_state(config->serial',
            'baas-ws-scrcpy-server.jar',
            'cat /proc/',
            'expected_cmdline',
            '1.19-ws7',
        ):
            self.assertIn(anchor, self.source)
        self.assertIn("never falls back", self.docs)
        self.assertIn("classic scrcpy jar", self.docs)

    def test_owned_cleanup_and_concurrency_barriers_are_explicit(self) -> None:
        for anchor in (
            "forward_tcp_zero",
            "remove_tcp_forward",
            "cleanup_owned",
            "owner_token_factory",
            "BAAS_WS_SCRCPY_OWNER=",
            "/environ",
            "parse_lease_record",
            "BAAS_WS_LEASE_PROBE",
            "BAAS_WS_SUPERVISOR_LAUNCH",
            "BAAS_WS_SUPERVISOR_STOP",
            "baas-ws-scrcpy.owner.*",
            "send_mutex_",
            "close_mutex_",
            "opens_drained_",
            "if (stopped_)",
            "reader.join()",
            "drained_.wait",
        ):
            self.assertIn(anchor, self.source)
        self.assertIn("Only a forward created by this session", self.docs)
        self.assertIn("concurrent close callers wait", self.docs)
        self.assertIn("256-bit", self.docs)
        self.assertIn("PID reuse", self.docs)
        self.assertIn("path-traversing lease targets fail closed", self.docs)

    def test_build_ci_and_test_gates_are_closed(self) -> None:
        self.assertIn("BAAS_service_remote_backend", self.cmake)
        self.assertIn("BAAS_service_remote_backend_tests", self.cmake)
        self.assertIn("BUILD_SERVICE_REMOTE_BACKEND_TESTS", self.root_cmake)
        self.assertIn("BAAS_service_remote_backend_tests", self.workflow)
        self.assertIn("ServiceRemoteBackend.cmake", self.workflow)
        self.assertIn("test_ws_scrcpy_resource_lock.py", self.workflow)
        self.assertIn("Verify ws-scrcpy resource lock", self.workflow)


if __name__ == "__main__":
    unittest.main()
