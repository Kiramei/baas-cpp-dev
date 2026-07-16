import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ServiceAdbDiscoveryTriggerDocumentationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "include/service/app/AdbDiscoveryTriggerRegistration.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src/service/app/AdbDiscoveryTriggerRegistration.cpp"
        ).read_text(encoding="utf-8")
        cls.application = (
            ROOT / "src/service/app/ServiceApplication.cpp"
        ).read_text(encoding="utf-8")
        cls.tests = (
            ROOT / "tests/service/AdbDiscoveryTriggerRegistrationTests.cpp"
        ).read_text(encoding="utf-8")
        cls.docs = (
            ROOT / "docs/script-runtime/SERVICE_ADB_DISCOVERY_TRIGGER.md"
        ).read_text(encoding="utf-8")
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.application_cmake = (
            ROOT / "cmake/ServiceApplication.cmake"
        ).read_text(encoding="utf-8")
        cls.foundation_ci = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")
        cls.application_ci = (
            ROOT / ".github/workflows/service-application.yml"
        ).read_text(encoding="utf-8")
        cls.auth_ci = (ROOT / ".github/workflows/service-auth.yml").read_text(
            encoding="utf-8"
        )

    def test_python_data_source_and_schema_are_frozen(self) -> None:
        for anchor in (
            "does not call `adb devices`",
            "dnplayer.exe",
            "index=`",
            "5555 + 2 * instance",
            '`{"addresses":[]}`',
            "Intentional contract hardening",
        ):
            self.assertIn(anchor, self.docs)
        self.assertIn('registration.descriptor_name = "detect_adb"', self.source)
        self.assertIn('output.append(R"({\"addresses\":[)")', self.source)

    def test_bounds_and_failure_contract_are_documented(self) -> None:
        for anchor in (
            "adb_discovery_hard_max_processes",
            "adb_discovery_hard_max_command_line_bytes",
            "adb_discovery_hard_max_addresses",
            "adb_discovery_hard_max_json_bytes",
        ):
            self.assertIn(anchor, self.header)
        for anchor in (
            "adb_discovery_source_capacity",
            "adb_discovery_source_unavailable",
            "adb_discovery_source_exception",
            "adb_discovery_response_rejected",
        ):
            self.assertIn(anchor, self.source)
            self.assertIn(anchor, self.docs)

    def test_production_registration_and_evidence_are_not_fake_only(self) -> None:
        self.assertIn("make_production_adb_discovery_source", self.application)
        self.assertIn("make_adb_discovery_trigger_registration", self.application)
        for anchor in (
            "application dispatcher must install the real detect_adb handler",
            "production detect_adb must preserve the Python Android envelope",
        ):
            application_tests = (
                ROOT / "tests/service/ServiceApplicationTests.cpp"
            ).read_text(encoding="utf-8")
            self.assertIn(anchor, application_tests)
        for anchor in (
            "test_vendor_process_mapping_order_and_deduplication",
            "test_vendor_resolved_mumu_address_precedes_formula_fallback",
            "test_production_android_environment_parity",
            "test_source_errors_and_exceptions_fail_closed",
        ):
            self.assertIn(anchor, self.tests)

    def test_build_and_ci_dependency_closure_is_explicit(self) -> None:
        for anchor in (
            "BUILD_SERVICE_ADB_DISCOVERY_TRIGGER",
            "BUILD_SERVICE_ADB_DISCOVERY_TRIGGER_TESTS",
            "ServiceAdbDiscoveryTrigger.cmake",
        ):
            self.assertIn(anchor, self.root_cmake)
        self.assertIn(
            "BAAS_service_adb_discovery_trigger", self.application_cmake
        )
        for anchor in (
            "-DBUILD_SERVICE_ADB_DISCOVERY_TRIGGER_TESTS=ON",
            "BAAS_service_adb_discovery_trigger_tests",
        ):
            self.assertIn(anchor, self.foundation_ci)
            self.assertIn(anchor, self.application_ci)
        self.assertIn(
            "-DBUILD_SERVICE_ADB_DISCOVERY_TRIGGER=ON", self.auth_ci
        )
        self.assertIn("BAAS_service_adb_discovery_trigger", self.auth_ci)
        for workflow in (
            self.foundation_ci,
            self.application_ci,
            self.auth_ci,
        ):
            self.assertEqual(
                workflow.count(
                    "'docs/script-runtime/SERVICE_ADB_DISCOVERY_TRIGGER.md'"
                ),
                2,
            )


if __name__ == "__main__":
    unittest.main()
