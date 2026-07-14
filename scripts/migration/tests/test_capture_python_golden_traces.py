from __future__ import annotations

import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[3]
SCRIPT_PATH = ROOT / "scripts" / "migration" / "capture_python_golden_traces.py"
EVIDENCE_PATH = ROOT / "docs" / "script-runtime" / "evidence" / "python-golden-traces.json"
DOCUMENT_PATH = ROOT / "docs" / "script-runtime" / "PYTHON_GOLDEN_TRACES.md"
ROADMAP_PATH = ROOT / "docs" / "script-runtime" / "ROADMAP.md"
WORKFLOW_PATH = ROOT / ".github" / "workflows" / "foundation-runtime.yml"

SPEC = importlib.util.spec_from_file_location("capture_python_golden_traces", SCRIPT_PATH)
assert SPEC and SPEC.loader
CAPTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPTURE)


class PythonGoldenTraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.evidence = json.loads(EVIDENCE_PATH.read_text(encoding="utf-8"))

    def test_committed_evidence_passes_strict_schema(self) -> None:
        CAPTURE.validate_evidence(self.evidence)
        self.assertEqual(self.evidence["schema"], "baas.python-golden-traces")
        self.assertEqual(self.evidence["summary"]["workflow_count"], 4)
        self.assertEqual(self.evidence["summary"]["record_count"], 30)
        self.assertLess(EVIDENCE_PATH.stat().st_size, CAPTURE.MAX_EVIDENCE_BYTES)

    def test_source_commits_and_runtime_are_anchored(self) -> None:
        source = self.evidence["source"]
        self.assertEqual(source["production_commit"], CAPTURE.PRODUCTION_COMMIT)
        self.assertEqual(source["trace_commit"], CAPTURE.TRACE_COMMIT)
        self.assertTrue(source["trace_parent_verified"])
        self.assertTrue(source["trace_checkout_clean"])
        self.assertEqual(source["runtime"]["python"], "3.11.9")
        self.assertEqual(source["runtime"]["cv2"], "4.8.1")
        self.assertEqual(source["runtime"]["numpy"], "1.26.4")

    def test_representative_results_cross_real_offline_boundaries(self) -> None:
        workflows = {workflow["id"]: workflow for workflow in self.evidence["workflows"]}

        def result(workflow_id: str) -> dict:
            record = next(
                item for item in workflows[workflow_id]["records"]
                if item["event"] == "task.result"
            )
            return record["payload"]["metadata"]

        self.assertTrue(result("configuration.snapshot_patch")["unknown_field_retained"])
        self.assertTrue(result("image.cafe_match")["expected_match_present"])
        self.assertEqual(result("scheduler.queue_rebuild")["selected"], "task_a")
        self.assertEqual(result("orchestration.grid_actions")["physical_click_count"], 3)
        operations = {
            operation
            for workflow in workflows.values()
            for operation in workflow["summary"]["operations"]
        }
        self.assertTrue(
            {
                "config.apply_patch",
                "image.cafe_reward.match",
                "scheduler.heartbeat",
                "orchestration.run_task_action",
                "baas.click",
            }.issubset(operations)
        )

    def test_evidence_is_bounded_sanitized_and_has_no_false_scope(self) -> None:
        rendered = EVIDENCE_PATH.read_text(encoding="utf-8")
        self.assertNotIn(CAPTURE.SECRET_SENTINEL, rendered)
        self.assertNotRegex(rendered, r"(?i)\b[a-z]:[\\/]")
        self.assertNotIn("generated_at", rendered)
        for forbidden_operation in ("ocr.", "adb.", "device.", "service.start", "tauri."):
            self.assertNotIn(f'"operation": "{forbidden_operation}', rendered)
        self.assertIn("OCR initialization or inference", rendered)
        for workflow in self.evidence["workflows"]:
            self.assertLessEqual(len(workflow["records"]), CAPTURE.MAX_RECORDS_PER_WORKFLOW)
            self.assertTrue(workflow["offline"])
            for record in workflow["records"]:
                self.assertLessEqual(len(CAPTURE.compact_json(record)), CAPTURE.MAX_RECORD_BYTES)

    def test_hashes_and_json_rendering_are_reproducible(self) -> None:
        workflows = self.evidence["workflows"]
        self.assertEqual(
            self.evidence["summary"]["workflows_sha256"],
            CAPTURE.sha256_json(workflows),
        )
        for workflow in workflows:
            self.assertEqual(
                workflow["summary"]["records_sha256"],
                CAPTURE.sha256_json(workflow["records"]),
            )
        first = CAPTURE.stable_json(self.evidence)
        second = CAPTURE.stable_json(json.loads(first))
        self.assertEqual(first, second)
        self.assertEqual(
            hashlib.sha256(EVIDENCE_PATH.read_bytes()).hexdigest(),
            "a3bf59d07d8b31863d3c7ed95642cd9e1d13171f3f29036eddff93a1d0e9e695",
        )

    def test_schema_rejects_tampering_and_reports_first_difference(self) -> None:
        tampered = copy.deepcopy(self.evidence)
        tampered["workflows"][0]["records"][1]["seq"] = 99
        with self.assertRaisesRegex(ValueError, "sequence is not contiguous"):
            CAPTURE.validate_evidence(tampered)
        self.assertEqual(
            CAPTURE.first_difference(self.evidence, tampered),
            "/workflows/0/records/1/seq",
        )

    def test_check_mode_is_read_only_and_has_pointer_diagnostic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "golden.json"
            output.write_text(CAPTURE.stable_json(self.evidence), encoding="utf-8")
            argv = [
                "--python-repo",
                directory,
                "--python-executable",
                sys.executable,
                "--check",
                "--output",
                str(output),
            ]
            with mock.patch.object(CAPTURE, "generate", return_value=self.evidence):
                stdout = io.StringIO()
                with contextlib.redirect_stdout(stdout):
                    self.assertEqual(CAPTURE.main(argv), 0)
                self.assertIn("verified", stdout.getvalue())
            before = output.read_bytes()
            changed = copy.deepcopy(self.evidence)
            changed["source"]["runtime"]["python"] = "3.11.10"
            with mock.patch.object(CAPTURE, "generate", return_value=changed):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr):
                    self.assertEqual(CAPTURE.main(argv), 1)
                self.assertIn("/source/runtime/python", stderr.getvalue())
            self.assertEqual(before, output.read_bytes())

    def test_output_write_failure_has_setup_diagnostic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            blocker = Path(directory) / "not-a-directory"
            blocker.write_text("preserve me", encoding="utf-8")
            argv = [
                "--python-repo",
                directory,
                "--python-executable",
                sys.executable,
                "--output",
                str(blocker / "golden.json"),
            ]
            with mock.patch.object(CAPTURE, "generate", return_value=self.evidence):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr):
                    self.assertEqual(CAPTURE.main(argv), 2)
            self.assertIn("could not write golden evidence", stderr.getvalue())
            self.assertEqual(blocker.read_text(encoding="utf-8"), "preserve me")

    def test_document_roadmap_and_ci_preserve_completion_boundary(self) -> None:
        document = DOCUMENT_PATH.read_text(encoding="utf-8")
        for text in (
            "configuration.snapshot_patch",
            "image.cafe_match",
            "scheduler.queue_rebuild",
            "orchestration.grid_actions",
            "does not execute device, OCR, service, or Tauri paths",
        ):
            self.assertIn(text, document)
        roadmap = ROADMAP_PATH.read_text(encoding="utf-8")
        self.assertIn("- [x] Capture baseline Python golden traces for representative workflows.", roadmap)
        self.assertIn("- [ ] Bind OCR lifecycle and inference operations.", roadmap)
        self.assertNotIn("Phase 0 is complete", roadmap)
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for watched_path in (
            "docs/script-runtime/PYTHON_GOLDEN_TRACES.md",
            "docs/script-runtime/evidence/python-golden-traces.json",
        ):
            self.assertEqual(workflow.count(watched_path), 2)
        self.assertIn("python -B -m unittest discover -s scripts/migration/tests", workflow)


if __name__ == "__main__":
    unittest.main()
