from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


class Marker(Exception):
    pass


class Primary(Exception):
    pass


class Cleanup(Exception):
    pass


def python_oracle() -> dict[str, object]:
    handled_trace = ["enter", "try"]

    def handled() -> str:
        try:
            try:
                raise Marker()
            except Marker:
                handled_trace.append("catch:Marker")
                return "handled"
        finally:
            handled_trace.append("finally")

    assert handled() == "handled"
    handled_trace.append("caller:handled")

    rethrow_trace = ["enter", "try"]
    retained: Marker | None = None

    def rethrow() -> None:
        nonlocal retained
        try:
            try:
                raise Marker()
            except Marker as error:
                retained = error
                rethrow_trace.append("catch:Marker")
                raise
        finally:
            rethrow_trace.append("finally")

    same_identity = False
    try:
        rethrow()
    except Marker as error:
        rethrow_trace.append("outer:Marker")
        same_identity = error is retained

    lifo_trace: list[str] = []

    def lifo() -> str:
        try:
            try:
                lifo_trace.append("body")
                return "ok"
            finally:
                lifo_trace.append("cleanup:inner")
        finally:
            lifo_trace.append("cleanup:outer")

    assert lifo() == "ok"
    lifo_trace.append("caller:ok")

    cleanup_trace: list[str] = []
    python_cleanup = {"primary": "", "context": ""}
    try:
        try:
            cleanup_trace.append("body")
            raise Primary()
        finally:
            cleanup_trace.append("cleanup")
            raise Cleanup()
    except Cleanup as error:
        python_cleanup["primary"] = type(error).__name__
        python_cleanup["context"] = type(error.__context__).__name__

    return {
        "handled_return": handled_trace,
        "rethrow_identity": rethrow_trace,
        "same_identity": same_identity,
        "lifo_return": lifo_trace,
        "cleanup_raises": cleanup_trace,
        "python_cleanup": python_cleanup,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--package-root", required=True, type=Path)
    arguments = parser.parse_args()

    completed = subprocess.run(
        [
            str(arguments.runner),
            "--package-root",
            str(arguments.package_root),
            "--entry",
            "structured/main",
        ],
        check=False,
        capture_output=True,
    )
    assert completed.returncode == 0, completed.stdout.decode("utf-8", errors="replace")
    assert completed.stderr == b"", completed.stderr.decode("utf-8", errors="replace")
    document = json.loads(completed.stdout.decode("utf-8", errors="strict"))
    assert document["ok"] is True
    value = document["value"]
    oracle = python_oracle()

    for key in ("handled_return", "rethrow_identity", "same_identity", "lifo_return"):
        assert value[key] == oracle[key], (key, value[key], oracle[key])
    assert value["cleanup_raises_baas"] == oracle["cleanup_raises"]
    assert oracle["python_cleanup"] == {"primary": "Cleanup", "context": "Primary"}
    assert value["cleanup_primary"] == "ThrownValue"
    assert value["cleanup_suppressed"] == "ThrownValue"
    assert document["stats"]["registered_defers"] == 5
    assert document["stats"]["executed_defers"] == 5
    assert document["stats"]["cleanup_steps"] > 0


if __name__ == "__main__":
    main()
