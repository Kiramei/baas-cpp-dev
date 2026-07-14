from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.migration.operation_index import (
    BEGIN_MARKER,
    END_MARKER,
    OperationIndexer,
    RuleSet,
    normalize_path_text,
    render_generated_matrix,
    stable_json,
    update_matrix,
)


HERE = Path(__file__).resolve().parent
FIXTURE_REPO = HERE / "fixtures" / "operation_repo"
INDEXER = HERE.parent / "operation_index.py"
RULES = HERE.parent / "operation_rules.v1.json"


class OperationIndexTests(unittest.TestCase):
    def generate(self, root: Path = FIXTURE_REPO) -> dict:
        return OperationIndexer(root, RuleSet(RULES)).generate()

    def test_fixture_covers_alias_member_self_super_chained_and_dynamic(self) -> None:
        report = self.generate()
        by_symbol = {operation["symbol"]: operation for operation in report["operations"]}

        self.assertEqual(by_symbol["core.picture.co_detect"]["call_form"], "alias-member")
        self.assertEqual(by_symbol["self.click"]["call_form"], "self")
        self.assertEqual(by_symbol["self.click"]["occurrences"], 3)
        self.assertEqual(by_symbol["self.ocr.recognize"]["family"], "ocr.inference")
        self.assertEqual(by_symbol["obj.member"]["call_form"], "member")
        self.assertEqual(by_symbol["super().run"]["call_form"], "super")
        self.assertEqual(
            by_symbol["dynamic:call-result(factory)().send"]["call_form"],
            "chained",
        )
        dynamic = next(
            operation
            for operation in report["operations"]
            if operation["symbol"].startswith("dynamic:getattr(self,?)")
        )
        self.assertEqual(dynamic["migration_status"], "UNCLASSIFIED")
        self.assertEqual(dynamic["resolution"], "dynamic")

        alias = by_symbol["core.device.Control.Control"]
        self.assertEqual(alias["call_form"], "alias")
        self.assertEqual(alias["family"], "device.input-capture")

    def test_fixture_discovers_registries_routes_dispatch_and_parse_errors(self) -> None:
        report = self.generate()
        symbols = {operation["symbol"] for operation in report["operations"]}

        self.assertIn("registry:core.Baas_thread.func_dict:sample_task", symbols)
        self.assertIn("dispatch:operation:click*", symbols)
        self.assertIn("dispatch:operation:end-turn", symbols)
        self.assertIn("route:POST:/command", symbols)
        self.assertIn("dispatch:command:status", symbols)
        self.assertIn("dispatch:command:start", symbols)
        self.assertIn("dispatch:command:stop", symbols)
        self.assertEqual(report["summary"]["parse_errors"], 1)
        self.assertEqual(report["unresolved_sources"][0]["file"], "broken.py")

    def test_shapes_locations_and_paths_are_deterministic(self) -> None:
        first = stable_json(self.generate())
        second = stable_json(self.generate())
        self.assertEqual(first, second)
        self.assertNotIn(str(FIXTURE_REPO), first)
        self.assertNotIn("\\", first)
        self.assertEqual(normalize_path_text(r"core\device\Control.py"), "core/device/Control.py")

        report = json.loads(first)
        click = next(operation for operation in report["operations"] if operation["symbol"] == "self.click")
        self.assertEqual(
            [shape["shape"] for shape in click["call_shapes"]],
            [
                "pos=2;kw=-;*args=0;**kwargs=0;await=false",
                "pos=2;kw=wait_over;*args=0;**kwargs=0;await=false",
            ],
        )
        self.assertLessEqual(len(click["representative_locations"]), 3)

    def test_strict_fails_for_unclassified_and_parse_error(self) -> None:
        process = subprocess.run(
            [
                sys.executable,
                str(INDEXER),
                "--python-repo",
                str(FIXTURE_REPO),
                "--rules",
                str(RULES),
                "--strict",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(process.returncode, 1, process.stderr)
        report = json.loads(process.stdout)
        self.assertGreater(report["summary"]["unclassified_operations"], 0)
        self.assertEqual(report["summary"]["parse_errors"], 1)

    def test_strict_succeeds_for_fully_classified_fixture(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory) / "repo"
            repository.mkdir()
            (repository / "only.py").write_text(
                "def run(self):\n    self.click(1, 2, wait_over=True)\n",
                encoding="utf-8",
            )
            process = subprocess.run(
                [
                    sys.executable,
                    str(INDEXER),
                    "--python-repo",
                    str(repository),
                    "--rules",
                    str(RULES),
                    "--strict",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(process.returncode, 0, process.stderr)
            report = json.loads(process.stdout)
            self.assertEqual(report["summary"]["operation_count"], 1)
            self.assertEqual(report["summary"]["unclassified_operations"], 0)

    def test_matrix_marker_update_preserves_manual_sections(self) -> None:
        report = self.generate()
        generated = render_generated_matrix(report, "evidence/operation-index.json")
        with tempfile.TemporaryDirectory() as directory:
            matrix = Path(directory) / "MIGRATION_MATRIX.md"
            matrix.write_text("# Manual\n\nKeep this text.\n", encoding="utf-8")
            update_matrix(matrix, generated)
            first = matrix.read_text(encoding="utf-8")
            update_matrix(matrix, generated)
            second = matrix.read_text(encoding="utf-8")

        self.assertEqual(first, second)
        self.assertIn("Keep this text.", first)
        self.assertEqual(first.count(BEGIN_MARKER), 1)
        self.assertEqual(first.count(END_MARKER), 1)
        for operation in report["operations"]:
            self.assertIn(operation["id"], first)

    def test_output_is_independent_of_repository_copy_location(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            first_root = Path(directory) / "one"
            second_root = Path(directory) / "two"
            shutil.copytree(FIXTURE_REPO, first_root)
            shutil.copytree(FIXTURE_REPO, second_root)
            first = self.generate(first_root)
            second = self.generate(second_root)
        self.assertEqual(first, second)

    def test_rules_digest_is_independent_of_line_endings(self) -> None:
        content = RULES.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            lf = Path(directory) / "lf.json"
            crlf = Path(directory) / "crlf.json"
            lf.write_bytes(content.replace("\r\n", "\n").encode("utf-8"))
            crlf.write_bytes(content.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
            self.assertEqual(RuleSet(lf).sha256, RuleSet(crlf).sha256)


if __name__ == "__main__":
    unittest.main()
