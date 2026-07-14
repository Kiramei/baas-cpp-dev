from __future__ import annotations

import fnmatch
import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = ROOT / "docs" / "script-runtime" / "HOST_CAPABILITY_CONTRACTS.md"
CATALOG_PATH = ROOT / "docs" / "script-runtime" / "host-capabilities.v1.json"
INDEX_PATH = ROOT / "docs" / "script-runtime" / "evidence" / "operation-index.json"
MATRIX_PATH = ROOT / "docs" / "script-runtime" / "MIGRATION_MATRIX.md"
RULES_PATH = ROOT / "scripts" / "migration" / "operation_rules.v4.json"
ROADMAP_PATH = ROOT / "docs" / "script-runtime" / "ROADMAP.md"
PACKAGE_PATH = ROOT / "docs" / "script-runtime" / "PACKAGE_VERSIONING.md"
LANGUAGE_PATH = ROOT / "docs" / "script-runtime" / "LANGUAGE_SPEC_DRAFT.md"
ADR_PATH = ROOT / "docs" / "script-runtime" / "ADR-0001-runtime-architecture.md"
PRIVILEGED_ADR_PATH = ROOT / "docs" / "script-runtime" / "ADR-0003-privileged-operation-boundaries.md"
ASYNC_PATH = ROOT / "docs" / "script-runtime" / "ASYNC_TASKS.md"
ERRORS_PATH = ROOT / "docs" / "script-runtime" / "ERRORS_AND_CLEANUP.md"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "foundation-runtime.yml"

EXPECTED_CLAUSES = tuple(f"HST-{number:03d}" for number in range(1, 17))
CLAUSE_TERMS = {
    "HST-001": ("baas/<name>", "host.<domain>.<operation>.v<major>", "host-capabilities.v1.json", "MUST NOT be reused"),
    "HST-002": ("ordered parameters", "HostResult<T>", "raw pointer", "C++ exceptions"),
    "HST-003": ("HostError", "effect_state", "not_started", "committed", "unknown", "HOST016_BACKPRESSURE"),
    "HST-004": ("effective set", "HOST001_CAPABILITY_DENIED", "filesystem roots", "endpoint allowlists"),
    "HST-005": ("execution-context deadline", "stop token", "HOST003_CANCELLED", "effect_state: unknown"),
    "HST-006": ("reserved transactionally", "incrementally", "HOST005_BUDGET_EXCEEDED", "HOST016_BACKPRESSURE"),
    "HST-007": ("bounded_cpu_pool", "bounded_io_pool", "device_id", "socket handle", "MUST NOT re-enter"),
    "HST-008": ("generational", "HOST015_HANDLE_CLOSED", "MUST NOT cross execution contexts", "ADR-0002"),
    "HST-009": ("baas/vision", "baas/ocr", "baas/device", "HOST008_DEVICE_DISCONNECTED"),
    "HST-010": ("baas/config.snapshot", "expected revision", "baas/log.emit", "baas/notify.prompt", "NotificationAction"),
    "HST-011": ("baas/task", "baas/scheduler", "cancel(Task) -> bool", "cancel(host<ScheduledTask>) -> null", "MUST NOT be overloaded"),
    "HST-012": ("baas/resource", "baas/fs", "symlink/reparse escape", "write_atomic"),
    "HST-013": ("baas/service.publish", "baas/service.request", "MUST NOT grant", "HOST013_PROTOCOL_ERROR"),
    "HST-014": ("baas/process", "program_id", "baas/http.request", "DNS rebinding", "baas/socket"),
    "HST-015": ("HOST_BINDING_REQUIRED", "taxonomy_mappings", "eleven taxonomy-v3 gaps", "INVENTORIED"),
    "HST-016": ("four independent evidence", "Python-versus-C++", "adversarial", "MUST NOT count as C++ binding"),
}

EXPECTED_MODULES = (
    "baas/vision",
    "baas/ocr",
    "baas/device",
    "baas/config",
    "baas/log",
    "baas/notify",
    "baas/scheduler",
    "baas/resource",
    "baas/fs",
    "baas/service",
    "baas/process",
    "baas/http",
    "baas/socket",
)
EXPECTED_GAP_IDS = {
    "op-aebb00093f62aa31",
    "op-4c8a2ce2dc27e423",
    "op-cdc75ef6e48bfd96",
    "op-a5958fbf211fd3cf",
    "op-49bb09af53004a29",
    "op-a99ebc4a865d175a",
    "op-289268bbe3e5c936",
    "op-f0d262f6e29c2c90",
    "op-96a7854dbf227e4e",
    "op-158dcb9d554cf9bf",
    "op-ee53939b067f3ccd",
}
EXPECTED_ERRORS = tuple(f"HOST{number:03d}" for number in range(1, 17))


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def load(path: Path) -> dict:
    return json.loads(read(path))


