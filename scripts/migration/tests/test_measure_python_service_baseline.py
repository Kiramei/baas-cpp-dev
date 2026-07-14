from __future__ import annotations

import contextlib
import hashlib
import io
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts.migration import measure_python_service_baseline as SERVICE


ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = ROOT / "docs" / "script-runtime" / "evidence" / "python-service-performance-baseline.json"
OLD_EVIDENCE = ROOT / "docs" / "script-runtime" / "evidence" / "python-performance-baseline.json"
DOCUMENT = ROOT / "docs" / "script-runtime" / "PYTHON_SERVICE_PERFORMANCE_BASELINE.md"
ROADMAP = ROOT / "docs" / "script-runtime" / "ROADMAP.md"
WORKFLOW = ROOT / ".github" / "workflows" / "foundation-runtime.yml"


def completed(command: list[str], stdout: str = "", stderr: str = "", returncode: int = 0):
    return subprocess.CompletedProcess(command, returncode, stdout, stderr)


class FakeRunner:
    def __init__(self, status: str = "ok") -> None:
        self.status = status
        self.commands: list[list[str]] = []

    def __call__(self, command: list[str], **_: object):
        self.commands.append(command)
        if "rev-parse" in command:
            return completed(command, "0123456789abcdef0123456789abcdef01234567\n")
        if "status" in command and "--porcelain" in command:
            return completed(command, "")
        if self.status == "error":
            return completed(command, stderr="guard failure\n", returncode=1)
        if self.status == "missing":
            return completed(
                command,
                json.dumps(
                    {
                        "probe": SERVICE.PROBE_NAME,
                        "probe_status": "missing",
                        "dependency": "fastapi",
                        "diagnostic": "required dependency is missing: fastapi",
                    }
                )
                + "\n",
            )
        payload = {
            "probe": SERVICE.PROBE_NAME,
            "probe_status": "ok",
            "python_version": "3.11.9",
            "route_count": 10,
            "request_iterations": 2,
            "transport": "direct in-process ASGI 3.0 callable",
            "lifespan_started": False,
            "network_listener_opened": False,
            "isolated_project_root": True,
            "temporary_artifacts": [
                "config/service_signing_key.bin",
                "config/service_ticket.key",
            ],
            "guard_hits": {
                "service_lifespan_startup": 0,
                "service_lifespan_shutdown": 0,
                "ocr_core_initialization": 0,
                "data_initialization_thread": 0,
                "outbound_requests": 0,
            },
            "peak_rss_bytes": 14_000,
            "first_health_ms": 3.0,
            "first_status_code": 200,
            "asgi_ready_elapsed_ms": 30.0,
            "ready_rss_bytes": 12_000,
            "ready_peak_rss_bytes": 13_000,
            "request_call_ms": [0.5, 0.4],
            "request_batch_ms": 1.0,
            "request_status_codes": [200, 200],
            "response_keys": ["auth", "ok", "statuses"],
            "response_ok": True,
            "auth_keys": ["initialized", "pwd_epoch", "server_sign_public_key"],
            "asgi_scope_teardown_ms": [0.02, 0.01, 0.01],
        }
        return completed(command, json.dumps(payload) + "\n")


