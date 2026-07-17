import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
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


def _extract_push_paths(path: pathlib.Path) -> set[str]:
    paths: set[str] = set()
    in_on = False
    in_push = False
    in_paths = False
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        indentation = len(line) - len(line.lstrip())
        if indentation == 0:
            if not stripped or stripped.startswith("#"):
                continue
            if in_on:
                break
            in_on = stripped.split("#", 1)[0].strip() == "on:"
            continue
        if not in_on:
            continue
        if indentation == 2:
            in_push = stripped.split("#", 1)[0].strip() == "push:"
            in_paths = False
            continue
        if not in_push:
            continue
        if indentation == 4:
            in_paths = stripped.split("#", 1)[0].strip() == "paths:"
            continue
        if in_paths and indentation > 4 and stripped.startswith("- "):
            paths.add(stripped.removeprefix("- ").strip("'\""))
    return paths


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
        self.assertNotIn("BAAS_INTERNAL_", self.root_cmake)
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
            "tests/build/external_resource_boundary/**",
        )
        workflows = {
            ROOT / ".github/workflows/baas_app.yaml": (
                "apps/BAAS/**",
                "apps/ISA/**",
                "resource/**",
            ),
            ROOT / ".github/workflows/baas_ocr.yaml": (
                "apps/ocr_server/**",
                "resource/**",
            ),
            ROOT / ".github/workflows/baas_afwc.yaml": (
                "apps/BAAS_auto_fight_workflow_checker/**",
                "apps/BAAS/resource/image/CN/zh-cn/image_info/skill_active.json",
            ),
        }
        for path, app_paths in workflows.items():
            push_paths = _extract_push_paths(path)
            self.assertIn(f".github/workflows/{path.name}", push_paths)
            for changed_path in (*common_paths, *app_paths):
                self.assertIn(changed_path, push_paths, path.name)

        app_workflow = (ROOT / ".github/workflows/baas_app.yaml").read_text(
            encoding="utf-8"
        )
        self.assertNotRegex(app_workflow, r"(?m)^\s*ref:\s*[\"']?main")
        self.assertIn("legacy-resource-profile-smoke:", app_workflow)
        self.assertIn(
            "ExternalResourceDeclarationHarnessTests",
            app_workflow,
        )
        self.assertRegex(
            app_workflow,
            r"needs:\s+- Windows-latest-cmake-x64\s+"
            r"- legacy-resource-profile-smoke",
        )
        self.assertRegex(
            app_workflow,
            r"permissions:\s+contents: write\s+actions: write",
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

    def test_apps_include_real_extracted_resource_declarations(self) -> None:
        app_modules = {
            "apps/BAAS": "apps/BAAS/cmake/RuntimeResources.cmake",
            "apps/ISA": "apps/ISA/cmake/RuntimeResources.cmake",
            "apps/ocr_server": "apps/ocr_server/cmake/RuntimeResources.cmake",
            "apps/BAAS_auto_fight_workflow_checker": (
                "apps/BAAS_auto_fight_workflow_checker/cmake/RuntimeResources.cmake"
            ),
        }
        for app_directory, module_path in app_modules.items():
            app_cmake = (ROOT / app_directory / "CMakeLists.txt").read_text(
                encoding="utf-8"
            )
            module = (ROOT / module_path).read_text(encoding="utf-8")
            self.assertIn('include("${CMAKE_CURRENT_LIST_DIR}/cmake/RuntimeResources.cmake")', app_cmake)
            for resource_call in (
                "baas_require_runtime_resources(",
                "baas_fetch_resources(",
                "baas_copy_local_runtime_resources(",
            ):
                self.assertNotIn(resource_call, app_cmake, app_directory)
            self.assertTrue(
                any(
                    resource_call in module
                    for resource_call in (
                        "baas_require_runtime_resources(",
                        "baas_fetch_resources(",
                        "baas_copy_local_runtime_resources(",
                    )
                ),
                module_path,
            )

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
        updater_header = (
            ROOT / "include/runtime/repository/RuntimeRepositoryUpdater.h"
        ).read_text(encoding="utf-8")
        updater_source = (
            ROOT / "src/runtime/repository/RuntimeRepositoryUpdater.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("class RuntimeRepositoryCommitClaim", updater_header)
        self.assertIn("enum class RuntimeRepositoryRecoveryPolicy", updater_header)
        self.assertIn("recover(const RuntimeRepositoryTreeValidator& validator)", updater_header)
        self.assertIn("commit_claim->claim(journal.new_current.generation)", updater_source)
        self.assertIn("A same-generation no-op does not invoke the claim", documentation)
        self.assertIn("Crash recovery is a separate startup phase", documentation)
        self.assertIn("but a\nbrowser request is never a repository policy source", documentation)
        self.assertIn("baas.runtime-repositories.signed-plan-envelope/v1", documentation)
        self.assertIn("Signature verification happens before payload parsing", documentation)
        self.assertIn("one long-lived verifier that owns the trust key", documentation)
        self.assertIn("no-DOM SAX preparse", documentation)
        self.assertIn("highest accepted\nsequence", documentation)
        self.assertIn("RuntimeRepositoryTrustedPlanUpdateOwner", documentation)
        self.assertIn("No update request is\naccepted until this startup recovery succeeds", documentation)
        self.assertIn("The owner holds `.trusted-plan-writer.lock`", documentation)
        self.assertIn("Starting standalone owner recovery is the explicit, irreversible", documentation)
        self.assertIn("can then be adopted only by a\nsigned plan", documentation)
        self.assertIn("terminal `RuntimeRepositoryCommitClaim`", documentation)
        self.assertIn("The existing Tauri\nexact-generation launch path remains a reader-only path", documentation)
        self.assertIn("`BAAS_runtime_repository_update` publisher", documentation)
        trusted_plan_header = (
            ROOT / "include/service/app/RuntimeRepositoryTrustedPlan.h"
        ).read_text(encoding="utf-8")
        self.assertIn("class VerifiedRuntimeRepositoryPlan", trusted_plan_header)
        self.assertIn("class RuntimeRepositoryTrustedStateProvider", trusted_plan_header)
        self.assertIn("class RuntimeRepositoryTrustedPlanVerifier", trusted_plan_header)
        trusted_state_header = (
            ROOT / "include/service/app/RuntimeRepositoryTrustedPlanState.h"
        ).read_text(encoding="utf-8")
        owner_header = (
            ROOT / "include/service/app/RuntimeRepositoryTrustedPlanUpdateOwner.h"
        ).read_text(encoding="utf-8")
        self.assertIn("class RuntimeRepositoryTrustedPlanStateStore", trusted_state_header)
        self.assertIn("class RuntimeRepositoryTrustedPlanUpdateOwner", owner_header)
        self.assertIn("previous_generations", documentation)


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


class ExternalResourceDeclarationHarnessTests(unittest.TestCase):
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
            raise RuntimeError("CMake is required for resource declaration tests")
        cls.harness = ROOT / "tests/build/external_resource_boundary"

    def _run_harness(self, profile: str, mode: str) -> dict[str, str]:
        with tempfile.TemporaryDirectory() as temporary:
            temp = pathlib.Path(temporary)
            build = temp / "build"
            result_file = temp / "result.txt"
            environment = os.environ.copy()
            environment.update(
                {
                    "HTTP_PROXY": "http://127.0.0.1:9",
                    "HTTPS_PROXY": "http://127.0.0.1:9",
                    "ALL_PROXY": "http://127.0.0.1:9",
                    "NO_PROXY": "",
                }
            )
            result = subprocess.run(
                [
                    self.cmake,
                    "-S",
                    str(self.harness),
                    "-B",
                    str(build),
                    f"-DBAAS_REPOSITORY_ROOT={ROOT.as_posix()}",
                    f"-DBAAS_RESOURCE_PROFILE={profile}",
                    f"-DBAAS_RESOURCE_HARNESS_MODE={mode}",
                    f"-DBAAS_RESOURCE_RESULT_FILE={result_file.as_posix()}",
                ],
                cwd=ROOT,
                env=environment,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=30,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            values = {}
            for line in result_file.read_text(encoding="utf-8").splitlines():
                key, value = line.split("=", 1)
                values[key] = value.replace("\\", "/")
            if mode == "execute":
                fetch_sentinel = build / "fetch-must-not-run"
                self.assertTrue(fetch_sentinel.is_file())
                self.assertEqual(
                    fetch_sentinel.read_text(encoding="utf-8"),
                    "offline fetch sentinel\n",
                )
            return values

    @staticmethod
    def _local_directory_outputs(
        base: pathlib.Path, items: tuple[str, ...]
    ) -> set[str]:
        outputs: set[str] = set()
        for item in items:
            source = base / item
            if item == "config":
                outputs.update(
                    f"resource/{path.name}" for path in source.glob("*.json")
                )
                continue
            outputs.update(
                f"resource/{item}/{path.relative_to(source).as_posix()}"
                for path in source.rglob("*")
                if path.is_file()
            )
        return outputs

    def test_real_declaration_modules_record_exact_resource_sets(self) -> None:
        expected = {
            "baas": {
                "required": (
                    "global_config,static_config,adb,scrcpy_server,ocr_models,"
                    "yolo_models"
                ),
                "fetched": "",
                "copy_items": ["config,image,feature,procedure,auto_fight_workflow"],
                "copy_bases": ["/apps/BAAS/resource"],
                "copy_destinations": [""],
            },
            "isa": {
                "required": "global_config,static_config,adb,scrcpy_server,ocr_models",
                "fetched": "",
                "copy_items": ["config,image,feature,procedure"],
                "copy_bases": ["/apps/ISA/resource"],
                "copy_destinations": [""],
            },
            "ocr": {
                "required": "global_config,static_config,ocr_models",
                "fetched": "",
                "copy_items": [],
                "copy_bases": [],
                "copy_destinations": [],
            },
            "afwc": {
                "required": "",
                "fetched": "yolo_models",
                "copy_items": ["skill_active.json", "data.yaml"],
                "copy_bases": [
                    "/apps/BAAS/resource/image/CN/zh-cn/image_info",
                    "/recorded-fetched-root/yolo_models",
                ],
                "copy_destinations": ["/runtime", "/runtime"],
            },
        }
        for profile, contract in expected.items():
            with self.subTest(profile=profile):
                values = self._run_harness(profile, "record")
                self.assertEqual(values["required"], contract["required"])
                self.assertEqual(values["fetched"], contract["fetched"])
                copy_count = int(values["copy_count"])
                self.assertEqual(copy_count, len(contract["copy_items"]))
                self.assertEqual(
                    [values[f"copy_{index}_items"] for index in range(copy_count)],
                    contract["copy_items"],
                )
                for index, suffix in enumerate(contract["copy_bases"]):
                    self.assertTrue(values[f"copy_{index}_base"].endswith(suffix))
                for index, suffix in enumerate(contract["copy_destinations"]):
                    destination = values[f"copy_{index}_destination"]
                    if suffix:
                        self.assertTrue(destination.endswith(suffix))
                    else:
                        self.assertEqual(destination, "")

    def test_real_declarations_copy_and_install_from_offline_preseed(self) -> None:
        if os.name == "nt":
            platform_name = "Windows"
            adb_name = "adb.exe"
        elif sys.platform == "darwin":
            platform_name = "MacOS"
            adb_name = "adb"
        else:
            platform_name = "Linux"
            adb_name = "adb"

        common_installed = {
            "resource/global_setting.json",
            "resource/static.json",
        }
        platform_tools = {
            adb_name,
            f"resource/bin/{platform_name}/platform-tools/{adb_name}",
        }
        if os.name == "nt":
            for library in ("AdbWinApi.dll", "AdbWinUsbApi.dll"):
                platform_tools.update(
                    {
                        library,
                        f"resource/bin/{platform_name}/platform-tools/{library}",
                    }
                )

        baas_local = self._local_directory_outputs(
            ROOT / "apps/BAAS/resource",
            ("config", "image", "feature", "procedure", "auto_fight_workflow"),
        )
        isa_local = self._local_directory_outputs(
            ROOT / "apps/ISA/resource",
            ("config", "image", "feature", "procedure"),
        )
        expected_files = {
            "baas": common_installed
            | platform_tools
            | baas_local
            | {
                "resource/bin/scrcpy/server.bin",
                "resource/ocr_models/model.bin",
                "resource/yolo_models/model.bin",
                "resource/yolo_models/data.yaml",
            },
            "isa": common_installed
            | platform_tools
            | isa_local
            | {
                "resource/bin/scrcpy/server.bin",
                "resource/ocr_models/model.bin",
            },
            "ocr": common_installed | {"resource/ocr_models/model.bin"},
            "afwc": {"skill_active.json", "data.yaml"},
        }
        harness_source = (self.harness / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertEqual(
            harness_source.count("baas_install_required_runtime_resources()"), 1
        )
        self.assertIn("set(BAAS_FETCH_RESOURCES OFF)", harness_source)

        for profile, expected_profile_files in expected_files.items():
            with self.subTest(profile=profile):
                values = self._run_harness(profile, "execute")
                installed = set(filter(None, values["files"].split(",")))
                self.assertEqual(
                    installed,
                    expected_profile_files,
                    {
                        "unexpected": sorted(installed - expected_profile_files),
                        "missing": sorted(expected_profile_files - installed),
                    },
                )
                self.assertEqual(values["fetch_sentinel"], "unchanged")


if __name__ == "__main__":
    unittest.main()
