from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = ROOT / "docs" / "script-runtime" / "CONTROL_FLOW_AND_MODULES.md"
LANGUAGE_SPEC_PATH = ROOT / "docs" / "script-runtime" / "LANGUAGE_SPEC_DRAFT.md"
GRAMMAR_PATH = ROOT / "docs" / "script-runtime" / "LANGUAGE_GRAMMAR.md"
VALUE_SPEC_PATH = ROOT / "docs" / "script-runtime" / "VALUE_SEMANTICS.md"
PACKAGE_SPEC_PATH = ROOT / "docs" / "script-runtime" / "PACKAGE_VERSIONING.md"
ROADMAP_PATH = ROOT / "docs" / "script-runtime" / "ROADMAP.md"
AST_PATH = ROOT / "include" / "script" / "Ast.h"
SEMANTIC_HEADER_PATH = ROOT / "include" / "script" / "SemanticAnalyzer.h"
SEMANTIC_SOURCE_PATH = ROOT / "src" / "script" / "SemanticAnalyzer.cpp"
PARSER_SOURCE_PATH = ROOT / "src" / "script" / "Parser.cpp"
SEMANTIC_TEST_PATH = ROOT / "tests" / "script" / "SemanticAnalyzerTests.cpp"
PARSER_TEST_PATH = ROOT / "tests" / "script" / "ParserTests.cpp"
FIXTURE_PATH = ROOT / "tests" / "script" / "fixtures" / "control_modules_valid.baas"
CMAKE_PATH = ROOT / "cmake" / "ScriptRuntime.cmake"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "foundation-runtime.yml"


EXPECTED_CLAUSES = tuple(f"CTL-{number:03d}" for number in range(1, 21))

CLAUSE_TERMS = {
    "CTL-001": ("immutable source-spanned AST", "lexical semantic analysis", "verified bytecode", "stack VM"),
    "CTL-002": ("module scope", "BlockStatement", "function scope", "for", "catch", "MUST NOT add another scope"),
    "CTL-003": ("`Let`", "`Parameter`", "`Function`", "`Import`", "`For`", "`Catch`", "SEM001", "SEM002", "SEM004"),
    "CTL-004": ("declared uninitialized", "SEM003", "compound assignment", "UninitializedBinding", "not a complete control-flow"),
    "CTL-005": ("left-to-right", "PAR005", "PAR006", "PAR007", "call time", "earlier parameters"),
    "CTL-006": ("binding cell", "capture", "intermediate", "FunctionInfo", "BindingId", "ADR-0002"),
    "CTL-007": ("direct recursion", "no declaration hoisting", "SEM001", "mutual recursion", "budgets"),
    "CTL-008": ("truthiness", "exactly once", "nearest unmatched", "Short-circuit"),
    "CTL-009": ("before every iteration", "zero times", "continue", "break", "safe points"),
    "CTL-010": ("evaluate `iterable` once", "shallow snapshot", "insertion order", "one initialized mutable binding", "fresh nested scope"),
    "CTL-011": ("Bare `return;`", "PAR010", "PAR008", "PAR009", "resets the parser's loop context", "does not mark"),
    "CTL-012": ("import string as identifier", "`baas/`", "extensionless", "appends `.baas`", "case-sensitive", "MUST NOT probe"),
    "CTL-013": ("dependency graph", "PackageEntryMissing", "ImportCycle", "strongly connected component", "partial namespace"),
    "CTL-014": ("`Loading`", "`Ready`", "`Failed`", "lazily", "source order", "cache key", "transactional"),
    "CTL-015": ("declaration order", "names beginning with `_`", "member syntax", "namespace object"),
    "CTL-016": ("isolated per execution context", "immutable package/resource snapshot", "distinct namespace", "rollback", "MUST NOT alias"),
    "CTL-017": ("signed manifest", "capabilities", "import depth", "SEM006", "SEM007", "1,024"),
    "CTL-018": ("two-counter Minsky machine", "`counter = [counter];`", "`counter is null`", "`counter = counter[0];`", "Turing-complete"),
    "CTL-019": ("conformance:closure-recursion", "conformance:loop-branch-import", "BAAS_script_check", "SEM003", "PAR010"),
    "CTL-020": ("does not yet implement", "bytecode/compiler/VM", "module path resolution", "MUST remain pending", "Phase 1 as a whole"),
}

