import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ProductionHttpHostCompositionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "include" / "service" / "http" / "ProductionHttpHost.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT / "src" / "service" / "http" / "ProductionHttpHost.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (
            ROOT / "cmake" / "ServiceProductionHttpHost.cmake"
        ).read_text(encoding="utf-8")
        cls.docs = (
            ROOT / "docs" / "script-runtime" / "SERVICE_HTTP_COMPOSITION.md"
        ).read_text(encoding="utf-8")
        cls.android_contract = (
            ROOT
            / "tests"
            / "service"
            / "ProductionHttpHostAndroidPolicyCompile.cpp"
        ).read_text(encoding="utf-8")

    def test_dependencies_are_explicit_and_structured(self) -> None:
        for anchor in (
            "missing_auth_storage",
            "missing_provider_backend",
            "missing_resource_store",
            "missing_trigger_handler",
            "missing_remote_handler",
            "authentication_failed",
            "invalid_configuration",
        ):
            self.assertIn(anchor, self.header + self.source)
        self.assertIn("AuthOwner::open", self.source)
        self.assertIn("ProviderHandlerFactory", self.source)
        self.assertIn("SyncHandlerFactory", self.source)
        self.assertIn("ProductionSessionFactory", self.source)
        self.assertIn("AuthHttpAdapter", self.source)
        self.assertIn("std::make_unique<HttpHost>", self.source)

    def test_lifecycle_remote_policy_and_build_gate_are_owned(self) -> None:
        self.assertIn("result.error != HttpHostStartError::already_active", self.source)
        self.assertIn("ProductionHttpHost::~ProductionHttpHost", self.source)
        self.assertIn("production_remote_handler_required", self.header + self.source)
        self.assertIn("#if defined(__ANDROID__)", self.header)
        self.assertIn("__ANDROID__=1", self.cmake)
        self.assertIn("static_assert", self.android_contract)
        self.assertIn("BAAS_service_production_http_host_tests", self.cmake)
        self.assertIn("HttpHost", self.docs)
        self.assertIn("Trigger and remote handler implementations", self.docs)


if __name__ == "__main__":
    unittest.main()
