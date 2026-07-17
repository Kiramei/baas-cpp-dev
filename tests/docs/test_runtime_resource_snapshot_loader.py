from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RuntimeResourceSnapshotLoaderContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT
            / "include/runtime/resources/RuntimeResourceSnapshotLoader.h"
        ).read_text(encoding="utf-8")
        cls.cmake = (ROOT / "cmake/RuntimeResourceSnapshotLoader.cmake").read_text(
            encoding="utf-8"
        )
        cls.docs = (
            ROOT / "docs/script-runtime/RUNTIME_RESOURCE_SNAPSHOT_LOADER.md"
        ).read_text(encoding="utf-8")
        cls.foundation_ci = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")

    def test_pathless_capability_and_external_manifest(self) -> None:
        self.assertIn("const repository::RuntimeRepositoryReadView& resources", self.header)
        self.assertIn('"baas.resources.json"', self.header)
        self.assertIn('"baas.resources/v1"', self.header)
        self.assertNotIn("filesystem", self.header)
        self.assertNotIn("BAAS_RESOURCE_DIR", self.header)

    def test_selector_limits_stop_and_noexcept_result(self) -> None:
        self.assertIn("ResourceSelector selector", self.header)
        self.assertIn("max_manifest_bytes", self.header)
        self.assertIn("max_total_bytes", self.header)
        self.assertIn("max_json_depth", self.header)
        self.assertIn("max_json_nodes", self.header)
        self.assertIn("max_work", self.header)
        self.assertIn("std::stop_token", self.header)
        self.assertIn(") noexcept;", self.header)
        self.assertIn("class RuntimeResourceSnapshotActivation final", self.header)
        self.assertIn("const std::string& generation() const noexcept", self.header)
        self.assertIn("const std::string& commit() const noexcept", self.header)
        self.assertIn("activation;", self.header)

    def test_isolated_target_has_no_legacy_resource_tooling(self) -> None:
        self.assertIn("BAAS_runtime_resource_snapshot_loader", self.cmake)
        self.assertNotIn("BAASResources", self.cmake)
        self.assertNotIn("resource/", self.cmake)
        self.assertIn("remain external", self.docs)
        self.assertIn("must never be\naccepted from a browser request", self.docs)
        self.assertIn("not caller-constructible", self.docs)
        self.assertIn("same-generation repository selection", self.docs)

    def test_foundation_ci_builds_host_tests_and_android_production_target(self) -> None:
        self.assertIn(
            "-DBUILD_RUNTIME_RESOURCE_SNAPSHOT_LOADER_TESTS=ON",
            self.foundation_ci,
        )
        self.assertIn(
            "BAAS_runtime_resource_snapshot_loader_tests", self.foundation_ci
        )
        self.assertIn(
            "-DBUILD_RUNTIME_RESOURCE_SNAPSHOT_LOADER=ON", self.foundation_ci
        )
        self.assertIn(
            "BAAS_runtime_repository_updater BAAS_service_runtime_task_owner\n"
            "          BAAS_runtime_resource_snapshot_loader\n",
            self.foundation_ci,
        )
        self.assertIn(
            "docs/script-runtime/RUNTIME_RESOURCE_SNAPSHOT_LOADER.md",
            self.foundation_ci,
        )


if __name__ == "__main__":
    unittest.main()
