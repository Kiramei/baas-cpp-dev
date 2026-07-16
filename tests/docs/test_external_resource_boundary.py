import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
LEGACY_OPTIONS = (
    "BUILD_APP_BAAS",
    "BUILD_APP_ISA",
    "BUILD_BAAS_OCR",
    "BUILD_BAAS_AW_CHECKER",
)


def _extract_configure_commands(path: pathlib.Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    commands: list[str] = []
    for index, line in enumerate(lines):
        candidate = line.strip()
        if candidate.startswith("run:"):
            candidate = candidate.removeprefix("run:").strip()
        if not re.match(r"^(?:emcmake\s+)?cmake\s+", candidate):
            continue
        if any(marker in candidate for marker in ("cmake --build", "cmake --version")):
            continue

        pieces = [candidate]
        continuation = index + 1
        while continuation < len(lines):
            argument = lines[continuation].strip()
            if not re.match(r"^(?:-D|-G(?:\s|$)|[\"']-D)", argument):
                break
            pieces.append(argument)
            continuation += 1
        commands.append(" ".join(pieces))
    return commands


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
            self.root_cmake.count("baas_install_required_runtime_resources()"), 2
        )
        self.assertRegex(self.root_cmake, guarded_include)
        self.assertRegex(self.root_cmake, guarded_install)
        self.assertIn("if(BAAS_INTERNAL_RESOURCE_BOUNDARY_CONFIGURE_ONLY)", self.root_cmake)
        self.assertIn('"tooling=enabled\\ninstaller=called\\n"', self.root_cmake)
        self.assertIn(
            'option(BAAS_FETCH_RESOURCES "Download legacy BAAS runtime resources during configure" OFF)',
            " ".join(self.root_cmake.split()),
        )

    def test_every_nonlegacy_configure_command_closes_the_gate(self) -> None:
        workflows = {
            "service": ROOT / ".github/workflows/service-application.yml",
            "foundation": ROOT / ".github/workflows/foundation-runtime.yml",
            "runtime-git2": ROOT / ".github/workflows/runtime-repository-git2.yml",
        }
        for profile, path in workflows.items():
            commands = _extract_configure_commands(path)
            self.assertTrue(commands, profile)
            for command in commands:
                for option in LEGACY_OPTIONS:
                    self.assertIn(
                        f"-D{option}=OFF",
                        command,
                        f"{profile} command leaves {option} implicit: {command}",
                    )
                self.assertIn(
                    "-DBAAS_FETCH_RESOURCES=OFF",
                    command,
                    f"{profile} command leaves fetch policy implicit: {command}",
                )

    def test_every_legacy_command_and_preset_declares_the_full_profile(self) -> None:
        workflows = {
            ROOT / ".github/workflows/baas_app.yaml": "BUILD_APP_BAAS",
            ROOT / ".github/workflows/baas_ocr.yaml": "BUILD_BAAS_OCR",
            ROOT / ".github/workflows/baas_afwc.yaml": "BUILD_BAAS_AW_CHECKER",
        }
        for path, active_option in workflows.items():
            commands = _extract_configure_commands(path)
            self.assertTrue(commands, path.name)
            for command in commands:
                self.assertIn(f"-D{active_option}=", command, path.name)
                self.assertNotIn(f"-D{active_option}=OFF", command, path.name)
                for option in LEGACY_OPTIONS:
                    if option != active_option:
                        self.assertIn(f"-D{option}=OFF", command, path.name)
                self.assertIn("-DBAAS_FETCH_RESOURCES=ON", command, path.name)

        presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        isa_checked = False
        for preset in presets["configurePresets"]:
            variables = preset["cacheVariables"]
            for option in LEGACY_OPTIONS:
                self.assertIn(option, variables, preset["name"])
            enabled = [option for option in LEGACY_OPTIONS if variables[option] == "ON"]
            if enabled:
                self.assertEqual(len(enabled), 1, preset["name"])
                self.assertEqual(variables["BAAS_FETCH_RESOURCES"], "ON")
            else:
                self.assertEqual(variables["BAAS_FETCH_RESOURCES"], "OFF")
            if preset["name"] == "conan-windows-msvc-release-isa":
                self.assertEqual(enabled, ["BUILD_APP_ISA"])
                isa_checked = True
        self.assertTrue(isa_checked)

    def test_legacy_push_paths_and_source_checkouts_cover_boundary_changes(self) -> None:
        common_paths = (
            "CMakeLists.txt",
            "CMakePresets.json",
            "cmake/BAASResources.cmake",
            "resources.lock.json",
            "scripts/fetch_resources.py",
            "tests/docs/test_external_resource_boundary.py",
        )
        workflows = {
            ROOT / ".github/workflows/baas_app.yaml": ("apps/BAAS/**", "apps/ISA/**"),
            ROOT / ".github/workflows/baas_ocr.yaml": ("apps/ocr_server/**",),
            ROOT / ".github/workflows/baas_afwc.yaml": (
                "apps/BAAS_auto_fight_workflow_checker/**",
            ),
        }
        for path, app_paths in workflows.items():
            workflow = path.read_text(encoding="utf-8")
            self.assertIn(f"- '.github/workflows/{path.name}'", workflow)
            for changed_path in (*common_paths, *app_paths):
                self.assertIn(f"- '{changed_path}'", workflow, path.name)

        app_workflow = (ROOT / ".github/workflows/baas_app.yaml").read_text(
            encoding="utf-8"
        )
        self.assertNotRegex(app_workflow, r"(?m)^\s*ref:\s*[\"']?main")
        self.assertIn("legacy-resource-profile-smoke:", app_workflow)
        self.assertIn(
            "test_each_legacy_entry_reaches_the_real_gate_without_fetching",
            app_workflow,
        )
        ocr_workflow = (ROOT / ".github/workflows/baas_ocr.yaml").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("repository: pur1fying/BAAS_Cpp", ocr_workflow)
        self.assertNotRegex(ocr_workflow, r"(?m)^\s*ref:\s*[\"']?main\s*[\"']?\s*$")

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
        install_at = self.root_cmake.rindex(
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
        ocr_readme = (ROOT / "apps/ocr_server/README.md").read_text(encoding="utf-8")
        self.assertIn(".baas-updater/", gitignore)
        self.assertIn("/apps/ocr_server/test/test_images/", gitignore)
        self.assertIn(
            'DEFAULT_TEST_RESOURCE_ROOT = PROJECT_ROOT / "build" / "ocr-test-resources"',
            ocr_utils,
        )
        self.assertNotIn("DEFAULT_TEST_RESOURCE_ROOT = TEST_DIR", ocr_utils)
        self.assertIn("`build/ocr-test-resources/test_images`", ocr_readme)
        self.assertNotIn("`apps/ocr_server/test/test_images`", ocr_readme)

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


class ExternalResourceConfigureSmokeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cmake = shutil.which("cmake")
        if cls.cmake is None and os.name == "nt":
            bundled = pathlib.Path(
                "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/"
                "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
            )
            if bundled.is_file():
                cls.cmake = str(bundled)
        if cls.cmake is None:
            raise RuntimeError("CMake is required for resource-boundary configure tests")

    def _write_fake_conan_configs(self, directory: pathlib.Path) -> pathlib.Path:
        directory.mkdir(parents=True)
        toolchain = directory / "conan_toolchain.cmake"
        toolchain.write_text("# Offline resource-boundary dependency fixture.\n", encoding="utf-8")
        packages = {
            "baas-cpp-httplib-config.cmake": "BAAS::httplib",
            "baas-libsodium-config.cmake": "BAAS::sodium",
            "baas-nlohmann-json-config.cmake": "BAAS::nlohmann_json",
            "baas-miniz-config.cmake": "BAAS::miniz",
            "baas-libgit2-config.cmake": "BAAS::libgit2",
        }
        for filename, target in packages.items():
            (directory / filename).write_text(
                f"if(NOT TARGET {target})\n"
                f"  add_library({target} INTERFACE IMPORTED GLOBAL)\n"
                "endif()\n",
                encoding="utf-8",
            )
        return toolchain

    def _run_configure(
        self,
        build: pathlib.Path,
        arguments: list[str],
    ) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment.update(
            {
                "HTTP_PROXY": "http://127.0.0.1:9",
                "HTTPS_PROXY": "http://127.0.0.1:9",
                "ALL_PROXY": "http://127.0.0.1:9",
                "NO_PROXY": "",
            }
        )
        return subprocess.run(
            [self.cmake, "-S", str(ROOT), "-B", str(build), *arguments],
            cwd=ROOT,
            env=environment,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=120,
            check=False,
        )

    def _assert_no_resource_outputs(self, build: pathlib.Path) -> None:
        for path in (
            build / "bin/resource",
            build / "baas-resources",
            build / "baas-resource-downloads",
        ):
            self.assertFalse(path.exists(), str(path))

    def test_default_service_foundation_and_git2_configure_without_resources(self) -> None:
        foundation = [
            "-DBUILD_TESTING=ON",
            "-DBUILD_SCRIPT_TESTS=ON",
            "-DBUILD_SCRIPT_TOOLS=ON",
            "-DBUILD_RESOURCE_CORE_TESTS=ON",
            "-DBUILD_RUNTIME_REPOSITORY_TESTS=ON",
            "-DBUILD_RUNTIME_REPOSITORY_UPDATER_TESTS=ON",
            "-DBUILD_SCRIPT_RESOURCE_HOST_TESTS=ON",
            "-DBUILD_SERVICE_PROTOCOL_TESTS=ON",
            "-DBUILD_SERVICE_TRIGGER_CATALOG_TESTS=ON",
            "-DBUILD_SERVICE_TRIGGER_DISPATCH_TESTS=ON",
            "-DBUILD_SERVICE_TRIGGER_EXECUTOR_TESTS=ON",
            "-DBUILD_SERVICE_PIPE_HOST_TESTS=ON",
            "-DBUILD_SERVICE_TRIGGER_PIPE_CHANNEL_TESTS=ON",
            "-DBUILD_SERVICE_ROUTER_TESTS=ON",
            "-DBUILD_SERVICE_ORIGIN_POLICY_TESTS=ON",
        ]
        profiles = {
            "default": [],
            "service": ["-DBUILD_SERVICE_APP=ON"],
            "foundation": foundation,
            "runtime-git2": ["-DBUILD_RUNTIME_REPOSITORY_GIT2=ON"],
        }
        with tempfile.TemporaryDirectory() as temporary:
            temp = pathlib.Path(temporary)
            toolchain = self._write_fake_conan_configs(temp / "fake-conan")
            for profile, profile_arguments in profiles.items():
                with self.subTest(profile=profile):
                    build = temp / f"build-{profile}"
                    fetch_guard = temp / f"fetch-must-not-run-{profile}"
                    fetch_guard.write_text("offline fetch sentinel\n", encoding="utf-8")
                    common = [
                        "-DCMAKE_BUILD_TYPE=Debug",
                        f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}",
                        "-DBAAS_FETCH_RESOURCES=ON",
                        f"-DBAAS_RESOURCE_DOWNLOAD_ROOT={fetch_guard.as_posix()}",
                        f"-DBAAS_RESOURCE_OUTPUT_ROOT={fetch_guard.as_posix()}",
                        *(f"-D{option}=OFF" for option in LEGACY_OPTIONS),
                    ]
                    result = self._run_configure(build, [*common, *profile_arguments])
                    self.assertEqual(
                        result.returncode,
                        0,
                        result.stdout + result.stderr,
                    )
                    self.assertEqual(
                        fetch_guard.read_text(encoding="utf-8"),
                        "offline fetch sentinel\n",
                    )
                    self._assert_no_resource_outputs(build)

    def test_each_legacy_entry_reaches_the_real_gate_without_fetching(self) -> None:
        for active_option in LEGACY_OPTIONS:
            with self.subTest(active_option=active_option), tempfile.TemporaryDirectory() as temporary:
                temp = pathlib.Path(temporary)
                build = temp / "build"
                result_sentinel = temp / "configure-result.txt"
                fetch_guard = temp / "fetch-must-not-run"
                fetch_guard.write_text("offline fetch sentinel\n", encoding="utf-8")
                arguments = [
                    "-DCMAKE_BUILD_TYPE=Debug",
                    "-DBAAS_INTERNAL_RESOURCE_BOUNDARY_CONFIGURE_ONLY=ON",
                    f"-DBAAS_INTERNAL_RESOURCE_BOUNDARY_SENTINEL={result_sentinel.as_posix()}",
                    "-DBAAS_FETCH_RESOURCES=ON",
                    f"-DBAAS_RESOURCE_DOWNLOAD_ROOT={fetch_guard.as_posix()}",
                    f"-DBAAS_RESOURCE_OUTPUT_ROOT={fetch_guard.as_posix()}",
                    *(
                        f"-D{option}={'ON' if option == active_option else 'OFF'}"
                        for option in LEGACY_OPTIONS
                    ),
                ]
                result = self._run_configure(build, arguments)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(
                    result_sentinel.read_text(encoding="utf-8"),
                    "tooling=enabled\ninstaller=called\n",
                )
                self.assertEqual(
                    fetch_guard.read_text(encoding="utf-8"),
                    "offline fetch sentinel\n",
                )
                self._assert_no_resource_outputs(build)


if __name__ == "__main__":
    unittest.main()
