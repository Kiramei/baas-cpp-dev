from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class RuntimeScriptPackageLoaderContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.target_cmake = (
            ROOT / "cmake/RuntimeScriptPackageLoader.cmake"
        ).read_text(encoding="utf-8")
        cls.header = (
            ROOT / "include/runtime/script/RuntimeScriptPackageLoader.h"
        ).read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")

    def test_pathless_composition_target_and_legacy_exclusion(self) -> None:
        self.assertIn("option(BUILD_RUNTIME_SCRIPT_PACKAGE_LOADER", self.root_cmake)
        self.assertIn(
            'EXCLUDE REGEX "/src/runtime/script/.*\\\\.cpp$"', self.root_cmake
        )
        self.assertIn("BAAS_runtime_script_package_loader", self.target_cmake)
        self.assertIn(
            "PUBLIC BAAS_runtime_repository BAAS_script_runtime", self.target_cmake
        )
        self.assertNotIn("<filesystem>", self.header)
        self.assertIn("const repository::RuntimeRepositoryReadView& scripts", self.header)

    def test_host_and_android_ci_own_the_target(self) -> None:
        self.assertIn(
            "-DBUILD_RUNTIME_SCRIPT_PACKAGE_LOADER_TESTS=ON", self.workflow
        )
        self.assertIn("BAAS_runtime_script_package_loader_tests", self.workflow)
        self.assertIn("-DBUILD_RUNTIME_SCRIPT_PACKAGE_LOADER=ON", self.workflow)
        self.assertIn("BAAS_runtime_script_package_loader", self.workflow)
        self.assertIn("arm64-v8a", self.workflow)
        self.assertIn("x86_64", self.workflow)
        self.assertGreaterEqual(
            self.workflow.count(
                "- 'docs/script-runtime/RUNTIME_SCRIPT_PACKAGE_LOADER.md'"
            ),
            2,
        )


if __name__ == "__main__":
    unittest.main()
