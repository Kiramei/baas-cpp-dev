from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = ROOT / "docs" / "script-runtime" / "ERRORS_AND_CLEANUP.md"
LANGUAGE_SPEC_PATH = ROOT / "docs" / "script-runtime" / "LANGUAGE_SPEC_DRAFT.md"
GRAMMAR_PATH = ROOT / "docs" / "script-runtime" / "LANGUAGE_GRAMMAR.md"
VALUE_SPEC_PATH = ROOT / "docs" / "script-runtime" / "VALUE_SEMANTICS.md"
CONTROL_SPEC_PATH = ROOT / "docs" / "script-runtime" / "CONTROL_FLOW_AND_MODULES.md"
ROADMAP_PATH = ROOT / "docs" / "script-runtime" / "ROADMAP.md"
SOURCE_LOCATION_PATH = ROOT / "include" / "script" / "SourceLocation.h"
DIAGNOSTIC_PATH = ROOT / "include" / "script" / "Diagnostic.h"
AST_PATH = ROOT / "include" / "script" / "Ast.h"
SEMANTIC_HEADER_PATH = ROOT / "include" / "script" / "SemanticAnalyzer.h"
SEMANTIC_SOURCE_PATH = ROOT / "src" / "script" / "SemanticAnalyzer.cpp"
LEXER_SOURCE_PATH = ROOT / "src" / "script" / "Lexer.cpp"
PARSER_SOURCE_PATH = ROOT / "src" / "script" / "Parser.cpp"
VALUE_HEADER_PATH = ROOT / "include" / "script" / "runtime" / "ValueHeap.h"
VALUE_SOURCE_PATH = ROOT / "src" / "script" / "runtime" / "ValueHeap.cpp"
EXECUTOR_HEADER_PATH = ROOT / "include" / "script" / "runtime" / "BoundedExecutor.h"
ERROR_TRANSLATION_HEADER_PATH = ROOT / "include" / "script" / "runtime" / "ErrorTranslation.h"
ERROR_TRANSLATION_SOURCE_PATH = ROOT / "src" / "script" / "runtime" / "ErrorTranslation.cpp"
PARSER_TEST_PATH = ROOT / "tests" / "script" / "ParserTests.cpp"
SEMANTIC_TEST_PATH = ROOT / "tests" / "script" / "SemanticAnalyzerTests.cpp"
VALID_FIXTURE_PATH = ROOT / "tests" / "script" / "fixtures" / "errors_cleanup_valid.baas"
INVALID_FIXTURE_PATH = ROOT / "tests" / "script" / "fixtures" / "errors_cleanup_invalid.baas"
SEMANTIC_INVALID_FIXTURE_PATH = (
    ROOT / "tests" / "script" / "fixtures" / "errors_cleanup_semantic_invalid.baas"
)
CMAKE_PATH = ROOT / "cmake" / "ScriptRuntime.cmake"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "foundation-runtime.yml"


