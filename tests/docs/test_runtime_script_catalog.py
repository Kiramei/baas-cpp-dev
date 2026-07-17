from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class RuntimeScriptCatalogContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.target_cmake = (ROOT / "cmake/RuntimeScriptCatalog.cmake").read_text(
            encoding="utf-8"
        )
        cls.header = (
            ROOT / "include/runtime/script/RuntimeScriptCatalog.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src/runtime/script/RuntimeScriptCatalog.cpp"
        ).read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.docs = (
            ROOT / "docs/script-runtime/RUNTIME_SCRIPT_CATALOG.md"
        ).read_text(encoding="utf-8")

    def test_pathless_pin_and_immutable_target(self) -> None:
        self.assertIn("BUILD_RUNTIME_SCRIPT_CATALOG", self.root_cmake)
        self.assertIn("BAAS_runtime_script_catalog", self.target_cmake)
        self.assertIn(
            "PUBLIC BAAS_runtime_repository BAAS_script_runtime", self.target_cmake
        )
        self.assertIn("RuntimeScriptCatalogPin expected", self.header)
        self.assertIn("const repository::RuntimeRepositoryReadView& scripts", self.header)
        self.assertNotIn("<filesystem>", self.header)
        self.assertIn("std::shared_ptr<const Impl>", self.header)

    def test_aliases_are_data_not_production_code(self) -> None:
        aliases = (
            "start_hard_task",
            "start_normal_task",
            "start_fhx",
            "start_main_story",
            "start_group_story",
            "start_mini_story",
            "start_explore_activity_story",
            "start_explore_activity_mission",
            "start_explore_activity_challenge",
        )
        for alias in aliases:
            self.assertNotIn(alias, self.source)
            self.assertIn(alias, self.docs)
        self.assertIn("legacy_aliases", self.source)
        self.assertIn("runtime_script_catalog_manifest", self.source)

    def test_host_and_android_ci_own_the_target(self) -> None:
        self.assertIn("-DBUILD_RUNTIME_SCRIPT_CATALOG_TESTS=ON", self.workflow)
        self.assertIn("BAAS_runtime_script_catalog_tests", self.workflow)
        self.assertIn("-DBUILD_RUNTIME_SCRIPT_CATALOG=ON", self.workflow)
        self.assertIn("BAAS_runtime_script_catalog", self.workflow)
        self.assertIn("arm64-v8a", self.workflow)
        self.assertIn("x86_64", self.workflow)
        self.assertGreaterEqual(
            self.workflow.count(
                "- 'docs/script-runtime/RUNTIME_SCRIPT_CATALOG.md'"
            ),
            2,
        )


if __name__ == "__main__":
    unittest.main()