EXPECTED_BINDING_KINDS = ("Let", "Parameter", "Function", "Import", "For", "Catch")
EXPECTED_SEMANTIC_CODES = (
    ("unknown_name", "SEM001"),
    ("duplicate_declaration", "SEM002"),
    ("uninitialized_read", "SEM003"),
    ("duplicate_parameter", "SEM004"),
    ("node_limit", "SEM006"),
    ("nesting_limit", "SEM007"),
    ("malformed_ast", "SEM008"),
)
EXPECTED_DOCUMENTED_SEMANTICS = tuple(f"SEM{number:03d}" for number in range(1, 9))
EXPECTED_CONFORMANCE_IDS = (
    "turing-machine",
    "closure-recursion",
    "loop-branch-import",
)
EXPECTED_FUTURE_ERRORS = (
    "UninitializedBinding",
    "NotCallable",
    "CallArityMismatch",
    "CallArgumentDuplicate",
    "CallArgumentUnknown",
    "ImportSpecifierInvalid",
    "ImportCycle",
    "ImportDepthLimit",
    "ModuleInitializationFailed",
    "ModuleMemberMissing",
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def clause_bodies(document: str) -> dict[str, str]:
    matches = list(re.finditer(r"^### (CTL-\d{3}) — .+$", document, re.MULTILINE))
    result: dict[str, str] = {}
    for index, match in enumerate(matches):
        stop = matches[index + 1].start() if index + 1 < len(matches) else len(document)
        result[match.group(1)] = document[match.end():stop]
    return result


def conformance_examples(document: str) -> dict[str, str]:
    return {
        identifier: source
        for identifier, source in re.findall(
            r"<!-- conformance:([a-z-]+) -->\s*```baas\n(.*?)\n```",
            document,
            re.DOTALL,
        )
    }


class ControlAndModulesSpecificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.spec = read(SPEC_PATH)
        cls.language_spec = read(LANGUAGE_SPEC_PATH)
        cls.grammar = read(GRAMMAR_PATH)
        cls.value_spec = read(VALUE_SPEC_PATH)
        cls.package_spec = read(PACKAGE_SPEC_PATH)
        cls.roadmap = read(ROADMAP_PATH)
        cls.ast = read(AST_PATH)
        cls.semantic_header = read(SEMANTIC_HEADER_PATH)
        cls.semantic_source = read(SEMANTIC_SOURCE_PATH)
        cls.parser_source = read(PARSER_SOURCE_PATH)
        cls.semantic_tests = read(SEMANTIC_TEST_PATH)
        cls.parser_tests = read(PARSER_TEST_PATH)
        cls.fixture = read(FIXTURE_PATH)
        cls.cmake = read(CMAKE_PATH)
        cls.workflow = read(WORKFLOW_PATH)

    def test_complete_normative_clause_inventory_and_terms(self) -> None:
        bodies = clause_bodies(self.spec)
        self.assertEqual(tuple(bodies), EXPECTED_CLAUSES)
        self.assertEqual(set(CLAUSE_TERMS), set(EXPECTED_CLAUSES))
        for clause, terms in CLAUSE_TERMS.items():
            with self.subTest(clause=clause):
                normalized = re.sub(r"\s+", " ", bodies[clause])
                self.assertIn("MUST", normalized)
                for term in terms:
                    self.assertIn(term, normalized)

    def test_grammar_and_ast_cover_every_control_form(self) -> None:
        grammar_anchors = (
            'block           = "{", statement*, "}" ;',
            'if-statement    = "if", "(", expression, ")", statement,',
            'while-statement = "while", "(", expression, ")", statement ;',
            'for-statement   = "for", "(", identifier, "in", expression, ")", statement ;',
            'function-declaration = "async"?, "fn", identifier, parameters, function-body ;',
            'return-statement   = "return", expression?, ";" ;',
            'break-statement    = "break", ";" ;',
            'continue-statement = "continue", ";" ;',
            'import-statement   = "import", string, "as", identifier, ";" ;',
            'function-expression = "async"?, "fn", parameters, function-body ;',
        )
        for anchor in grammar_anchors:
            self.assertIn(anchor, self.grammar)

        node_match = re.search(r"enum class NodeKind\s*\{(?P<body>.*?)\};", self.ast, re.DOTALL)
        self.assertIsNotNone(node_match)
        node_kinds = set(re.findall(r"\b([A-Z]\w+)\b", node_match.group("body")))
        required = {
            "BlockStatement", "LetStatement", "IfStatement", "WhileStatement",
            "ForStatement", "FunctionDeclaration", "FunctionExpression",
            "ReturnStatement", "BreakStatement", "ContinueStatement", "ImportStatement",
        }
        self.assertTrue(required.issubset(node_kinds), required - node_kinds)
        self.assertIn("SourceSpan span", self.ast)
        self.assertIn("const std::vector<StmtPtr> statements", self.ast)

    def test_binding_and_diagnostic_inventories_match_semantic_api(self) -> None:
        binding_match = re.search(
            r"enum class BindingKind\s*\{(?P<body>.*?)\};",
            self.semantic_header,
            re.DOTALL,
        )
        self.assertIsNotNone(binding_match)
        binding_kinds = tuple(re.findall(r"\b([A-Z]\w+)\b", binding_match.group("body")))
        self.assertEqual(binding_kinds, EXPECTED_BINDING_KINDS)
        for binding in EXPECTED_BINDING_KINDS:
            self.assertIn(f"`{binding}`", self.spec)

        source_codes = tuple(re.findall(
            r"inline constexpr std::string_view (\w+)\{\"(SEM\d{3})\"\};",
            self.semantic_header,
        ))
        self.assertEqual(source_codes, EXPECTED_SEMANTIC_CODES)
        documented = tuple(re.findall(r"^\| `(SEM\d{3})` \|", self.spec, re.MULTILINE))
        self.assertEqual(documented, EXPECTED_DOCUMENTED_SEMANTICS)
        self.assertIn("SEM005 is reserved", self.semantic_header)

    def test_semantic_analyzer_anchors_scope_initialization_and_capture(self) -> None:
        anchors = (
            "scopes_.emplace_back(); // module scope",
            "struct ScopeGuard",
            "struct FunctionGuard",
            "declare(binding.name, BindingKind::Let, binding.span, false)",
            "result_.bindings[*id].initialized = true",
            "ScopeGuard scope(*this);",
            "declare(loop.binding, BindingKind::For, loop.span, true)",
            "declare(function.name, BindingKind::Function, function.span, true)",
            "declare(imported.alias, BindingKind::Import, imported.span, true)",
            "AccessMode::ReadWrite",
            "Propagate through intermediate closures",
            "function.captures.push_back(binding)",
        )
        for anchor in anchors:
            self.assertIn(anchor, self.semantic_source)
        test_anchors = (
            "test_success_bindings_and_recursion",
            "test_unicode_shadowing_and_nearest_assignment",
            "test_closure_capture_propagation_and_nested_recursion",
            "test_declaration_order_is_conservative",
        )
        for anchor in test_anchors:
            self.assertIn(anchor, self.semantic_tests)

    def test_parser_context_and_call_diagnostics_are_anchored(self) -> None:
        required_codes = ("PAR004", "PAR005", "PAR006", "PAR007", "PAR008", "PAR009", "PAR010")
        combined = self.parser_source + self.parser_tests
        for code in required_codes:
            self.assertIn(f'"{code}"', self.parser_source)
            self.assertIn(f'"{code}"', self.parser_tests)
            self.assertIn(f"`{code}`", self.spec)
        self.assertIn("parser.loop_depth_ = 0", self.parser_source)
        self.assertIn("parser.function_depth_", self.parser_source)

    def test_conformance_examples_are_complete_and_syntax_checked(self) -> None:
        examples = conformance_examples(self.spec)
        self.assertEqual(tuple(examples), EXPECTED_CONFORMANCE_IDS)
        example_anchors = {
            "turing-machine": ("c0 = [c0];", "c0 = c0[0];", "while (pc != halt)"),
            "closure-recursion": ("fn make_counter", "value += 1", "factorial(n - 1)"),
            "loop-branch-import": ('import "baas/log" as log;', "for (item in", "continue;", "break;"),
        }
        for identifier, anchors in example_anchors.items():
            for anchor in anchors:
                self.assertIn(anchor, examples[identifier])
                self.assertIn(anchor, self.fixture)
        self.assertIn("BAAS_script_check_control_modules_valid_cli", self.cmake)
        self.assertIn("tests/script/fixtures/control_modules_valid.baas", self.cmake)

    def test_module_contract_is_versioned_isolated_and_still_pending(self) -> None:
        for link in ("`VALUE_SEMANTICS.md`", "`PACKAGE_VERSIONING.md`", "`ADR-0001-runtime-architecture.md`"):
            self.assertIn(link, self.spec)
        package_anchors = (
            "immutable resolution",
            "host_modules",
            "capabilities",
            "PackageEntryMissing",
            "LanguageVersionMismatch",
            "PackageCompileFailed",
        )
        for anchor in package_anchors:
            self.assertIn(anchor, self.package_spec)
        pending_files = (
            ROOT / "include" / "script" / "runtime" / "ModuleLoader.h",
            ROOT / "src" / "script" / "runtime" / "ModuleLoader.cpp",
            ROOT / "include" / "script" / "runtime" / "Vm.h",
            ROOT / "src" / "script" / "runtime" / "Vm.cpp",
        )
        for path in pending_files:
            self.assertFalse(path.exists(), f"update pending boundary for new implementation: {path}")
        self.assertIn("- [ ] Implement modules, imports, and native-function registration.", self.roadmap)
        self.assertIn("rooted lexical environments", self.roadmap)
        self.assertIn("closure\n  execution, evaluator/VM", self.roadmap)

    def test_constructive_turing_argument_has_both_unbounded_counters(self) -> None:
        clause = re.sub(r"\s+", " ", clause_bodies(self.spec)["CTL-018"])
        for anchor in (
            "two-counter Minsky machine",
            "Represent natural-number zero as `null`",
            "successor `n + 1` as the one-element list `[n]`",
            "Two such linked-list counters",
            "instruction, deadline, call, or heap budgets",
        ):
            self.assertIn(anchor, clause)
        self.assertIn("let c0 = null;", self.fixture)
        self.assertIn("let c1 = null;", self.fixture)

    def test_future_dynamic_error_categories_are_explicit_and_pending(self) -> None:
        documented = tuple(re.findall(
            r"^\| `([A-Z][A-Za-z]+)` \|",
            self.spec,
            re.MULTILINE,
        ))
        self.assertEqual(documented, EXPECTED_FUTURE_ERRORS)
        self.assertIn(
            "structured\npayload, stack, and source-span schema follows `ERRORS_AND_CLEANUP.md`",
            self.spec,
        )
        self.assertIn("dynamic loader/VM limits are pending implementation", self.spec)

    def test_language_docs_roadmap_and_ci_link_the_normative_spec(self) -> None:
        self.assertIn("`CONTROL_FLOW_AND_MODULES.md`", self.grammar)
        self.assertIn("`CONTROL_FLOW_AND_MODULES.md`", self.language_spec)
        self.assertIn(
            "- [x] Specify lexical scope, functions, closures, recursion, loops, branching,\n"
            "  non-local control flow, and module loading sufficient for Turing completeness.",
            self.roadmap,
        )
        self.assertIn(
            "- [ ] Specify futures/tasks, cancellation, deadlines, thread-safety boundaries,",
            self.roadmap,
        )
        self.assertIn("`CONTROL_FLOW_AND_MODULES.md` defines source-order lexical bindings", self.roadmap)
        self.assertGreaterEqual(
            self.workflow.count("'docs/script-runtime/CONTROL_FLOW_AND_MODULES.md'"),
            2,
        )
        self.assertIn(
            'python -B -m unittest discover -s tests/docs -p "test_*.py" -v',
            self.workflow,
        )
        self.assertIn("uses only the Python standard library", self.spec)


if __name__ == "__main__":
    unittest.main()
