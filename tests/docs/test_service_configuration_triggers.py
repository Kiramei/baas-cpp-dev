import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceConfigurationTriggerDocsTests(unittest.TestCase):
    def test_production_slice_is_registered_built_and_migration_bounded(self) -> None:
        header = (ROOT / "include/service/app/ConfigurationTriggerRegistration.h").read_text(encoding="utf-8")
        source = (ROOT / "src/service/app/ConfigurationTriggerRegistration.cpp").read_text(encoding="utf-8")
        app = (ROOT / "src/service/app/ServiceApplication.cpp").read_text(encoding="utf-8")
        cmake = (ROOT / "cmake/ServiceConfigurationTriggers.cmake").read_text(encoding="utf-8")
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/foundation-runtime.yml").read_text(encoding="utf-8")
        spec = (ROOT / "docs/script-runtime/SERVICE_CONFIGURATION_TRIGGERS.md").read_text(encoding="utf-8")

        self.assertIn('"copy_config"', source)
        self.assertIn('"remove_config*"', source)
        self.assertIn("make_configuration_trigger_registrations", app)
        self.assertIn("BAAS_service_configuration_trigger_tests", cmake)
        self.assertIn("BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS", root_cmake)
        self.assertIn("BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS=ON", workflow)
        self.assertIn("BAAS_service_configuration_trigger_tests", workflow)
        for anchor in (
            "service/api/commands.py",
            "ServiceRuntime._copy_config_sync",
            "ServiceRuntime.remove_config",
            "`add_config*`, `export_config`, `import_config`, and `update_setup_toml` remain",
            "4,096 regular files",
            "64 MiB total",
            "protected DACL",
        ):
            self.assertIn(anchor, spec)
        self.assertEqual(
            workflow.count("docs/script-runtime/SERVICE_CONFIGURATION_TRIGGERS.md"), 2
        )


if __name__ == "__main__":
    unittest.main()
