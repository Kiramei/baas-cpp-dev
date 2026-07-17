from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class SchedulerPolicyContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.target_cmake = (ROOT / "cmake/SchedulerPolicy.cmake").read_text(
            encoding="utf-8"
        )
        cls.workflow = (
            ROOT / ".github/workflows/service-application.yml"
        ).read_text(encoding="utf-8")
        cls.foundation_workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")

    def test_scheduler_target_is_opt_in_and_excluded_from_legacy_glob(self) -> None:
        self.assertIn("option(BUILD_SCHEDULER_POLICY", self.root_cmake)
        self.assertIn("option(BUILD_SCHEDULER_POLICY_TESTS", self.root_cmake)
        self.assertIn('EXCLUDE REGEX "/src/scheduler/.*\\\\.cpp$"', self.root_cmake)
        self.assertIn("add_library(\n        BAAS_scheduler_policy", self.target_cmake)
        self.assertIn("BAAS_scheduler_policy_tests", self.target_cmake)
        self.assertIn("add_test(", self.target_cmake)

    def test_host_ci_covers_scheduler_on_all_supported_desktops(self) -> None:
        for path in (
            "include/scheduler/**",
            "src/scheduler/**",
            "tests/scheduler/**",
            "docs/scheduler/**",
            "tests/docs/test_scheduler_policy.py",
        ):
            self.assertGreaterEqual(self.workflow.count(f"- '{path}'"), 2, path)
        for runner in ("windows-latest", "ubuntu-latest", "macos-latest"):
            self.assertIn(f"os: {runner}", self.workflow)
        self.assertIn("-DBUILD_SCHEDULER_POLICY_TESTS=ON", self.workflow)
        self.assertIn("BAAS_scheduler_policy_tests", self.workflow)
        self.assertIn("BAAS_scheduler_policy_tests)$", self.workflow)
        self.assertIn(
            "python -m unittest discover -s tests/docs -p test_scheduler_policy.py",
            self.workflow,
        )
        self.assertNotIn("BUILD_SCHEDULER_POLICY", self.foundation_workflow)
        self.assertNotIn("BAAS_scheduler_policy", self.foundation_workflow)

    def test_android_ci_has_real_nlohmann_cross_dependency_and_compile_target(self) -> None:
        self.assertIn("android-scheduler-policy:", self.workflow)
        self.assertIn("android-clang-arm64-v8a-release", self.workflow)
        self.assertIn("android-clang-x86_64-release", self.workflow)
        self.assertIn(
            "conan create deploy/conan/recipes/baas-nlohmann-json", self.workflow
        )
        self.assertIn("--requires=baas-nlohmann-json/3.11.3", self.workflow)
        self.assertIn('tools.android:ndk_path=${ANDROID_NDK_HOME}', self.workflow)
        self.assertIn("-DBUILD_SCHEDULER_POLICY=ON", self.workflow)
        self.assertIn("--target BAAS_scheduler_policy", self.workflow)


if __name__ == "__main__":
    unittest.main()
