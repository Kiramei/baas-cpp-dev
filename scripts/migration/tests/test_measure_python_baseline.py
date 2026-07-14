from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts.migration.measure_python_baseline import (
    BaselineMeasurer,
    main,
    scan_directory,
    stable_json,
    summarize,
)


def completed(command: list[str], stdout: str = "", stderr: str = "", returncode: int = 0):
    return subprocess.CompletedProcess(command, returncode, stdout, stderr)


class FakeRunner:
    def __init__(self, failing_probe: str | None = None) -> None:
        self.failing_probe = failing_probe
        self.commands: list[list[str]] = []

    def __call__(self, command: list[str], **_: object):
        self.commands.append(command)
        if "ls-files" in command:
            return completed(command, "module/cafe_reward.py\0extra.py\0src/data.bin\0")
        if "rev-parse" in command:
            return completed(command, "0123456789abcdef\n")
        if "status" in command:
            return completed(command, "")
        if command[-2:] == ["-c", "pass"]:
            return completed(command)
        probe = command[command.index("--child-probe") + 1]
        if probe == self.failing_probe:
            return completed(command, stderr="ModuleNotFoundError: No module named 'cv2'\n", returncode=1)
        payload: dict[str, object] = {
            "probe": probe,
            "python_version": "3.11.9",
            "probe_elapsed_ms": 2.5,
            "ready_rss_bytes": 10_000,
            "peak_rss_bytes": 12_000,
        }
        if probe == "cv2_numpy_import":
            payload.update(cv2_version="4.10.0", numpy_version="2.0.0")
        elif probe == "service_app_import":
            payload.update(route_count=12, service_context_materialized=False)
        elif probe in {"legacy_module_match", "service_injected_match"}:
            injected = probe == "service_injected_match"
            payload.update(
                algorithm_call_ms=[10.0],
                result_counts=[4],
                result_checksum=1234,
                input_shape=[720, 1280, 3],
                template_shape=[32, 32, 3],
                implementation_module="service.injection" if injected else "module.cafe_reward",
                implementation_line=532 if injected else 160,
                service_injection_applied=injected,
                service_injection_marker=injected,
            )
        return completed(command, json.dumps(payload) + "\n")


class PythonBaselineTests(unittest.TestCase):
    def make_repo(self, root: Path) -> tuple[Path, Path]:
        repo = root / "baas-dev"
        (repo / "module").mkdir(parents=True)
        (repo / "module" / "cafe_reward.py").write_text("def match(image): return []\n", encoding="utf-8")
        (repo / "extra.py").write_text("value = 1\n", encoding="utf-8")
        (repo / "src").mkdir()
        (repo / "src" / "data.bin").write_bytes(b"1234")
        (repo / "toolkit").mkdir()
        (repo / "toolkit" / "runtime.bin").write_bytes(b"123456")
        executable = repo / ".venv" / "Scripts" / "python.exe"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"fixture")
        return repo, executable

    def test_summary_uses_nearest_rank_and_preserves_sample_order(self) -> None:
        result = summarize([3.0, 1.0, 2.0], "ms")
        self.assertEqual(result["samples"], [3.0, 1.0, 2.0])
        self.assertEqual(result["median"], 2.0)
        self.assertEqual(result["p95_nearest_rank"], 3.0)

    def test_scan_directory_is_bounded_to_logical_file_sizes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "nested").mkdir()
            (root / "a.bin").write_bytes(b"12")
            (root / "nested" / "b.bin").write_bytes(b"345")
            self.assertEqual(
                scan_directory(root),
                {"status": "ok", "file_count": 2, "logical_bytes": 5},
            )
            self.assertEqual(scan_directory(root / "missing")["status"], "missing")

    def test_mocked_subprocess_report_has_stable_scope_and_single_process_policy(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, executable = self.make_repo(Path(directory))
            runner = FakeRunner()
            report = BaselineMeasurer(repo, executable, 1, 1, 5, runner).generate()

        self.assertEqual(report["summary"], {"successful_probes": 5, "failed_probes": 0})
        self.assertEqual(report["configuration"]["max_concurrent_probe_processes"], 1)
        self.assertFalse(report["configuration"]["parallel_execution"])
        self.assertFalse(report["configuration"]["os_file_cache_flushed"])
        self.assertFalse(report["probes"]["service_app_import"]["service_context_materialized"])
        legacy = report["probes"]["legacy_module_match"]
        injected = report["probes"]["service_injected_match"]
        self.assertTrue(legacy["gui_stubs_injected"])
        self.assertFalse(legacy["service_injection_applied"])
        self.assertEqual(legacy["implementation"]["module"], "module.cafe_reward")
        self.assertTrue(injected["service_injection_applied"])
        self.assertEqual(injected["implementation"]["module"], "service.injection")
        self.assertEqual(legacy["result_counts"], [[4]])
        self.assertEqual(injected["result_checksums"], [1234])
        self.assertIsNone(injected["subsequent_algorithm_call_ms"])
        self.assertEqual(report["host"]["benchmark_python_version"], "3.11.9")
        self.assertIn("FastAPI lifespan", report["scope"]["not_measured"][0])
        self.assertNotIn(str(repo), stable_json(report))
        child_commands = [command for command in runner.commands if "--child-probe" in command]
        self.assertEqual(len(child_commands), 4)
        empty = next(command for command in runner.commands if command[-2:] == ["-c", "pass"])
        self.assertIn("-I", empty)
        self.assertIn("-S", empty)
        self.assertTrue(all("--child-probe" in command for command in child_commands))
        self.assertIsNone(report["probes"]["empty_startup"]["ready_rss_bytes"])

    def test_probe_failure_has_dependency_diagnostic_and_nonzero_cli(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repo, executable = self.make_repo(root)
            output = root / "report.json"
            code = main(
                [
                    "--python-repo",
                    str(repo),
                    "--python-executable",
                    str(executable),
                    "--output",
                    str(output),
                    "--quick",
                ],
                runner=FakeRunner("cv2_numpy_import"),
            )
            report = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(code, 1)
        self.assertEqual(report["probes"]["cv2_numpy_import"]["status"], "error")
        self.assertIn("No module named 'cv2'", report["probes"]["cv2_numpy_import"]["diagnostic"])

    def test_cli_writes_json_with_mocked_child_processes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repo, executable = self.make_repo(root)
            output = root / "report.json"
            code = main(
                [
                    "--python-repo",
                    str(repo),
                    "--python-executable",
                    str(executable),
                    "--output",
                    str(output),
                    "--repetitions",
                    "1",
                    "--algorithm-iterations",
                    "1",
                ],
                runner=FakeRunner(),
            )
            report = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(code, 0)
        self.assertEqual(report["schema_version"], 1)
        self.assertEqual(report["repository"]["revision"], "0123456789abcdef")
        self.assertEqual(report["sizes"]["src"]["logical_bytes"], 4)

    def test_output_write_failure_returns_setup_exit_code(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repo, executable = self.make_repo(root)
            non_directory = root / "not-a-directory"
            non_directory.write_text("occupied", encoding="utf-8")
            code = main(
                [
                    "--python-repo",
                    str(repo),
                    "--python-executable",
                    str(executable),
                    "--output",
                    str(non_directory / "report.json"),
                    "--quick",
                ],
                runner=FakeRunner(),
            )

        self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
