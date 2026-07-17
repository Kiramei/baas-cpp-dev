from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = ROOT / "docs" / "script-runtime" / "VALUE_SEMANTICS.md"
LANGUAGE_SPEC_PATH = ROOT / "docs" / "script-runtime" / "LANGUAGE_SPEC_DRAFT.md"
GRAMMAR_PATH = ROOT / "docs" / "script-runtime" / "LANGUAGE_GRAMMAR.md"
ROADMAP_PATH = ROOT / "docs" / "script-runtime" / "ROADMAP.md"
VALUE_HEADER_PATH = ROOT / "include" / "script" / "runtime" / "ValueHeap.h"
VALUE_SOURCE_PATH = ROOT / "src" / "script" / "runtime" / "ValueHeap.cpp"
JSON_HEADER_PATH = ROOT / "include" / "script" / "runtime" / "JsonBridge.h"
JSON_SOURCE_PATH = ROOT / "src" / "script" / "runtime" / "JsonBridge.cpp"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "foundation-runtime.yml"


EXPECTED_CLAUSES = tuple(f"VAL-{number:03d}" for number in range(1, 19))

CLAUSE_TERMS = {
    "VAL-001": (
        "`null`", "`bool`", "`int`", "`float`", "`string`", "`list`",
        "`ordered-map`", "`function`", "`module`", "`error`", "`task`",
        "`host-handle`", "HeapReference",
    ),
    "VAL-002": ("well-formed UTF-8", "RT011_INVALID_UTF8", "U+0000", "byte-exact"),
    "VAL-003": ("no general implicit coercion", "RT001_TYPE_MISMATCH", "and", "or"),
    "VAL-004": ("9,223,372,036,854,775,807", "PAR018", "PAR019", "NaN", "9,007,199,254,740,992"),
    "VAL-005": ("Truthiness", "`int`", "zero elements", "never"),
    "VAL-006": ("cycle-aware", "mixed int/float", "independent of insertion order", "`is`"),
    "VAL-007": ("zero-based", "RT016_INDEX_OUT_OF_RANGE", "aliases", "negative-index"),
    "VAL-008": ("last-value-wins", "first insertion position", "string keys", "JSON duplicate keys"),
    "VAL-009": ("Function", "module", "error", "task", "host handle", "release record"),
    "VAL-010": ("non-moving tracing heap", "RT003_STALE_REFERENCE", "RT015_HEAP_TORN_DOWN", "partial"),
    "VAL-011": ("RT002_CROSS_HEAP_REFERENCE", "RT003_STALE_REFERENCE", "Independent heaps", "queue"),
    "VAL-012": ("dependency-free", "signed 64-bit integer", "finite binary64", "unique string-key"),
    "VAL-013": ("RT012_JSON_CYCLE", "RT013_JSON_NON_FINITE", "RT014_JSON_UNSUPPORTED", "RT023_JSON_DUPLICATE_KEY"),
    "VAL-014": ("deep_copy_json_value", "DAG", "independent destination objects", "temporarily rooted"),
    "VAL-015": ("max_live_bytes", "max_cells", "max_collection_work", "max_pending_release_records"),
    "VAL-016": ("max_depth", "max_nodes", "max_string_bytes", "max_total_bytes", "max_work", "1,024"),
    "VAL-017": ("RT001_TYPE_MISMATCH", "RT011_INVALID_UTF8", "RT018_JSON_DEPTH_LIMIT_EXCEEDED", "RT023_JSON_DUPLICATE_KEY"),
    "VAL-018": ("LANGUAGE_GRAMMAR.md", "SynchronousEvaluator", "JSON text I/O", "Python or JavaScript"),
}

