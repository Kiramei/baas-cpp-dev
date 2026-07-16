import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceConfigurationTriggerDocsTests(unittest.TestCase):
    def test_defaults_are_admitted_from_the_runtime_resource_snapshot(self) -> None:
        self.assertFalse(
            (ROOT / "src/service/adapters/ConfigurationDefaults.h").exists()
        )
        loader = (
            ROOT / "src/service/app/RuntimeConfigurationDefaults.cpp"
        ).read_text(encoding="utf-8")
        application = (ROOT / "src/service/app/ServiceApplication.cpp").read_text(
            encoding="utf-8"
        )
        for name in ("user.json", "event.json", "switch.json", "static.json"):
            self.assertIn(f'"service/configuration/defaults/{name}"', loader)
        self.assertIn("consumer_limits.max_json_depth", loader)
        self.assertIn("consumer_limits.max_json_nodes", loader)
        self.assertIn("consumer_limits.max_json_bytes", loader)
        self.assertIn("adapters::bounded_json::parse_json", loader)
        self.assertIn("value.dump(2).size()", loader)
        self.assertIn('candidate["name"] = "x"', loader)
        for server in ('u8"官服"', 'u8"国际服青少年"', 'u8"日服PC端"'):
            self.assertIn(server, loader)
        self.assertIn("load_runtime_configuration_defaults", application)
        self.assertLess(
            application.index("load_runtime_configuration_defaults"),
            application.index("make_shared<adapters::FileResourceStore>"),
        )

    def test_store_uses_injected_defaults_without_a_compiled_fallback(self) -> None:
        header = (ROOT / "include/service/adapters/ConfigurationDefaults.h").read_text(
            encoding="utf-8"
        )
        store = (ROOT / "src/service/adapters/FileResourceStore.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("std::string user_json", header)
        self.assertIn("configuration_defaults", store)
        self.assertIn('.static_json).at("create_item_order")', store)
        self.assertNotIn("BAAS_DEFAULT", store)
        self.assertNotIn("BAAS_STATIC", store)

    def test_production_slice_is_registered_built_and_migration_bounded(self) -> None:
        header = (ROOT / "include/service/app/ConfigurationTriggerRegistration.h").read_text(encoding="utf-8")
        source = (ROOT / "src/service/app/ConfigurationTriggerRegistration.cpp").read_text(encoding="utf-8")
        app = (ROOT / "src/service/app/ServiceApplication.cpp").read_text(encoding="utf-8")
        cmake = (ROOT / "cmake/ServiceConfigurationTriggers.cmake").read_text(encoding="utf-8")
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/service-application.yml").read_text(encoding="utf-8")
        android_workflow = (ROOT / ".github/workflows/service-auth.yml").read_text(encoding="utf-8")
        spec = (ROOT / "docs/script-runtime/SERVICE_CONFIGURATION_TRIGGERS.md").read_text(encoding="utf-8")

        self.assertIn('"copy_config"', source)
        self.assertIn('"remove_config*"', source)
        self.assertIn('"add_config*"', source)
        self.assertIn('"export_config"', source)
        self.assertIn('"import_config"', source)
        self.assertIn("make_configuration_trigger_registrations", app)
        self.assertIn("BAAS_service_configuration_trigger_tests", cmake)
        self.assertIn("BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS", root_cmake)
        self.assertIn("BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS=ON", workflow)
        self.assertIn("BAAS_service_configuration_trigger_tests", workflow)
        self.assertIn("-DBUILD_SERVICE_CONFIGURATION_TRIGGERS=ON", android_workflow)
        self.assertIn("BAAS_service_configuration_triggers", android_workflow)
        self.assertIn(
            "-DBUILD_SERVICE_RUNTIME_CONFIGURATION_DEFAULTS=ON", android_workflow
        )
        self.assertIn(
            "BAAS_service_runtime_configuration_defaults", android_workflow
        )
        self.assertIn("--requires=baas-nlohmann-json/3.11.3", android_workflow)
        self.assertIn("--requires=baas-miniz/3.1.2", workflow)
        self.assertIn("--requires=baas-miniz/3.1.2", android_workflow)
        self.assertIn("BAAS_service_config_archive_codec_tests", workflow)
        self.assertIn("arm64-v8a", android_workflow)
        self.assertIn("x86_64", android_workflow)
        for anchor in (
            "service/api/commands.py",
            "ServiceRuntime._copy_config_sync",
            "ServiceRuntime.remove_config",
            "`update_setup_toml` remains unregistered",
            "one coherent five-command",
            "1,024:1",
            ".baas-import-journal-*",
            "ServiceRuntime.add_config",
            "config/.baas-create-*",
            "4,096 total tree entries",
            "64 MiB total",
            "protected DACL",
        ):
            self.assertIn(anchor, spec)
        self.assertEqual(
            workflow.count("docs/script-runtime/SERVICE_CONFIGURATION_TRIGGERS.md"), 2
        )


if __name__ == "__main__":
    unittest.main()
