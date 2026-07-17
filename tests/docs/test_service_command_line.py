import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ServiceCommandLineSpecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "include/service/app/ServiceCommandLine.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src/service/app/ServiceCommandLine.cpp"
        ).read_text(encoding="utf-8")
        cls.native_tests = (
            ROOT / "tests/service/ServiceCommandLineTests.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (ROOT / "cmake/ServiceCommandLine.cmake").read_text(
            encoding="utf-8"
        )
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.spec = (
            ROOT / "docs/script-runtime/SERVICE_COMMAND_LINE.md"
        ).read_text(encoding="utf-8")

    def test_contract_is_bounded_and_structured(self) -> None:
        for anchor in (
            "service_command_line_max_argument_count",
            "service_command_line_max_argument_bytes",
            "service_command_line_max_aggregate_bytes",
            "ServiceCommandLineDisposition",
            "ServiceCommandLineError",
            "service_command_line_error_name",
            "parse_service_command_line",
            "noexcept",
        ):
            self.assertIn(anchor, self.header)
        self.assertIn("std::from_chars", self.source)
        self.assertIn("std::filesystem::is_directory", self.source)
        self.assertIn("std::error_code filesystem_error", self.source)
        self.assertIn("catch (const std::filesystem::filesystem_error&)", self.source)
        self.assertIn("catch (const std::bad_alloc&)", self.source)

    def test_tauri_run_and_platform_contract_is_explicit(self) -> None:
        for anchor in (
            "--project-root",
            "--host",
            "127.0.0.1",
            "--port",
            "--pipe-name",
            "--help",
            "--version",
            "pipe_not_supported",
            "Windows",
            "Android",
            "does not",
            "service executable",
        ):
            self.assertIn(anchor, self.spec)
        self.assertIn("ServiceCommandLinePlatform::windows", self.native_tests)
        self.assertIn("ServiceCommandLinePlatform::unix_like", self.native_tests)
        self.assertIn("ServiceCommandLinePlatform::android", self.native_tests)

    def test_target_is_opt_in_excluded_and_gated(self) -> None:
        self.assertIn("BAAS_service_command_line", self.cmake)
        self.assertIn("BAAS_service_command_line_tests", self.cmake)
        self.assertIn("BUILD_SERVICE_COMMAND_LINE_TESTS", self.root_cmake)
        self.assertIn('/src/service/.*\\\\.cpp$', self.root_cmake)
        self.assertIn("BUILD_SERVICE_COMMAND_LINE_TESTS=ON", self.workflow)
        self.assertIn("BAAS_service_command_line_tests", self.workflow)
        self.assertEqual(
            self.workflow.count("docs/script-runtime/SERVICE_COMMAND_LINE.md"), 2
        )
        self.assertIn("real `BAAS_service` executable", self.spec)


if __name__ == "__main__":
    unittest.main()
