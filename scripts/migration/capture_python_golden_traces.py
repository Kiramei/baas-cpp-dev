#!/usr/bin/env python3
"""Capture deterministic offline Python workflow traces for C++ parity.

The controller is standard-library-only.  Its isolated child imports the
audited ``baas-dev`` trace worktree and executes only fixture-backed host paths;
it never starts the service lifespan, contacts a device, initializes OCR, or
uses the network.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import datetime
import hashlib
import io
import json
import os
import platform
import re
import subprocess
import sys
import tempfile
import types
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Callable, Mapping, Sequence


SCHEMA = "baas.python-golden-traces"
SCHEMA_VERSION = 1
GENERATOR_VERSION = "1.0.0"
TRACE_SCHEMA = "baas.parity-trace"
TRACE_SCHEMA_VERSION = 1
PRODUCTION_COMMIT = "75bbacb545bc87e9510d85cbe8034f9180397004"
TRACE_COMMIT = "3a8f58585b69bf7cf54fe66115352b41f4094aa3"
WORKFLOW_IDS = (
    "configuration.snapshot_patch",
    "image.cafe_match",
    "scheduler.queue_rebuild",
    "orchestration.grid_actions",
)
MAX_WORKFLOWS = 8
MAX_RECORDS_PER_WORKFLOW = 64
MAX_RECORD_BYTES = 64 * 1024
MAX_EVIDENCE_BYTES = 256 * 1024
DEFAULT_TIMEOUT_SECONDS = 60.0
SECRET_SENTINEL = "GOLDEN-TRACE-SECRET-MUST-NOT-APPEAR"
WINDOWS_ABSOLUTE = re.compile(r"(?i)\b[a-z]:[\\/]")
SENSITIVE_KEY = re.compile(
    r"(?:password|passwd|pwd|token|secret|authorization|cookie|"
    r"api[_-]?key|private[_-]?key|access[_-]?key)",
    re.IGNORECASE,
)


def stable_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def compact_json(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def sha256_json(value: Any) -> str:
    return hashlib.sha256(compact_json(value)).hexdigest()


def install_gui_import_stubs() -> None:
    """Install only the GUI types imported by the exercised modules."""
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


class StepClock:
    def __init__(self, start: int, step: int = 10) -> None:
        self.value = start
        self.step = step

    def __call__(self) -> int:
        value = self.value
        self.value += self.step
        return value


class CapturedLogger:
    def __init__(self) -> None:
        self.messages: list[list[str]] = []

    def _record(self, level: str, message: Any) -> None:
        self.messages.append([level, str(message)])

    def info(self, message: Any) -> None:
        self._record("info", message)

    def warning(self, message: Any) -> None:
        self._record("warning", message)

    def error(self, message: Any) -> None:
        self._record("error", message)


def _trace_records(
    workflow_id: str,
    clock_start: int,
    callback: Callable[[Any], dict[str, Any]],
) -> list[dict[str, Any]]:
    from core.parity_trace import ParityTraceRecorder

    stream = io.StringIO()
    clock = StepClock(clock_start)
    recorder = ParityTraceRecorder(
        stream,
        session_id=f"golden-{workflow_id}",
        task_id=workflow_id,
        metadata={"fixture": workflow_id, "offline": True},
        monotonic_ns=clock,
        wall_time_ns=lambda: 1_700_000_000_000_000_000,
        clock_id="golden.step-clock.v1",
        rng_id="golden.no-rng.v1",
        rng_injected=True,
        max_events=MAX_RECORDS_PER_WORKFLOW,
        max_event_bytes=MAX_RECORD_BYTES,
    )
    try:
        result = callback(recorder)
        recorder.record_task("task.result", result)
    finally:
        recorder.close()
    return [json.loads(line) for line in stream.getvalue().splitlines()]


def _configuration_workflow(_repo: Path, recorder: Any) -> dict[str, Any]:
    from service.conf import manager as manager_module
    from service.conf.manager import ConfigManager

    fixed_timestamp = 1_700_000_000_000.0
    manager_module.file_mtime_ms = lambda _path: fixed_timestamp
    manager_module.unix_timestamp_ms = lambda: fixed_timestamp + 1_000.0

    async def scenario() -> dict[str, Any]:
        with tempfile.TemporaryDirectory(prefix="baas-golden-config-") as directory:
            root = Path(directory)
            config_dir = root / "config" / "fixture_config"
            config_dir.mkdir(parents=True)
            initial = {
                "name": "Golden Fixture",
                "server": "CN-official",
                "difficulty": 1,
                "unknown_extension": {"preserve": True},
                "credentials": {"api_token": SECRET_SENTINEL},
            }
            (config_dir / "config.json").write_text(
                json.dumps(initial, ensure_ascii=False), encoding="utf-8"
            )
            (config_dir / "event.json").write_text("[]", encoding="utf-8")
            manager = ConfigManager(root)
            manager.set_loop(asyncio.get_running_loop())
            updates = await manager.subscribe_updates()

            with recorder.host_operation(
                "config.snapshot",
                {"resource": "config", "resource_id": "fixture_config"},
            ) as span:
                snapshot = await manager.get_snapshot("config", "fixture_config")
                span.set_result({"data": snapshot.data, "timestamp": snapshot.timestamp})

            operations = [
                {"op": "replace", "path": "/difficulty", "value": 3},
                {"op": "add", "path": "/new_option", "value": ["a", "b"]},
            ]
            with recorder.host_operation(
                "config.apply_patch",
                {
                    "resource": "config",
                    "resource_id": "fixture_config",
                    "operations": operations,
                    "timestamp": snapshot.timestamp,
                },
            ) as span:
                updated = await manager.apply_patch(
                    "config",
                    "fixture_config",
                    operations,
                    snapshot.timestamp,
                    origin="frontend",
                )
                push = await asyncio.wait_for(updates.get(), timeout=1.0)
                persisted = json.loads(
                    (config_dir / "config.json").read_text(encoding="utf-8")
                )
                span.set_result(
                    {
                        "data": updated.data,
                        "timestamp": updated.timestamp,
                        "push": push,
                        "persisted_equals_snapshot": persisted == updated.data,
                        "unknown_field_retained": persisted.get("unknown_extension")
                        == {"preserve": True},
                    }
                )
            manager.unsubscribe_updates(updates)
            return {
                "patched_difficulty": updated.data["difficulty"],
                "new_option": updated.data["new_option"],
                "unknown_field_retained": updated.data["unknown_extension"]["preserve"],
            }

    return asyncio.run(scenario())


def _image_workflow(repo: Path, recorder: Any) -> dict[str, Any]:
    import cv2
    import numpy as np
    from core.parity_trace import ImageFixture
    from module import cafe_reward
    from service import injection

    os.environ.pop("BAAS_ANDROID", None)
    injection._patch_cafe_reward()
    cafe_reward._happy_face_templates = None
    template_ref = "src/images/CN/cafe/happy_face1.png"
    template = cv2.imread(str(repo / template_ref))
    if template is None:
        raise FileNotFoundError(template_ref)
    height, width = template.shape[:2]
    image = np.full((720, 1280, 3), 40, dtype=np.uint8)
    x, y = 300, 200
    image[y : y + height, x : x + width] = template
    with recorder.host_operation(
        "image.cafe_reward.match",
        {
            "image": ImageFixture(image, "fixtures/cafe-composite-v1"),
            "template": ImageFixture(template, template_ref),
            "algorithm": "service-injected",
        },
    ) as span:
        matches = cafe_reward.match(image)
        span.set_result(
            {
                "matches": matches,
                "service_injection_marker": bool(
                    getattr(cafe_reward, "_baas_service_injected", False)
                ),
            }
        )
    expected = [int(x + width / 2), int(y + height / 2 + 58)]
    return {
        "match_count": len(matches),
        "expected_match": expected,
        "expected_match_present": any(
            abs(point[0] - expected[0]) <= 4 and abs(point[1] - expected[1]) <= 4
            for point in matches
        ),
        "input_shape": list(image.shape),
        "template_shape": list(template.shape),
    }


def _scheduler_workflow(_repo: Path, recorder: Any) -> dict[str, Any]:
    from core import scheduler as scheduler_module

    fixed_epoch = 1_700_000_000.0
    scheduler_module.time.time = lambda: fixed_epoch

    class FixedDateTime(datetime.datetime):
        @classmethod
        def now(cls, tz: datetime.tzinfo | None = None) -> "FixedDateTime":
            return cls(2023, 11, 14, 12, 0, 0, tzinfo=tz)

    scheduler_module.datetime = FixedDateTime

    class Signal:
        def __init__(self) -> None:
            self.events: list[Any] = []

        def emit(self, value: Any) -> None:
            self.events.append(value)

    def event(name: str, func_name: str, priority: int) -> dict[str, Any]:
        return {
            "event_name": name,
            "func_name": func_name,
            "enabled": True,
            "next_tick": 0,
            "priority": priority,
            "pre_task": [],
            "post_task": [],
            "disabled_time_range": [],
            "interval": 3600,
            "daily_reset": [[4, 0, 0]],
        }

    with tempfile.TemporaryDirectory(prefix="baas-golden-scheduler-") as directory:
        config_dir = Path(directory)
        events = [event("Task B", "task_b", 2), event("Task A", "task_a", 1)]
        (config_dir / "event.json").write_text(
            json.dumps(events, ensure_ascii=False), encoding="utf-8"
        )
        signal = Signal()
        scheduler = scheduler_module.Scheduler(signal, str(config_dir))
        with recorder.host_operation("scheduler.queue_rebuild", {"passes": 2}) as span:
            scheduler.update_valid_task_queue()
            scheduler.update_valid_task_queue()
            span.set_result(
                {
                    "waiting": scheduler.getWaitingTaskList(),
                    "queue": scheduler._valid_task_queue,
                }
            )
        with recorder.host_operation("scheduler.heartbeat", {}) as span:
            selected = scheduler.heartbeat()
            span.set_result(
                {
                    "selected": selected,
                    "remaining": scheduler.getWaitingTaskList(),
                    "signals": signal.events,
                }
            )
        return {
            "selected": selected["current_task"] if selected else None,
            "remaining": scheduler.getWaitingTaskList(),
            "signals": signal.events,
        }


def _orchestration_workflow(_repo: Path, recorder: Any) -> dict[str, Any]:
    from core.Baas_thread import Baas_thread
    from module.explore_tasks import task_utils

    task_utils.get_formation_index = lambda _self: 2
    popup_calls: list[bool] = []
    task_utils.handle_task_pop_ups = lambda _self, loading: popup_calls.append(bool(loading))
    task_utils.confirm_teleport = lambda _self: None
    interval_changes: list[float] = []
    physical_clicks: list[dict[str, Any]] = []

    thread = Baas_thread.__new__(Baas_thread)
    thread.flag_run = True
    thread.logger = CapturedLogger()
    thread.config = SimpleNamespace(screenshot_interval=0.25)
    thread.set_parity_trace(recorder)

    def set_interval(_self: Any, value: float) -> None:
        interval_changes.append(value)
        _self.screenshot_interval = value

    def click_host(
        _self: Any,
        x: int,
        y: int,
        count: int = 1,
        rate: float = 0,
        duration: float = 0,
        wait_over: bool = False,
    ) -> None:
        physical_clicks.append(
            {
                "x": x,
                "y": y,
                "count": count,
                "rate": rate,
                "duration": duration,
                "wait_over": wait_over,
            }
        )

    thread.set_screenshot_interval = types.MethodType(set_interval, thread)
    thread._click_untraced = types.MethodType(click_host, thread)
    actions = [
        {"t": "click", "p": [320, 240], "description": "fixture click"},
        {"t": "choose_and_change", "p": [600, 350]},
    ]
    with recorder.host_operation("orchestration.run_task_action", {"actions": actions}) as span:
        task_utils.run_task_action(thread, actions)
        span.set_result(
            {
                "physical_clicks": physical_clicks,
                "interval_changes": interval_changes,
                "popup_calls": popup_calls,
                "log_messages": thread.logger.messages,
            }
        )
    return {
        "physical_click_count": len(physical_clicks),
        "interval_changes": interval_changes,
        "popup_calls": popup_calls,
    }


def run_child(repo: Path) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="strict")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    sys.path.insert(0, str(repo))
    os.chdir(repo)
    install_gui_import_stubs()
    workflows: list[dict[str, Any]] = []
    definitions = (
        ("configuration.snapshot_patch", "configuration", "service.conf.ConfigManager filesystem snapshot/JSON Patch/push", "service.conf.manager.ConfigManager.apply_patch", _configuration_workflow),
        ("image.cafe_match", "image_matching", "production service-injected OpenCV template matching", "service.injection._patch_cafe_reward -> module.cafe_reward.match", _image_workflow),
        ("scheduler.queue_rebuild", "scheduling", "core scheduler event-file filtering, ordering, and heartbeat", "core.scheduler.Scheduler.heartbeat", _scheduler_workflow),
        ("orchestration.grid_actions", "operation_orchestration", "real grid-action dispatcher crossing the traced click facade into a deterministic host double", "module.explore_tasks.task_utils.run_task_action", _orchestration_workflow),
    )
    for index, (workflow_id, category, boundary, entrypoint, callback) in enumerate(definitions):
        records = _trace_records(
            workflow_id,
            (index + 1) * 1_000,
            lambda recorder, callback=callback: callback(repo, recorder),
        )
        workflows.append(
            {
                "id": workflow_id,
                "category": category,
                "boundary": boundary,
                "entrypoint": entrypoint,
                "offline": True,
                "records": records,
            }
        )

    import cv2
    import numpy

    payload = {
        "runtime": {
            "python": platform.python_version(),
            "cv2": cv2.__version__,
            "numpy": numpy.__version__,
        },
        "workflows": workflows,
    }
    sys.stdout.write(json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n")
    return 0


def _run_git(repo: Path, args: Sequence[str], timeout: float) -> str:
    try:
        completed = subprocess.run(
            ["git", "-C", str(repo), *args],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RuntimeError(f"git {' '.join(args)} failed to launch: {error}") from error
    if completed.returncode != 0:
        diagnostic = (completed.stderr or completed.stdout).strip().splitlines()
        raise RuntimeError(
            f"git {' '.join(args)} failed: {(diagnostic[-1] if diagnostic else 'no diagnostic')[:300]}"
        )
    return completed.stdout.strip()


def source_metadata(repo: Path, timeout: float) -> dict[str, Any]:
    revision = _run_git(repo, ("rev-parse", "HEAD"), timeout)
    parent = _run_git(repo, ("rev-parse", "HEAD^"), timeout)
    status = _run_git(repo, ("status", "--porcelain"), timeout)
    if revision != TRACE_COMMIT:
        raise RuntimeError(
            f"trace checkout revision mismatch: expected {TRACE_COMMIT}, got {revision}"
        )
    if parent != PRODUCTION_COMMIT:
        raise RuntimeError(
            f"trace parent mismatch: expected {PRODUCTION_COMMIT}, got {parent}"
        )
    if status:
        raise RuntimeError("trace checkout is dirty; refusing to capture mutable source")
    return {
        "repository": "Kiramei/baas-dev",
        "production_commit": PRODUCTION_COMMIT,
        "trace_commit": TRACE_COMMIT,
        "trace_parent_verified": True,
        "trace_checkout_clean": True,
    }


def find_default_python(repo: Path) -> Path | None:
    candidates = [
        repo / ".venv" / "Scripts" / "python.exe",
        repo / ".venv" / "bin" / "python",
        repo.parent.parent / "baas-dev" / ".venv" / "Scripts" / "python.exe",
        repo.parent.parent / "baas-dev" / ".venv" / "bin" / "python",
    ]
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def compact_diagnostic(completed: subprocess.CompletedProcess[str]) -> str:
    text = (completed.stderr or completed.stdout or "child returned no diagnostic").strip()
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return (" | ".join(lines[-3:]) if lines else "child returned no diagnostic")[:500]


def capture_child(
    script: Path,
    repo: Path,
    executable: Path,
    timeout: float,
) -> dict[str, Any]:
    try:
        completed = subprocess.run(
            [str(executable), "-I", str(script), "--child", "--python-repo", str(repo)],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env={**os.environ, "PYTHONIOENCODING": "utf-8"},
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"golden trace child timed out after {timeout:g}s") from error
    except OSError as error:
        raise RuntimeError(f"could not launch golden trace child: {error}") from error
    if completed.returncode != 0:
        raise RuntimeError(f"golden trace child failed: {compact_diagnostic(completed)}")
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("golden trace child produced no JSON")
    try:
        payload = json.loads(lines[-1])
    except json.JSONDecodeError as error:
        raise RuntimeError(f"golden trace child produced invalid JSON: {error.msg}") from error
    if not isinstance(payload, dict):
        raise RuntimeError("golden trace child JSON root is not an object")
    return payload


def _event_summary(records: list[dict[str, Any]]) -> dict[str, Any]:
    event_counts: dict[str, int] = {}
    operations: list[str] = []
    for record in records:
        event = record["event"]
        event_counts[event] = event_counts.get(event, 0) + 1
        operation = record.get("payload", {}).get("operation")
        if isinstance(operation, str) and operation not in operations:
            operations.append(operation)
    return {
        "record_count": len(records),
        "event_counts": dict(sorted(event_counts.items())),
        "operations": sorted(operations),
        "records_sha256": sha256_json(records),
    }


def build_evidence(source: dict[str, Any], child: dict[str, Any]) -> dict[str, Any]:
    raw_workflows = child.get("workflows")
    if not isinstance(raw_workflows, list):
        raise RuntimeError("child workflows must be an array")
    workflows: list[dict[str, Any]] = []
    for raw in raw_workflows:
        if not isinstance(raw, dict) or not isinstance(raw.get("records"), list):
            raise RuntimeError("child workflow has invalid shape")
        workflow = dict(raw)
        workflow["summary"] = _event_summary(workflow["records"])
        workflows.append(workflow)
    evidence = {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "generator_version": GENERATOR_VERSION,
        "source": {**source, "runtime": child.get("runtime")},
        "limits": {
            "max_workflows": MAX_WORKFLOWS,
            "max_records_per_workflow": MAX_RECORDS_PER_WORKFLOW,
            "max_record_bytes": MAX_RECORD_BYTES,
            "max_evidence_bytes": MAX_EVIDENCE_BYTES,
        },
        "scope": {
            "executed": [
                "configuration snapshot, JSON Patch persistence, and update push",
                "service-injected offline image template matching",
                "scheduler queue rebuild and heartbeat ordering",
                "grid-action dispatch through the traced click host facade",
            ],
            "not_executed": [
                "device, emulator, ADB, scrcpy, Nemu, or uiautomator I/O",
                "OCR initialization or inference",
                "network access or remote repository operations",
                "FastAPI service lifespan or WebSocket/pipe transport",
                "Tauri integration or packaged application startup",
            ],
        },
        "workflows": workflows,
        "summary": {
            "workflow_count": len(workflows),
            "workflow_ids": [workflow["id"] for workflow in workflows],
            "categories": sorted(workflow["category"] for workflow in workflows),
            "record_count": sum(len(workflow["records"]) for workflow in workflows),
            "workflows_sha256": sha256_json(workflows),
        },
    }
    validate_evidence(evidence)
    encoded = stable_json(evidence).encode("utf-8")
    if len(encoded) > MAX_EVIDENCE_BYTES:
        raise RuntimeError(
            f"evidence is {len(encoded)} bytes, exceeding {MAX_EVIDENCE_BYTES}"
        )
    return evidence


def _strings(value: Any):
    if isinstance(value, dict):
        for key, child in value.items():
            yield str(key)
            yield from _strings(child)
    elif isinstance(value, list):
        for child in value:
            yield from _strings(child)
    elif isinstance(value, str):
        yield value


def _validate_sensitive_values(value: Any, pointer: str = "") -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            child_pointer = f"{pointer}/{str(key).replace('~', '~0').replace('/', '~1')}"
            if SENSITIVE_KEY.search(str(key)) and child != "<redacted>":
                raise ValueError(f"sensitive value is not redacted at {child_pointer}")
            _validate_sensitive_values(child, child_pointer)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _validate_sensitive_values(child, f"{pointer}/{index}")


def validate_evidence(evidence: Mapping[str, Any]) -> None:
    required_root = {
        "schema",
        "schema_version",
        "generator_version",
        "source",
        "limits",
        "scope",
        "workflows",
        "summary",
    }
    if set(evidence) != required_root:
        raise ValueError(f"root keys differ: {sorted(set(evidence) ^ required_root)}")
    if evidence["schema"] != SCHEMA or evidence["schema_version"] != SCHEMA_VERSION:
        raise ValueError("unsupported golden evidence schema")
    source = evidence["source"]
    if not isinstance(source, Mapping):
        raise ValueError("source must be an object")
    expected_source_keys = {
        "repository",
        "production_commit",
        "trace_commit",
        "trace_parent_verified",
        "trace_checkout_clean",
        "runtime",
    }
    if set(source) != expected_source_keys or source.get("repository") != "Kiramei/baas-dev":
        raise ValueError("source keys/repository differ from the v1 schema")
    if source.get("production_commit") != PRODUCTION_COMMIT or source.get("trace_commit") != TRACE_COMMIT:
        raise ValueError("source commits do not match audited anchors")
    if source.get("trace_parent_verified") is not True or source.get("trace_checkout_clean") is not True:
        raise ValueError("source verification flags must be true")
    runtime = source.get("runtime")
    if not isinstance(runtime, Mapping) or set(runtime) != {"python", "cv2", "numpy"}:
        raise ValueError("runtime versions differ from the v1 schema")
    if not all(isinstance(runtime[key], str) and runtime[key] for key in runtime):
        raise ValueError("runtime versions must be non-empty strings")
    limits = evidence["limits"]
    expected_limits = {
        "max_workflows": MAX_WORKFLOWS,
        "max_records_per_workflow": MAX_RECORDS_PER_WORKFLOW,
        "max_record_bytes": MAX_RECORD_BYTES,
        "max_evidence_bytes": MAX_EVIDENCE_BYTES,
    }
    if limits != expected_limits:
        raise ValueError("recorded limits differ from enforced v1 limits")
    scope = evidence["scope"]
    if not isinstance(scope, Mapping) or set(scope) != {"executed", "not_executed"}:
        raise ValueError("scope keys differ from the v1 schema")
    if not all(isinstance(scope[key], list) and all(isinstance(item, str) for item in scope[key]) for key in scope):
        raise ValueError("scope entries must be string arrays")
    workflows = evidence["workflows"]
    if not isinstance(workflows, list) or len(workflows) < 3 or len(workflows) > MAX_WORKFLOWS:
        raise ValueError("workflow count is outside the required bounds")
    if [workflow.get("id") for workflow in workflows] != list(WORKFLOW_IDS):
        raise ValueError("workflow ids/order differ from the v1 corpus")
    categories = {workflow.get("category") for workflow in workflows}
    if categories != {"configuration", "image_matching", "scheduling", "operation_orchestration"}:
        raise ValueError("representative workflow categories are incomplete")
    total_records = 0
    for workflow in workflows:
        expected_keys = {"id", "category", "boundary", "entrypoint", "offline", "records", "summary"}
        if set(workflow) != expected_keys or workflow.get("offline") is not True:
            raise ValueError(f"workflow {workflow.get('id')} has invalid keys/offline marker")
        if not all(isinstance(workflow[key], str) and workflow[key] for key in ("id", "category", "boundary", "entrypoint")):
            raise ValueError(f"workflow {workflow.get('id')} metadata must be non-empty strings")
        records = workflow["records"]
        if not isinstance(records, list) or not 2 <= len(records) <= MAX_RECORDS_PER_WORKFLOW:
            raise ValueError(f"workflow {workflow['id']} record count is invalid")
        if [record.get("seq") for record in records] != list(range(1, len(records) + 1)):
            raise ValueError(f"workflow {workflow['id']} sequence is not contiguous")
        if records[0].get("event") != "session.start" or records[-1].get("event") != "session.end":
            raise ValueError(f"workflow {workflow['id']} lacks session boundaries")
        session_ids = {record.get("session_id") for record in records}
        if len(session_ids) != 1:
            raise ValueError(f"workflow {workflow['id']} mixes sessions")
        for record in records:
            expected_record_keys = {
                "schema",
                "schema_version",
                "session_id",
                "task_id",
                "seq",
                "monotonic_ns",
                "event",
                "payload",
            }
            if set(record) != expected_record_keys:
                raise ValueError(f"workflow {workflow['id']} record keys differ")
            if record.get("schema") != TRACE_SCHEMA or record.get("schema_version") != TRACE_SCHEMA_VERSION:
                raise ValueError(f"workflow {workflow['id']} contains a foreign trace schema")
            if record.get("task_id") != workflow["id"]:
                raise ValueError(f"workflow {workflow['id']} record task id differs")
            if not isinstance(record.get("monotonic_ns"), int) or record["monotonic_ns"] < 0:
                raise ValueError(f"workflow {workflow['id']} has invalid monotonic time")
            if record.get("event") not in {
                "session.start",
                "session.end",
                "task.result",
                "host.begin",
                "host.end",
            }:
                raise ValueError(f"workflow {workflow['id']} has an unsupported event")
            if not isinstance(record.get("payload"), dict):
                raise ValueError(f"workflow {workflow['id']} payload is not an object")
            if len(compact_json(record)) > MAX_RECORD_BYTES:
                raise ValueError(f"workflow {workflow['id']} contains an oversized record")
        terminal = records[-1].get("payload", {})
        if terminal.get("dropped_events") != 0 or terminal.get("write_errors") != 0:
            raise ValueError(f"workflow {workflow['id']} dropped or failed to write events")
        begins = {
            record["payload"]["operation_id"]
            for record in records
            if record.get("event") == "host.begin"
        }
        terminals = {
            record["payload"]["operation_id"]
            for record in records
            if record.get("event") in {"host.end", "host.error", "host.cancel"}
        }
        if begins != terminals:
            raise ValueError(f"workflow {workflow['id']} has unmatched operations")
        if any(record.get("event") in {"host.error", "host.cancel"} for record in records):
            raise ValueError(f"workflow {workflow['id']} contains an error/cancel event")
        summary = workflow.get("summary")
        expected_summary_keys = {"record_count", "event_counts", "operations", "records_sha256"}
        if not isinstance(summary, Mapping) or set(summary) != expected_summary_keys:
            raise ValueError(f"workflow {workflow['id']} summary keys differ")
        expected_event_summary = _event_summary(records)
        if summary != expected_event_summary:
            raise ValueError(f"workflow {workflow['id']} summary hash differs")
        total_records += len(records)
    summary = evidence["summary"]
    if not isinstance(summary, Mapping):
        raise ValueError("summary must be an object")
    expected_root_summary_keys = {
        "workflow_count",
        "workflow_ids",
        "categories",
        "record_count",
        "workflows_sha256",
    }
    if set(summary) != expected_root_summary_keys:
        raise ValueError("summary keys differ from the v1 schema")
    if summary.get("workflow_count") != len(workflows) or summary.get("record_count") != total_records:
        raise ValueError("summary counts differ")
    if summary.get("workflows_sha256") != sha256_json(workflows):
        raise ValueError("summary workflow hash differs")
    rendered_strings = "\n".join(_strings(evidence))
    if SECRET_SENTINEL in rendered_strings:
        raise ValueError("sensitive fixture sentinel leaked into evidence")
    if WINDOWS_ABSOLUTE.search(rendered_strings):
        raise ValueError("absolute Windows path leaked into evidence")
    if "generated_at" in evidence:
        raise ValueError("generation timestamp is forbidden")
    _validate_sensitive_values(evidence)


def first_difference(expected: Any, actual: Any, pointer: str = "") -> str | None:
    if type(expected) is not type(actual):
        return pointer or "/"
    if isinstance(expected, dict):
        if set(expected) != set(actual):
            return pointer or "/"
        for key in sorted(expected):
            escaped = str(key).replace("~", "~0").replace("/", "~1")
            difference = first_difference(expected[key], actual[key], f"{pointer}/{escaped}")
            if difference:
                return difference
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return pointer or "/"
        for index, (left, right) in enumerate(zip(expected, actual)):
            difference = first_difference(left, right, f"{pointer}/{index}")
            if difference:
                return difference
        return None
    return None if expected == actual else (pointer or "/")


def generate(repo: Path, executable: Path, timeout: float) -> dict[str, Any]:
    if not repo.is_dir():
        raise RuntimeError(f"Python trace checkout is not a directory: {repo}")
    if not executable.is_file():
        raise RuntimeError(f"Python executable is not a file: {executable}")
    source = source_metadata(repo, timeout)
    child = capture_child(Path(__file__).resolve(), repo, executable, timeout)
    return build_evidence(source, child)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python-repo", type=Path, help="clean baas-dev trace checkout at the pinned trace commit")
    parser.add_argument("--python-executable", type=Path, help="Python 3.11 executable with baas-dev test dependencies")
    parser.add_argument("--output", type=Path, help="golden JSON path; stdout when omitted")
    parser.add_argument("--check", action="store_true", help="rebuild and compare --output without modifying it")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS, help="child/git timeout in seconds")
    parser.add_argument("--child", action="store_true", help=argparse.SUPPRESS)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.child:
        if args.python_repo is None:
            print("error: --python-repo is required for child mode", file=sys.stderr)
            return 2
        try:
            return run_child(args.python_repo.resolve())
        except Exception as error:
            print(f"error: child workflow failed: {type(error).__name__}: {error}", file=sys.stderr)
            return 1
    if args.python_repo is None:
        print("error: --python-repo is required", file=sys.stderr)
        return 2
    if args.timeout <= 0 or args.timeout > 600:
        print("error: --timeout must be in (0, 600]", file=sys.stderr)
        return 2
    repo = args.python_repo.resolve()
    executable = args.python_executable.resolve() if args.python_executable else find_default_python(repo)
    if executable is None:
        print("error: no Python executable found; pass --python-executable", file=sys.stderr)
        return 2
    if args.check and args.output is None:
        print("error: --check requires --output", file=sys.stderr)
        return 2
    try:
        evidence = generate(repo, executable, args.timeout)
    except (RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    rendered = stable_json(evidence)
    if args.check:
        assert args.output is not None
        if not args.output.is_file():
            print(f"error: golden evidence is missing: {args.output}", file=sys.stderr)
            return 1
        try:
            expected = json.loads(args.output.read_text(encoding="utf-8"))
            validate_evidence(expected)
        except (OSError, json.JSONDecodeError, ValueError) as error:
            print(f"error: committed golden evidence is invalid: {error}", file=sys.stderr)
            return 1
        if expected != evidence:
            pointer = first_difference(expected, evidence) or "/"
            print(
                "error: golden evidence differs at "
                f"{pointer}; committed={hashlib.sha256(stable_json(expected).encode()).hexdigest()} "
                f"rebuilt={hashlib.sha256(rendered.encode()).hexdigest()}",
                file=sys.stderr,
            )
            return 1
        print(f"verified {args.output}")
        return 0
    if args.output is None:
        sys.stdout.write(rendered)
    else:
        try:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8", newline="\n")
        except OSError as error:
            print(f"error: could not write golden evidence: {error}", file=sys.stderr)
            return 2
        print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
