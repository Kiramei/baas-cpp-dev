from __future__ import annotations

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "include" / "script" / "runtime" / "HostModuleRegistry.h"
SOURCE = ROOT / "src" / "script" / "runtime" / "HostModuleRegistry.cpp"
CPP_TEST = ROOT / "tests" / "script" / "HostModuleRegistryTests.cpp"
CATALOG = ROOT / "docs" / "script-runtime" / "host-capabilities.v1.json"
HOST_SPEC = ROOT / "docs" / "script-runtime" / "HOST_CAPABILITY_CONTRACTS.md"
PACKAGE_SPEC = ROOT / "docs" / "script-runtime" / "PACKAGE_VERSIONING.md"
ROADMAP = ROOT / "docs" / "script-runtime" / "ROADMAP.md"
CMAKE = ROOT / "cmake" / "ScriptRuntime.cmake"
WORKFLOW = ROOT / ".github" / "workflows" / "foundation-runtime.yml"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class HostRegistryFoundationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = read(HEADER)
        cls.source = read(SOURCE)
        cls.cpp_test = read(CPP_TEST)
        cls.catalog = json.loads(read(CATALOG))
        cls.host_spec = read(HOST_SPEC)
        cls.package_spec = read(PACKAGE_SPEC)
        cls.roadmap = read(ROADMAP)
        cls.cmake = read(CMAKE)
        cls.workflow = read(WORKFLOW)

    def test_all_catalog_modules_and_bindings_fit_descriptor_contract(self) -> None:
        modules = self.catalog["modules"]
        self.assertEqual(len(modules), 13)
        self.assertEqual(self.catalog["api_version"], {"major": 1, "minor": 0})
        module_ids: set[str] = set()
        binding_ids: set[str] = set()
        descriptor_count = 0
        for module in modules:
            with self.subTest(module=module["id"]):
                self.assertRegex(module["id"], r"^baas/[a-z][a-z0-9_]*$")
                self.assertNotIn(module["id"], module_ids)
                module_ids.add(module["id"])
                exports: set[str] = set()
                for binding in module["bindings"]:
                    descriptor_count += 1
                    self.assertRegex(binding["export"], r"^[a-z][a-z0-9_]*$")
                    self.assertNotIn(binding["export"], exports)
                    exports.add(binding["export"])
                    self.assertRegex(
                        binding["id"],
                        r"^host\.[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*\.v1$",
                    )
                    self.assertNotIn(binding["id"], binding_ids)
                    binding_ids.add(binding["id"])
                    self.assertRegex(
                        binding["capability"],
                        r"^[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+$",
                    )
                    self.assertIn(binding["capability"], module["capabilities"])
        self.assertEqual(descriptor_count, 41)

    def test_descriptor_surface_is_metadata_only_and_immutable_after_build(self) -> None:
        descriptor = self.header.split("struct HostModuleDescriptor", 1)[1].split("};", 1)[0]
        host_export = self.header.split("struct HostExportDescriptor", 1)[1].split("};", 1)[0]
        for anchor in (
            "std::string canonical_id",
            "HostApiVersion version",
            "std::vector<HostExportDescriptor> exports",
            "std::string export_name",
            "std::string binding_id",
            "std::string capability",
        ):
            self.assertIn(anchor, descriptor + host_export)
        for forbidden in ("void*", "std::function", "callback", "adapter"):
            self.assertNotIn(forbidden, descriptor.lower() + host_export.lower())
        self.assertIn("operator=(const HostModuleRegistry&) = delete", self.header)
        self.assertIn("HostResolution resolve(const HostResolutionRequest& request) const", self.header)

    def test_same_major_min_minor_selection_is_exact_and_deterministic(self) -> None:
        for anchor in (
            "greatest registered minor",
            "minor >= min_minor",
            "independent of descriptor or request order",
            "exact major",
        ):
            self.assertIn(anchor, self.host_spec)
        for anchor in (
            "selects the greatest such minor",
            "never falls forward or",
            "selected exact pair",
        ):
            self.assertIn(anchor, self.package_spec)
        self.assertIn("module->second.rbegin()", self.source)
        self.assertIn("iterator->first.major == requirement.major", self.source)

    def test_capability_gate_composes_all_manifest_and_narrowing_layers(self) -> None:
        for field in (
            "declared_modules",
            "declared_capabilities",
            "policy_capabilities",
            "platform_capabilities",
            "task_capabilities",
        ):
            self.assertIn(field, self.header)
        self.assertIn("policy, platform, or task narrowing", self.host_spec)
        self.assertIn("manifest declaration, service/user policy, platform availability", self.package_spec)
        self.assertIn('require_layer(policy, "policy")', self.source)
        self.assertIn('require_layer(platform, "platform")', self.source)
        self.assertIn('require_layer(task, "task")', self.source)

    def test_stable_error_inventory_matches_header_source_and_contract(self) -> None:
        enum_body = self.header.split("enum class HostRegistryErrorCode", 1)[1].split("};", 1)[0]
        enum_names = re.findall(r"^\s{4}([A-Z][A-Za-z0-9]+),$", enum_body, re.MULTILINE)
        source_codes = re.findall(
            r'case [A-Za-z0-9]+: return "(HREG\d{3}_[A-Z0-9_]+)";', self.source
        )
        doc_codes = re.findall(r"^\| `(HREG\d{3}_[A-Z0-9_]+)` \|", self.host_spec, re.MULTILINE)
        self.assertEqual(len(enum_names), 25)
        self.assertEqual(len(source_codes), 25)
        self.assertEqual(source_codes, doc_codes)
        self.assertEqual(source_codes[0], "HREG001_INVALID_LIMITS")
        self.assertEqual(source_codes[-1], "HREG025_CAPABILITY_DENIED")

    def test_native_tests_cover_required_foundation_boundaries(self) -> None:
        for function in (
            "test_catalog_shape_and_deterministic_success",
            "test_duplicate_and_minor_contract_validation",
            "test_version_declaration_and_capability_failures",
            "test_limits_and_utf8_fail_closed",
            "test_concurrent_read_only_resolution",
        ):
            self.assertIn(function, self.cpp_test)
        for anchor in (
            "DuplicateModuleVersion",
            "VersionIncompatible",
            "UndeclaredModule",
            "UndeclaredCapability",
            "CapabilityDenied",
            "ValidationWorkLimitExceeded",
            "InvalidUtf8",
            "std::thread",
        ):
            self.assertIn(anchor, self.cpp_test)

    def test_build_ci_and_roadmap_preserve_the_implementation_boundary(self) -> None:
        for path in (
            "src/script/runtime/HostModuleRegistry.cpp",
            "tests/script/HostModuleRegistryTests.cpp",
        ):
            self.assertIn(path, self.cmake)
        self.assertIn("BAAS_script_host_registry_tests", self.cmake)
        self.assertIn("BAAS_script_host_registry_tests", self.workflow)
        self.assertIn("- [~] Implement modules, imports, and native-function registration.", self.roadmap)
        self.assertIn("- [~] Add host capability permissions and thread-safety declarations.", self.roadmap)
        self.assertIn("the remaining real Host adapters remain\n  pending", self.roadmap)
        self.assertIn("do not define or invoke", self.host_spec)
        self.assertIn("remain pending", self.host_spec)


if __name__ == "__main__":
    unittest.main()
