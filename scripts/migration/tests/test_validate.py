from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.migration.validate import Validator


HERE = Path(__file__).resolve().parent
FIXTURE_REPO = HERE / "fixtures" / "python_repo"
VALIDATOR = HERE.parent / "validate.py"


class ValidatorTests(unittest.TestCase):
    def test_fixture_finds_known_migration_failures(self) -> None:
        report = Validator(FIXTURE_REPO).report()

        self.assertEqual(report["tasks"]["supported"]["prefixes"], ["click", "exchange"])
        self.assertEqual(report["tasks"]["supported"]["exact"], ["choose_and_change", "end-turn"])
        self.assertEqual(report["summary"]["tasks"], 3)
        self.assertEqual(report["summary"]["image_mappings"], 3)
        self.assertEqual(report["summary"]["locales"], 1)

        codes = [issue["code"] for issue in report["issues"]]
        self.assertIn("task.operation.type", codes)
        self.assertIn("task.operation.unsupported", codes)
        self.assertIn("image.resource.missing", codes)
        self.assertIn("image.region.empty", codes)
        self.assertIn("ocr.method.missing", codes)

        typed = next(issue for issue in report["issues"] if issue["code"] == "task.operation.type")
        self.assertEqual(typed["pointer"], "/bad_array/action/0/t")
        missing_calls = [call for call in report["ocr"]["calls"] if not call["exists"]]
        self.assertEqual([call["method"] for call in missing_calls], ["recognize_number"])

        locale = report["images"]["locales"]["CN"]
        self.assertEqual(locale["missing_count"], 1)
        self.assertEqual(
            [mapping["identifier"] for mapping in locale["mappings"]],
            ["sample_missing", "sample_placeholder", "sample_present"],
        )

    def test_report_is_deterministic(self) -> None:
        first = json.dumps(Validator(FIXTURE_REPO).report(), ensure_ascii=False, indent=2, sort_keys=True)
        second = json.dumps(Validator(FIXTURE_REPO).report(), ensure_ascii=False, indent=2, sort_keys=True)
        self.assertEqual(first, second)
        self.assertNotIn(str(FIXTURE_REPO), first)

    def test_cli_writes_report_and_strict_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "report.json"
            process = subprocess.run(
                [
                    sys.executable,
                    str(VALIDATOR),
                    "--python-repo",
                    str(FIXTURE_REPO),
                    "--output",
                    str(output),
                    "--strict",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(process.returncode, 1, process.stderr)
            self.assertEqual(process.stdout, "")
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertGreater(report["summary"]["errors"], 0)

    def test_strict_promotes_warning_only_report_to_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory) / "python_repo"
            shutil.copytree(FIXTURE_REPO, repository)
            (repository / "src" / "images" / "CN" / "sample" / "missing.png").write_bytes(b"fixture")
            (repository / "src" / "explore_task_data" / "tasks.json").write_text(
                '{"valid":{"start":[["burst",[10,20]]],"action":[{"t":"end-turn"}]}}',
                encoding="utf-8",
            )
            (repository / "module" / "ocr_calls.py").write_text(
                "def inspect(self):\n    self.ocr.recognize_int(self, (0, 0, 1, 1))\n",
                encoding="utf-8",
            )
            command = [sys.executable, str(VALIDATOR), "--python-repo", str(repository)]
            normal = subprocess.run(command, check=False, capture_output=True, text=True)
            strict = subprocess.run([*command, "--strict"], check=False, capture_output=True, text=True)
            report = json.loads(normal.stdout)
            self.assertEqual(report["summary"]["errors"], 0)
            self.assertEqual(report["summary"]["warnings"], 1)
            self.assertEqual(normal.returncode, 0, normal.stderr)
            self.assertEqual(strict.returncode, 1, strict.stderr)


if __name__ == "__main__":
    unittest.main()
