#!/usr/bin/env python3
"""Measure a bounded, repeatable host-side BAAS Python performance baseline.

The public command launches one isolated probe process at a time.  It never
starts the FastAPI lifespan, imports user configuration, contacts a device, or
runs OCR.  The private child-probe interface exists only so startup and import
costs can be measured in fresh Python processes.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable, Sequence


SCHEMA_VERSION = 1
GENERATOR_VERSION = "1.0.0"
DEFAULT_REPETITIONS = 5
QUICK_REPETITIONS = 2
DEFAULT_ALGORITHM_ITERATIONS = 3
QUICK_ALGORITHM_ITERATIONS = 1
PROBE_NAMES = (
    "empty_startup",
    "cv2_numpy_import",
    "service_app_import",
    "legacy_module_match",
    "service_injected_match",
)
CommandRunner = Callable[..., subprocess.CompletedProcess[str]]


def stable_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def normalize_path(value: str | os.PathLike[str]) -> str:
    return str(value).replace("\\", "/")


def rounded(value: float) -> float:
    return round(value, 3)


def summarize(samples: Sequence[float], unit: str) -> dict[str, Any]:
    """Return deterministic summary fields; p95 uses nearest-rank."""
    if not samples:
        raise ValueError("at least one sample is required")
    ordered = sorted(float(value) for value in samples)
    rank = max(1, math.ceil(0.95 * len(ordered)))
    return {
        "unit": unit,
        "samples": [rounded(value) for value in samples],
        "min": rounded(ordered[0]),
        "median": rounded(statistics.median(ordered)),
        "mean": rounded(statistics.fmean(ordered)),
        "p95_nearest_rank": rounded(ordered[rank - 1]),
        "max": rounded(ordered[-1]),
    }


def current_and_peak_rss_bytes() -> tuple[int | None, int | None]:
    """Return current/peak RSS with only the Python standard library."""
    if os.name == "nt":
        class ProcessMemoryCountersEx(ctypes.Structure):
            _fields_ = [
                ("cb", ctypes.c_ulong),
                ("PageFaultCount", ctypes.c_ulong),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
                ("PrivateUsage", ctypes.c_size_t),
            ]

        counters = ProcessMemoryCountersEx()
        counters.cb = ctypes.sizeof(counters)
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
        kernel32.GetCurrentProcess.restype = ctypes.c_void_p
        psapi.GetProcessMemoryInfo.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ProcessMemoryCountersEx),
            ctypes.c_ulong,
        ]
        psapi.GetProcessMemoryInfo.restype = ctypes.c_int
        process = kernel32.GetCurrentProcess()
        ok = psapi.GetProcessMemoryInfo(
            process, ctypes.byref(counters), counters.cb
        )
        if not ok:
            raise ctypes.WinError()
        return int(counters.WorkingSetSize), int(counters.PeakWorkingSetSize)

    try:
        import resource

        peak = int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
        if sys.platform != "darwin":
            peak *= 1024
        return None, peak
    except (ImportError, OSError):
        return None, None


def physical_memory_bytes() -> int | None:
    if os.name == "nt":
        class MemoryStatusEx(ctypes.Structure):
            _fields_ = [
                ("dwLength", ctypes.c_ulong),
                ("dwMemoryLoad", ctypes.c_ulong),
                ("ullTotalPhys", ctypes.c_ulonglong),
                ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong),
                ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong),
                ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
            ]

        status = MemoryStatusEx()
        status.dwLength = ctypes.sizeof(status)
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.GlobalMemoryStatusEx.argtypes = [ctypes.POINTER(MemoryStatusEx)]
        kernel32.GlobalMemoryStatusEx.restype = ctypes.c_int
        if not kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return None
        return int(status.ullTotalPhys)
    try:
        return int(os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES"))
    except (AttributeError, OSError, ValueError):
        return None


def child_payload(extra: dict[str, Any], started: float) -> dict[str, Any]:
    current, peak = current_and_peak_rss_bytes()
    return {
        **extra,
        "python_version": platform.python_version(),
        "probe_elapsed_ms": rounded((time.perf_counter() - started) * 1000),
        "ready_rss_bytes": current,
        "peak_rss_bytes": peak,
    }


def install_gui_import_stubs() -> None:
    """Install only the two GUI modules used by the Python test harness."""
    import types

    translator = types.ModuleType("gui.util.translator")

    class Translator:
        @staticmethod
        def tr(value: Any) -> Any:
            return value

        @staticmethod
        def undo(value: Any) -> Any:
            return value

    translator.baasTranslator = Translator()
    customized_ui = types.ModuleType("gui.util.customized_ui")
    customized_ui.BoundComponent = object
    sys.modules[translator.__name__] = translator
    sys.modules[customized_ui.__name__] = customized_ui


def run_child_probe(name: str, python_repo: Path | None, iterations: int) -> int:
    started = time.perf_counter()
    if name == "empty_startup":
        payload = child_payload({"probe": name}, started)
    elif name == "cv2_numpy_import":
        import cv2
        import numpy

        payload = child_payload(
            {
                "probe": name,
                "cv2_version": cv2.__version__,
                "numpy_version": numpy.__version__,
            },
            started,
        )
    elif name == "service_app_import":
        if python_repo is None:
            raise ValueError("--python-repo is required for service_app_import")
        sys.path.insert(0, str(python_repo))
        os.chdir(python_repo)
        import importlib

        service_app = importlib.import_module("service.app")
        from service.api.state import context

        payload = child_payload(
            {
                "probe": name,
                "route_count": len(service_app.app.routes),
                "service_context_materialized": context._context is not None,
            },
            started,
        )
    elif name in {"legacy_module_match", "service_injected_match"}:
        if python_repo is None:
            raise ValueError(f"--python-repo is required for {name}")
        sys.path.insert(0, str(python_repo))
        os.chdir(python_repo)
        install_gui_import_stubs()
        import cv2
        import numpy as np
        from module import cafe_reward

        service_injection_applied = name == "service_injected_match"
        if service_injection_applied:
            from service import injection

            injection._patch_cafe_reward()
            cafe_reward._happy_face_templates = None

        template = cv2.imread("src/images/CN/cafe/happy_face1.png")
        if template is None:
            raise FileNotFoundError("src/images/CN/cafe/happy_face1.png")
        height, width = template.shape[:2]
        image = np.full((720, 1280, 3), 40, dtype=np.uint8)
        x, y = 300, 200
        image[y : y + height, x : x + width] = template
        call_samples: list[float] = []
        result_counts: list[int] = []
        checksum = 0
        for _ in range(iterations):
            call_started = time.perf_counter()
            result = cafe_reward.match(image)
            call_samples.append((time.perf_counter() - call_started) * 1000)
            result_counts.append(len(result))
            checksum += sum(point[0] * 10000 + point[1] for point in result)
        payload = child_payload(
            {
                "probe": name,
                "algorithm_iterations": iterations,
                "algorithm_call_ms": [rounded(value) for value in call_samples],
                "result_counts": result_counts,
                "result_checksum": checksum,
                "input_shape": list(image.shape),
                "template_shape": list(template.shape),
                "gui_stubs_injected": True,
                "service_injection_applied": service_injection_applied,
                "service_injection_marker": bool(
                    getattr(cafe_reward, "_baas_service_injected", False)
                ),
                "implementation_module": cafe_reward.match.__module__,
                "implementation_line": cafe_reward.match.__code__.co_firstlineno,
            },
            started,
        )
    else:
        raise ValueError(f"unknown child probe: {name}")
    sys.stdout.write(json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n")
    return 0


def scan_directory(path: Path) -> dict[str, Any]:
    if not path.is_dir():
        return {"status": "missing", "file_count": 0, "logical_bytes": 0}
    count = 0
    total = 0
    for directory, directories, filenames in os.walk(path, topdown=True, followlinks=False):
        directories.sort()
        for filename in sorted(filenames):
            candidate = Path(directory) / filename
            try:
                stat = candidate.stat(follow_symlinks=False)
            except OSError:
                continue
            count += 1
            total += stat.st_size
    return {"status": "ok", "file_count": count, "logical_bytes": total}


def completed_runner(*args: Any, **kwargs: Any) -> subprocess.CompletedProcess[str]:
    return subprocess.run(*args, **kwargs)


def run_command(
    command: list[str], runner: CommandRunner, timeout: float
) -> tuple[subprocess.CompletedProcess[str] | None, float, str | None]:
    started = time.perf_counter()
    try:
        process = runner(
            command,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env={**os.environ, "PYTHONIOENCODING": "utf-8"},
            timeout=timeout,
        )
        return process, (time.perf_counter() - started) * 1000, None
    except subprocess.TimeoutExpired:
        return None, (time.perf_counter() - started) * 1000, f"timed out after {timeout:g}s"
    except OSError as error:
        return None, (time.perf_counter() - started) * 1000, f"could not launch probe: {error}"


def compact_diagnostic(process: subprocess.CompletedProcess[str]) -> str:
    value = (process.stderr or process.stdout or "probe returned no diagnostic").strip()
    lines = [line.strip() for line in value.splitlines() if line.strip()]
    return lines[-1][:500] if lines else "probe returned no diagnostic"


def parse_child_output(process: subprocess.CompletedProcess[str]) -> dict[str, Any]:
    if process.returncode != 0:
        raise RuntimeError(compact_diagnostic(process))
    lines = [line for line in process.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("probe produced no JSON output")
    try:
        payload = json.loads(lines[-1])
    except json.JSONDecodeError as error:
        raise RuntimeError(f"probe produced invalid JSON: {error.msg}") from error
    if not isinstance(payload, dict):
        raise RuntimeError("probe JSON root must be an object")
    return payload


def tracked_sizes(repo: Path, runner: CommandRunner, timeout: float) -> dict[str, Any]:
    process, _, diagnostic = run_command(
        ["git", "-C", str(repo), "ls-files", "-z"], runner, timeout
    )
    if process is None:
        raise RuntimeError(diagnostic)
    if process.returncode != 0:
        raise RuntimeError(f"git ls-files failed: {compact_diagnostic(process)}")
    names = [name for name in process.stdout.split("\0") if name]
    tracked_count = 0
    tracked_bytes = 0
    python_count = 0
    python_bytes = 0
    for name in names:
        path = repo / name
        try:
            size = path.stat(follow_symlinks=False).st_size
        except OSError:
            continue
        tracked_count += 1
        tracked_bytes += size
        if name.lower().endswith(".py") and not name.startswith(("toolkit/", ".venv/")):
            python_count += 1
            python_bytes += size
    return {
        "git_tracked": {
            "status": "ok",
            "file_count": tracked_count,
            "logical_bytes": tracked_bytes,
        },
        "python_source_tracked": {
            "status": "ok",
            "definition": "tracked *.py excluding toolkit/ and .venv/",
            "file_count": python_count,
            "logical_bytes": python_bytes,
        },
        ".venv": scan_directory(repo / ".venv"),
        "toolkit": scan_directory(repo / "toolkit"),
        "src": scan_directory(repo / "src"),
    }


def git_metadata(repo: Path, runner: CommandRunner, timeout: float) -> dict[str, Any]:
    values: dict[str, str] = {}
    for key, args in (
        ("revision", ["rev-parse", "HEAD"]),
        ("status", ["status", "--porcelain"]),
    ):
        process, _, diagnostic = run_command(["git", "-C", str(repo), *args], runner, timeout)
        if process is None:
            raise RuntimeError(diagnostic)
        if process.returncode != 0:
            raise RuntimeError(f"git {args[0]} failed: {compact_diagnostic(process)}")
        values[key] = process.stdout.strip()
    return {"revision": values["revision"], "clean": not bool(values["status"])}


def relative_executable(repo: Path, executable: Path) -> str:
    try:
        return normalize_path(executable.resolve().relative_to(repo.resolve()))
    except ValueError:
        return normalize_path(executable.name)


class BaselineMeasurer:
    def __init__(
        self,
        repo: Path,
        executable: Path,
        repetitions: int,
        algorithm_iterations: int,
        timeout: float,
        runner: CommandRunner = completed_runner,
    ) -> None:
        self.repo = repo
        self.executable = executable
        self.repetitions = repetitions
        self.algorithm_iterations = algorithm_iterations
        self.timeout = timeout
        self.runner = runner
        self.script = Path(__file__).resolve()

    def probe_command(self, name: str) -> list[str]:
        if name == "empty_startup":
            return [str(self.executable), "-I", "-S", "-c", "pass"]
        command = [str(self.executable), "-I"]
        command.extend([str(self.script), "--child-probe", name])
        if name in {"service_app_import", "legacy_module_match", "service_injected_match"}:
            command.extend(["--python-repo", str(self.repo)])
        if name in {"legacy_module_match", "service_injected_match"}:
            command.extend(["--algorithm-iterations", str(self.algorithm_iterations)])
        return command

    def measure_probe(self, name: str) -> dict[str, Any]:
        payloads: list[dict[str, Any]] = []
        walls: list[float] = []
        for _ in range(self.repetitions):
            process, wall, diagnostic = run_command(
                self.probe_command(name), self.runner, self.timeout
            )
            if process is None:
                return {"status": "error", "diagnostic": diagnostic}
            if name == "empty_startup":
                if process.returncode != 0:
                    return {"status": "error", "diagnostic": compact_diagnostic(process)}
                walls.append(wall)
                continue
            try:
                payload = parse_child_output(process)
            except RuntimeError as error:
                return {"status": "error", "diagnostic": str(error)}
            payloads.append(payload)
            walls.append(wall)

        if name == "empty_startup":
            return {
                "status": "ok",
                "repetitions": self.repetitions,
                "process_wall_ms": summarize(walls, "ms"),
                "probe_elapsed_ms": None,
                "ready_rss_bytes": None,
                "peak_rss_bytes": None,
            }

        result: dict[str, Any] = {
            "status": "ok",
            "repetitions": self.repetitions,
            "python_version": payloads[0]["python_version"],
            "process_wall_ms": summarize(walls, "ms"),
            "probe_elapsed_ms": summarize(
                [payload["probe_elapsed_ms"] for payload in payloads], "ms"
            ),
            "ready_rss_bytes": summarize(
                [payload["ready_rss_bytes"] for payload in payloads if payload["ready_rss_bytes"] is not None],
                "bytes",
            )
            if any(payload["ready_rss_bytes"] is not None for payload in payloads)
            else None,
            "peak_rss_bytes": summarize(
                [payload["peak_rss_bytes"] for payload in payloads if payload["peak_rss_bytes"] is not None],
                "bytes",
            )
            if any(payload["peak_rss_bytes"] is not None for payload in payloads)
            else None,
        }
        if name == "cv2_numpy_import":
            result["versions"] = {
                "cv2": payloads[0]["cv2_version"],
                "numpy": payloads[0]["numpy_version"],
            }
        elif name == "service_app_import":
            result["route_count"] = payloads[0]["route_count"]
            result["service_context_materialized"] = any(
                payload["service_context_materialized"] for payload in payloads
            )
            if result["service_context_materialized"]:
                return {
                    "status": "error",
                    "diagnostic": "service.app import unexpectedly materialized ServiceContext",
                }
        elif name in {"legacy_module_match", "service_injected_match"}:
            calls = [value for payload in payloads for value in payload["algorithm_call_ms"]]
            injection_applied = name == "service_injected_match"
            if any(
                payload["service_injection_applied"] != injection_applied
                or payload["service_injection_marker"] != injection_applied
                for payload in payloads
            ):
                return {
                    "status": "error",
                    "diagnostic": f"{name} did not preserve the requested service injection mode",
                }
            result.update(
                {
                    "scope": (
                        "legacy module.cafe_reward.match with test-only GUI import stubs"
                        if not injection_applied
                        else "production service-injected cafe_reward.match with test-only GUI import stubs"
                    ),
                    "algorithm_iterations_per_process": self.algorithm_iterations,
                    "algorithm_call_ms": summarize(calls, "ms"),
                    "first_algorithm_call_ms": summarize(
                        [payload["algorithm_call_ms"][0] for payload in payloads], "ms"
                    ),
                    "subsequent_algorithm_call_ms": summarize(
                        [
                            value
                            for payload in payloads
                            for value in payload["algorithm_call_ms"][1:]
                        ],
                        "ms",
                    )
                    if self.algorithm_iterations > 1
                    else None,
                    "throughput_calls_per_second": summarize(
                        [1000.0 / value for value in calls if value > 0], "calls/s"
                    ),
                    "input_shape": payloads[0]["input_shape"],
                    "template_shape": payloads[0]["template_shape"],
                    "result_counts": [payload["result_counts"] for payload in payloads],
                    "result_checksums": [payload["result_checksum"] for payload in payloads],
                    "gui_stubs_injected": True,
                    "service_injection_applied": injection_applied,
                    "implementation": {
                        "module": payloads[0]["implementation_module"],
                        "line": payloads[0]["implementation_line"],
                    },
                }
            )
        return result

    def generate(self) -> dict[str, Any]:
        probes = {name: self.measure_probe(name) for name in PROBE_NAMES}
        return {
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
                "benchmark_python_version": next(
                    (
                        probe.get("python_version")
                        for name, probe in probes.items()
                        if name != "empty_startup" and probe["status"] == "ok"
                    ),
                    None,
                ),
                "benchmark_python": relative_executable(self.repo, self.executable),
            },
            "configuration": {
                "repetitions": self.repetitions,
                "algorithm_iterations_per_process": self.algorithm_iterations,
                "timeout_seconds_per_process": self.timeout,
                "max_concurrent_probe_processes": 1,
                "parallel_execution": False,
                "os_file_cache_flushed": False,
            },
            "scope": {
                "measured": [
                    "fresh isolated Python -I -S -c pass process startup",
                    "isolated cv2 and numpy import readiness",
                    "isolated service.app import readiness without lifespan",
                    "legacy module.cafe_reward.match algorithm with test-only GUI import stubs",
                    "production service-injected cafe_reward.match algorithm with test-only GUI import stubs",
                    "logical repository, Python source, .venv, toolkit, and src sizes",
                ],
                "not_measured": [
                    "FastAPI lifespan or ServiceContext.startup",
                    "device or emulator discovery and I/O",
                    "OCR initialization or inference",
                    "Tauri end-to-end startup",
                    "packaged installer or native runtime size",
                ],
            },
            "sizes": tracked_sizes(self.repo, self.runner, self.timeout),
            "probes": probes,
            "summary": {
                "successful_probes": sum(value["status"] == "ok" for value in probes.values()),
                "failed_probes": sum(value["status"] != "ok" for value in probes.values()),
            },
        }


def find_default_python(repo: Path) -> Path | None:
    candidates = [repo / ".venv" / "Scripts" / "python.exe", repo / ".venv" / "bin" / "python"]
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python-repo", type=Path, help="path to the baas-dev checkout")
    parser.add_argument("--python-executable", type=Path, help="Python 3.11 executable (defaults to repo .venv)")
    parser.add_argument("--output", type=Path, help="JSON output path; stdout when omitted")
    parser.add_argument("--repetitions", type=int, help=f"fresh processes per probe (default {DEFAULT_REPETITIONS})")
    parser.add_argument("--algorithm-iterations", type=int, help=f"match calls in each process (default {DEFAULT_ALGORITHM_ITERATIONS})")
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds allowed per child process")
    parser.add_argument("--quick", action="store_true", help="use two processes and one algorithm call unless overridden")
    parser.add_argument("--child-probe", choices=PROBE_NAMES, help=argparse.SUPPRESS)
    return parser


def main(argv: list[str] | None = None, *, runner: CommandRunner = completed_runner) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.child_probe:
        try:
            return run_child_probe(args.child_probe, args.python_repo, args.algorithm_iterations or 1)
        except Exception as error:  # child boundary: concise diagnostic for the parent
            print(f"{type(error).__name__}: {error}", file=sys.stderr)
            return 1

    if args.python_repo is None:
        parser.error("--python-repo is required")
    repo = args.python_repo.resolve()
    if not repo.is_dir() or not (repo / "module" / "cafe_reward.py").is_file():
        parser.error("--python-repo must be a baas-dev checkout containing module/cafe_reward.py")
    executable = args.python_executable or find_default_python(repo)
    if executable is None:
        parser.error("no repo .venv Python found; pass --python-executable")
    executable = executable.resolve()
    if not executable.is_file():
        parser.error(f"Python executable does not exist: {executable}")
    repetitions = args.repetitions or (QUICK_REPETITIONS if args.quick else DEFAULT_REPETITIONS)
    iterations = args.algorithm_iterations or (
        QUICK_ALGORITHM_ITERATIONS if args.quick else DEFAULT_ALGORITHM_ITERATIONS
    )
    if repetitions < 1 or iterations < 1 or args.timeout <= 0:
        parser.error("repetitions, algorithm iterations, and timeout must be positive")

    try:
        report = BaselineMeasurer(
            repo, executable, repetitions, iterations, args.timeout, runner
        ).generate()
    except RuntimeError as error:
        print(f"measurement setup failed: {error}", file=sys.stderr)
        return 2
    rendered = stable_json(report)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(rendered)
    if report["summary"]["failed_probes"]:
        print(
            f"{report['summary']['failed_probes']} probe(s) failed; inspect JSON diagnostics",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