EXPECTED_CLAUSES = tuple(f"ERR-{number:03d}" for number in range(1, 21))
CLAUSE_TERMS = {
    "ERR-001": ("non-catchable compile diagnostics", "immutable Error", "MUST NOT become catch bindings"),
    "ERR-002": ("`LEXnnn`", "`PARnnn`", "`SEMnnn`", "`SEM009`", "half-open range"),
    "ERR-003": ("identity-bearing", "`baas.script.error/v1`", "`suppressed`", "context-local"),
    "ERR-004": ("minimum Draft 0.1", "Catchable", "Terminal status", "MUST NOT change"),
    "ERR-005": ("immutable package snapshot identity", "filesystem path", "source excerpt"),
    "ERR-006": ("`ThrowStatement.span`", "`CallExpression.span`", "`DeferStatement.span`"),
    "ERR-007": ("innermost frame first", "Re-throwing", "max_stack_frames", "96 innermost", "32 outermost"),
    "ERR-008": ("`cause`", "`suppressed`", "acyclic", "max_cause_depth", "max_suppressed_errors"),
    "ERR-009": ("exactly once", "same immutable Error", "details.thrown_kind", "MUST NOT retain"),
    "ERR-010": ("exact Error identity", "Terminal errors", "future standard error constructor"),
    "ERR-011": ("MUST NOT be swallowed", "primary outcome", "atomically published", "deterministic precedence"),
    "ERR-012": ("current function activation", "`PAR014`", "cleanup time", "function activations"),
    "ERR-013": ("strict last-registered-first-executed order", "rooted before cleanup", "emergency cleanup allowance"),
    "ERR-014": ("remaining cleanup thunks", "remains primary", "suppressed", "execution order"),
    "ERR-015": ("`return`", "`break`", "`continue`", "`await`", "`SEM009`", "max_defers_per_frame"),
    "ERR-016": ("outermost native ABI boundary", "MUST NOT", "`HostInternal`", "`std::bad_alloc`"),
    "ERR-017": ("RT001_TYPE_MISMATCH", "RT023_JSON_DUPLICATE_KEY", "`runtime_code`", "`TaskCancelled`"),
    "ERR-018": ("deterministic", "non-throwing", "same twelve ERR-003 fields", "correlation id", "redact"),
    "ERR-019": ("conformance:error-cleanup-valid", "conformance:error-cleanup-invalid", "conformance:error-cleanup-semantic-invalid", "`SEM009`"),
    "ERR-020": ("implemented foundations", "does not yet implement", "MUST remain pending", "Phase 1 as a whole"),
}

EXPECTED_ERROR_FIELDS = (
    "schema", "code", "message", "origin", "catchable", "source", "stack",
    "cause", "suppressed", "details", "context", "truncated",
)

EXPECTED_LANGUAGE_ERRORS = (
    ("ThrownValue", "yes"),
    ("TypeMismatch", "yes"),
    ("ArgumentInvalid", "yes"),
    ("NameNotFound", "yes"),
    ("UninitializedBinding", "yes"),
    ("NotCallable", "yes"),
    ("CallArityMismatch", "yes"),
    ("CallArgumentDuplicate", "yes"),
    ("CallArgumentUnknown", "yes"),
    ("TaskCycle", "yes"),
    ("IndexOutOfRange", "yes"),
    ("NumericOverflow", "yes"),
    ("DivisionByZero", "yes"),
    ("InvalidUtf8", "yes"),
    ("JsonCycle", "yes"),
    ("JsonNonFinite", "yes"),
    ("JsonUnsupported", "yes"),
    ("JsonDuplicateKey", "yes"),
    ("JsonLimitExceeded", "yes"),
    ("ImportSpecifierInvalid", "yes"),
    ("ImportCycle", "yes"),
    ("ImportDepthLimit", "yes"),
    ("ModuleInitializationFailed", "yes"),
    ("ModuleMemberMissing", "yes"),
    ("CapabilityDenied", "yes"),
    ("HostValidationFailed", "yes"),
    ("HostUnavailable", "yes"),
    ("DeviceDisconnected", "yes"),
    ("PackageMismatch", "yes"),
    ("OcrModelUnavailable", "yes"),
    ("ResourceMissing", "yes"),
    ("Timeout", "yes"),
    ("HostInternal", "yes"),
    ("Cancelled", "no"),
    ("HumanTakeover", "no"),
    ("DeadlineExceeded", "no"),
    ("InstructionLimitExceeded", "no"),
    ("MemoryLimitExceeded", "no"),
    ("StackLimitExceeded", "no"),
    ("CleanupLimitExceeded", "no"),
    ("TaskLimitExceeded", "no"),
    ("InternalInvariant", "no"),
)

