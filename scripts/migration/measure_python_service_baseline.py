#!/usr/bin/env python3
"""Measure the safe offline portion of the BAAS Python service baseline.

The controller launches one isolated child process at a time.  The measured
path imports the production FastAPI app and serves its real ``/health`` route
through a direct in-process ASGI call.  It deliberately does not enter the app
lifespan or open a socket because the audited production startup initializes
OCR/core state, watchers, update tasks, and a data-initialization thread.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import importlib
import json
import os
import platform
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable, Sequence
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

from measure_python_baseline import (
    compact_diagnostic,
    current_and_peak_rss_bytes,
    git_metadata,
    parse_child_output,
    physical_memory_bytes,
    relative_executable,
    rounded,
    run_command,
    stable_json,
    summarize,
)


SCHEMA = "baas.python-service-performance-baseline"
SCHEMA_VERSION = 1
GENERATOR_VERSION = "1.0.0"
DEFAULT_REPETITIONS = 5
QUICK_REPETITIONS = 2
DEFAULT_REQUESTS = 50
QUICK_REQUESTS = 5
PROBE_NAME = "service_asgi_health_offline"
PROBE_KEYS = (PROBE_NAME, "full_service_lifespan", "localhost_uvicorn")
CommandRunner = Callable[..., subprocess.CompletedProcess[str]]


def _deny(counter: dict[str, int], key: str):
    def denied(*_args: Any, **_kwargs: Any) -> None:
        counter[key] += 1
        raise RuntimeError(f"offline safety guard blocked {key}")

    return denied


async def _deny_async(counter: dict[str, int], key: str, *_args: Any, **_kwargs: Any) -> None:
    counter[key] += 1
    raise RuntimeError(f"offline safety guard blocked {key}")


def _relative_files(root: Path) -> list[str]:
    return sorted(
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file()
    )


def run_child_probe(repo: Path, request_iterations: int) -> int:
    """Run the production health route without startup or network listeners."""
    started = time.perf_counter()
    sys.stdout.reconfigure(encoding="utf-8")
    sys.path.insert(0, str(repo))

    try:
        import requests
        service_app_module = importlib.import_module("service.app")
        from service.api.state import context
        from service.context import ServiceContext
        from service.runtime import ServiceRuntime
    except ModuleNotFoundError as error:
        sys.stdout.write(
            json.dumps(
                {
                    "probe": PROBE_NAME,
                    "probe_status": "missing",
                    "dependency": error.name or "unknown",
                    "diagnostic": f"required dependency is missing: {error.name or 'unknown'}",
                },
                sort_keys=True,
            )
            + "\n"
        )
        return 0

    guard_hits = {
        "service_lifespan_startup": 0,
        "service_lifespan_shutdown": 0,
        "ocr_core_initialization": 0,
        "data_initialization_thread": 0,
        "outbound_requests": 0,
    }

    with tempfile.TemporaryDirectory(prefix="baas-service-baseline-") as directory:
        isolated_root = Path(directory)
        previous_cwd = Path.cwd()
        os.chdir(isolated_root)
        context.project_root = isolated_root
        context._context = None

        async def invoke_health() -> tuple[int, dict[str, Any], float]:
            request_sent = False
            response_status: int | None = None
            response_parts: list[bytes] = []
            response_finished_at: float | None = None
            wait_forever = asyncio.Event()

            async def receive() -> dict[str, Any]:
                nonlocal request_sent
                if not request_sent:
                    request_sent = True
                    return {"type": "http.request", "body": b"", "more_body": False}
                await wait_forever.wait()
                raise AssertionError("unreachable")

            async def send(message: dict[str, Any]) -> None:
                nonlocal response_status, response_finished_at
                if message["type"] == "http.response.start":
                    response_status = int(message["status"])
                elif message["type"] == "http.response.body":
                    response_parts.append(message.get("body", b""))
                    if not message.get("more_body", False):
                        response_finished_at = time.perf_counter()

            scope = {
                "type": "http",
                "asgi": {"version": "3.0", "spec_version": "2.3"},
                "http_version": "1.1",
                "method": "GET",
                "scheme": "http",
                "path": "/health",
                "raw_path": b"/health",
                "root_path": "",
                "query_string": b"",
                "headers": [(b"host", b"offline.local")],
                "client": ("127.0.0.1", 0),
                "server": ("offline.local", 80),
                "state": {},
            }
            await service_app_module.app(scope, receive, send)
            if response_status is None or response_finished_at is None:
                raise RuntimeError("ASGI app did not complete an HTTP response")
            response_payload = json.loads(b"".join(response_parts).decode("utf-8"))
            teardown_ms = (time.perf_counter() - response_finished_at) * 1000
            return response_status, response_payload, teardown_ms

        async def run_requests() -> dict[str, Any]:
            first_started = time.perf_counter()
            first_status, payload, first_teardown_ms = await invoke_health()
            first_ms = (time.perf_counter() - first_started) * 1000
            ready_elapsed_ms = (time.perf_counter() - started) * 1000
            ready_rss, ready_peak = current_and_peak_rss_bytes()

            call_ms: list[float] = []
            teardown_ms: list[float] = []
            codes: list[int] = []
            batch_started = time.perf_counter()
            for _ in range(request_iterations):
                call_started = time.perf_counter()
                status, response_payload, scope_teardown_ms = await invoke_health()
                call_ms.append((time.perf_counter() - call_started) * 1000)
                teardown_ms.append(scope_teardown_ms)
                codes.append(status)
                if response_payload.get("ok") is not True:
                    raise RuntimeError("/health returned ok != true")
            batch_ms = (time.perf_counter() - batch_started) * 1000
            return {
                "first_health_ms": rounded(first_ms),
                "first_status_code": first_status,
                "asgi_ready_elapsed_ms": rounded(ready_elapsed_ms),
                "ready_rss_bytes": ready_rss,
                "ready_peak_rss_bytes": ready_peak,
                "request_call_ms": [rounded(value) for value in call_ms],
                "request_batch_ms": rounded(batch_ms),
                "request_status_codes": codes,
                "response_keys": sorted(payload),
                "response_ok": payload.get("ok") is True,
                "auth_keys": sorted(payload.get("auth", {})),
                "asgi_scope_teardown_ms": [rounded(first_teardown_ms), *[
                    rounded(value) for value in teardown_ms
                ]],
            }

        try:
            with contextlib.ExitStack() as stack:
                stack.enter_context(
                    mock.patch.object(
                        ServiceContext,
                        "startup",
                        new=lambda *_args, **_kwargs: _deny_async(
                            guard_hits, "service_lifespan_startup"
                        ),
                    )
                )
                stack.enter_context(
                    mock.patch.object(
                        ServiceContext,
                        "shutdown",
                        new=lambda *_args, **_kwargs: _deny_async(
                            guard_hits, "service_lifespan_shutdown"
                        ),
                    )
                )
                stack.enter_context(
                    mock.patch.object(
                        ServiceRuntime,
                        "ensure_ready",
                        new=lambda *_args, **_kwargs: _deny_async(
                            guard_hits, "ocr_core_initialization"
                        ),
                    )
                )
                stack.enter_context(
                    mock.patch.object(
                        ServiceRuntime,
                        "init_all_data",
                        new=_deny(guard_hits, "data_initialization_thread"),
                    )
                )
                stack.enter_context(
                    mock.patch.object(
                        requests.sessions.Session,
                        "request",
                        new=_deny(guard_hits, "outbound_requests"),
                    )
                )
                result = asyncio.run(run_requests())
            artifacts = _relative_files(isolated_root)
        finally:
            os.chdir(previous_cwd)

    if any(guard_hits.values()):
        raise RuntimeError(f"offline safety guard was reached: {guard_hits}")
    if context._context is None:
        raise RuntimeError("health route did not materialize the isolated ServiceContext")
    if result["first_status_code"] != 200:
        raise RuntimeError("first health request did not return HTTP 200")
    if result["request_status_codes"] != [200] * request_iterations:
        raise RuntimeError("one or more health requests did not return HTTP 200")

    _current, peak = current_and_peak_rss_bytes()
    payload = {
        "probe": PROBE_NAME,
        "probe_status": "ok",
        "python_version": platform.python_version(),
        "route_count": len(service_app_module.app.routes),
        "request_iterations": request_iterations,
        "transport": "direct in-process ASGI 3.0 callable",
        "lifespan_started": False,
        "network_listener_opened": False,
        "isolated_project_root": True,
        "temporary_artifacts": artifacts,
        "guard_hits": guard_hits,
        "peak_rss_bytes": peak,
        **result,
    }
    sys.stdout.write(json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n")
    return 0


def _summary_or_none(values: Sequence[float | int | None], unit: str) -> dict[str, Any] | None:
    present = [float(value) for value in values if value is not None]
    return summarize(present, unit) if present else None


def completed_runner(*args: Any, **kwargs: Any) -> subprocess.CompletedProcess[str]:
    return subprocess.run(*args, **kwargs)


class ServiceBaselineMeasurer:
    def __init__(
        self,
        repo: Path,
        executable: Path,
        repetitions: int,
        request_iterations: int,
        timeout: float,
        runner: CommandRunner = completed_runner,
    ) -> None:
        self.repo = repo
        self.executable = executable
        self.repetitions = repetitions
        self.request_iterations = request_iterations
        self.timeout = timeout
        self.runner = runner
        self.script = Path(__file__).resolve()

    def probe_command(self) -> list[str]:
        return [
            str(self.executable),
            "-I",
            "-B",
            str(self.script),
            "--child-probe",
            PROBE_NAME,
            "--python-repo",
            str(self.repo),
            "--request-iterations",
            str(self.request_iterations),
        ]

    def measure_asgi(self) -> dict[str, Any]:
        payloads: list[dict[str, Any]] = []
        walls: list[float] = []
        for _ in range(self.repetitions):
            process, wall, diagnostic = run_command(
                self.probe_command(), self.runner, self.timeout
            )
            if process is None:
                return {"status": "error", "diagnostic": diagnostic}
            try:
                payload = parse_child_output(process)
            except RuntimeError as error:
                return {"status": "error", "diagnostic": str(error)}
            if payload.get("probe_status") == "missing":
                return {
                    "status": "missing",
                    "dependency": payload.get("dependency", "unknown"),
                    "diagnostic": payload.get("diagnostic", "required dependency is missing"),
                }
            if payload.get("probe_status") != "ok":
                return {"status": "error", "diagnostic": "child returned an unknown status"}
            payloads.append(payload)
            walls.append(wall)

        request_calls = [value for payload in payloads for value in payload["request_call_ms"]]
        total_requests = self.repetitions * self.request_iterations
        total_batch_ms = sum(float(payload["request_batch_ms"]) for payload in payloads)
        if any(any(payload["guard_hits"].values()) for payload in payloads):
            return {"status": "error", "diagnostic": "an offline safety guard was reached"}
        return {
            "status": "ok",
            "repetitions": self.repetitions,
            "request_iterations_per_process": self.request_iterations,
            "total_measured_requests": total_requests,
            "python_version": payloads[0]["python_version"],
            "route_count": payloads[0]["route_count"],
            "transport": payloads[0]["transport"],
            "process_wall_ms": summarize(walls, "ms"),
            "asgi_ready_elapsed_ms": summarize(
                [payload["asgi_ready_elapsed_ms"] for payload in payloads], "ms"
            ),
            "first_health_ms": summarize(
                [payload["first_health_ms"] for payload in payloads], "ms"
            ),
            "ready_rss_bytes": _summary_or_none(
                [payload["ready_rss_bytes"] for payload in payloads], "bytes"
            ),
            "peak_rss_bytes": _summary_or_none(
                [payload["peak_rss_bytes"] for payload in payloads], "bytes"
            ),
            "health_request_ms": summarize(request_calls, "ms"),
            "health_requests_per_second": rounded(
                total_requests * 1000.0 / total_batch_ms
            ),
            "asgi_scope_teardown_ms": summarize(
                [
                    value
                    for payload in payloads
                    for value in payload["asgi_scope_teardown_ms"]
                ],
                "ms",
            ),
            "response_contract": {
                "status_code": 200,
                "response_keys": payloads[0]["response_keys"],
                "auth_keys": payloads[0]["auth_keys"],
                "ok": all(payload["response_ok"] for payload in payloads),
            },
            "isolation": {
                "lifespan_started": any(payload["lifespan_started"] for payload in payloads),
                "network_listener_opened": any(
                    payload["network_listener_opened"] for payload in payloads
                ),
                "isolated_project_root": all(
                    payload["isolated_project_root"] for payload in payloads
                ),
                "temporary_artifacts": payloads[0]["temporary_artifacts"],
                "guard_hits": payloads[0]["guard_hits"],
            },
            "close_boundary": "ASGI request-scope teardown; not ServiceContext.shutdown",
            "service_shutdown": {
                "status": "skipped",
                "latency_ms": None,
                "reason": "production shutdown is only valid after the unsafe production lifespan",
            },
        }

    def generate(self) -> dict[str, Any]:
        asgi = self.measure_asgi()
        probes = {
            PROBE_NAME: asgi,
            "full_service_lifespan": {
                "status": "skipped",
                "reason": (
                    "production startup initializes OCR/core state, filesystem watching, "
                    "periodic update work, and a background data-initialization thread"
                ),
            },
            "localhost_uvicorn": {
                "status": "skipped",
                "reason": (
                    "Uvicorn enters the unsafe production lifespan; a direct ASGI call "
                    "covers the real health route without a listener"
                ),
            },
        }
        statuses = [probe["status"] for probe in probes.values()]
        return {
            "schema": SCHEMA,
            "schema_version": SCHEMA_VERSION,
            "generator_version": GENERATOR_VERSION,
            "repository": {
                "name": self.repo.name,
                **git_metadata(self.repo, self.runner, self.timeout),
            },
            "host": {
                "os": platform.system(),
                "os_release": platform.release(),
                "machine": platform.machine(),
                "logical_cpu_count": os.cpu_count(),
                "physical_memory_bytes": physical_memory_bytes(),
                "controller_python_version": platform.python_version(),
                "benchmark_python_version": asgi.get("python_version"),
                "benchmark_python": relative_executable(self.repo, self.executable),
            },
            "configuration": {
                "repetitions": self.repetitions,
                "request_iterations_per_process": self.request_iterations,
                "timeout_seconds_per_process": self.timeout,
                "max_concurrent_probe_processes": 1,
                "parallel_execution": False,
                "os_file_cache_flushed": False,
            },
            "safety_audit": {
                "status": "full_lifespan_unsafe_for_offline_baseline",
                "production_anchors": [
                    "service/app.py:lifespan",
                    "service/context.py:ServiceContext.startup",
                    "service/runtime.py:ServiceRuntime.ensure_ready",
                    "service/runtime.py:ServiceRuntime.init_all_data",
                ],
                "measured_boundary": (
                    "production FastAPI /health route through a direct in-process ASGI call, "
                    "with lazy context rooted in a temporary directory"
                ),
                "guards": [
                    "ServiceContext startup and shutdown",
                    "OCR/core ensure_ready",
                    "background init_all_data",
                    "requests.Session outbound HTTP",
                ],
            },
            "timing_policy": {
                "values_are_observations_not_regression_thresholds": True,
                "natural_variation_expected": True,
                "fresh_process_per_repetition": True,
                "old_host_import_algorithm_evidence_preserved": (
                    "docs/script-runtime/evidence/python-performance-baseline.json"
                ),
            },
            "probes": probes,
            "summary": {
                "ok": statuses.count("ok"),
                "skipped": statuses.count("skipped"),
                "missing": statuses.count("missing"),
                "error": statuses.count("error"),
            },
        }


def _is_windows_absolute_path(text: str) -> bool:
    return bool(re.search(r"(?:^|[\"' ])(?:[A-Za-z]:\\\\|[A-Za-z]:/)", text))


def validate_report(report: dict[str, Any]) -> None:
    if report.get("schema") != SCHEMA or report.get("schema_version") != SCHEMA_VERSION:
        raise ValueError("schema identity/version mismatch")
    if set(report.get("probes", {})) != set(PROBE_KEYS):
        raise ValueError("probe keys mismatch")
    repository = report.get("repository", {})
    if repository.get("name") != "baas-dev" or repository.get("clean") is not True:
        raise ValueError("evidence must come from a clean baas-dev checkout")
    if not re.fullmatch(r"[0-9a-f]{40}", str(repository.get("revision", ""))):
        raise ValueError("repository revision must be a full lowercase commit")
    summary = report.get("summary", {})
    statuses = [probe.get("status") for probe in report["probes"].values()]
    expected_summary = {
        "ok": statuses.count("ok"),
        "skipped": statuses.count("skipped"),
        "missing": statuses.count("missing"),
        "error": statuses.count("error"),
    }
    if summary != expected_summary:
        raise ValueError("summary does not match probe statuses")
    if report["probes"]["full_service_lifespan"].get("status") != "skipped":
        raise ValueError("full service lifespan must remain explicitly skipped")
    if report["probes"]["localhost_uvicorn"].get("status") != "skipped":
        raise ValueError("localhost Uvicorn must remain explicitly skipped")
    asgi = report["probes"][PROBE_NAME]
    if asgi.get("status") == "ok":
        isolation = asgi.get("isolation", {})
        if isolation.get("lifespan_started") or isolation.get("network_listener_opened"):
            raise ValueError("offline ASGI probe entered a forbidden service boundary")
        if isolation.get("isolated_project_root") is not True:
            raise ValueError("offline ASGI probe did not use an isolated project root")
        if any(isolation.get("guard_hits", {}).values()):
            raise ValueError("offline ASGI probe reached a safety guard")
        contract = asgi.get("response_contract", {})
        if contract.get("status_code") != 200 or contract.get("ok") is not True:
            raise ValueError("health response contract was not satisfied")
        if asgi.get("total_measured_requests", 0) < 1:
            raise ValueError("offline ASGI probe has no measured requests")
    elif asgi.get("status") not in {"missing", "error"}:
        raise ValueError("offline ASGI probe has an unknown status")
    rendered = stable_json(report)
    if _is_windows_absolute_path(rendered):
        raise ValueError("evidence contains a Windows absolute path")
    if report.get("timing_policy", {}).get("natural_variation_expected") is not True:
        raise ValueError("timing variation policy is missing")


def find_default_python(repo: Path) -> Path | None:
    candidates = [repo / ".venv" / "Scripts" / "python.exe", repo / ".venv" / "bin" / "python"]
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python-repo", type=Path, help="path to the baas-dev checkout")
    parser.add_argument("--python-executable", type=Path, help="Python executable (defaults to repo .venv)")
    parser.add_argument("--output", type=Path, help="JSON output path; stdout when omitted")
    parser.add_argument("--repetitions", type=int, help=f"fresh processes (default {DEFAULT_REPETITIONS})")
    parser.add_argument("--request-iterations", type=int, help=f"health calls per process (default {DEFAULT_REQUESTS})")
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds allowed per child process")
    parser.add_argument("--quick", action="store_true", help="use two processes and five requests unless overridden")
    parser.add_argument("--check-evidence", type=Path, help="validate existing evidence without running a benchmark")
    parser.add_argument("--child-probe", choices=(PROBE_NAME,), help=argparse.SUPPRESS)
    return parser


def main(argv: list[str] | None = None, *, runner: CommandRunner = completed_runner) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.check_evidence:
        try:
            report = json.loads(args.check_evidence.read_text(encoding="utf-8"))
            validate_report(report)
        except (OSError, json.JSONDecodeError, ValueError) as error:
            print(f"invalid service baseline evidence: {error}", file=sys.stderr)
            return 1
        print(f"verified {args.check_evidence}")
        return 0
    if args.child_probe:
        if args.python_repo is None:
            parser.error("--python-repo is required for child probe")
        try:
            return run_child_probe(args.python_repo.resolve(), args.request_iterations or 1)
        except Exception as error:
            print(f"{type(error).__name__}: {error}", file=sys.stderr)
            return 1
    if args.python_repo is None:
        parser.error("--python-repo is required")
    repo = args.python_repo.resolve()
    if not repo.is_dir() or not (repo / "service" / "app.py").is_file():
        parser.error("--python-repo must be a baas-dev checkout containing service/app.py")
    executable = (args.python_executable or find_default_python(repo))
    if executable is None:
        parser.error("no repo .venv Python found; pass --python-executable")
    executable = executable.resolve()
    if not executable.is_file():
        parser.error(f"Python executable does not exist: {executable}")
    repetitions = args.repetitions or (QUICK_REPETITIONS if args.quick else DEFAULT_REPETITIONS)
    requests = args.request_iterations or (QUICK_REQUESTS if args.quick else DEFAULT_REQUESTS)
    if repetitions < 1 or requests < 1 or args.timeout <= 0:
        parser.error("repetitions, request iterations, and timeout must be positive")

    try:
        report = ServiceBaselineMeasurer(
            repo, executable, repetitions, requests, args.timeout, runner
        ).generate()
        validate_report(report)
    except (RuntimeError, ValueError) as error:
        print(f"measurement setup failed: {error}", file=sys.stderr)
        return 2
    rendered = stable_json(report)
    if args.output:
        try:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8", newline="\n")
        except OSError as error:
            print(f"could not write service measurement report: {error}", file=sys.stderr)
            return 2
    else:
        sys.stdout.write(rendered)
    if report["summary"]["error"]:
        print("service baseline probe failed; inspect JSON diagnostic", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
