import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ProcedureHostFoundationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "include/script/host/ProcedureHost.h").read_text(encoding="utf-8")
        cls.snapshot = (ROOT / "include/script/host/ProcedureSnapshot.h").read_text(encoding="utf-8")
        cls.source = (ROOT / "src/script/host/ProcedureHost.cpp").read_text(encoding="utf-8")
        cls.tests = (ROOT / "tests/script/ProcedureHostTests.cpp").read_text(encoding="utf-8")
        cls.cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/foundation-runtime.yml").read_text(
            encoding="utf-8"
        )
        cls.spec = (ROOT / "docs/script-runtime/PROCEDURE_HOST.md").read_text(
            encoding="utf-8"
        )
        catalog = json.loads(
            (ROOT / "docs/script-runtime/host-capabilities.v1.json").read_text(
                encoding="utf-8"
            )
        )
        cls.module = next(module for module in catalog["modules"] if module["id"] == "baas/procedure")

    def test_runtime_descriptor_matches_machine_catalog(self) -> None:
        binding = self.module["bindings"][0]
        self.assertEqual(binding["id"], "host.procedure.run.v1")
        self.assertEqual(binding["export"], "run")
        self.assertEqual(binding["capability"], "procedure.execute")
        self.assertEqual(binding["budget"], "procedure_steps")
        self.assertEqual(binding["cancellation"], "cooperative")
        self.assertEqual(
            binding["errors"],
            [
                "HOST001_CAPABILITY_DENIED",
                "HOST002_INVALID_ARGUMENT",
                "HOST003_CANCELLED",
                "HOST004_DEADLINE_EXCEEDED",
                "HOST005_BUDGET_EXCEEDED",
                "HOST006_UNAVAILABLE",
                "HOST008_DEVICE_DISCONNECTED",
                "HOST010_RESOURCE_NOT_FOUND",
                "HOST014_INTERNAL",
                "HOST016_BACKPRESSURE",
            ],
        )
        for anchor in (
            'run.binding_id = "host.procedure.run.v1"',
            '"procedure_id", HostValueType::String, true',
            '"options", HostValueType::OrderedStringJsonMap, false',
            'HostValueType::OrderedStringJsonMap, "procedure_steps"',
            "HostCancellationMode::Cooperative",
            '"baas/procedure", {1, 0}',
            '"run", "host.procedure.run.v1", "procedure.execute"',
            "context.deadline_exceeded()",
            "context.cancelled()",
        ):
            self.assertIn(anchor, self.source)
        for anchor in (
            "procedure_host_error_codes",
            "HostErrorCode::CapabilityDenied",
            "HostErrorCode::InvalidArgument",
            "HostErrorCode::Cancelled",
            "HostErrorCode::DeadlineExceeded",
            "HostErrorCode::BudgetExceeded",
            "HostErrorCode::Unavailable",
            "HostErrorCode::DeviceDisconnected",
            "HostErrorCode::ResourceNotFound",
            "HostErrorCode::Internal",
            "HostErrorCode::Backpressure",
        ):
            self.assertIn(anchor, self.header)

    def test_snapshot_has_no_ambient_or_embedded_inputs(self) -> None:
        for anchor in (
            "std::shared_ptr<const resources::ResourceSnapshot>",
            "ProcedureDescriptorInput",
            "terminal_ids",
            "declared_effects",
            "resource_ids",
            "snapshot_id",
            "numeric_snapshot_id",
            "procedure_descriptor_sha256",
        ):
            self.assertIn(anchor, self.snapshot)
        combined = self.snapshot + self.header
        for forbidden in (
            "filesystem::path",
            "BAASConfig",
            "solve_procedure",
            "Python.h",
            "baas-dev",
        ):
            self.assertNotIn(forbidden, combined)

    def test_foreground_discriminator_and_bridge_are_exact(self) -> None:
        evaluator = (ROOT / "src/script/runtime/SynchronousEvaluator.cpp").read_text(
            encoding="utf-8"
        )
        evaluator_tests = (
            ROOT / "tests/script/SynchronousHostEvaluatorTests.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('"foreground_package_mismatch"', self.source)
        self.assertIn('"foreground_package_mismatch"', evaluator)
        self.assertIn('mismatch_string("reason") == "foreground_package_mismatch"', evaluator_tests)
        self.assertIn('module_export(\n                     "main", "retryable").as_boolean()', evaluator_tests)
        self.assertNotIn("actual_package", self.source)
        self.assertNotIn("expected_package", self.source)
        self.assertIn("trace.input_effect_state()", self.source)
        self.assertIn("reported_input", self.source)

    def test_concurrency_cancellation_lifetime_and_stress_evidence(self) -> None:
        for anchor in (
            "same physical device must serialize across distinct Host instances",
            "different physical devices must execute concurrently",
            "same-thread same-device reentry",
            "strand wait must cooperatively cancel",
            "cancellation during executor",
            "deadline must win",
            "binding callback must own snapshot",
            "executor allocation failure",
            "for (int repeat = 0; repeat < 64; ++repeat)",
            "real evaluator + mock Procedure/LogHost",
            "actual ProcedureHost foreground mismatch must survive ERR-016",
            "non-input effects and supplied state must not forge foreground input effect state",
            "begun or indeterminate input must make foreground mismatch effect unknown",
        ):
            self.assertIn(anchor, self.tests)

    def test_build_and_android_closure_are_explicit(self) -> None:
        self.assertIn("option(BUILD_SCRIPT_PROCEDURE_HOST ", self.cmake)
        self.assertIn("option(BUILD_SCRIPT_PROCEDURE_HOST_TESTS ", self.cmake)
        for anchor in (
            "-DBUILD_SCRIPT_PROCEDURE_HOST_TESTS=ON",
            "BAAS_script_procedure_host_tests",
            "-DBUILD_SCRIPT_PROCEDURE_HOST=ON",
            "BAAS_script_procedure_host",
            "docs/script-runtime/PROCEDURE_HOST.md",
        ):
            self.assertIn(anchor, self.workflow)

    def test_pending_boundary_is_honest(self) -> None:
        for anchor in (
            "legacy automation adapter",
            "task-backend composition",
            "converted task packages",
            "Python parity remain",
            "does not link or call",
            "BAAS::solve_procedure",
        ):
            self.assertIn(anchor, self.spec)

    def test_mock_runner_api_is_documented_exactly(self) -> None:
        for anchor in (
            "BAAS_script_procedure_host`",
            "BAAS_script_procedure_host_tests`",
            "resources::ResourceSnapshot::build",
            "ProcedureSnapshot::build",
            "PhysicalDeviceCoordinator::create",
            "make_procedure_host_runtime",
            "ProcedureHostRuntime::metadata",
            "ProcedureHostRuntime::bindings",
            "host.procedure.run.v1",
            "SynchronousEvaluator",
            "error.details.host_details.unavailable_reason",
        ):
            self.assertIn(anchor, self.spec)


if __name__ == "__main__":
    unittest.main()