EXPECTED_RT_MAPPINGS = (
    ("RT001_TYPE_MISMATCH", "TypeMismatch"),
    ("RT002_CROSS_HEAP_REFERENCE", "InternalInvariant"),
    ("RT003_STALE_REFERENCE", "InternalInvariant"),
    ("RT004_CELL_KIND_MISMATCH", "InternalInvariant"),
    ("RT005_MEMORY_LIMIT_EXCEEDED", "MemoryLimitExceeded"),
    ("RT006_CELL_LIMIT_EXCEEDED", "MemoryLimitExceeded"),
    ("RT007_SINGLE_ALLOCATION_EXCEEDED", "MemoryLimitExceeded"),
    ("RT008_STRING_LIMIT_EXCEEDED", "MemoryLimitExceeded"),
    ("RT009_EXTERNAL_MEMORY_LIMIT_EXCEEDED", "MemoryLimitExceeded"),
    ("RT010_COLLECTION_WORK_LIMIT_EXCEEDED", "MemoryLimitExceeded"),
    ("RT011_INVALID_UTF8", "InvalidUtf8"),
    ("RT012_JSON_CYCLE", "JsonCycle"),
    ("RT013_JSON_NON_FINITE", "JsonNonFinite"),
    ("RT014_JSON_UNSUPPORTED", "JsonUnsupported"),
    ("RT015_HEAP_TORN_DOWN", "InternalInvariant"),
    ("RT016_INDEX_OUT_OF_RANGE", "IndexOutOfRange"),
    ("RT017_RELEASE_QUEUE_LIMIT_EXCEEDED", "CleanupLimitExceeded"),
    ("RT018_JSON_DEPTH_LIMIT_EXCEEDED", "JsonLimitExceeded"),
    ("RT019_JSON_NODE_LIMIT_EXCEEDED", "JsonLimitExceeded"),
    ("RT020_JSON_STRING_LIMIT_EXCEEDED", "JsonLimitExceeded"),
    ("RT021_JSON_BYTE_LIMIT_EXCEEDED", "JsonLimitExceeded"),
    ("RT022_JSON_WORK_LIMIT_EXCEEDED", "JsonLimitExceeded"),
    ("RT023_JSON_DUPLICATE_KEY", "JsonDuplicateKey"),
)

