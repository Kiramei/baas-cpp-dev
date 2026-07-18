import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceRuntimeScriptTaskBackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "include/service/runtime/RuntimeScriptTaskBackend.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src/service/runtime/RuntimeScriptTaskBackend.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.doc = (
            ROOT
            / "docs/script-runtime/SERVICE_RUNTIME_SCRIPT_TASK_BACKEND.md"
        ).read_text(encoding="utf-8")

    def test_backend_is_explicit_opt_in(self) -> None:
        self.assertIn(
            'option(BUILD_SERVICE_RUNTIME_SCRIPT_TASK_BACKEND '
            '"Build the explicit native script RuntimeTaskBackend" OFF)',
            self.cmake,
        )
        self.assertIn(
            "does not register a service command", self.doc
        )
        self.assertIn("does not", self.doc)
        self.assertIn("replace the legacy Python backend", self.doc)

    def test_exact_identity_and_fail_closed_contract_are_public(self) -> None:
        for anchor in (
            "config_snapshot_id",
            "profile",
            "device_id",
            "runtime_generation",
            "scripts_commit",
            "resources_commit",
            "canonical_task",
            "RuntimeScriptTaskExecutionControl",
            "RuntimeScriptTaskRuntimeFactory",
        ):
            self.assertIn(anchor, self.header)
        self.assertIn("catch (...)", self.source)
        self.assertIn("runtime_script_task_deadline_exit_code = 124", self.header)
        self.assertIn("runtime_script_task_cancelled_exit_code = 130", self.header)

    def test_runtime_state_is_injected_not_embedded(self) -> None:
        for anchor in (
            "No resources, config defaults, BAAS Script packages",
            "User config is\ncreated and edited at runtime",
            "libgit2-backed pinned\nfactory",
        ):
            self.assertIn(anchor, self.doc)
        for forbidden in (
            "BAAS_FETCH_RESOURCES",
            "BAAS_RESOURCE_DIR",
            "resources/config",
            "scripts/group",
        ):
            self.assertNotIn(forbidden, self.source)

    def test_host_and_android_ci_cover_the_target(self) -> None:
        self.assertGreaterEqual(
            self.workflow.count("BUILD_SERVICE_RUNTIME_SCRIPT_TASK_BACKEND"),
            2,
        )
        self.assertGreaterEqual(
            self.workflow.count("BAAS_service_runtime_script_task_backend"),
            2,
        )
        for abi in ("arm64-v8a", "x86_64"):
            self.assertIn(abi, self.workflow)


if __name__ == "__main__":
    unittest.main()
