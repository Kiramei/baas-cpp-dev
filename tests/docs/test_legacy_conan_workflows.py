import ast
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
OCR_WORKFLOW = ROOT / ".github/workflows/baas_ocr.yaml"
AFWC_WORKFLOW = ROOT / ".github/workflows/baas_afwc.yaml"


def _assignment_literal(path: pathlib.Path, name: str):
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if any(isinstance(target, ast.Name) and target.id == name for target in node.targets):
            return node.value
    raise AssertionError(f"{name} assignment not found in {path}")


def _dependency_set(path: pathlib.Path, name: str) -> list[str]:
    value = _assignment_literal(path, "DEPENDENCY_SETS")
    if not isinstance(value, ast.Dict):
        raise AssertionError("DEPENDENCY_SETS must be a literal dictionary")
    for key, item in zip(value.keys, value.values):
        if ast.literal_eval(key) == name:
            return ast.literal_eval(item)
    raise AssertionError(f"dependency set {name!r} not found")


class LegacyConanWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ocr = OCR_WORKFLOW.read_text(encoding="utf-8")
        cls.afwc = AFWC_WORKFLOW.read_text(encoding="utf-8")
        cls.android_script = (ROOT / "scripts/dev/Build-AndroidOcr.ps1").read_text(
            encoding="utf-8"
        )
        cls.conanfile = ROOT / "deploy/conan/conanfile.py"

    def test_dependency_sets_match_the_cmake_targets(self) -> None:
        self.assertEqual(
            _dependency_set(self.conanfile, "ocr"),
            [
                "baas-opencv",
                "baas-onnxruntime",
                "baas-nlohmann-json",
                "baas-cpp-httplib",
                "baas-spdlog",
                "baas-simdutf",
            ],
        )
        self.assertEqual(
            _dependency_set(self.conanfile, "afwc"),
            [
                "baas-opencv",
                "baas-nlohmann-json",
                "baas-spdlog",
                "baas-simdutf",
            ],
        )
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            "baas_request_dependencies(opencv nlohmann_json cpp_httplib spdlog simdutf)",
            root_cmake,
        )
        self.assertIn("baas_request_dependencies(onnxruntime)", root_cmake)
        self.assertIn(
            "baas_request_dependencies(opencv nlohmann_json spdlog simdutf)",
            root_cmake,
        )
        afwc_cmake = (
            ROOT / "apps/BAAS_auto_fight_workflow_checker/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        self.assertNotIn("src/simdutf.cpp", afwc_cmake)
        self.assertIn("BAAS::simdutf", afwc_cmake)
        afwc_source = (
            ROOT / "apps/BAAS_auto_fight_workflow_checker/main.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn('_init("")', afwc_source)

    def test_ocr_jobs_use_pinned_conan_and_generated_toolchains(self) -> None:
        self.assertEqual(self.ocr.count("conan==2.30.0"), 4)
        for profile in (
            "linux-gcc-release",
            "macos-appleclang-release",
            "windows-msvc-release",
        ):
            self.assertIn(f"-pr:h=deploy/conan/profiles/{profile}", self.ocr)
            self.assertIn(f"-pr:b=deploy/conan/profiles/{profile}", self.ocr)
            self.assertRegex(
                self.ocr,
                rf"CMAKE_TOOLCHAIN_FILE=.*build[/\\]conan[/\\]{re.escape(profile)}-ocr[/\\]generators[/\\]conan_toolchain\.cmake",
            )
        self.assertEqual(self.ocr.count('dependency_set=ocr'), 3)
        self.assertGreaterEqual(self.ocr.count("--no-remote"), 3)
        self.assertIn("ndk;29.0.13846066", self.ocr)
        self.assertIn("Build-AndroidOcr.ps1", self.ocr)
        self.assertIn("-pr:b=deploy/conan/profiles/windows-msvc-release", self.android_script)
        self.assertIn('"&:dependency_set=ocr"', self.android_script)
        self.assertIn('"--no-remote"', self.android_script)
        self.assertIn("conan/recipes/baas-cpp-httplib", self.android_script)

    def test_emscripten_job_has_explicit_host_and_build_contexts(self) -> None:
        self.assertIn('ref: "6.0.3"', self.afwc)
        self.assertIn("./emsdk install 6.0.3", self.afwc)
        self.assertIn("conan==2.30.0", self.afwc)
        self.assertIn("-pr:h=deploy/conan/profiles/emscripten-wasm-release", self.afwc)
        self.assertIn("-pr:b=deploy/conan/profiles/linux-gcc-release", self.afwc)
        self.assertIn('dependency_set=afwc', self.afwc)
        self.assertIn("--no-remote", self.afwc)
        self.assertIn(
            "build/conan/emscripten-wasm-release-afwc/generators/conan_toolchain.cmake",
            self.afwc,
        )
        self.assertNotIn("emcmake cmake", self.afwc)
        self.assertNotIn("-DCMAKE_TOOLCHAIN_FILE=\"../emsdk/", self.afwc)

    def test_emscripten_profile_pins_static_private_packages(self) -> None:
        profile = (ROOT / "deploy/conan/profiles/emscripten-wasm-release").read_text(
            encoding="utf-8"
        )
        for anchor in (
            "os=Emscripten",
            "arch=wasm",
            "compiler=emcc",
            "compiler.version=6.0.3",
            "baas-opencv/*:shared=False",
            "baas-spdlog/*:shared=False",
            "baas-simdutf/*:shared=False",
            "tools.cmake.cmaketoolchain:user_toolchain=",
        ):
            self.assertIn(anchor, profile)
        for recipe in ("baas-opencv", "baas-spdlog", "baas-simdutf"):
            text = (
                ROOT / f"deploy/conan/recipes/{recipe}/conanfile.py"
            ).read_text(encoding="utf-8")
            self.assertIn('"shared": [True, False]', text)
            self.assertIn('"BUILD_SHARED_LIBS"', text)
        opencv_data = (
            ROOT / "deploy/conan/recipes/baas-opencv/conandata.yml"
        ).read_text(encoding="utf-8")
        self.assertEqual(opencv_data.count('WITH_ITT: "OFF"'), 2)
        self.assertEqual(opencv_data.count('BUILD_WITH_STATIC_CRT: "OFF"'), 2)

    def test_workflow_filters_cover_the_conan_contract(self) -> None:
        for workflow in (self.ocr, self.afwc):
            for anchor in (
                "'cmake/**'",
                "'deploy/conan/**'",
                "'include/**'",
                "'src/**'",
                "'tests/docs/test_legacy_conan_workflows.py'",
            ):
                self.assertGreaterEqual(workflow.count(anchor), 2, anchor)


if __name__ == "__main__":
    unittest.main()
