import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ServiceApplicationContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "include/service/app/ServiceApplication.h").read_text(
            encoding="utf-8"
        )
        cls.source = (ROOT / "src/service/app/ServiceApplication.cpp").read_text(
            encoding="utf-8"
        )
        cls.main = (ROOT / "apps/BAAS_service/main.cpp").read_text(encoding="utf-8")
        cls.cmake = (ROOT / "cmake/ServiceApplication.cmake").read_text(
            encoding="utf-8"
        )
        cls.tests = (ROOT / "tests/service/ServiceApplicationTests.cpp").read_text(
            encoding="utf-8"
        )
        cls.docs = (ROOT / "docs/script-runtime/SERVICE_APPLICATION.md").read_text(
            encoding="utf-8"
        )
        cls.workflow = (
            ROOT / ".github/workflows/service-application.yml"
        ).read_text(encoding="utf-8")

    def test_real_composition_is_explicit_and_remote_is_production(self) -> None:
        for anchor in (
            "ProductionProviderBackend",
            "FileResourceStore",
            "ProductionRemoteBackend",
            "ServiceAdbTransport",
            "RemoteHandlerFactory",
            "make_status_trigger_registration",
            "make_adb_discovery_trigger_registration",
            "TriggerDispatcher::create",
            "TriggerExecutor",
            "TriggerHandlerFactory",
            "make_file_auth_storage",
            "make_system_auth_clock",
            "make_system_auth_random",
            "make_sodium_password_deriver",
            "open_production_http_host",
            "RemoteChannelPolicy::desktop_only",
            "remote_server_jar",
        ):
            self.assertIn(anchor, self.source)

    def test_default_application_keeps_production_runtime_heavy_closure_opt_in(self) -> None:
        application_target = self.cmake[: self.cmake.index(
            "if(TARGET BAAS_service_production_runtime_task_control)"
        )]
        self.assertNotIn(
            "BAAS_service_production_runtime_script_task_factory", application_target
        )
        self.assertNotIn(
            "BAAS_service_production_runtime_task_control", application_target
        )
        self.assertIn("runtime_task_composition_factory", self.header)
        self.assertIn("runtime_task_composition_factory->compose", self.source)

    def test_executable_contract_and_pipe_boundary_are_owned(self) -> None:
        self.assertIn("int wmain", self.main)
        self.assertIn("int main", self.main)
        self.assertIn("WideCharToMultiByte", self.main)
        self.assertIn("OUTPUT_NAME \"BAAS_service\"", self.cmake)
        self.assertIn("BAAS_service_remote_backend", self.cmake)
        self.assertIn("pipe_transport_unavailable", self.source)
        self.assertIn("--project-root <directory>", self.docs)
        self.assertIn("desktop remote transport is enabled", self.source)
        self.assertIn("<project-root>/service/remote/scrcpy-server.jar", self.docs)
        for exit_code in ("command_line = 2", "pipe_unavailable = 3", "host_start = 6"):
            self.assertIn(exit_code, self.header)

    def test_executable_and_tauri_wire_identities_are_separate(self) -> None:
        self.assertIn(
            'service_application_executable_name =\n    "BAAS_service"', self.header
        )
        self.assertIn(
            'service_application_wire_name =\n    "BAAS Service"', self.header
        )
        self.assertIn("std::string{service_application_wire_name}", self.source)
        self.assertIn('"service":"BAAS Service"', self.tests)
        for anchor in (
            "`baas-tauri` revision `a1c8c837`",
            "`src-tauri/src/commands.rs`",
            "`cpp_backend_ready`",
            "exact service\nliteral `BAAS Service`",
        ):
            self.assertIn(anchor, self.docs)

    def test_real_evidence_is_hook_free_and_cross_platform(self) -> None:
        for anchor in (
            "health_starting",
            "invalid_remember_request",
            "copy_config",
            "detect_adb",
            "command_response",
            "storage_failure",
            "bind_failed",
            "pipe_transport_unavailable",
        ):
            self.assertIn(anchor, self.tests)
        self.assertNotIn("TEST_HOOKS=1", self.cmake)
        self.assertIn("BUILD_SERVICE_APP_TESTS", self.cmake)
        self.assertIn("BAAS_service_no_embedded_configuration_defaults", self.cmake)
        self.assertIn("no_embedded_configuration_defaults", self.workflow)
        for platform in ("windows-latest", "ubuntu-latest", "macos-latest"):
            self.assertIn(platform, self.workflow)
        for build_type in ("Debug", "Release"):
            self.assertIn(build_type, self.workflow)

    def test_application_ci_path_filter_covers_shared_build_and_conan_glue(self) -> None:
        for path in (
            "'cmake/**'",
            "'deploy/conan/conanfile.py'",
            "'deploy/conan/profiles/**'",
            "'deploy/conan/scripts/**'",
            "'deploy/conan/recipes/baas-libsodium/**'",
            "'deploy/conan/recipes/baas-cpp-httplib/**'",
            "'deploy/conan/recipes/baas-nlohmann-json/**'",
            "'tests/conan/baas-opencv-sibling-scope/**'",
        ):
            self.assertEqual(self.workflow.count(path), 2)
        self.assertNotIn("'cmake/Service*.cmake'", self.workflow)

    def test_opencv_sibling_scope_smoke_is_a_host_matrix_gate(self) -> None:
        smoke = self.workflow[
            self.workflow.index(
                "- name: Configure OpenCV sibling-scope package smoke"
            ) : self.workflow.index("- name: Configure hook-free production application")
        ]
        self.assertIn("tests/conan/baas-opencv-sibling-scope", smoke)
        self.assertIn("build/conan/service-application/conan_toolchain.cmake", smoke)
        self.assertIn("matrix.build_type", smoke)
        self.assertIn("cmake --build build/baas-opencv-sibling-scope", smoke)
        self.assertIn("ctest --test-dir build/baas-opencv-sibling-scope", smoke)

    def test_android_ci_explicitly_enables_every_built_production_runtime_target(self) -> None:
        configure = self.workflow[
            self.workflow.index("- name: Configure Android service composition") :
            self.workflow.index("- name: Cross-build Android service composition")
        ]
        build = self.workflow[
            self.workflow.index("- name: Cross-build Android service composition") :
        ]
        for option in (
            "-DBUILD_SERVICE_PRODUCTION_RUNTIME_SCRIPT_TASK_FACTORY=ON",
            "-DBUILD_SERVICE_PRODUCTION_RUNTIME_TASK_CONTROL=ON",
        ):
            self.assertIn(option, configure)
        for target in (
            "BAAS_service_production_runtime_script_task_factory",
            "BAAS_service_production_runtime_task_control",
        ):
            self.assertIn(target, build)

    def test_default_lightweight_ci_build_has_no_opencv_injected(self) -> None:
        dependencies = self.workflow[
            self.workflow.index(
                "- name: Generate default lightweight application dependencies"
            ) : self.workflow.index(
                "- name: Configure default lightweight application without OpenCV"
            )
        ]
        configure = self.workflow[
            self.workflow.index(
                "- name: Configure default lightweight application without OpenCV"
            ) : self.workflow.index(
                "- name: Build default lightweight application"
            )
        ]
        build = self.workflow[
            self.workflow.index("- name: Build default lightweight application") :
            self.workflow.index("- name: Build pinned OpenCV recipe")
        ]
        self.assertNotIn("opencv", dependencies.lower())
        self.assertIn("-DBUILD_SERVICE_APP=ON", configure)
        for option in (
            "-DBUILD_SERVICE_PRODUCTION_RUNTIME_SCRIPT_TASK_FACTORY=OFF",
            "-DBUILD_SERVICE_PRODUCTION_RUNTIME_TASK_CONTROL=OFF",
            "-DBAAS_FETCH_RESOURCES=OFF",
        ):
            self.assertIn(option, configure)
        for target in ("BAAS_service", "BAAS_service_application"):
            self.assertIn(target, build)


if __name__ == "__main__":
    unittest.main()
