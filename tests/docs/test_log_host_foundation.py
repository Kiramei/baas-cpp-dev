import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class LogHostFoundationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "include/script/runtime/LogHost.h").read_text(encoding="utf-8")
        cls.source = (ROOT / "src/script/runtime/LogHost.cpp").read_text(encoding="utf-8")
        cls.adapter_header = (
            ROOT / "include/script/host/BAASLoggerLogSink.h"
        ).read_text(encoding="utf-8")
        cls.adapter_source = (
            ROOT / "src/script/host/BAASLoggerLogSink.cpp"
        ).read_text(encoding="utf-8")
        cls.native_tests = (ROOT / "tests/script/LogHostTests.cpp").read_text(encoding="utf-8")
        cls.cmake = (ROOT / "cmake/ScriptRuntime.cmake").read_text(encoding="utf-8")
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.spec = (ROOT / "docs/script-runtime/LOG_HOST.md").read_text(encoding="utf-8")
        cls.host_contract = (
            ROOT / "docs/script-runtime/HOST_CAPABILITY_CONTRACTS.md"
        ).read_text(encoding="utf-8")
        cls.roadmap = (ROOT / "docs/script-runtime/ROADMAP.md").read_text(encoding="utf-8")

    def test_dependency_free_runtime_owns_bounded_ordered_queue(self) -> None:
        self.assertIn("class QueuedLogHost final", self.header)
        self.assertIn("BoundedExecutor executor", self.source)
        self.assertIn("executor(1, limits.queue_capacity)", self.source)
        self.assertIn("TrySubmit", self.source)
        self.assertIn("HostErrorCode::Backpressure", self.source)
        self.assertIn("HostErrorCode::Unavailable", self.source)
        self.assertNotIn("BAASLogger.h", self.header)
        self.assertNotIn("BAASLogger.h", self.source)

    def test_identity_redaction_and_effect_contracts_are_executable(self) -> None:
        for anchor in (
            "task_id",
            "session_id",
            "config_name",
            "[REDACTED]",
            "DuplicateKey",
            "max_redaction_work",
            "budget_scope",
            "effect_state=not_started",
            "Successful queue insertion is the Host effect boundary",
        ):
            self.assertIn(anchor, self.source + self.spec)
        self.assertIn("redaction must reject field-key collisions", self.native_tests)
        self.assertIn("secret scans must consume", self.native_tests)
        self.assertIn("evaluator must cross the production queued LogHost", self.native_tests)

    def test_real_baas_logger_adapter_is_application_scoped(self) -> None:
        self.assertIn("class BAASLoggerLogSink final", self.adapter_header)
        self.assertIn('#include "BAASLogger.h"', self.adapter_source)
        self.assertIn("logger_->_out", self.adapter_source)
        self.assertIn("make_baas_logger_log_binding", self.adapter_source)
        self.assertIn("if(TARGET BAAS_APP)", self.cmake)
        self.assertIn("BAAS_script_baas_logger_adapter", self.cmake)
        self.assertIn("PRIVATE BAAS::spdlog BAAS::simdutf", self.cmake)
        self.assertIn("if(BUILD_APP_BAAS AND NOT BUILD_SCRIPT_RUNTIME)", self.root_cmake)

    def test_ci_builds_tests_and_tracks_normative_document(self) -> None:
        self.assertIn("BAAS_script_log_host_tests", self.cmake)
        self.assertIn("BAAS_script_log_host_tests", self.workflow)
        self.assertEqual(self.workflow.count("docs/script-runtime/LOG_HOST.md"), 2)

    def test_docs_preserve_live_activation_boundary(self) -> None:
        self.assertIn("real logger adapter", self.host_contract)
        self.assertIn("package activation and execution-context", self.host_contract)
        self.assertIn("- [~] Bind logging and structured events.", self.roadmap)
        for pending in (
            "live package activation",
            "real task/session/config propagation",
            "service structured-log streaming",
            "end-to-end Tauri display",
        ):
            self.assertIn(pending, self.spec)


if __name__ == "__main__":
    unittest.main()
