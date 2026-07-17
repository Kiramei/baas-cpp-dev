from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RuntimeProcedureActivationContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "include/runtime/procedure/RuntimeProcedureActivation.h").read_text(encoding="utf-8")
        cls.source = (ROOT / "src/runtime/procedure/RuntimeProcedureActivation.cpp").read_text(encoding="utf-8")
        cls.doc = (ROOT / "docs/script-runtime/RUNTIME_PROCEDURE_ACTIVATION.md").read_text(encoding="utf-8")
        cls.cmake = (ROOT / "cmake/RuntimeProcedureActivation.cmake").read_text(encoding="utf-8")
        cls.root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.host_cmake = (ROOT / "cmake/ScriptProcedureHost.cmake").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/foundation-runtime.yml").read_text(encoding="utf-8")

    def test_external_exact_schemas_and_pathless_contract_are_documented(self):
        for token in (
            "baas.procedures.json", "baas.procedures/v1",
            "baas.procedure-definition/v1", "legacy.appear_then_click/v1",
            "strict JSON", "native path", "Only the sorted",
        ):
            self.assertIn(token, self.doc)

    def test_publication_is_unforgeable_and_generation_bound(self):
        for token in (
            "RuntimeProcedureActivation(const RuntimeProcedureActivation&) = delete",
            "RuntimeProcedureActivation(RuntimeProcedureActivation&&) = delete",
            "RuntimeProcedureDefinition(const RuntimeProcedureDefinition&) = delete",
            "load_runtime_procedure_activation", "resources_commit()", "activation_id()",
        ):
            self.assertIn(token, self.header)
        self.assertIn("resources->generation() != scripts.generation()", self.source)
        self.assertIn("plan.procedure_ids()", self.source)

    def test_definition_and_terminal_mapping_bind_descriptor_v2(self):
        snapshot_source = (ROOT / "src/script/host/ProcedureSnapshot.cpp").read_text(encoding="utf-8")
        self.assertIn("baas.procedure.descriptor/v2", snapshot_source)
        self.assertIn("baas.procedure.snapshot/v2", snapshot_source)
        self.assertIn("implementation_sha256", self.source)
        self.assertIn("terminal.source", self.source)

    def test_split_targets_and_foundation_closure_are_wired(self):
        self.assertIn("BAAS_script_procedure_snapshot", self.cmake)
        self.assertIn("BAAS_runtime_procedure_activation_tests", self.cmake)
        self.assertIn("BAAS_script_procedure_snapshot", self.host_cmake)
        self.assertNotIn("ProcedureSnapshot.cpp", self.host_cmake)
        self.assertIn('/src/runtime/procedure/.*\\\\.cpp$', self.root_cmake)
        for token in (
            "BUILD_RUNTIME_PROCEDURE_ACTIVATION_TESTS=ON",
            "BAAS_runtime_procedure_activation_tests",
            "BUILD_RUNTIME_PROCEDURE_ACTIVATION=ON",
            "BAAS_runtime_procedure_activation", "arm64-v8a", "x86_64",
        ):
            self.assertIn(token, self.workflow)

    def test_stable_error_names_cover_fail_closed_classes(self):
        for token in (
            "RPA005_GENERATION_MISMATCH", "RPA018_WORK_LIMIT_EXCEEDED",
            "RPA027_DEFINITION_DIGEST_MISMATCH", "RPA032_CANCELLED",
            "RPA033_RESOURCE_EXHAUSTED",
        ):
            self.assertIn(token, self.source)


if __name__ == "__main__":
    unittest.main()
