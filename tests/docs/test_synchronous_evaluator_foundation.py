import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = (ROOT / "include/script/runtime/SynchronousEvaluator.h").read_text(encoding="utf-8")
SOURCE = (ROOT / "src/script/runtime/SynchronousEvaluator.cpp").read_text(encoding="utf-8")
TESTS = (ROOT / "tests/script/SynchronousEvaluatorTests.cpp").read_text(encoding="utf-8")
HOST_TESTS = (ROOT / "tests/script/SynchronousHostEvaluatorTests.cpp").read_text(encoding="utf-8")
CMAKE = (ROOT / "cmake/ScriptRuntime.cmake").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/foundation-runtime.yml").read_text(encoding="utf-8")
CONTROL = (ROOT / "docs/script-runtime/CONTROL_FLOW_AND_MODULES.md").read_text(encoding="utf-8")
VALUES = (ROOT / "docs/script-runtime/VALUE_SEMANTICS.md").read_text(encoding="utf-8")
ROADMAP = (ROOT / "docs/script-runtime/ROADMAP.md").read_text(encoding="utf-8")


class SynchronousEvaluatorFoundationTest(unittest.TestCase):
    def test_public_limits_and_preparse_source_gate_are_wired(self) -> None:
        for anchor in (
            "max_module_source_bytes",
            "max_total_source_bytes",
            "max_steps",
            "max_call_depth",
            "max_value_stack",
            "max_collection_work",
            "max_functions",
            "max_import_depth",
            "max_modules",
        ):
            self.assertIn(anchor, HEADER)
        gate = SOURCE.index("source.source.size() > limits.max_module_source_bytes")
        parse = SOURCE.index("parse(source.source)")
        self.assertLess(gate, parse)

    def test_sync_execution_and_budget_regressions_are_executable(self) -> None:
        for anchor in (
            "test_control_flow_closures_defaults_and_recursion",
            "test_multi_module_cache_and_namespace_calls",
            "test_module_failure_cache_and_lazy_initialization",
            "test_constructive_two_counter_program",
            "test_source_and_expanded_collection_preflights",
            "test_closure_side_table_survives_heap_collection",
        ):
            self.assertIn(anchor, TESTS)
        self.assertIn("BAAS_script_sync_evaluator_tests", CMAKE)
        self.assertIn("BAAS_script_sync_evaluator_tests", WORKFLOW)
        self.assertIn("BAAS_script_sync_host_tests", CMAKE)
        self.assertIn("BAAS_script_sync_host_evaluator_tests", CMAKE)
        self.assertIn("BAAS_script_sync_host_evaluator_tests", WORKFLOW)

    def test_transitional_closure_and_two_layer_equality_boundaries_are_explicit(self) -> None:
        self.assertIn("heap.allocate_function({CallableKind::Script, id, {}})", SOURCE)
        self.assertIn("unreachable side-table records are retained", CONTROL)
        self.assertIn("non-reclaiming transitional side", VALUES)
        self.assertIn("not cumulatively add nested heap equality traversal", VALUES)

    def test_roadmap_does_not_overclaim_production_vm_or_host_support(self) -> None:
        self.assertIn("bounded synchronous AST evaluator", ROADMAP)
        self.assertIn("production VM execution remain pending", ROADMAP)
        self.assertIn("every real Host adapter remain pending", ROADMAP)
        self.assertIn("SynchronousHostOptions", HEADER)
        self.assertIn("authorize_host_member", SOURCE)
        self.assertIn("test_capability_adapter_and_syntax_gates_precede_arguments", HOST_TESTS)
        self.assertIn("test_cache_transaction_permission_preflight_and_failure_cache", HOST_TESTS)
        for forbidden in ("ErrorEnvelope", "HttpHost"):
            self.assertNotIn(forbidden, HEADER)
            self.assertNotIn(forbidden, SOURCE)


if __name__ == "__main__":
    unittest.main()
