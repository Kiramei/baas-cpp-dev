from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = ROOT / "docs" / "script-runtime" / "ASYNC_TASKS.md"
LANGUAGE_SPEC_PATH = ROOT / "docs" / "script-runtime" / "LANGUAGE_SPEC_DRAFT.md"
GRAMMAR_PATH = ROOT / "docs" / "script-runtime" / "LANGUAGE_GRAMMAR.md"
ERROR_SPEC_PATH = ROOT / "docs" / "script-runtime" / "ERRORS_AND_CLEANUP.md"
VALUE_SPEC_PATH = ROOT / "docs" / "script-runtime" / "VALUE_SEMANTICS.md"
CONTROL_SPEC_PATH = ROOT / "docs" / "script-runtime" / "CONTROL_FLOW_AND_MODULES.md"
ROADMAP_PATH = ROOT / "docs" / "script-runtime" / "ROADMAP.md"
AST_PATH = ROOT / "include" / "script" / "Ast.h"
PARSER_PATH = ROOT / "src" / "script" / "Parser.cpp"
SEMANTIC_PATH = ROOT / "src" / "script" / "SemanticAnalyzer.cpp"
PARSER_TEST_PATH = ROOT / "tests" / "script" / "ParserTests.cpp"
VALUE_HEADER_PATH = ROOT / "include" / "script" / "runtime" / "ValueHeap.h"
VALUE_SOURCE_PATH = ROOT / "src" / "script" / "runtime" / "ValueHeap.cpp"
ENVIRONMENT_HEADER_PATH = ROOT / "include" / "script" / "runtime" / "Environment.h"
EXECUTOR_HEADER_PATH = ROOT / "include" / "script" / "runtime" / "BoundedExecutor.h"
EXECUTOR_SOURCE_PATH = ROOT / "src" / "script" / "runtime" / "BoundedExecutor.cpp"
EXECUTOR_TEST_PATH = ROOT / "tests" / "script" / "BoundedExecutorTests.cpp"
VALID_FIXTURE_PATH = ROOT / "tests" / "script" / "fixtures" / "async_tasks_valid.baas"
INVALID_FIXTURE_PATH = ROOT / "tests" / "script" / "fixtures" / "async_tasks_invalid.baas"
CMAKE_PATH = ROOT / "cmake" / "ScriptRuntime.cmake"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "foundation-runtime.yml"


EXPECTED_CLAUSES = tuple(f"ASY-{number:03d}" for number in range(1, 21))
CLAUSE_TERMS = {
    "ASY-001": ("`async fn`", "first-class Task", "without blocking", "logical strand"),
    "ASY-002": ("task_id", "creation sequence", "ordered awaiters", "MUST NOT encode"),
    "ASY-003": ("`Pending`", "`Running`", "`Succeeded`", "`Failed`", "`Cancelled`", "exactly once"),
    "ASY-004": ("synchronously", "atomically", "`TaskLimitExceeded`", "later scheduler turn"),
    "ASY-005": ("exactly once", "same terminal", "roots", "`TaskCycle`", "ownership edges"),
    "ASY-006": ("root task scope", "MUST NOT outlive", "cannot detach", "privileged service host"),
    "ASY-007": ("unobserved", "scheduler event order", "suppressed", "requests cancellation"),
    "ASY-008": ("`spawn(async_callable, args...)`", "`all(tasks)`", "`race(tasks)`", "`shield(async_callable)`"),
    "ASY-009": ("idempotent", "Pending", "Running", "parent-before-child", "creation sequence"),
    "ASY-010": ("MUST NOT mask", "`Cancelled`", "`HumanTakeover`", "outermost shield", "atomic commit"),
    "ASY-011": ("every bytecode dispatch", "safe point", "1024", "instruction quantum", "MUST NOT defer"),
    "ASY-012": ("injected monotonic clock", "minimum", "now >= deadline", "kind_priority"),
    "ASY-013": ("exact priority", "`InternalInvariant`", "normal success", "suppressed", "MUST NOT decide"),
    "ASY-014": ("execution-context strand", "never overlap", "immutable bounded completion records", "affinity"),
    "ASY-015": ("suspension token", "posted exactly once", "request_cancel", "late result discarded", "MUST NOT"),
    "ASY-016": ("exact heap roots", "context teardown", "Late completions", "MUST NOT silently leak"),
    "ASY-017": ("injected `MonotonicClock`", "FIFO ready queue", "task_create", "Replay", "MUST NOT depend"),
    "ASY-018": ("max_tasks_per_context", "`detail::TaskPhase::Queued`", "`TrySubmit` full", "steady_clock"),
    "ASY-019": ("conformance:async-tasks-valid", "conformance:async-tasks-invalid", "`PAR011`", "MUST remain"),
    "ASY-020": ("foundation evidence only", "does not yet implement", "MUST remain pending", "Phase 1 as a whole"),
}

