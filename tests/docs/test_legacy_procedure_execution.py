import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class LegacyProcedureExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "include/procedure/LegacyProcedureExecution.h").read_text(
            encoding="utf-8"
        )
        cls.baas_header = (ROOT / "include/BAAS.h").read_text(encoding="utf-8")
        cls.baas_source = (ROOT / "src/BAAS.cpp").read_text(encoding="utf-8")
        cls.appear = (ROOT / "src/procedure/AppearThenClickProcedure.cpp").read_text(
            encoding="utf-8"
        )
        cls.host = (ROOT / "src/script/host/ProcedureHost.cpp").read_text(
            encoding="utf-8"
        )
        cls.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.foundation = (ROOT / ".github/workflows/foundation-runtime.yml").read_text(
            encoding="utf-8"
        )
        cls.app = (ROOT / ".github/workflows/baas_app.yaml").read_text(encoding="utf-8")
        cls.spec = (ROOT / "docs/script-runtime/LEGACY_PROCEDURE_EXECUTION.md").read_text(
            encoding="utf-8"
        )

    def test_typed_surface_is_complete_and_fail_closed(self) -> None:
        for anchor in (
            "source_terminal",
            "MissingProcedure",
            "InvalidDefinition",
            "Cancelled",
            "DeadlineExceeded",
            "DeviceDisconnected",
            "ForegroundPackageMismatch",
            "ResourceNotFound",
            "BudgetExceeded",
            "ResourceExhausted",
            "Unavailable",
            "LegacyProcedureExecutionControl",
            "LegacyProcedureEffectScope",
            "legacy_procedure_production_ready",
            "return false;",
        ):
            self.assertIn(anchor, self.header)

    def test_direct_entry_has_no_ambient_catalog_or_map_mutation(self) -> None:
        direct = self.baas_source.split(
            "LegacyProcedureRunResult BAAS::run_procedure_definition", 1
        )[1].split("bool BAAS::solve", 1)[0]
        self.assertNotIn("BAAS_PROCEDURE_DIR", direct)
        self.assertNotIn("procedures[", direct)
        self.assertNotIn("_create_procedure", direct)
        self.assertNotIn("filesystem", direct)
        self.assertIn("_make_procedure", direct)
        self.assertIn("DirectDefinitionsOnly", self.baas_header)
        self.assertIn("ProcedureCatalogMode::DirectDefinitionsOnly", self.spec)
        self.assertIn(
            "procedure_catalog_mode == ProcedureCatalogMode::LoadAmbientLegacyCatalog",
            self.baas_source,
        )
        self.assertIn("auto owned_config = std::make_unique<BAASUserConfig>", self.baas_source)
        self.assertIn("owned_control.release()", self.baas_source)
        screenshot = (ROOT / "src/device/screenshot/BAASScreenshot.cpp").read_text(
            encoding="utf-8"
        ).split("void BAASScreenshot::screenshot_controlled", 1)[1].split(
            "void BAASScreenshot::immediate_screenshot", 1
        )[0]
        self.assertLess(screenshot.index("last_screenshot_time ="), screenshot.rindex("checkpoint();"))
        for anchor in ("RuntimeProcedureActivation", "ProcedureDescriptor", "terminal set"):
            self.assertIn(anchor, self.spec)

    def test_cancellation_cleanup_and_legacy_bug_fixes_are_present(self) -> None:
        self.assertIn("std::atomic_bool flag_run", self.baas_header)
        self.assertIn("void request_stop() noexcept", self.baas_header)
        self.assertNotIn("throw e;", self.baas_source)
        factory = self.baas_source.split("std::unique_ptr<BaseProcedure> BAAS::_make_procedure", 1)[1]
        self.assertIn('cfg.getInt("procedure_type", 0)', factory)
        self.assertNotIn('config->getInt("procedure_type", 0)', factory)
        self.assertIn("zero.assign(6 - ld.length(), ' ')", self.appear)
        self.assertGreaterEqual(self.appear.count("checkpoint();"), 12)

    def test_build_app_and_android_closure_are_explicit(self) -> None:
        self.assertIn("option(BUILD_LEGACY_PROCEDURE_EXECUTION ", self.cmake)
        self.assertIn("option(BUILD_LEGACY_PROCEDURE_EXECUTION_TESTS ", self.cmake)
        self.assertIn("option(BUILD_LEGACY_PROCEDURE_DEFINITION_TESTS ", self.cmake)
        for workflow in (self.foundation, self.app):
            self.assertIn("BAAS_legacy_procedure_execution_tests", workflow)
            self.assertIn("BAAS_legacy_procedure_definition_tests", workflow)
        self.assertIn("-DBUILD_LEGACY_PROCEDURE_EXECUTION=ON", self.foundation)
        self.assertIn("BAAS_legacy_procedure_execution", self.foundation)
        for path in ("include/BAAS.h", "include/procedure/**", "src/BAAS.cpp", "src/procedure/**"):
            self.assertIn(f"- '{path}'", self.app)

    def test_effect_machine_is_operation_strict_and_unknown_is_terminal(self) -> None:
        self.assertIn("compare_exchange_weak", self.host)
        self.assertIn("EffectState::Committed", self.host)
        self.assertIn("EffectState::Unknown", self.host)
        self.assertIn("two complete operations of one declared effect", (
            ROOT / "tests/script/ProcedureHostTests.cpp"
        ).read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
