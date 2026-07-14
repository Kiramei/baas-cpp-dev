from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = ROOT / "scripts" / "platform" / "check_smoke_prerequisites.py"
EVIDENCE_PATH = ROOT / "docs" / "script-runtime" / "evidence" / "platform_smoke_prerequisites.windows.json"
ROADMAP_PATH = ROOT / "docs" / "script-runtime" / "ROADMAP.md"
DOCUMENT_PATH = ROOT / "docs" / "script-runtime" / "PLATFORM_SMOKE_PREREQUISITES.md"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "foundation-runtime.yml"

SPEC = importlib.util.spec_from_file_location("check_smoke_prerequisites", SCRIPT_PATH)
assert SPEC and SPEC.loader
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


def _walk(value: object):
    if isinstance(value, dict):
        for key, child in value.items():
            yield key
            yield from _walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from _walk(child)
    elif isinstance(value, str):
        yield value


class SmokePrerequisiteTests(unittest.TestCase):
    def fake_collect(self):
        return CHECKER.collect(
            ROOT,
            selected_profiles=CHECKER.PROFILE_IDS,
            environ={"HOME": "/nonexistent-baas-home", "PATH": ""},
            host_system="Windows",
            host_machine="AMD64",
            which=lambda _name: None,
        )

    def test_profile_ids_and_status_vocabulary_are_stable(self) -> None:
        evidence = self.fake_collect()
        self.assertEqual(
            [profile["id"] for profile in evidence["profiles"]],
            list(CHECKER.PROFILE_IDS),
        )
        self.assertTrue(
            {check["status"] for check in evidence["checks"]}.issubset(
                set(CHECKER.STATUSES)
            )
        )

    def test_collection_is_deterministic_and_strict_detects_missing(self) -> None:
        first = self.fake_collect()
        second = self.fake_collect()
        self.assertEqual(first, second)
        self.assertTrue(CHECKER.strict_failure(first))
        self.assertEqual(first["host"], {"os": "windows", "arch": "x86_64"})

    def test_checker_records_no_absolute_paths_or_timestamps(self) -> None:
        evidence = self.fake_collect()
        rendered = json.dumps(evidence, sort_keys=True)
        self.assertNotIn("Kiramei", rendered)
        self.assertNotIn("D:\\\\", rendered)
        self.assertNotIn("/nonexistent-baas-home", rendered)
        self.assertNotIn("generated_at", rendered)
        self.assertNotRegex(rendered, r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}")
        self.assertFalse(evidence["safety"]["records_absolute_paths"])
        self.assertFalse(evidence["safety"]["records_timestamp"])

    def test_main_output_and_strict_exit_are_reproducible(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "evidence.json"
            kwargs = {
                "environ": {"HOME": "/nonexistent-baas-home", "PATH": ""},
                "host_system": "Windows",
                "host_machine": "AMD64",
                "which": lambda _name: None,
            }
            self.assertEqual(CHECKER.main(["--output", str(output)], **kwargs), 0)
            first = output.read_bytes()
            self.assertEqual(
                CHECKER.main(["--output", str(output), "--strict"], **kwargs),
                1,
            )
            self.assertEqual(first, output.read_bytes())

    def test_not_run_host_profile_does_not_mean_missing(self) -> None:
        evidence = CHECKER.collect(
            ROOT,
            selected_profiles=("linux-foundation",),
            environ={"HOME": "/nonexistent-baas-home", "PATH": ""},
            host_system="Windows",
            host_machine="AMD64",
            which=lambda _name: None,
        )
        self.assertFalse(CHECKER.strict_failure(evidence))
        self.assertEqual(evidence["profiles"], [{"id": "linux-foundation", "status": "not_run"}])

    def test_committed_windows_evidence_is_sanitized_and_incomplete(self) -> None:
        evidence = json.loads(EVIDENCE_PATH.read_text(encoding="utf-8"))
        self.assertEqual(evidence["schema_version"], 1)
        self.assertEqual(evidence["host"], {"arch": "x86_64", "os": "windows"})
        self.assertGreater(evidence["summary"]["status_counts"]["missing"], 0)
        self.assertGreater(evidence["summary"]["status_counts"]["not_run"], 0)
        rendered = EVIDENCE_PATH.read_text(encoding="utf-8")
        self.assertNotRegex(rendered, r"[A-Za-z]:[\\/]")
        self.assertNotIn("Kiramei", rendered)
        self.assertEqual(
            evidence["summary"]["status_counts"],
            {"available": 43, "discovered": 17, "missing": 6, "not_run": 16},
        )

    def test_only_phase_zero_prerequisite_item_is_completed(self) -> None:
        roadmap = ROADMAP_PATH.read_text(encoding="utf-8")
        self.assertIn("- [x] Record platform and emulator smoke-test prerequisites.", roadmap)
        self.assertIn("- [ ] Android arm64-v8a/x86_64 build and emulator smoke pipelines.", roadmap)
        self.assertNotIn("Phase 6 is complete", roadmap)

    def test_document_and_ci_keep_runtime_smoke_explicitly_future(self) -> None:
        document = DOCUMENT_PATH.read_text(encoding="utf-8")
        for required_text in (
            "run_windows_desktop_pipe_smoke.py",
            "bun run tauri:android:dev",
            "Chaquopy success is not JNI migration success",
            "does not complete the Phase 0 exit criteria",
        ):
            self.assertIn(required_text, document)
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for watched_path in (
            "scripts/platform/**",
            "tests/platform/**",
            "docs/script-runtime/PLATFORM_SMOKE_PREREQUISITES.md",
            "docs/script-runtime/evidence/platform_smoke_prerequisites.windows.json",
        ):
            self.assertEqual(workflow.count(watched_path), 2)
        self.assertIn("python -m unittest discover -s tests/platform", workflow)


if __name__ == "__main__":
    unittest.main()
