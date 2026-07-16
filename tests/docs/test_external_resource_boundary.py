import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
LEGACY_OPTIONS = (
    "BUILD_APP_BAAS",
    "BUILD_APP_ISA",
    "BUILD_BAAS_OCR",
    "BUILD_BAAS_AW_CHECKER",
)


class ExternalResourceBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.resource_cmake = (ROOT / "cmake/BAASResources.cmake").read_text(
            encoding="utf-8"
        )

    def test_resource_tooling_is_reachable_only_from_the_legacy_gate(self) -> None:
        gate = " OR ".join(LEGACY_OPTIONS)
        guarded_include = re.compile(
            rf"if\({re.escape(gate)}\)\s+"
            r"set\(_baas_legacy_resource_tooling_enabled ON\)\s+"
            r'include\("\$\{CMAKE_CURRENT_LIST_DIR\}/cmake/BAASResources\.cmake"\)\s+'
            r"endif\(\)",
            re.MULTILINE,
        )
        guarded_install = re.compile(
            r"if\(_baas_legacy_resource_tooling_enabled\)\s+"
            r"baas_install_required_runtime_resources\(\)\s+endif\(\)",
            re.MULTILINE,
        )

        self.assertEqual(self.root_cmake.count("cmake/BAASResources.cmake"), 1)
        self.assertEqual(
            self.root_cmake.count("baas_install_required_runtime_resources()"), 1
        )
        self.assertRegex(self.root_cmake, guarded_include)
        self.assertRegex(self.root_cmake, guarded_install)
        self.assertIn(
            'option(BAAS_FETCH_RESOURCES "Download legacy BAAS runtime resources during configure" OFF)',
            " ".join(self.root_cmake.split()),
        )

    def test_default_service_foundation_and_git2_profiles_keep_gate_closed(self) -> None:
        for option in LEGACY_OPTIONS:
            self.assertRegex(
                self.root_cmake,
                rf"option\({option}\s+\"[^\"]+\"\s+OFF\)",
            )

        workflows = {
            "service": ROOT / ".github/workflows/service-application.yml",
            "foundation": ROOT / ".github/workflows/foundation-runtime.yml",
            "runtime-git2": ROOT / ".github/workflows/runtime-repository-git2.yml",
        }
        for profile, path in workflows.items():
            workflow = path.read_text(encoding="utf-8")
            for option in LEGACY_OPTIONS:
                self.assertIn(
                    f"-D{option}=OFF",
                    workflow,
                    f"{profile} must keep the legacy resource gate closed",
                )

        legacy_subdirectories = {
            "BUILD_APP_BAAS": "apps/BAAS",
            "BUILD_APP_ISA": "apps/ISA",
            "BUILD_BAAS_OCR": "apps/ocr_server",
            "BUILD_BAAS_AW_CHECKER": "apps/BAAS_auto_fight_workflow_checker",
        }
        for option, directory in legacy_subdirectories.items():
            self.assertRegex(
                self.root_cmake,
                rf"if\s*\({option}\)\s+add_subdirectory\({re.escape(directory)}\)\s+endif\(\)",
            )

    def test_nonlegacy_cmake_has_no_payload_or_runtime_tree_input(self) -> None:
        cmake_files = [ROOT / "CMakeLists.txt", *sorted((ROOT / "cmake").glob("*.cmake"))]
        isolated = []
        for path in cmake_files:
            text = path.read_text(encoding="utf-8")
            if (
                "resources.lock.json" in text
                or "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resource" in text
            ):
                isolated.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(isolated, ["cmake/BAASResources.cmake"])

    def test_legacy_entry_points_explicitly_enable_downloads(self) -> None:
        workflows = (
            ROOT / ".github/workflows/baas_app.yaml",
            ROOT / ".github/workflows/baas_ocr.yaml",
            ROOT / ".github/workflows/baas_afwc.yaml",
        )
        for path in workflows:
            configure_commands = [
                line
                for line in path.read_text(encoding="utf-8").splitlines()
                if ("cmake " in line or line.lstrip().startswith("cmake "))
                and "--build" not in line
            ]
            self.assertTrue(configure_commands, path.name)
            for command in configure_commands:
                self.assertIn("-DBAAS_FETCH_RESOURCES=ON", command, path.name)

        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        for preset in presets["configurePresets"]:
            variables = preset["cacheVariables"]
            if any(variables.get(option) == "ON" for option in LEGACY_OPTIONS):
                self.assertEqual(variables.get("BAAS_FETCH_RESOURCES"), "ON")

    def test_shared_legacy_registry_has_one_final_install_pass(self) -> None:
        self.assertIn(
            "get_property(_resources GLOBAL PROPERTY BAAS_REQUIRED_RUNTIME_RESOURCES)",
            self.resource_cmake,
        )
        self.assertIn("list(APPEND _resources ${ARGN})", self.resource_cmake)
        self.assertIn(
            'set_property(GLOBAL PROPERTY BAAS_REQUIRED_RUNTIME_RESOURCES "${_resources}")',
            self.resource_cmake,
        )
        self.assertIn("list(REMOVE_DUPLICATES _resources)", self.resource_cmake)
        install_at = self.root_cmake.index(
            "baas_install_required_runtime_resources()"
        )
        for app_directory in (
            "add_subdirectory(apps/BAAS)",
            "add_subdirectory(apps/ISA)",
            "add_subdirectory(apps/ocr_server)",
            "add_subdirectory(apps/BAAS_auto_fight_workflow_checker)",
        ):
            self.assertLess(self.root_cmake.index(app_directory), install_at)

    def test_generated_state_and_ocr_fixtures_stay_out_of_source(self) -> None:
        gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
        ocr_utils = (ROOT / "apps/ocr_server/test/utils.py").read_text(
            encoding="utf-8"
        )
        self.assertIn(".baas-updater/", gitignore)
        self.assertIn("/apps/ocr_server/test/test_images/", gitignore)
        self.assertIn(
            'DEFAULT_TEST_RESOURCE_ROOT = PROJECT_ROOT / "build" / "ocr-test-resources"',
            ocr_utils,
        )
        self.assertNotIn("DEFAULT_TEST_RESOURCE_ROOT = TEST_DIR", ocr_utils)

    def test_documentation_assigns_external_state_ownership(self) -> None:
        documentation = (ROOT / "docs/script-runtime/RUNTIME_REPOSITORIES.md").read_text(
            encoding="utf-8"
        )
        for anchor in (
            "service runtime and optional libgit2 backend consume application-owned",
            "neither `resources.lock.json` nor the",
            "configure-time resource helper",
            "legacy\napplication exception",
            "New service, foundation, and runtime-repository\ntargets must not use that helper",
        ):
            self.assertIn(anchor, documentation)


if __name__ == "__main__":
    unittest.main()
