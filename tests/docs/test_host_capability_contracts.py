from __future__ import annotations

import fnmatch
import hashlib
import json
import re
import unittest
from pathlib import Path

from scripts.migration.operation_index import (
    BEGIN_MARKER,
    END_MARKER,
    render_generated_matrix,
    stable_json,
)


ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = ROOT / "docs" / "script-runtime" / "HOST_CAPABILITY_CONTRACTS.md"
CATALOG_PATH = ROOT / "docs" / "script-runtime" / "host-capabilities.v1.json"
INDEX_PATH = ROOT / "docs" / "script-runtime" / "evidence" / "operation-index.json"
MATRIX_PATH = ROOT / "docs" / "script-runtime" / "MIGRATION_MATRIX.md"
RULES_PATH = ROOT / "scripts" / "migration" / "operation_rules.v5.json"
ROADMAP_PATH = ROOT / "docs" / "script-runtime" / "ROADMAP.md"
PACKAGE_PATH = ROOT / "docs" / "script-runtime" / "PACKAGE_VERSIONING.md"
LANGUAGE_PATH = ROOT / "docs" / "script-runtime" / "LANGUAGE_SPEC_DRAFT.md"
ADR_PATH = ROOT / "docs" / "script-runtime" / "ADR-0001-runtime-architecture.md"
MEMORY_ADR_PATH = ROOT / "docs" / "script-runtime" / "ADR-0002-vm-memory-management.md"
PRIVILEGED_ADR_PATH = ROOT / "docs" / "script-runtime" / "ADR-0003-privileged-operation-boundaries.md"
CORE_BOUNDARY_ADR_PATH = ROOT / "docs" / "script-runtime" / "ADR-0004-core-runtime-boundary.md"
ASYNC_PATH = ROOT / "docs" / "script-runtime" / "ASYNC_TASKS.md"
ERRORS_PATH = ROOT / "docs" / "script-runtime" / "ERRORS_AND_CLEANUP.md"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "foundation-runtime.yml"
CONFIG_HOST_PATH = ROOT / "docs" / "script-runtime" / "CONFIG_HOST.md"
SYNCHRONOUS_HOST_SOURCE_PATH = (
    ROOT / "src" / "script" / "runtime" / "SynchronousHost.cpp"
)

