import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ProductionProviderBackendContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "include" / "service" / "app" / "ProductionProviderBackend.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src" / "service" / "app" / "ProductionProviderBackend.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (
            ROOT / "cmake" / "ServiceProviderBackend.cmake"
        ).read_text(encoding="utf-8")
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.docs = (
            ROOT / "docs" / "script-runtime" / "SERVICE_PROVIDER_BACKEND.md"
        ).read_text(encoding="utf-8")

    def test_backend_surface_and_limits_are_explicit(self) -> None:
        for anchor in (
            "ProductionProviderBackendLimits",
            "max_history_entries",
            "max_history_bytes",
            "max_snapshot_json_bytes",
            "publish_log",
            "publish_status",
            "set_initialized",
            "replace_static",
        ):
            self.assertIn(anchor, self.header)
        self.assertIn("public channels::ProviderBackend", self.header)

    def test_validation_history_and_subscription_barriers_are_owned(self) -> None:
        for anchor in (
            "JsonValidator",
            "auth::is_valid_utf8",
            "evict_oldest",
            "CallbackSlot",
            "active_calls_",
            "slot->close()",
        ):
            self.assertIn(anchor, self.source)
        self.assertIn("callbacks without", self.docs)
        self.assertIn("oldest entries are evicted", self.docs)
        self.assertIn("Callback exceptions never escape", self.docs)

    def test_build_gate_and_legacy_glob_exclusion_are_present(self) -> None:
        self.assertIn("BAAS_service_provider_backend", self.cmake)
        self.assertIn("BAAS_service_provider_backend_tests", self.cmake)
        self.assertIn("BUILD_SERVICE_PROVIDER_BACKEND_TESTS", self.root_cmake)
        self.assertIn('/src/service/app/.*\\\\.cpp$', self.root_cmake)


if __name__ == "__main__":
    unittest.main()
