import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "baas_app.yaml"


class BaasAppCiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_dependency_closure_is_generated_by_pinned_conan(self) -> None:
        self.assertIn("conan==2.30.0", self.workflow)
        self.assertIn("ninja==1.11.1.1", self.workflow)
        self.assertIn("python deploy/conan/scripts/manage_recipes.py export", self.workflow)
        self.assertIn("conan install deploy/conan", self.workflow)
        self.assertIn("deploy/conan/profiles/windows-msvc-release", self.workflow)
        self.assertIn("deploy/conan/profiles/dependency-versions-default", self.workflow)
        self.assertIn('"&:onnxruntime_use_cuda=${{ matrix.CONAN_CUDA }}"', self.workflow)
        self.assertIn("--build=missing", self.workflow)
        self.assertIn("generators/conan_toolchain.cmake", self.workflow)

    def test_cpu_and_cuda_artifacts_use_isolated_dependency_graphs(self) -> None:
        self.assertIn("BAAS_CONAN_MSVC_VERSION: '195'", self.workflow)
        self.assertIn("CONAN_CUDA: 'True'", self.workflow)
        self.assertIn("CONAN_CUDA: 'False'", self.workflow)
        self.assertIn("BUILD_SUFFIX: '-cuda'", self.workflow)
        self.assertIn("BAAS_APP_USE_CUDA=${{ matrix.BAAS_APP_USE_CUDA }}", self.workflow)
        self.assertIn("--target BAAS_APP", self.workflow)
        self.assertIn(
            "baas-app-bin-windows-x64-CUDA-${{ matrix.BAAS_APP_USE_CUDA }}",
            self.workflow,
        )

    def test_onnxruntime_is_not_manually_spliced_into_the_output(self) -> None:
        for obsolete in (
            "ONNXRUNTIME_URL",
            "Invoke-WebRequest",
            "Expand-Archive",
            "onnxruntime_providers_cuda.dll",
            'Destination "dll\\Windows\\"',
        ):
            self.assertNotIn(obsolete, self.workflow)

    def test_dependency_changes_trigger_the_workflow(self) -> None:
        for path in (
            "cmake/BAASDependency.cmake",
            "cmake/BAASConanRuntime.cmake",
            "cmake/BAASTarget.cmake",
            "deploy/conan/**",
            "tests/docs/test_baas_app_ci.py",
        ):
            self.assertIn(f"- '{path}'", self.workflow)

    def test_legacy_procedure_sources_and_native_tests_are_in_scope(self) -> None:
        for path in (
            "cmake/LegacyProcedureExecution.cmake",
            "cmake/LegacyProcedureDefinitionValidation.cmake",
            "include/BAAS.h",
            "include/device/ExactBackendLifetime.h",
            "include/device/control/BAASControl.h",
            "include/procedure/**",
            "src/BAAS.cpp",
            "src/device/control/BAASControl.cpp",
            "src/procedure/**",
            "include/device/screenshot/BAASScreenshot.h",
            "include/device/screenshot/ScreenshotInterval.h",
            "src/device/screenshot/BAASScreenshot.cpp",
            "tests/procedure/**",
            "tests/docs/test_legacy_procedure_execution.py",
        ):
            self.assertIn(f"- '{path}'", self.workflow)
        self.assertIn("-DBUILD_LEGACY_PROCEDURE_EXECUTION_TESTS=ON", self.workflow)
        self.assertIn("-DBUILD_LEGACY_PROCEDURE_DEFINITION_TESTS=ON", self.workflow)
        self.assertIn("BAAS_legacy_procedure_execution_tests", self.workflow)
        self.assertIn("BAAS_legacy_procedure_definition_tests", self.workflow)


if __name__ == "__main__":
    unittest.main()
