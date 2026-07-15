import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ServiceApplicationContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "include/service/app/ServiceApplication.h").read_text(
            encoding="utf-8"
        )
        cls.source = (ROOT / "src/service/app/ServiceApplication.cpp").read_text(
            encoding="utf-8"
        )
        cls.main = (ROOT / "apps/BAAS_service/main.cpp").read_text(encoding="utf-8")
        cls.cmake = (ROOT / "cmake/ServiceApplication.cmake").read_text(
            encoding="utf-8"
        )
        cls.tests = (ROOT / "tests/service/ServiceApplicationTests.cpp").read_text(
            encoding="utf-8"
        )
        cls.docs = (ROOT / "docs/script-runtime/SERVICE_APPLICATION.md").read_text(
            encoding="utf-8"
        )
        cls.workflow = (
            ROOT / ".github/workflows/service-application.yml"
        ).read_text(encoding="utf-8")

    def test_real_composition_is_explicit_and_remote_is_disabled(self) -> None:
        for anchor in (
            "ProductionProviderBackend",
            "FileResourceStore",
            "make_status_trigger_registration",
            "TriggerDispatcher::create",
            "TriggerExecutor",
            "TriggerHandlerFactory",
            "make_file_auth_storage",
            "make_system_auth_clock",
            "make_system_auth_random",
            "make_sodium_password_deriver",
            "open_production_http_host",
            "RemoteChannelPolicy::disabled",
            "dependencies.remote = nullptr",
        ):
            self.assertIn(anchor, self.source)

    def test_executable_contract_and_pipe_boundary_are_owned(self) -> None:
        self.assertIn("int wmain", self.main)
        self.assertIn("int main", self.main)
        self.assertIn("WideCharToMultiByte", self.main)
        self.assertIn("OUTPUT_NAME \"BAAS_service\"", self.cmake)
        self.assertIn("pipe_transport_unavailable", self.source)
        self.assertIn("--project-root <directory>", self.docs)
        self.assertIn("does not claim remote", self.docs)
        for exit_code in ("command_line = 2", "pipe_unavailable = 3", "host_start = 6"):
            self.assertIn(exit_code, self.header)

    def test_real_evidence_is_hook_free_and_cross_platform(self) -> None:
        for anchor in (
            "health_starting",
            "invalid_remember_request",
            "unregistered_command",
            "command_response",
            "storage_failure",
            "bind_failed",
            "pipe_transport_unavailable",
        ):
            self.assertIn(anchor, self.tests)
        self.assertNotIn("TEST_HOOKS=1", self.cmake)
        self.assertIn("BUILD_SERVICE_APP_TESTS", self.cmake)
        for platform in ("windows-latest", "ubuntu-latest", "macos-latest"):
            self.assertIn(platform, self.workflow)
        for build_type in ("Debug", "Release"):
            self.assertIn(build_type, self.workflow)


if __name__ == "__main__":
    unittest.main()