EXPECTED_ERRORS = (
    ("TypeMismatch", "RT001_TYPE_MISMATCH"),
    ("CrossHeapReference", "RT002_CROSS_HEAP_REFERENCE"),
    ("StaleReference", "RT003_STALE_REFERENCE"),
    ("CellKindMismatch", "RT004_CELL_KIND_MISMATCH"),
    ("MemoryLimitExceeded", "RT005_MEMORY_LIMIT_EXCEEDED"),
    ("CellLimitExceeded", "RT006_CELL_LIMIT_EXCEEDED"),
    ("SingleAllocationExceeded", "RT007_SINGLE_ALLOCATION_EXCEEDED"),
    ("StringLimitExceeded", "RT008_STRING_LIMIT_EXCEEDED"),
    ("ExternalMemoryLimitExceeded", "RT009_EXTERNAL_MEMORY_LIMIT_EXCEEDED"),
    ("CollectionWorkLimitExceeded", "RT010_COLLECTION_WORK_LIMIT_EXCEEDED"),
    ("InvalidUtf8", "RT011_INVALID_UTF8"),
    ("JsonCycle", "RT012_JSON_CYCLE"),
    ("JsonNonFinite", "RT013_JSON_NON_FINITE"),
    ("JsonUnsupported", "RT014_JSON_UNSUPPORTED"),
    ("HeapTornDown", "RT015_HEAP_TORN_DOWN"),
    ("IndexOutOfRange", "RT016_INDEX_OUT_OF_RANGE"),
    ("ReleaseQueueLimitExceeded", "RT017_RELEASE_QUEUE_LIMIT_EXCEEDED"),
    ("JsonDepthLimitExceeded", "RT018_JSON_DEPTH_LIMIT_EXCEEDED"),
    ("JsonNodeLimitExceeded", "RT019_JSON_NODE_LIMIT_EXCEEDED"),
    ("JsonStringLimitExceeded", "RT020_JSON_STRING_LIMIT_EXCEEDED"),
    ("JsonByteLimitExceeded", "RT021_JSON_BYTE_LIMIT_EXCEEDED"),
    ("JsonWorkLimitExceeded", "RT022_JSON_WORK_LIMIT_EXCEEDED"),
    ("JsonDuplicateKey", "RT023_JSON_DUPLICATE_KEY"),
    ("HeapBusy", "RT024_HEAP_BUSY"),
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def clause_bodies(document: str) -> dict[str, str]:
    matches = list(re.finditer(r"^### (VAL-\d{3}) — .+$", document, re.MULTILINE))
    result: dict[str, str] = {}
    for index, match in enumerate(matches):
        stop = matches[index + 1].start() if index + 1 < len(matches) else len(document)
        result[match.group(1)] = document[match.end():stop]
    return result


class ValueSemanticsSpecificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.spec = read(SPEC_PATH)
        cls.language_spec = read(LANGUAGE_SPEC_PATH)
        cls.grammar = read(GRAMMAR_PATH)
        cls.roadmap = read(ROADMAP_PATH)
        cls.value_header = read(VALUE_HEADER_PATH)
        cls.value_source = read(VALUE_SOURCE_PATH)
        cls.json_header = read(JSON_HEADER_PATH)
        cls.json_source = read(JSON_SOURCE_PATH)
        cls.workflow = read(WORKFLOW_PATH)

    def test_complete_normative_clause_inventory_and_terms(self) -> None:
        bodies = clause_bodies(self.spec)
        self.assertEqual(tuple(bodies), EXPECTED_CLAUSES)
        self.assertEqual(set(CLAUSE_TERMS), set(EXPECTED_CLAUSES))
        for clause, terms in CLAUSE_TERMS.items():
            with self.subTest(clause=clause):
                self.assertIn("MUST", bodies[clause])
                normalized = re.sub(r"\s+", " ", bodies[clause])
                for term in terms:
                    self.assertIn(term, normalized)

    def test_language_value_table_matches_runtime_inventory(self) -> None:
        enum_match = re.search(
            r"enum class ValueKind\s*\{(?P<body>.*?)\};",
            self.value_header,
            re.DOTALL,
        )
        self.assertIsNotNone(enum_match)
        runtime_kinds = tuple(re.findall(r"^\s*(\w+),?\s*$", enum_match.group("body"), re.MULTILINE))
        self.assertEqual(
            runtime_kinds,
            (
                "Null", "Boolean", "Integer", "Float", "HeapReference",
                "String", "List", "OrderedMap", "Function", "Module",
                "Error", "Task", "HostHandle", "Bytes",
            ),
        )
        language_rows = {
            "null": "Null", "bool": "Boolean", "int": "Integer", "float": "Float",
            "string": "String", "list": "List", "ordered-map": "OrderedMap",
            "function": "Function", "module": "Module", "error": "Error",
            "task": "Task", "host-handle": "HostHandle", "bytes": "Bytes",
        }
        for language_kind, runtime_kind in language_rows.items():
            self.assertIn(f"| `{language_kind}` | `{runtime_kind}` |", self.spec)
        self.assertIn("not a fourteenth language\nkind", self.spec)

    def test_stable_error_table_exactly_matches_runtime(self) -> None:
        enum_match = re.search(
            r"enum class RuntimeErrorCode\s*\{(?P<body>.*?)\};",
            self.value_header,
            re.DOTALL,
        )
        self.assertIsNotNone(enum_match)
        enum_names = tuple(re.findall(r"^\s*(\w+),\s*$", enum_match.group("body"), re.MULTILINE))
        source_pairs = tuple(re.findall(
            r"case RuntimeErrorCode::(\w+): return \"(RT\d{3}_[A-Z0-9_]+)\";",
            self.value_source,
        ))
        self.assertEqual(tuple(name for name, _ in EXPECTED_ERRORS), enum_names)
        self.assertEqual(source_pairs, EXPECTED_ERRORS)
        documented = tuple(re.findall(r"^\| `(RT\d{3}_[A-Z0-9_]+)` \|", self.spec, re.MULTILINE))
        self.assertEqual(documented, tuple(stable for _, stable in EXPECTED_ERRORS))

    def test_default_limits_match_headers_and_normative_tables(self) -> None:
        heap_defaults = {
            "max_live_bytes{64U * 1024U * 1024U}": "| `max_live_bytes` | 64 MiB |",
            "max_cells{1'000'000}": "| `max_cells` | 1,000,000 |",
            "max_single_allocation{8U * 1024U * 1024U}": "| `max_single_allocation` | 8 MiB |",
            "max_string_bytes{16U * 1024U * 1024U}": "| `max_string_bytes` | 16 MiB |",
            "max_external_bytes{64U * 1024U * 1024U}": "| `max_external_bytes` | 64 MiB |",
            "soft_collect_threshold{48U * 1024U * 1024U}": "| `soft_collect_threshold` | 48 MiB |",
            "max_collection_work{2'000'000}": "| `max_collection_work` | 2,000,000 cells |",
            "max_pending_release_records{1'000'000}": "| `max_pending_release_records` | 1,000,000 |",
        }
        bridge_defaults = {
            "max_depth{256}": "| `max_depth` | 256 |",
            "max_nodes{100'000}": "| `max_nodes` | 100,000 |",
            "max_string_bytes{16U * 1024U * 1024U}": "| `max_string_bytes` | 16 MiB |",
            "max_total_bytes{64U * 1024U * 1024U}": "| `max_total_bytes` | 64 MiB |",
            "max_work{500'000}": "| `max_work` | 500,000 units |",
        }
        for source_fragment, spec_fragment in heap_defaults.items():
            self.assertIn(source_fragment, self.value_header)
            self.assertIn(spec_fragment, self.spec)
        for source_fragment, spec_fragment in bridge_defaults.items():
            self.assertIn(source_fragment, self.json_header)
            self.assertIn(spec_fragment, self.spec)
        self.assertIn("constexpr std::size_t hard_depth_ceiling = 1024;", self.json_source)
        self.assertIn("hard\nceiling of 1,024", self.spec)

    def test_json_and_heap_safety_anchors_are_normative(self) -> None:
        source_anchors = (
            "RuntimeErrorCode::CrossHeapReference",
            "RuntimeErrorCode::StaleReference",
            "RuntimeErrorCode::JsonCycle",
            "RuntimeErrorCode::JsonNonFinite",
            "RuntimeErrorCode::JsonUnsupported",
            "RuntimeErrorCode::JsonDuplicateKey",
        )
        combined_source = self.value_source + self.json_source
        for anchor in source_anchors:
            self.assertIn(anchor, combined_source)
        self.assertIn("keys.insert(entry.first)", self.json_source)
        self.assertIn("before any destination-heap\nallocation", self.spec)
        self.assertIn("always duplicates mutable lists/maps", self.json_header)
        self.assertIn("two independent destination objects", self.spec)

    def test_grammar_and_language_draft_link_to_normative_contract(self) -> None:
        self.assertIn("runtime contract requires a\n  string key", self.grammar)
        self.assertGreaterEqual(self.language_spec.count("`VALUE_SEMANTICS.md`"), 2)
        self.assertIn("`LANGUAGE_GRAMMAR.md`", self.spec)
        self.assertIn("JSON text parsing or serialization", self.language_spec)

    def test_roadmap_marks_the_completed_value_spec_item(self) -> None:
        self.assertIn(
            "- [x] Specify value types, equality, conversions, collections, mutability, and\n"
            "  JSON interoperation.",
            self.roadmap,
        )
        self.assertIn(
            "- [x] Specify capability-scoped host APIs for image, OCR, device, configuration,",
            self.roadmap,
        )
        self.assertIn("`VALUE_SEMANTICS.md` defines the normative value", self.roadmap)

    def test_foundation_ci_runs_the_standard_library_checker(self) -> None:
        self.assertGreaterEqual(self.workflow.count("'tests/docs/**'"), 2)
        self.assertGreaterEqual(
            self.workflow.count("'docs/script-runtime/VALUE_SEMANTICS.md'"),
            2,
        )
        self.assertIn("Verify normative script specifications", self.workflow)
        self.assertIn(
            'python -B -m unittest discover -s tests/docs -p "test_*.py" -v',
            self.workflow,
        )
        self.assertIn("uses only the Python standard library", self.spec)


if __name__ == "__main__":
    unittest.main()
