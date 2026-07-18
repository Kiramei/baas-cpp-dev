import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class RuntimeScriptExecutionPlanContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.target_cmake = (ROOT / "cmake/RuntimeScriptExecutionPlan.cmake").read_text(
            encoding="utf-8"
        )
        cls.header = (
            ROOT / "include/runtime/script/RuntimeScriptExecutionPlan.h"
        ).read_text(encoding="utf-8")
        cls.loader_header = (
            ROOT / "include/runtime/script/RuntimeScriptPackageLoader.h"
        ).read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/foundation-runtime.yml").read_text(
            encoding="utf-8"
        )
        cls.doc = (
            ROOT / "docs/script-runtime/RUNTIME_SCRIPT_EXECUTION_PLAN.md"
        ).read_text(encoding="utf-8")
        cls.package_doc = (
            ROOT / "docs/script-runtime/PACKAGE_VERSIONING.md"
        ).read_text(encoding="utf-8")

    def test_target_is_independent_and_opt_in(self):
        self.assertIn("option(BUILD_RUNTIME_SCRIPT_EXECUTION_PLAN", self.root_cmake)
        self.assertIn("BAAS_runtime_script_execution_plan", self.target_cmake)
        self.assertIn("BAAS_runtime_script_catalog", self.target_cmake)
        self.assertIn("BAAS_runtime_script_package_loader", self.target_cmake)

    def test_plan_is_owned_and_pin_bound(self):
        self.assertIn("std::shared_ptr<const Impl>", self.header)
        self.assertIn("RuntimeScriptCatalogResolution", self.header)
        self.assertIn("generation()", self.header)
        self.assertIn("commit()", self.header)
        self.assertIn("RuntimeScriptRepositoryTrustEvidence", self.header)
        self.assertIn("load_manifested_runtime_script_package", self.loader_header)
        self.assertIn("procedure_ids()", self.header)
        self.assertIn("max_procedures", self.header)
        self.assertIn("procedure_requirements_missing", self.header)

    def test_fail_closed_contract_is_documented(self):
        for phrase in (
            "resources` and `profiles` must be empty",
            "signature fields are rejected",
            "unknown or duplicate field",
            "Imports outside the allowlist",
            "never contains a native path",
            "browser-provided strings",
            "permissive in-process",
            "Two packages may each contain",
            "schema-1 package that declares that Host requirement fails",
            "immutable sorted set",
        ):
            self.assertIn(phrase, self.doc)
        for phrase in (
            "Manifest schema 2 procedure closure",
            '"procedures": ["group/menu", "group/reward"]',
            "RSE028_PROCEDURE_REQUIREMENTS_MISSING",
            "requirements, not procedure definitions",
        ):
            self.assertIn(phrase, self.package_doc)

    def test_host_and_android_ci_cover_production(self):
        self.assertIn("-DBUILD_RUNTIME_SCRIPT_EXECUTION_PLAN_TESTS=ON", self.workflow)
        self.assertIn("BAAS_runtime_script_execution_plan_tests", self.workflow)
        self.assertIn("-DBUILD_RUNTIME_SCRIPT_EXECUTION_PLAN=ON", self.workflow)
        self.assertIn("BAAS_runtime_script_execution_plan", self.workflow)
        self.assertGreaterEqual(
            self.workflow.count(
                "docs/script-runtime/RUNTIME_SCRIPT_EXECUTION_PLAN.md"
            ),
            2,
        )
        self.assertGreaterEqual(
            self.workflow.count("docs/script-runtime/PACKAGE_VERSIONING.md"), 2
        )


if __name__ == "__main__":
    unittest.main()