class PythonServiceBaselineTests(unittest.TestCase):
    def make_repo(self, root: Path) -> tuple[Path, Path]:
        repo = root / "baas-dev"
        (repo / "service").mkdir(parents=True)
        (repo / "service" / "app.py").write_text("app = object()\n", encoding="utf-8")
        executable = repo / ".venv" / "Scripts" / "python.exe"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"fixture")
        return repo, executable

    def test_mocked_controller_is_serial_bounded_and_preserves_skip_boundary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, executable = self.make_repo(Path(directory))
            runner = FakeRunner()
            report = SERVICE.ServiceBaselineMeasurer(
                repo, executable, repetitions=2, request_iterations=2, timeout=5, runner=runner
            ).generate()
        SERVICE.validate_report(report)
        self.assertEqual(report["summary"], {"ok": 1, "skipped": 2, "missing": 0, "error": 0})
        self.assertEqual(report["configuration"]["max_concurrent_probe_processes"], 1)
        self.assertFalse(report["configuration"]["parallel_execution"])
        self.assertEqual(report["probes"][SERVICE.PROBE_NAME]["total_measured_requests"], 4)
        self.assertEqual(report["probes"]["full_service_lifespan"]["status"], "skipped")
        child_commands = [command for command in runner.commands if "--child-probe" in command]
        self.assertEqual(len(child_commands), 2)
        self.assertTrue(all("-I" in command and "-B" in command for command in child_commands))
        self.assertNotIn(str(repo), SERVICE.stable_json(report))

    def test_missing_dependency_is_structured_not_fabricated(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, executable = self.make_repo(Path(directory))
            report = SERVICE.ServiceBaselineMeasurer(
                repo, executable, 1, 2, 5, FakeRunner("missing")
            ).generate()
        SERVICE.validate_report(report)
        probe = report["probes"][SERVICE.PROBE_NAME]
        self.assertEqual(probe["status"], "missing")
        self.assertEqual(probe["dependency"], "fastapi")
        self.assertEqual(report["summary"]["missing"], 1)

    def test_probe_failure_is_structured(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, executable = self.make_repo(Path(directory))
            report = SERVICE.ServiceBaselineMeasurer(
                repo, executable, 1, 2, 5, FakeRunner("error")
            ).generate()
        SERVICE.validate_report(report)
        self.assertEqual(report["probes"][SERVICE.PROBE_NAME]["status"], "error")
        self.assertIn("guard failure", report["probes"][SERVICE.PROBE_NAME]["diagnostic"])

    def test_committed_evidence_passes_schema_without_performance_assertions(self) -> None:
        report = json.loads(EVIDENCE.read_text(encoding="utf-8"))
        SERVICE.validate_report(report)
        self.assertEqual(report["repository"]["revision"], "75bbacb545bc87e9510d85cbe8034f9180397004")
        self.assertEqual(report["probes"]["full_service_lifespan"]["status"], "skipped")
        self.assertEqual(report["probes"][SERVICE.PROBE_NAME]["isolation"]["guard_hits"], {
            "data_initialization_thread": 0,
            "ocr_core_initialization": 0,
            "outbound_requests": 0,
            "service_lifespan_shutdown": 0,
            "service_lifespan_startup": 0,
        })

    def test_old_host_baseline_is_byte_for_byte_preserved(self) -> None:
        self.assertEqual(
            hashlib.sha256(OLD_EVIDENCE.read_bytes()).hexdigest(),
            "81b192f640ba2df3ac2a1c4e884d084ae5d9acaf9775fefea3565d29d31c3b8f",
        )

    def test_check_evidence_validates_without_running_probe(self) -> None:
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            self.assertEqual(SERVICE.main(["--check-evidence", str(EVIDENCE)]), 0)
        self.assertIn("verified", stdout.getvalue())

    def test_docs_roadmap_and_ci_do_not_overclaim_service_startup(self) -> None:
        document = DOCUMENT.read_text(encoding="utf-8")
        for phrase in (
            "full production lifespan was not run",
            "direct in-process ASGI",
            "natural timing variation",
            "python-performance-baseline.json",
        ):
            self.assertIn(phrase, document)
        roadmap = ROADMAP.read_text(encoding="utf-8")
        self.assertIn("direct in-process ASGI `/health`", roadmap)
        self.assertIn("full service lifespan", roadmap)
        self.assertNotIn("full service startup baseline is complete", roadmap)
        workflow = WORKFLOW.read_text(encoding="utf-8")
        for watched_path in (
            "docs/script-runtime/PYTHON_SERVICE_PERFORMANCE_BASELINE.md",
            "docs/script-runtime/evidence/python-service-performance-baseline.json",
        ):
            self.assertEqual(workflow.count(watched_path), 2)
        self.assertIn("python -B -m unittest discover -s scripts/migration/tests", workflow)


if __name__ == "__main__":
    unittest.main()