EXPECTED_LEX_CODES = tuple(f"LEX{number:03d}" for number in range(1, 7))
EXPECTED_PARSE_CODES = (
    *(f"PAR{number:03d}" for number in range(1, 12)),
    "PAR014", "PAR018", "PAR019",
)
EXPECTED_SEMANTIC_CODES = (
    "SEM001", "SEM002", "SEM003", "SEM004", "SEM006", "SEM007", "SEM008", "SEM009",
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def clause_bodies(document: str) -> dict[str, str]:
    matches = list(re.finditer(r"^### (ERR-\d{3}) — .+$", document, re.MULTILINE))
    result: dict[str, str] = {}
    for index, match in enumerate(matches):
        stop = matches[index + 1].start() if index + 1 < len(matches) else len(document)
        result[match.group(1)] = document[match.end():stop]
    return result


class ErrorsAndCleanupSpecificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.spec = read(SPEC_PATH)
        cls.language_spec = read(LANGUAGE_SPEC_PATH)
        cls.grammar = read(GRAMMAR_PATH)
        cls.value_spec = read(VALUE_SPEC_PATH)
        cls.control_spec = read(CONTROL_SPEC_PATH)
        cls.roadmap = read(ROADMAP_PATH)
        cls.source_location = read(SOURCE_LOCATION_PATH)
        cls.diagnostic = read(DIAGNOSTIC_PATH)
        cls.ast = read(AST_PATH)
        cls.semantic_header = read(SEMANTIC_HEADER_PATH)
        cls.semantic_source = read(SEMANTIC_SOURCE_PATH)
        cls.lexer_source = read(LEXER_SOURCE_PATH)
        cls.parser_source = read(PARSER_SOURCE_PATH)
        cls.value_header = read(VALUE_HEADER_PATH)
        cls.value_source = read(VALUE_SOURCE_PATH)
        cls.executor_header = read(EXECUTOR_HEADER_PATH)
        cls.error_translation_header = read(ERROR_TRANSLATION_HEADER_PATH)
        cls.error_translation_source = read(ERROR_TRANSLATION_SOURCE_PATH)
        cls.parser_tests = read(PARSER_TEST_PATH)
        cls.semantic_tests = read(SEMANTIC_TEST_PATH)
        cls.valid_fixture = read(VALID_FIXTURE_PATH)
        cls.invalid_fixture = read(INVALID_FIXTURE_PATH)
        cls.semantic_invalid_fixture = read(SEMANTIC_INVALID_FIXTURE_PATH)
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

    def test_error_envelope_fields_and_language_catalog_are_exact(self) -> None:
        body = clause_bodies(self.spec)["ERR-003"]
        fields = tuple(re.findall(r"^\| `([a-z]+)` \|", body, re.MULTILINE))
        self.assertEqual(fields, EXPECTED_ERROR_FIELDS)
        catalog = tuple(re.findall(
            r"^\| `([A-Z][A-Za-z0-9]+)` \| (yes|no) \|",
            clause_bodies(self.spec)["ERR-004"],
            re.MULTILINE,
        ))
        self.assertEqual(catalog, EXPECTED_LANGUAGE_ERRORS)
        for anchor in (
            "exact field\norder is `snapshot_id`, `module`, `span`",
            "`byte_offset`, `line`, `column`",
            "exact order: `task_id`,\n`session_id`, `package_id`, `snapshot_id`, `language_version`,",
            "`stack_frames`, `cause_errors`,\n`suppressed_errors`, `message_bytes`, and `detail_bytes`",
            "Boolean `details_replaced` and `fallback`",
        ):
            self.assertIn(anchor, self.spec)

    def test_source_location_and_compile_diagnostic_contract_match_code(self) -> None:
        self.assertIn("schema\n`baas.script.diagnostic/v1`", self.spec)
        self.assertIn(
            "field order `schema`, `severity`, `code`,\n`message`, `source`",
            self.spec,
        )
        for anchor in (
            "std::size_t byte_offset{0};", "std::size_t line{1};",
            "std::size_t column{1};", "Half-open source range [begin, end)",
        ):
            self.assertIn(anchor, self.source_location)
        for anchor in (
            "DiagnosticSeverity severity", "std::string code;",
            "std::string message;", "SourceSpan span{};",
        ):
            self.assertIn(anchor, self.diagnostic)

        lex_codes = tuple(sorted(set(re.findall(r'"(LEX\d{3})"', self.lexer_source))))
        parse_codes = tuple(sorted(set(re.findall(r'"(PAR\d{3})"', self.parser_source))))
        semantic_codes = tuple(sorted(set(re.findall(r'"(SEM\d{3})"', self.semantic_header))))
        self.assertEqual(lex_codes, EXPECTED_LEX_CODES)
        self.assertEqual(parse_codes, EXPECTED_PARSE_CODES)
        self.assertEqual(semantic_codes, EXPECTED_SEMANTIC_CODES)
        self.assertIn('cleanup_control{"SEM009"}', self.semantic_header)
        for anchor in (
            'reject_cleanup_control(*statement, "return")',
            'reject_cleanup_control(*statement, "break")',
            'reject_cleanup_control(*statement, "continue")',
            'reject_cleanup_control(*expression, "await")',
            'reject_cleanup_control(*statement, "nested defer")',
            "previous_cleanup_depth(analyzer.cleanup_body_depth_)",
        ):
            self.assertIn(anchor, self.semantic_source)
        self.assertIn("count_code(invalid_result, semantic_diagnostic_code::cleanup_control) == 5", self.semantic_tests)

    def test_throw_catch_defer_grammar_ast_and_semantics_are_anchored(self) -> None:
        for anchor in (
            'throw-statement    = "throw", expression, ";" ;',
            'try-statement      = "try", block, "catch", (identifier | "(", identifier, ")"), block ;',
            'defer-statement    = "defer", statement ;',
        ):
            self.assertIn(anchor, self.grammar)
        for node in ("ThrowStatement", "TryCatchStatement", "DeferStatement"):
            self.assertIn(node, self.ast)
        for anchor in (
            "throw_statement(previous())", "try_statement(previous())",
            "defer_statement(previous())", 'error("PAR014"',
        ):
            self.assertIn(anchor, self.parser_source)
        for anchor in (
            "case NodeKind::ThrowStatement", "case NodeKind::TryCatchStatement",
            "BindingKind::Catch", "case NodeKind::DeferStatement",
        ):
            self.assertIn(anchor, self.semantic_source)
        self.assertIn('has_code(invalid, "PAR014")', self.parser_tests)

    def test_stack_cause_cleanup_precedence_and_limits_are_normative(self) -> None:
        stack_body = re.sub(r"\s+", " ", clause_bodies(self.spec)["ERR-007"])
        for field in (
            "`kind`", "`module`", "`function`", "`phase`", "`call_source`",
            "`definition_source`", "`defer_source`",
        ):
            self.assertIn(field, stack_body)
        self.assertIn("ordered innermost frame first", stack_body)
        self.assertIn("default `max_stack_frames` is 128", stack_body)

        cause_body = re.sub(r"\s+", " ", clause_bodies(self.spec)["ERR-008"])
        for default in (
            "max_cause_depth = 16", "max_suppressed_errors = 16",
            "max_error_message_bytes = 4096", "max_error_detail_bytes = 65536",
        ):
            self.assertIn(default, cause_body)
        self.assertIn("valid UTF-8 scalar boundary", cause_body)
        cleanup_body = re.sub(r"\s+", " ", clause_bodies(self.spec)["ERR-014"])
        self.assertIn("primary Error remains primary", cleanup_body)
        self.assertIn("first cleanup failure becomes primary", cleanup_body)
        self.assertIn("later cleanup failures are suppressed", cleanup_body)
        self.assertIn("construct one derived immutable primary Error", cleanup_body)
        self.assertIn("Error that entered unwind is not mutated", cleanup_body)

        serialization_body = re.sub(r"\s+", " ", clause_bodies(self.spec)["ERR-018"])
        self.assertIn("same twelve ERR-003 fields", serialization_body)
        self.assertIn("every context value is null except", serialization_body)
        self.assertIn("`truncated.fallback` is true", serialization_body)

    def test_foundation_runtime_translation_table_is_complete(self) -> None:
        documented = tuple(re.findall(
            r"^\| `(RT\d{3}_[A-Z0-9_]+)` \| `([A-Z][A-Za-z0-9]+)` \|",
            clause_bodies(self.spec)["ERR-017"],
            re.MULTILINE,
        ))
        self.assertEqual(documented, EXPECTED_RT_MAPPINGS)
        implemented_names = tuple(re.findall(
            r'return "(RT\d{3}_[A-Z0-9_]+)";',
            self.value_source,
        ))
        self.assertIn("RT000_UNKNOWN", implemented_names)
        stable_implemented_names = tuple(
            code for code in implemented_names if code != "RT000_UNKNOWN"
        )
        self.assertEqual(tuple(code for code, _ in documented), stable_implemented_names)
        for exception, script_code in (
            ("TaskCancelled", "Cancelled"),
            ("SubmitTimeout", "Timeout"),
            ("ExecutorShutdown", "HostUnavailable"),
        ):
            self.assertIn(f"class {exception}", self.executor_header)
            self.assertIn(f"| `{exception}` | `{script_code}` |", self.spec)
        self.assertIn("translate_runtime_error_code", self.error_translation_header)
        for _, script_code in documented:
            self.assertIn(f'{{"{script_code}",', self.error_translation_source)

    def test_current_error_cell_is_foundation_not_false_completion(self) -> None:
        for anchor in (
            "struct ErrorMetadata", "std::string code;", "std::string message;",
            "std::optional<SourceSpan> span;", "std::vector<Value> details;",
            "allocate_error(ErrorMetadata metadata)",
        ):
            self.assertIn(anchor, self.value_header)
        for path in (
            ROOT / "include" / "script" / "runtime" / "Vm.h",
            ROOT / "include" / "script" / "runtime" / "StructuredError.h",
            ROOT / "include" / "script" / "runtime" / "HostErrorTranslator.h",
        ):
            self.assertFalse(path.exists(), f"update pending boundary for new implementation: {path}")
        self.assertIn("does not yet implement VM execution", self.spec)
        self.assertIn("- [~] Implement structured exceptions, stack traces, cancellation, and limits.", self.roadmap)
        self.assertIn("Error envelopes, stack capture, VM unwinding", self.roadmap)

    def test_static_conformance_fixtures_and_ctest_wiring(self) -> None:
        valid_example = re.search(
            r"<!-- conformance:error-cleanup-valid -->\s*```baas\n(.*?)\n```",
            self.spec,
            re.DOTALL,
        )
        invalid_example = re.search(
            r"<!-- conformance:error-cleanup-invalid -->\s*```baas\n(.*?)\n```",
            self.spec,
            re.DOTALL,
        )
        semantic_invalid_example = re.search(
            r"<!-- conformance:error-cleanup-semantic-invalid -->\s*```baas\n(.*?)\n```",
            self.spec,
            re.DOTALL,
        )
        self.assertIsNotNone(valid_example)
        self.assertIsNotNone(invalid_example)
        self.assertIsNotNone(semantic_invalid_example)
        self.assertEqual(valid_example.group(1) + "\n", self.valid_fixture)
        self.assertEqual(invalid_example.group(1) + "\n", self.invalid_fixture)
        self.assertEqual(
            semantic_invalid_example.group(1) + "\n",
            self.semantic_invalid_fixture,
        )
        for anchor in (
            "BAAS_script_check_errors_cleanup_valid_cli",
            "tests/script/fixtures/errors_cleanup_valid.baas",
            "BAAS_script_check_errors_cleanup_invalid_cli",
            "tests/script/fixtures/errors_cleanup_invalid.baas",
            "BAAS_script_check_errors_cleanup_semantic_invalid_cli",
            "tests/script/fixtures/errors_cleanup_semantic_invalid.baas",
            "WILL_FAIL TRUE",
        ):
            self.assertIn(anchor, self.cmake)

    def test_language_docs_roadmap_and_ci_link_the_normative_spec(self) -> None:
        self.assertIn("`ERRORS_AND_CLEANUP.md`", self.language_spec)
        self.assertIn("`ERRORS_AND_CLEANUP.md`", self.grammar)
        self.assertIn("`ERRORS_AND_CLEANUP.md`", self.control_spec)
        self.assertIn(
            "- [x] Specify structured errors, stack traces, source spans, cleanup/defer, and\n"
            "  host-error translation.",
            self.roadmap,
        )
        self.assertIn(
            "- [ ] Specify capability-scoped host APIs for image, OCR, device, configuration,",
            self.roadmap,
        )
        self.assertIn("`ERRORS_AND_CLEANUP.md` defines compile/runtime error separation", self.roadmap)
        self.assertGreaterEqual(
            self.workflow.count("'docs/script-runtime/ERRORS_AND_CLEANUP.md'"),
            2,
        )
        self.assertIn(
            'python -B -m unittest discover -s tests/docs -p "test_*.py" -v',
            self.workflow,
        )
        self.assertIn("uses only the Python standard library", self.spec)


if __name__ == "__main__":
    unittest.main()