EXPECTED_CLAUSES = tuple(f"HST-{number:03d}" for number in range(1, 17))
CLAUSE_TERMS = {
    "HST-001": ("baas/<name>", "host.<domain>.<operation>.v<major>", "host-capabilities.v1.json", "MUST NOT be reused"),
    "HST-002": ("ordered parameters", "HostResult<T>", "raw pointer", "C++ exceptions"),
    "HST-003": ("HostError", "effect_state", "not_started", "committed", "unknown", "HOST016_BACKPRESSURE"),
    "HST-004": ("effective set", "HOST001_CAPABILITY_DENIED", "filesystem roots", "endpoint allowlists"),
    "HST-005": ("execution-context deadline", "stop token", "HOST003_CANCELLED", "effect_state: unknown"),
    "HST-006": ("reserved transactionally", "incrementally", "HOST005_BUDGET_EXCEEDED", "HOST016_BACKPRESSURE"),
    "HST-007": ("bounded_cpu_pool", "bounded_io_pool", "device_id", "socket handle", "MUST NOT re-enter"),
    "HST-008": (
        "generational",
        "HOST015_HANDLE_CLOSED",
        "MUST NOT cross execution contexts",
        "ADR-0002",
        "destruction_safe()",
        "fail-fast",
    ),
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
    "baas/clock",
    "baas/config",
    "baas/log",
    "baas/notify",
    "baas/scheduler",
    "baas/procedure",
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
ERROR_VARIANT_FIELDS = {
    "code",
    "details",
    "effect_state",
    "language_mapping",
    "python_parity",
    "retryable",
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def load(path: Path) -> dict:
    return json.loads(read(path))


def valid_error_variant_shape(variant: object) -> bool:
    return (
        isinstance(variant, dict)
        and set(variant) == ERROR_VARIANT_FIELDS
        and isinstance(variant.get("retryable"), bool)
    )


def valid_foreground_mismatch_variant(variant: object) -> bool:
    return (
        valid_error_variant_shape(variant)
        and variant["code"] == "HOST006_UNAVAILABLE"
        and variant["details"]
        == {"unavailable_reason": "foreground_package_mismatch"}
        and variant["retryable"] is True
    )


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
        cls.memory_adr = read(MEMORY_ADR_PATH)
        cls.privileged_adr = read(PRIVILEGED_ADR_PATH)
        cls.core_boundary_adr = read(CORE_BOUNDARY_ADR_PATH)
        cls.async_spec = read(ASYNC_PATH)
        cls.errors_spec = read(ERRORS_PATH)
        cls.workflow = read(WORKFLOW_PATH)
        cls.config_host = read(CONFIG_HOST_PATH)
        cls.synchronous_host_source = read(SYNCHRONOUS_HOST_SOURCE_PATH)

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

    def test_host_handle_release_owner_lifecycle_is_fail_closed(self) -> None:
        for document in (self.spec, self.memory_adr, self.errors_spec):
            normalized = re.sub(r"\s+", " ", document)
            self.assertIn("destruction_safe()", normalized)
            self.assertIn("owner strand", normalized)
            self.assertRegex(normalized, r"fail(?:s)?[- ]fast")
            self.assertIn("native ownership", normalized)

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
        common_binding_fields = {
            "budget", "cancellation", "capability", "errors", "export", "id",
            "parameters", "parity_test", "returns",
        }
        extension_binding_fields = {
            "argument_contract", "composite_effects", "control_contract", "error_variants",
            "result_schema",
        }
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
                self.assertFalse(
                    set(binding) - common_binding_fields - extension_binding_fields,
                    f"unknown binding catalog fields in {binding['id']}",
                )
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
                self.assertTrue(
                    {
                        "HOST001_CAPABILITY_DENIED",
                        "HOST003_CANCELLED",
                        "HOST004_DEADLINE_EXCEEDED",
                    }.issubset(binding["errors"]),
                    f"framework/gate errors missing from {binding['id']}",
                )
                parameter_names = [item["name"] for item in binding["parameters"]]
                self.assertEqual(len(parameter_names), len(set(parameter_names)))
                for parameter in binding["parameters"]:
                    self.assertFalse(
                        set(parameter) - {"default", "name", "required", "type"},
                        f"unknown parameter catalog fields in {binding['id']}",
                    )
                    self.assertIsInstance(parameter["required"], bool)
                    self.assertTrue(parameter["type"])
                    if "default" in parameter:
                        self.assertFalse(parameter["required"])
                        self.assertEqual(parameter["type"], "ordered-map")
                        self.assertEqual(parameter["default"], {})
                if "result_schema" in binding:
                    schema = binding["result_schema"]
                    self.assertEqual(set(schema), {"field_order", "fields", "unknown_fields"})
                    self.assertEqual(schema["unknown_fields"], "forbidden")
                    self.assertEqual(set(schema["field_order"]), set(schema["fields"]))
                    self.assertEqual(len(schema["field_order"]), len(schema["fields"]))
                    for field in schema["fields"].values():
                        self.assertFalse(
                            set(field) - {"required", "semantics", "type", "unit"},
                            f"unknown result field catalog keys in {binding['id']}",
                        )
                        self.assertIs(field["required"], True)
                        self.assertTrue(field["semantics"])
                        self.assertTrue(field["type"])
                if "composite_effects" in binding or "control_contract" in binding:
                    self.assertEqual(binding["id"], "host.procedure.run.v1")
                if "argument_contract" in binding:
                    self.assertEqual(binding["id"], "host.device.lifecycle.v1")
                if "error_variants" in binding:
                    self.assertEqual(binding["id"], "host.procedure.run.v1")
                    self.assertTrue(binding["error_variants"])
                    for variant in binding["error_variants"]:
                        self.assertTrue(valid_error_variant_shape(variant))
                        self.assertIn(variant["code"], binding["errors"])
                        self.assertEqual(
                            set(variant["effect_state"]),
                            {"not_started", "committed", "unknown"},
                        )
                        self.assertEqual(
                            set(variant["python_parity"]),
                            {"exception", "normalization"},
                        )
        self.assertEqual(len(binding_ids), 44)
        callback = self.synchronous_host_source.split(
            "HostResult invoke_host_callback(", 1
        )[1].split("std::vector<InMemoryLogEvent>", 1)[0]
        self.assertLess(
            callback.index("context.deadline_exceeded()"),
            callback.index("context.cancelled()"),
        )
        normalized = re.sub(r"\s+", " ", self.spec)
        for anchor in (
            "before every callback",
            "independent of whether the binding's mode is `preflight` or `cooperative`",
            "every binding's complete catalog `errors` list MUST include both",
            "every complete catalog `errors` list MUST include `HOST001_CAPABILITY_DENIED`",
            "HOST003_CANCELLED",
            "HOST004_DEADLINE_EXCEEDED",
        ):
            self.assertIn(anchor, normalized)

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
            "clock.read",
            "config.read",
            "config.write",
            "log.emit",
            "notification.show",
            "notification.interact",
            "scheduler.register",
            "scheduler.dispatch",
            "scheduler.schedule",
            "procedure.execute",
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

    def test_procedure_clock_and_refresh_contracts_are_explicit_gaps(self) -> None:
        modules = {module["id"]: module for module in self.catalog["modules"]}
        procedure = modules["baas/procedure"]
        self.assertEqual(procedure["cpp_interfaces"], ["baas::script::host::ProcedureHost"])
        self.assertEqual(procedure["capabilities"], ["procedure.execute"])
        self.assertEqual(
            procedure["concurrency"],
            {"mode": "device_strand", "strand_key": "device_id"},
        )
        binding = procedure["bindings"][0]
        self.assertEqual(binding["id"], "host.procedure.run.v1")
        self.assertEqual(binding["export"], "run")
        self.assertEqual(
            binding["parameters"],
            [
                {"name": "procedure_id", "required": True, "type": "string"},
                {"default": {}, "name": "options", "required": False, "type": "ordered-map"},
            ],
        )
        self.assertEqual(binding["returns"], "ordered-map<ProcedureResult>")
        self.assertEqual(binding["cancellation"], "cooperative")
        self.assertEqual(binding["budget"], "procedure_steps")
        self.assertEqual(
            binding["composite_effects"],
            ["capture", "vision", "input", "wait", "foreground_check"],
        )
        self.assertEqual(
            binding["control_contract"],
            ["cooperative_cancellation", "context_or_call_timeout"],
        )
        self.assertEqual(
            binding["result_schema"],
            {
                "field_order": ["end"],
                "fields": {
                    "end": {
                        "required": True,
                        "semantics": (
                            "logical terminal match identifier selected by the registered "
                            "procedure's deterministic ordered-match rules"
                        ),
                        "type": "string",
                    }
                },
                "unknown_fields": "forbidden",
            },
        )
        self.assertEqual(
            binding["error_variants"],
            [
                {
                    "code": "HOST006_UNAVAILABLE",
                    "details": {
                        "unavailable_reason": "foreground_package_mismatch",
                    },
                    "effect_state": {
                        "committed": (
                            "one or more input effects are confirmed committed before the mismatch"
                        ),
                        "not_started": "no input effect was committed before the mismatch",
                        "unknown": (
                            "an input effect may have committed but completion cannot be proven"
                        ),
                    },
                    "language_mapping": "HostUnavailable",
                    "python_parity": {
                        "exception": "PackageIncorrect",
                        "normalization": (
                            "foreground package differs from the execution context's expected package"
                        ),
                    },
                    "retryable": True,
                }
            ],
        )
        foreground_variant = binding["error_variants"][0]
        self.assertTrue(valid_foreground_mismatch_variant(foreground_variant))
        missing_retryable = dict(foreground_variant)
        missing_retryable.pop("retryable")
        false_retryable = dict(foreground_variant)
        false_retryable["retryable"] = False
        self.assertFalse(valid_foreground_mismatch_variant(missing_retryable))
        self.assertFalse(valid_foreground_mismatch_variant(false_retryable))
        unavailable = next(
            item for item in self.catalog["error_codes"]
            if item["code"] == "HOST006_UNAVAILABLE"
        )
        self.assertEqual(unavailable["language_mapping"], {"default": "HostUnavailable"})
        self.assertTrue(
            {
                "HOST003_CANCELLED",
                "HOST004_DEADLINE_EXCEEDED",
                "HOST005_BUDGET_EXCEEDED",
                "HOST008_DEVICE_DISCONNECTED",
                "HOST010_RESOURCE_NOT_FOUND",
            }.issubset(binding["errors"])
        )

        clock = modules["baas/clock"]
        self.assertEqual(clock["cpp_interfaces"], ["baas::script::host::ClockHost"])
        self.assertEqual(clock["capabilities"], ["clock.read"])
        self.assertEqual(clock["bindings"][0]["id"], "host.clock.now.v1")
        self.assertEqual(clock["bindings"][0]["returns"], "ordered-map<ClockReading>")
        self.assertEqual(
            clock["bindings"][0]["result_schema"],
            {
                "field_order": ["unix_time_ns", "monotonic_time_ns"],
                "fields": {
                    "monotonic_time_ns": {
                        "required": True,
                        "semantics": (
                            "nondecreasing execution-context elapsed-time source with an "
                            "unspecified epoch"
                        ),
                        "type": "int",
                        "unit": "nanoseconds",
                    },
                    "unix_time_ns": {
                        "required": True,
                        "semantics": (
                            "signed nanoseconds since 1970-01-01T00:00:00Z excluding leap seconds"
                        ),
                        "type": "int",
                        "unit": "nanoseconds",
                    },
                },
                "unknown_fields": "forbidden",
            },
        )

        lifecycle = next(
            item for item in modules["baas/device"]["bindings"]
            if item["id"] == "host.device.lifecycle.v1"
        )
        self.assertEqual(lifecycle["capability"], "device.lifecycle")
        self.assertEqual(lifecycle["cancellation"], "cooperative")
        self.assertTrue(
            {
                "HOST002_INVALID_ARGUMENT",
                "HOST003_CANCELLED",
                "HOST004_DEADLINE_EXCEEDED",
                "HOST005_BUDGET_EXCEEDED",
                "HOST006_UNAVAILABLE",
                "HOST008_DEVICE_DISCONNECTED",
            }.issubset(lifecycle["errors"])
        )
        self.assertEqual(lifecycle["parameters"][-1], {
            "default": {}, "name": "options", "required": False, "type": "ordered-map",
        })
        self.assertEqual(
            lifecycle["argument_contract"],
            {
                "action_values": ["stop", "start", "wait_ready"],
                "target_kinds": {
                    "application": ["stop", "start"],
                    "automation_service": ["stop", "start", "wait_ready"],
                },
            },
        )
        self.assertEqual(
            lifecycle["result_schema"],
            {
                "field_order": ["target_kind", "target_id", "state"],
                "fields": {
                    "state": {
                        "required": True,
                        "semantics": "observed postcondition: stopped, started, or ready",
                        "type": "string",
                    },
                    "target_id": {
                        "required": True,
                        "semantics": "logical policy-approved target identifier",
                        "type": "string",
                    },
                    "target_kind": {
                        "required": True,
                        "semantics": "application or automation_service",
                        "type": "string",
                    },
                },
                "unknown_fields": "forbidden",
            },
        )

        rules = {rule["id"]: rule for rule in self.rules["rules"]}
        self.assertEqual(
            rules["procedure-host-v5"]["symbol_patterns"],
            ["core.picture.co_detect"],
        )
        self.assertNotEqual(
            rules["procedure-host-v5"]["cpp_host_binding"],
            rules["vision-host-v2"]["cpp_host_binding"],
        )
        self.assertEqual(
            rules["device-lifecycle-host-v5"]["symbol_patterns"],
            [
                "self.u2.app_stop",
                "self.u2.uiautomator.start",
                "self.wait_uiautomator_start",
            ],
        )
        self.assertEqual(rules["clock-host-v5"]["symbol_patterns"], ["time.time"])
        normalized_spec = re.sub(r"\s+", " ", self.spec)
        for anchor in (
            "baas/procedure.run",
            "host.procedure.run.v1",
            "procedure.execute",
            "logical ID",
            "native source path",
            "capture, vision, input, wait, and foreground-package checks",
            "snapshot-owned `ProcedureHost` foundation now implements this ABI",
            "legacy global procedure adapter",
            "no ClockHost adapter",
            "host.device.lifecycle.v1",
            "DeviceHost::lifecycle",
            "deterministic empty ordered-map",
            "unavailable_reason = foreground_package_mismatch",
            "Python `PackageIncorrect`",
            "no committed input becomes `not_started`",
            "confirmed input becomes `committed`",
            "indeterminate completion becomes `unknown`",
            "`retryable` MUST be `true`",
            "bridge MUST preserve the allowlisted `details.unavailable_reason`",
        ):
            self.assertIn(anchor, normalized_spec)

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
        self.assertEqual(len(host_decisions), 149)
        decision_rules = {decision["classification_rule"] for _, decision in host_decisions}
        self.assertTrue(decision_rules.issubset(mappings))
        self.assertEqual(
            set(mappings) - decision_rules,
            {"process-host-v3", "http-host-v3", "socket-host-v3"},
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

    def test_core_and_module_sources_have_disjoint_v5_ownership(self) -> None:
        source_rules = {item["id"]: item for item in self.rules["source_scope_rules"]}
        self.assertEqual(
            source_rules["cpp-runtime-sources-v5"],
            {"id": "cpp-runtime-sources-v5", "source_patterns": ["core/*"], "source_scope": "CPP_RUNTIME"},
        )
        self.assertEqual(
            source_rules["script-runtime-sources-v5"]["source_patterns"],
            ["module/*", "main.py", "cli.example.py"],
        )
        summary = self.index["summary"]
        self.assertEqual(summary["unresolved_disposition_scope_decisions"], 0)
        self.assertEqual(summary["host_binding_gaps"], 0)
        self.assertEqual(summary["parse_errors"], 0)
        self.assertEqual(summary["disposition_scope_decisions"]["CPP_RUNTIME_INTERNAL"], 761)
        self.assertIn("`core/*`", self.core_boundary_adr)
        self.assertIn("`module/*`", self.core_boundary_adr)
        self.assertIn("CPP_RUNTIME_INTERNAL", self.core_boundary_adr)
        self.assertIn("does not claim", self.core_boundary_adr)
        self.assertGreaterEqual(
            self.workflow.count("'docs/script-runtime/ADR-0004-core-runtime-boundary.md'"),
            2,
        )

    def test_eleven_v3_gaps_are_native_and_retain_reserved_host_contracts(self) -> None:
        gaps = self.catalog["gap_resolutions"]
        self.assertEqual({item["operation_id"] for item in gaps}, EXPECTED_GAP_IDS)
        self.assertEqual(len(gaps), 11)
        bindings = {
            binding["id"]: binding
            for module in self.catalog["modules"]
            for binding in module["bindings"]
        }
        operations = {operation["id"]: operation for operation in self.index["operations"]}
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
                    if item["source_scope"] == gap["taxonomy_v5_source_scope"]
                    and item["classification_rule"] == gap["taxonomy_v5_rule"]
                )
                self.assertEqual(decision["disposition"], gap["taxonomy_v5_disposition"])
                self.assertEqual(decision["cpp_host_binding"], "NOT_APPLICABLE")
                self.assertEqual(decision["parity_test_id"], "NOT_APPLICABLE")

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

    def test_generated_evidence_source_counts_and_matrix_have_no_drift(self) -> None:
        self.assertEqual(
            self.index["repository"],
            {
                "revision": "75bbacb545bc87e9510d85cbe8034f9180397004",
                "source_snapshot_sha256": "76f974d77e7c63034296acefb86a707e150c68602db007f4dbec891c66f712ec",
            },
        )
        summary = self.index["summary"]
        self.assertEqual(summary["python_files"], 569)
        self.assertEqual(summary["operation_count"], 4340)
        self.assertEqual(summary["call_sites"], 15469)
        self.assertEqual(summary["scope_decision_count"], 5060)
        self.assertEqual(stable_json(self.index), read(INDEX_PATH))

        generated = render_generated_matrix(self.index, "evidence/operation-index.json")
        begin = self.matrix.index(BEGIN_MARKER)
        end = self.matrix.index(END_MARKER, begin) + len(END_MARKER)
        self.assertEqual(self.matrix[begin:end], generated.rstrip())

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
        self.assertIn("- [~] Bind logging and structured events.", self.roadmap)
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
            rule = rule_by_id[gap["legacy_host_taxonomy_rule"]]
            self.assertTrue(
                any(fnmatch.fnmatchcase(gap["symbol"], pattern) for pattern in rule["symbol_patterns"])
            )
        for path in (
            ROOT / "include" / "script" / "host" / "ProcessHost.h",
            ROOT / "include" / "script" / "host" / "HttpHost.h",
            ROOT / "include" / "script" / "host" / "SocketHost.h",
            ROOT / "include" / "script" / "host" / "ServiceHost.h",
            ROOT / "include" / "script" / "host" / "ClockHost.h",
        ):
            self.assertFalse(path.exists(), f"update implementation boundary when added: {path}")
        for path in (
            ROOT / "include" / "script" / "host" / "ProcedureHost.h",
            ROOT / "include" / "script" / "host" / "ProcedureSnapshot.h",
            ROOT / "src" / "script" / "host" / "ProcedureHost.cpp",
            ROOT / "src" / "script" / "host" / "ProcedureSnapshot.cpp",
            ROOT / "tests" / "script" / "ProcedureHostTests.cpp",
            ROOT / "cmake" / "ScriptProcedureHost.cmake",
        ):
            self.assertTrue(path.exists(), f"ProcedureHost foundation evidence is missing: {path}")

    def test_config_host_request_local_abi_and_foundation_evidence(self) -> None:
        config = next(
            module for module in self.catalog["modules"]
            if module["id"] == "baas/config"
        )
        bindings = {binding["export"]: binding for binding in config["bindings"]}
        self.assertEqual(bindings["snapshot"]["parameters"], [])
        self.assertEqual(
            bindings["get"]["parameters"],
            [
                {"name": "path", "required": True, "type": "string"},
                {"name": "default", "required": False, "type": "json"},
            ],
        )
        self.assertEqual(
            bindings["transact"]["parameters"],
            [
                {"name": "expected_revision", "required": True, "type": "int"},
                {"name": "patch", "required": True, "type": "ordered-map<string,json>"},
            ],
        )
        for name in ("snapshot", "transact"):
            self.assertEqual(
                bindings[name]["result_schema"]["field_order"],
                ["revision", "snapshot_id"],
            )
        for term in (
            "Request-local",
            "immutable",
            "ConfigHostPort",
            "expected-revision",
            "ProductionRuntimeScriptExtensions::make_host_contributions",
            "No repository path",
        ):
            self.assertIn(term, self.config_host)
        for path in (
            ROOT / "include" / "script" / "host" / "ConfigHost.h",
            ROOT / "src" / "script" / "host" / "ConfigHost.cpp",
            ROOT / "tests" / "script" / "ConfigHostTests.cpp",
            ROOT / "cmake" / "ScriptConfigHost.cmake",
        ):
            self.assertTrue(path.exists(), f"ConfigHost foundation evidence is missing: {path}")
        self.assertIn("BUILD_SCRIPT_CONFIG_HOST_TESTS=ON", self.workflow)
        self.assertIn("BAAS_script_config_host_tests", self.workflow)
        self.assertIn("BUILD_SCRIPT_CONFIG_HOST=ON", self.workflow)
        self.assertIn("BAAS_script_config_host", self.workflow)


if __name__ == "__main__":
    unittest.main()
