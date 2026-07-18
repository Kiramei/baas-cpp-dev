import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceProductionRuntimeScriptTaskFactoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT
            / "include/service/runtime/ProductionRuntimeScriptTaskFactory.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT
            / "src/service/runtime/ProductionRuntimeScriptTaskFactory.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.doc = (
            ROOT
            / "docs/script-runtime/SERVICE_PRODUCTION_RUNTIME_SCRIPT_TASK_FACTORY.md"
        ).read_text(encoding="utf-8")

    def test_factory_is_additive_and_runtime_state_is_dynamic(self) -> None:
        self.assertIn(
            "option(BUILD_SERVICE_PRODUCTION_RUNTIME_SCRIPT_TASK_FACTORY ",
            self.cmake,
        )
        for anchor in (
            "does not register a command",
            "replace the legacy Python backend",
            "No config,\nresource payload, BAAS Script package",
            "libgit2-backed updater",
        ):
            self.assertIn(anchor, self.doc)

    def test_exact_pin_and_cleanup_contract_are_public(self) -> None:
        for anchor in (
            "ProductionRuntimeScriptConfigSnapshot",
            "ProductionRuntimeScriptExtensionIdentity",
            "RuntimeRepositoryReadBundle",
            "CoDetectProductionDeviceIdentity",
            "ProductionRuntimeScriptTaskProvider",
        ):
            self.assertIn(anchor, self.header)
        for anchor in (
            "task.evaluator->invoke_export",
            "ValueKind::Boolean",
            "retry_detached_releases",
            "log_host->shutdown",
            "exact_device_identity",
        ):
            self.assertIn(anchor, self.source)

    def test_dependency_complete_ci_covers_host_and_android(self) -> None:
        self.assertGreaterEqual(
            self.workflow.count(
                "BUILD_SERVICE_PRODUCTION_RUNTIME_SCRIPT_TASK_FACTORY"
            ),
            3,
        )
        self.assertGreaterEqual(
            self.workflow.count(
                "BAAS_service_production_runtime_script_task_factory"
            ),
            3,
        )
        for abi in ("arm64-v8a", "x86_64"):
            self.assertIn(abi, self.workflow)


if __name__ == "__main__":
    unittest.main()