EXPECTED_TASK_STATES = ("Pending", "Running", "Succeeded", "Failed", "Cancelled")
EXPECTED_TRANSITIONS = (
    ("Pending", "Running"),
    ("Pending", "Cancelled"),
    ("Running", "Succeeded"),
    ("Running", "Failed"),
    ("Running", "Cancelled"),
)
EXPECTED_ASYNC_ERRORS = (
    ("ArgumentInvalid", "yes"),
    ("TaskCycle", "yes"),
    ("TaskLimitExceeded", "no"),
)
EXPECTED_OPERATIONS = (
    "spawn(async_callable, args...)",
    "join(task)",
    "all(tasks)",
    "race(tasks)",
    "timeout(task, duration)",
    "cancel(task)",
    "scope(async_callable)",
    "shield(async_callable)",
)
EXPECTED_PRIORITY = (
    ("1", "`InternalInvariant`"),
    ("2", "`MemoryLimitExceeded`, `StackLimitExceeded`, `InstructionLimitExceeded`, `CleanupLimitExceeded`, `TaskLimitExceeded`"),
    ("3", "`HumanTakeover`"),
    ("4", "`DeadlineExceeded`"),
    ("5", "`Cancelled`"),
    ("6", "escaping catchable body/host Error"),
    ("7", "normal success"),
)
EXPECTED_TRACE_EVENTS = (
    "task_create", "task_start", "task_suspend", "task_resume",
    "cancel_request", "cancel_observed", "timer_set", "timer_fire",
    "host_start", "host_complete", "host_discard", "race_loser_error",
    "task_complete",
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def clause_bodies(document: str) -> dict[str, str]:
    matches = list(re.finditer(r"^### (ASY-\d{3}) — .+$", document, re.MULTILINE))
    result: dict[str, str] = {}
    for index, match in enumerate(matches):
        stop = matches[index + 1].start() if index + 1 < len(matches) else len(document)
        result[match.group(1)] = document[match.end():stop]
    return result


class AsyncTasksSpecificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.spec = read(SPEC_PATH)
        cls.language_spec = read(LANGUAGE_SPEC_PATH)
        cls.grammar = read(GRAMMAR_PATH)
        cls.error_spec = read(ERROR_SPEC_PATH)
        cls.value_spec = read(VALUE_SPEC_PATH)
        cls.control_spec = read(CONTROL_SPEC_PATH)
        cls.roadmap = read(ROADMAP_PATH)
        cls.ast = read(AST_PATH)
        cls.parser = read(PARSER_PATH)
        cls.semantic = read(SEMANTIC_PATH)
        cls.parser_tests = read(PARSER_TEST_PATH)
        cls.value_header = read(VALUE_HEADER_PATH)
        cls.value_source = read(VALUE_SOURCE_PATH)
        cls.environment_header = read(ENVIRONMENT_HEADER_PATH)
        cls.executor_header = read(EXECUTOR_HEADER_PATH)
        cls.executor_source = read(EXECUTOR_SOURCE_PATH)
        cls.executor_tests = read(EXECUTOR_TEST_PATH)
        cls.valid_fixture = read(VALID_FIXTURE_PATH)
        cls.invalid_fixture = read(INVALID_FIXTURE_PATH)
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

    def test_task_state_machine_and_heap_metadata_are_exact(self) -> None:
        body = clause_bodies(self.spec)["ASY-003"]
        state_inventory = tuple(re.findall(
            r"exactly (`[A-Za-z]+`), (`[A-Za-z]+`), (`[A-Za-z]+`),\n"
            r"(`?[A-Za-z]+`?), and (`[A-Za-z]+`)",
            body,
        )[0])
        self.assertEqual(tuple(value.strip("`") for value in state_inventory), EXPECTED_TASK_STATES)
        transitions = tuple(re.findall(r"^(Pending|Running) -> (Running|Succeeded|Failed|Cancelled)$", body, re.MULTILINE))
        self.assertEqual(transitions, EXPECTED_TRANSITIONS)

        enum_match = re.search(r"enum class TaskState \{ ([^}]+) \};", self.value_header)
        self.assertIsNotNone(enum_match)
        implemented = tuple(value.strip() for value in enum_match.group(1).split(","))
        self.assertEqual(implemented, EXPECTED_TASK_STATES)
        for anchor in (
            "struct TaskMetadata", "std::uint64_t task_id{};",
            "TaskState state{TaskState::Pending};", "std::vector<Value> retained_values;",
            "struct TaskCell", "for (const auto value : cell.metadata.retained_values)",
        ):
            self.assertIn(anchor, self.value_header + self.value_source)

    def test_async_await_grammar_parser_ast_and_semantics_are_anchored(self) -> None:
        for anchor in (
            'function-declaration = "async"?, "fn", identifier, parameters, function-body ;',
            'unary           = ("+" | "-" | "not" | "await"), unary | power ;',
            'function-expression = "async"?, "fn", parameters, function-body ;',
        ):
            self.assertIn(anchor, self.grammar)
        for anchor in (
            "struct AwaitExpression", "bool is_async;", "NodeKind::AwaitExpression",
        ):
            self.assertIn(anchor, self.ast)
        for anchor in (
            "struct FunctionContextGuard", "parser.in_async_function_ = async",
            'error("PAR011"', "std::make_shared<const AwaitExpression>",
        ):
            self.assertIn(anchor, self.parser)
        self.assertIn("case NodeKind::AwaitExpression", self.semantic)
        self.assertIn('has_code(invalid, "PAR011")', self.parser_tests)
        self.assertIn("await_errors == 2", self.parser_tests)

    def test_structured_operations_wait_graph_and_async_errors_are_stable(self) -> None:
        operations = tuple(re.findall(
            r"^\| `([^`]+)` \|",
            clause_bodies(self.spec)["ASY-008"],
            re.MULTILINE,
        ))
        self.assertEqual(operations, EXPECTED_OPERATIONS)
        self.assertIn("Adding an await edge that reaches the waiter", self.spec)
        self.assertIn("A child MUST NOT outlive its owning scope", self.spec)
        self.assertIn("An unawaited Failed\nchild is unobserved", self.spec)

        catalog = dict(re.findall(
            r"^\| `([A-Z][A-Za-z0-9]+)` \| (yes|no) \|",
            self.error_spec,
            re.MULTILINE,
        ))
        self.assertEqual(
            tuple((code, catalog.get(code)) for code, _ in EXPECTED_ASYNC_ERRORS),
            EXPECTED_ASYNC_ERRORS,
        )

    def test_cancellation_safe_points_deadlines_and_priority_are_exact(self) -> None:
        for anchor in (
            "parent-before-child ownership order", "outermost shield exits",
            "max_instructions_between_cancel_checks = 1024",
            "max_instructions_per_turn = 1024", "now >= deadline",
            "signed 64-bit count of nanoseconds", "round upward",
        ):
            self.assertIn(anchor, self.spec)
        priority = tuple(re.findall(
            r"^\| ([1-7]) \| (.+) \|$",
            clause_bodies(self.spec)["ASY-013"],
            re.MULTILINE,
        ))
        self.assertEqual(priority, EXPECTED_PRIORITY)
        for terminal in (
            "Cancelled", "HumanTakeover", "DeadlineExceeded",
            "InstructionLimitExceeded", "MemoryLimitExceeded",
            "StackLimitExceeded", "CleanupLimitExceeded", "TaskLimitExceeded",
            "InternalInvariant",
        ):
            self.assertIn(f"`{terminal}`", self.error_spec)

    def test_thread_confinement_host_async_roots_and_limits_are_normative(self) -> None:
        for anchor in (
            "turns for one context never overlap",
            "MUST NOT dereference `Value`",
            "operation id, generation, affinity, deadline, cancel hook, and release hook",
            "posted exactly once to the context strand",
            "Pending/Running tasks, suspended continuations",
            "max_tasks_per_context = 4096",
            "max_host_operations_per_context = 1024",
        ):
            self.assertIn(anchor, self.spec)
        self.assertIn("class Environment final", self.environment_header)
        self.assertIn("Heap must outlive all environments", self.environment_header)
        self.assertIn(
            "one heap belongs to one\nexecution-context strand",
            self.value_spec,
        )

    def test_deterministic_hooks_and_foundation_executor_mapping_are_anchored(self) -> None:
        for event in EXPECTED_TRACE_EVENTS:
            self.assertIn(f"`{event}`", self.spec)
        for anchor in (
            "FIFO ready queue", "(due_tick,\nkind_priority, creation_sequence)",
            "same trace and terminal Error envelope",
            "external arrival ticks are explicit trace inputs",
        ):
            self.assertIn(anchor, self.spec)

        phase_match = re.search(r"enum class TaskPhase \{\s*(.*?)\s*\};", self.executor_header, re.DOTALL)
        self.assertIsNotNone(phase_match)
        phases = tuple(re.findall(r"\b(Queued|Running|Finished|Cancelled)\b", phase_match.group(1)))
        self.assertEqual(phases, ("Queued", "Running", "Finished", "Cancelled"))
        for anchor in (
            "std::stop_source stop_source_", "stop_source_.request_stop()",
            "SubmitUntil", "std::chrono::steady_clock::now()", "ShutdownMode::CancelPending",
            "TaskCancelled", "SubmitTimeout", "ExecutorShutdown",
        ):
            self.assertIn(anchor, self.executor_header + self.executor_source)
        for test_name in (
            "test_full_queue_backpressure_and_deadline",
            "test_queued_cancellation_releases_capacity",
            "test_running_cooperative_stop",
            "test_shutdown_cancel_pending",
            "test_submit_shutdown_race",
        ):
            self.assertIn(test_name, self.executor_tests)

    def test_static_fixtures_and_ctest_wiring_are_exact(self) -> None:
        valid_example = re.search(
            r"<!-- conformance:async-tasks-valid -->\s*```baas\n(.*?)\n```",
            self.spec,
            re.DOTALL,
        )
        invalid_example = re.search(
            r"<!-- conformance:async-tasks-invalid -->\s*```baas\n(.*?)\n```",
            self.spec,
            re.DOTALL,
        )
        self.assertIsNotNone(valid_example)
        self.assertIsNotNone(invalid_example)
        self.assertEqual(valid_example.group(1) + "\n", self.valid_fixture)
        self.assertEqual(invalid_example.group(1) + "\n", self.invalid_fixture)
        for anchor in (
            "BAAS_script_check_async_tasks_valid_cli",
            "tests/script/fixtures/async_tasks_valid.baas",
            "BAAS_script_check_async_tasks_invalid_cli",
            "tests/script/fixtures/async_tasks_invalid.baas",
            "WILL_FAIL TRUE",
        ):
            self.assertIn(anchor, self.cmake)

    def test_runtime_boundary_remains_explicitly_pending(self) -> None:
        for path in (
            ROOT / "include" / "script" / "runtime" / "Vm.h",
            ROOT / "include" / "script" / "runtime" / "TaskRuntime.h",
            ROOT / "include" / "script" / "runtime" / "DeterministicScheduler.h",
            ROOT / "include" / "script" / "runtime" / "HostAsyncOperation.h",
        ):
            self.assertFalse(path.exists(), f"update pending boundary for new implementation: {path}")
        self.assertIn("does not yet implement async call execution", self.spec)
        self.assertIn(
            "- [ ] Integrate language-level task/future primitives with the VM and executor.",
            self.roadmap,
        )
        self.assertIn("foundation evidence only", self.spec)

    def test_language_docs_roadmap_and_ci_link_the_normative_spec(self) -> None:
        for document in (self.language_spec, self.grammar, self.error_spec, self.control_spec):
            self.assertIn("`ASYNC_TASKS.md`", document)
        self.assertIn(
            "- [x] Specify futures/tasks, cancellation, deadlines, thread-safety boundaries,\n"
            "  and deterministic testing hooks.",
            self.roadmap,
        )
        self.assertIn(
            "- [x] Specify capability-scoped host APIs for image, OCR, device, configuration,",
            self.roadmap,
        )
        self.assertIn("`ASYNC_TASKS.md` defines the Task state machine", self.roadmap)
        self.assertGreaterEqual(self.workflow.count("'docs/script-runtime/ASYNC_TASKS.md'"), 2)
        self.assertIn(
            'python -B -m unittest discover -s tests/docs -p "test_*.py" -v',
            self.workflow,
        )
        self.assertIn("uses only the Python standard library", self.spec)


if __name__ == "__main__":
    unittest.main()