def clause_bodies(document: str) -> dict[str, str]:
    matches = list(re.finditer(r"^### (HST-\d{3}) — .+$", document, re.MULTILINE))
    result: dict[str, str] = {}
    for index, match in enumerate(matches):
        stop = matches[index + 1].start() if index + 1 < len(matches) else len(document)
        result[match.group(1)] = document[match.end():stop]
    return result


class HostCapabilityContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.spec = read(SPEC_PATH)
        cls.catalog = load(CATALOG_PATH)
        cls.index = load(INDEX_PATH)
        cls.matrix = read(MATRIX_PATH)
        cls.rules = load(RULES_PATH)
        cls.roadmap = read(ROADMAP_PATH)
        cls.package = read(PACKAGE_PATH)
        cls.language = read(LANGUAGE_PATH)
        cls.adr = read(ADR_PATH)
        cls.privileged_adr = read(PRIVILEGED_ADR_PATH)
        cls.async_spec = read(ASYNC_PATH)
        cls.errors_spec = read(ERRORS_PATH)
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

    def test_machine_catalog_has_stable_complete_binding_contracts(self) -> None:
        self.assertEqual(self.catalog["schema_version"], 1)
        self.assertEqual(self.catalog["api_version"], {"major": 1, "minor": 0})
        self.assertEqual(self.catalog["status"], "specified_not_implemented")
        modules = self.catalog["modules"]
        self.assertEqual(tuple(module["id"] for module in modules), EXPECTED_MODULES)

        error_codes = [item["code"] for item in self.catalog["error_codes"]]
        known_errors = set(error_codes)
        self.assertEqual(
            tuple(code.split("_", 1)[0] for code in error_codes),
            EXPECTED_ERRORS,
        )
        binding_ids: set[str] = set()
        capability_ids: set[str] = set()
        for module in modules:
            self.assertRegex(module["id"], r"^baas/[a-z]+$")
            self.assertTrue(module["cpp_interfaces"])
            self.assertTrue(module["default_budget"])
            self.assertIn("mode", module["concurrency"])
            self.assertIn("strand_key", module["concurrency"])
            module_capabilities = set(module["capabilities"])
            self.assertTrue(module_capabilities)
            self.assertFalse(capability_ids & module_capabilities)
            capability_ids.update(module_capabilities)
            for capability in module_capabilities:
                self.assertRegex(capability, r"^[a-z]+(?:\.[a-z]+)+$")
            for binding in module["bindings"]:
                self.assertRegex(binding["id"], r"^host\.[a-z]+\.[a-z_]+\.v1$")
                self.assertNotIn(binding["id"], binding_ids)
                binding_ids.add(binding["id"])
                self.assertIn(binding["capability"], module_capabilities)
                self.assertIn(binding["cancellation"], {"cooperative", "preflight"})
                self.assertTrue(binding["budget"])
                self.assertTrue(binding["returns"])
                self.assertRegex(binding["parity_test"], r"^PARITY-[A-Z-]+$")
                self.assertTrue(binding["errors"])
                self.assertTrue(set(binding["errors"]).issubset(known_errors))
                parameter_names = [item["name"] for item in binding["parameters"]]
                self.assertEqual(len(parameter_names), len(set(parameter_names)))
                for parameter in binding["parameters"]:
                    self.assertIsInstance(parameter["required"], bool)
                    self.assertTrue(parameter["type"])
        self.assertEqual(len(binding_ids), 41)

    def test_catalog_covers_every_requested_domain_and_privileged_capability(self) -> None:
        capabilities = {
            capability
            for module in self.catalog["modules"]
            for capability in module["capabilities"]
        }
        required = {
            "vision.analyze",
            "ocr.infer",
            "ocr.model",
            "device.capture",
            "device.input",
            "device.lifecycle",
            "config.read",
            "config.write",
            "log.emit",
            "notification.show",
            "notification.interact",
            "scheduler.register",
            "scheduler.dispatch",
            "scheduler.schedule",
            "resource.read",
            "filesystem.read",
            "filesystem.write",
            "service.event",
            "service.request",
            "process.inspect",
            "process.spawn",
            "network.http",
            "network.socket",
        }
        self.assertEqual(capabilities, required)
        normalized_spec = self.spec.lower()
        for anchor in (
            "image", "ocr", "device", "configuration", "logging", "scheduler",
            "resource", "filesystem", "service", "psutil", "requests", "socket",
            "subprocess", "notification",
        ):
            self.assertIn(anchor, normalized_spec)

    def test_every_host_status_has_an_exact_existing_language_error_mapping(self) -> None:
        err004 = self.errors_spec.split("### ERR-004", 1)[1].split("### ERR-005", 1)[0]
        language_codes = set(
            re.findall(r"^\| `([A-Z][A-Za-z0-9]+)` \| (?:yes|no) \|", err004, re.MULTILINE)
        )
        self.assertTrue(language_codes)
        mappings = {item["code"]: item["language_mapping"] for item in self.catalog["error_codes"]}
        self.assertEqual(len(mappings), 16)
        for code, mapping in mappings.items():
            with self.subTest(code=code):
                self.assertNotEqual("default" in mapping, "discriminator" in mapping)
                targets = {mapping["default"]} if "default" in mapping else set(mapping["cases"].values())
                self.assertTrue(targets)
                self.assertTrue(targets.issubset(language_codes))
        self.assertEqual(
            mappings["HOST004_DEADLINE_EXCEEDED"],
            {
                "discriminator": "details.deadline_scope",
                "cases": {"call": "Timeout", "context": "DeadlineExceeded"},
            },
        )
        self.assertEqual(
            mappings["HOST005_BUDGET_EXCEEDED"],
            {
                "discriminator": "details.budget_scope",
                "cases": {
                    "external_memory": "MemoryLimitExceeded",
                    "host_operation": "TaskLimitExceeded",
                },
            },
        )
        scopes = {item["budget"]: item["scope"] for item in self.catalog["budget_scopes"]}
        self.assertEqual(len(scopes), len(self.catalog["budget_scopes"]))
        used_budgets = {
            binding["budget"]
            for module in self.catalog["modules"]
            for binding in module["bindings"]
        }
        self.assertEqual(set(scopes), used_budgets)
        self.assertTrue(set(scopes.values()).issubset({"external_memory", "host_operation"}))

    def test_language_tasks_and_host_scheduler_have_disjoint_abis(self) -> None:
        module = next(item for item in self.catalog["modules"] if item["id"] == "baas/scheduler")
        self.assertEqual(module["cpp_interfaces"], ["baas::script::host::SchedulerHost"])
        self.assertNotIn("baas/task", {item["id"] for item in self.catalog["modules"]})
        self.assertTrue(all(binding["id"].startswith("host.scheduler.") for binding in module["bindings"]))
        self.assertTrue(all(binding["capability"].startswith("scheduler.") for binding in module["bindings"]))
        self.assertNotIn("host.task.", json.dumps(self.catalog, sort_keys=True))
        cancel = next(binding for binding in module["bindings"] if binding["export"] == "cancel")
        self.assertEqual(cancel["parameters"][0]["type"], "host<ScheduledTask>")
        self.assertEqual(cancel["returns"], "null")
        for anchor in (
            "baas/task.cancel(Task) -> bool",
            "baas/scheduler",
            "host<ScheduledTask>",
            "MUST NOT overload",
            "SchedulerHost",
        ):
            self.assertIn(anchor, self.async_spec)
        rules = {item["id"]: item for item in self.rules["rules"]}
        self.assertEqual(
            rules["task-registry-v2"]["cpp_host_binding"],
            "baas::script::host::SchedulerHost::register_task",
        )
        self.assertEqual(
            rules["task-action-dispatch-v2"]["cpp_host_binding"],
            "baas::script::host::SchedulerHost::dispatch",
        )

    def test_every_host_decision_has_an_exact_catalog_mapping(self) -> None:
        mappings = {item["rule"]: item for item in self.catalog["taxonomy_mappings"]}
        self.assertEqual(len(mappings), len(self.catalog["taxonomy_mappings"]))
        modules = {module["id"] for module in self.catalog["modules"]}
        module_parity_ids = {
            module["id"]: {binding["parity_test"] for binding in module["bindings"]}
            for module in self.catalog["modules"]
        }
        for mapping in mappings.values():
            for module in mapping["modules"]:
                self.assertIn(mapping["parity_test"], module_parity_ids[module])
        host_decisions = []
        for operation in self.index["operations"]:
            for decision in operation["scope_decisions"]:
                if decision["disposition"] == "HOST_BINDING_REQUIRED":
                    host_decisions.append((operation, decision))
        self.assertEqual(len(host_decisions), 349)
        self.assertEqual(
            {decision["classification_rule"] for _, decision in host_decisions},
            set(mappings),
        )
        for operation, decision in host_decisions:
            with self.subTest(operation=operation["id"]):
                mapping = mappings[decision["classification_rule"]]
                self.assertIn(decision["family"], mapping["families"])
                self.assertEqual(decision["cpp_host_binding"], mapping["cpp_binding"])
                self.assertEqual(decision["parity_test_id"], mapping["parity_test"])
                self.assertTrue(set(mapping["modules"]).issubset(modules))
                self.assertEqual(decision["migration_status"], "INVENTORIED")
                self.assertEqual(decision["host_binding_gap_fields"], [])
        self.assertEqual(self.index["summary"]["host_binding_gaps"], 0)

    def test_privileged_boundary_adr_and_rules_preserve_exact_ownership(self) -> None:
        rules = {item["id"]: item for item in self.rules["rules"]}
        expected_sources = {
            "windows-shortcut-tooling-boundary-v4": ["core/Baas_thread.py"],
            "emulator-registry-device-boundary-v4": ["core/device/emulator_manager/*"],
            "nemu-native-device-boundary-v4": ["core/device/nemu_client.py"],
            "device-descriptor-boundary-v4": ["core/device/nemu_client.py"],
            "notification-host-boundary-v4": ["core/notification.py"],
            "ocr-updater-tooling-boundary-v4": ["core/ocr/baas_ocr_client/server_installer.py"],
            "socket-listener-service-boundary-v4": ["core/ocr/ocr.py"],
        }
        for identifier, sources in expected_sources.items():
            with self.subTest(rule=identifier):
                self.assertEqual(rules[identifier]["source_patterns"], sources)
                self.assertIn(identifier, self.privileged_adr)
        self.assertEqual(
            rules["notification-host-boundary-v4"]["cpp_host_binding"],
            "baas::script::host::NotifyHost",
        )
        self.assertIn("`baas/notify.prompt` preserves", self.privileged_adr)
        self.assertIn("receiver suffix", re.sub(r"\s+", " ", self.privileged_adr))
        self.assertNotIn("core/device/*", rules["nemu-native-device-boundary-v4"]["source_patterns"])
        self.assertIn(
            "docs/script-runtime/ADR-0003-privileged-operation-boundaries.md",
            self.workflow,
        )

    def test_eleven_v3_gaps_resolve_to_exact_capabilities_and_bindings(self) -> None:
        gaps = self.catalog["gap_resolutions"]
        self.assertEqual({item["operation_id"] for item in gaps}, EXPECTED_GAP_IDS)
        self.assertEqual(len(gaps), 11)
        bindings = {
            binding["id"]: binding
            for module in self.catalog["modules"]
            for binding in module["bindings"]
        }
        operations = {operation["id"]: operation for operation in self.index["operations"]}
        mappings = {item["rule"]: item for item in self.catalog["taxonomy_mappings"]}
        for gap in gaps:
            with self.subTest(operation=gap["operation_id"]):
                self.assertIn(gap["legacy_rule"], {"process-host-gap-v2", "network-host-gap-v2"})
                binding = bindings[gap["binding_id"]]
                self.assertEqual(binding["capability"], gap["capability"])
                operation = operations[gap["operation_id"]]
                self.assertEqual(operation["symbol"], gap["symbol"])
                decision = next(
                    item
                    for item in operation["scope_decisions"]
                    if item["source_scope"] == "SCRIPT_RUNTIME"
                    and item["classification_rule"] == gap["taxonomy_rule"]
                )
                self.assertEqual(decision["classification_rule"], gap["taxonomy_rule"])
                mapping = mappings[gap["taxonomy_rule"]]
                self.assertEqual(decision["cpp_host_binding"], mapping["cpp_binding"])
                self.assertEqual(decision["parity_test_id"], mapping["parity_test"])

    def test_v3_rules_and_evidence_digest_encode_the_contract_assignments(self) -> None:
        rules = {item["id"]: item for item in self.rules["rules"]}
        expected = {
            "process-host-v3": (
                "baas::script::host::ProcessHost",
                "Runtime Privileged I/O",
                "PARITY-PROCESS-HOST",
                ["os.popen", "os.system", "psutil.*", "subprocess.*"],
            ),
            "http-host-v3": (
                "baas::script::host::HttpHost",
                "Runtime Privileged I/O",
                "PARITY-HTTP-HOST",
                ["requests.delete", "requests.get", "requests.head", "requests.options", "requests.patch", "requests.post", "requests.put", "requests.request"],
            ),
            "socket-host-v3": (
                "baas::script::host::SocketHost",
                "Runtime Privileged I/O",
                "PARITY-SOCKET-HOST",
                ["socket.*"],
            ),
        }
        for identifier, values in expected.items():
            rule = rules[identifier]
            self.assertEqual(
                (rule["cpp_host_binding"], rule["owner"], rule["parity_test_id"], rule["symbol_patterns"]),
                values,
            )
            self.assertEqual(rule["migration_status"], "INVENTORIED")
        canonical = json.dumps(
            self.rules,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        self.assertEqual(hashlib.sha256(canonical).hexdigest(), self.index["rules"]["sha256"])

    def test_generated_matrix_links_every_host_decision_and_contract(self) -> None:
        self.assertIn("HOST_CAPABILITY_CONTRACTS.md", self.matrix)
        self.assertIn("host-capabilities.v1.json", self.matrix)
        self.assertIn("0 host-binding gaps", self.matrix)
        matrix_rows = {
            line.split("|", 2)[1].strip(): line
            for line in self.matrix.splitlines()
            if line.startswith("| op-")
        }
        for operation in self.index["operations"]:
            for decision in operation["scope_decisions"]:
                if decision["disposition"] != "HOST_BINDING_REQUIRED":
                    continue
                row = matrix_rows[decision["id"]]
                self.assertIn(f"`{decision['cpp_host_binding']}`", row)
                self.assertIn(decision["parity_test_id"], row)
                self.assertIn("INVENTORIED", row)

    def test_package_language_and_architecture_contracts_are_consistent(self) -> None:
        for anchor in (
            "effective capability set is the intersection",
            "Raw filesystem, process, network",
            "CapabilityDenied",
        ):
            self.assertIn(anchor, self.package)
        for module in EXPECTED_MODULES:
            self.assertIn(f"`{module}`", self.language)
        self.assertIn("per-device strand/actor", self.adr)
        self.assertIn("Cancellation is cooperative", self.adr)
        self.assertIn("Fake hosts use the same interfaces", self.adr)
        self.assertIn("ERR-016", self.spec)
        self.assertIn("ASY-015", self.spec)

    def test_roadmap_marks_only_the_completed_specification_item(self) -> None:
        self.assertIn(
            "- [x] Specify capability-scoped host APIs for image, OCR, device, configuration,\n"
            "  logging, scheduler, resources, filesystem, and service operations.",
            self.roadmap,
        )
        self.assertIn("`HOST_CAPABILITY_CONTRACTS.md` fixes stable module", self.roadmap)
        self.assertIn("- [ ] Bind logging and structured events.", self.roadmap)
        self.assertIn("- [~] Add host capability permissions and thread-safety declarations.", self.roadmap)
        self.assertIn("- [ ] Add Python-versus-C++ golden parity tests", self.roadmap)
        self.assertIn("specified, not implemented", self.spec)

    def test_ci_tracks_and_runs_the_standard_library_docs_checker(self) -> None:
        for path in (
            "docs/script-runtime/HOST_CAPABILITY_CONTRACTS.md",
            "docs/script-runtime/host-capabilities.v1.json",
            "docs/script-runtime/evidence/operation-index.json",
            "docs/script-runtime/MIGRATION_MATRIX.md",
        ):
            self.assertGreaterEqual(self.workflow.count(f"'{path}'"), 2)
        self.assertIn("'tests/docs/**'", self.workflow)
        self.assertIn("BAAS_script_module_specifier_tests", self.workflow)
        self.assertIn(
            'python -B -m unittest discover -s tests/docs -p "test_*.py" -v',
            self.workflow,
        )

    def test_generated_migration_evidence_has_checkout_stable_line_endings(self) -> None:
        attributes = (ROOT / ".gitattributes").read_text(encoding="utf-8")
        for path in (
            "docs/script-runtime/evidence/*.json text eol=lf",
            "docs/script-runtime/MIGRATION_MATRIX.md text eol=lf",
            "scripts/migration/*.json text eol=lf",
        ):
            self.assertIn(path, attributes)

    def test_catalog_patterns_do_not_silently_claim_unmapped_operations(self) -> None:
        rule_by_id = {item["id"]: item for item in self.rules["rules"]}
        for gap in self.catalog["gap_resolutions"]:
            rule = rule_by_id[gap["taxonomy_rule"]]
            self.assertTrue(
                any(fnmatch.fnmatchcase(gap["symbol"], pattern) for pattern in rule["symbol_patterns"])
            )
        for path in (
            ROOT / "include" / "script" / "host" / "ProcessHost.h",
            ROOT / "include" / "script" / "host" / "HttpHost.h",
            ROOT / "include" / "script" / "host" / "SocketHost.h",
            ROOT / "include" / "script" / "host" / "ServiceHost.h",
        ):
            self.assertFalse(path.exists(), f"update implementation boundary when added: {path}")


if __name__ == "__main__":
    unittest.main()
