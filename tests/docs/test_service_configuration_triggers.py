import hashlib
import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceConfigurationTriggerDocsTests(unittest.TestCase):
    def test_embedded_python_create_defaults_are_canonical(self) -> None:
        defaults = (
            ROOT / "src/service/adapters/ConfigurationDefaults.h"
        ).read_text(encoding="utf-8")
        expected = {
            "user": (97, "7ff8fd26b5afe0b1243a5e24ebcae674b31c60ecf7c53be3d8765ee4c2fc2d01"),
            "event": (26, "8e402fe3c3782058d5c65c69dca16f87a5f91317bfda82879ab1f295b75f1844"),
            "switches": (11, "ff935aa191bdbfc8b3b3e509b1f01b88a7c8c117c3b235816fa783997e68045a"),
        }
        for name, (length, digest) in expected.items():
            match = re.search(
                rf'{name} = R"BAAS_DEFAULT\((.*?)\)BAAS_DEFAULT"',
                defaults,
                re.DOTALL,
            )
            self.assertIsNotNone(match)
            document = json.loads(match.group(1))
            canonical = json.dumps(
                document, ensure_ascii=False, sort_keys=True, separators=(",", ":")
            ).encode("utf-8")
            self.assertEqual(len(document), length)
            self.assertEqual(hashlib.sha256(canonical).hexdigest(), digest)

    def test_embedded_python_static_default_is_complete(self) -> None:
        defaults = (
            ROOT / "src/service/adapters/ConfigurationDefaults.h"
        ).read_text(encoding="utf-8")
        parts = re.findall(
            r'R"BAAS_STATIC\((.*?)\)BAAS_STATIC"', defaults, re.DOTALL
        )
        self.assertEqual(len(parts), 13)
        document = json.loads("".join(parts))
        canonical = json.dumps(
            document, ensure_ascii=False, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        self.assertEqual(
            hashlib.sha256(canonical).hexdigest(),
            "4b31c708fbbcd88300eb00e1ec71a556bc22f596467e5af356330b5496d2b247",
        )
        self.assertEqual(len(document), 25)

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
        self.assertIn("make_configuration_trigger_registrations", app)
        self.assertIn("BAAS_service_configuration_trigger_tests", cmake)
        self.assertIn("BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS", root_cmake)
        self.assertIn("BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS=ON", workflow)
        self.assertIn("BAAS_service_configuration_trigger_tests", workflow)
        self.assertIn("-DBUILD_SERVICE_CONFIGURATION_TRIGGERS=ON", android_workflow)
        self.assertIn("BAAS_service_configuration_triggers", android_workflow)
        self.assertIn("--requires=baas-nlohmann-json/3.11.3", android_workflow)
        self.assertIn("arm64-v8a", android_workflow)
        self.assertIn("x86_64", android_workflow)
        for anchor in (
            "service/api/commands.py",
            "ServiceRuntime._copy_config_sync",
            "ServiceRuntime.remove_config",
            "`export_config`, `import_config`, and `update_setup_toml` remain unregistered",
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
