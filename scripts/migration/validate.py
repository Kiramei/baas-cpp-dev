#!/usr/bin/env python3
"""Static migration-readiness checks for a BAAS Python repository.

The validator intentionally does not import or execute files from the repository
being inspected.  Python sources are parsed with ``ast`` and JSON is decoded with
the Python standard library.
"""

from __future__ import annotations

import argparse
import ast
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
BOOLEAN_ACTION_FIELDS = ("ec", "wait-over", "end-turn-wait-over")
WAIT_ACTION_FIELDS = ("pre-wait", "post-wait")
FORMATION_TYPES = frozenset(("burst", "pierce", "mystic", "shock", "swipe"))


def relative(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def json_pointer(parts: Iterable[object]) -> str:
    encoded = []
    for part in parts:
        encoded.append(str(part).replace("~", "~0").replace("/", "~1"))
    return "/" + "/".join(encoded) if encoded else ""


def json_value(value: Any) -> Any:
    if isinstance(value, tuple):
        return [json_value(item) for item in value]
    if isinstance(value, list):
        return [json_value(item) for item in value]
    if isinstance(value, dict):
        return {str(key): json_value(item) for key, item in value.items()}
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    if isinstance(value, (set, frozenset)):
        return sorted((json_value(item) for item in value), key=repr)
    return repr(value)


@dataclass(frozen=True)
class Issue:
    severity: str
    code: str
    message: str
    file: str
    pointer: str | None = None
    line: int | None = None

    def as_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "code": self.code,
            "file": self.file,
            "message": self.message,
            "severity": self.severity,
        }
        if self.pointer is not None:
            result["pointer"] = self.pointer
        if self.line is not None:
            result["line"] = self.line
        return result


class Validator:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.issues: list[Issue] = []

    def issue(
        self,
        severity: str,
        code: str,
        message: str,
        path: Path,
        *,
        pointer: str | None = None,
        line: int | None = None,
    ) -> None:
        try:
            filename = relative(path, self.root)
        except ValueError:
            filename = path.as_posix()
        self.issues.append(Issue(severity, code, message, filename, pointer, line))

    def parse_python(self, path: Path, code: str) -> ast.Module | None:
        try:
            return ast.parse(path.read_text(encoding="utf-8"), filename=relative(path, self.root))
        except (OSError, UnicodeError, SyntaxError) as error:
            self.issue(
                "error",
                code,
                f"cannot statically parse Python source: {error}",
                path,
                line=getattr(error, "lineno", None),
            )
            return None

    def derive_operations(self) -> dict[str, Any]:
        executor = self.root / "module" / "explore_tasks" / "task_utils.py"
        if not executor.is_file():
            self.issue("error", "task.executor.missing", "task executor is missing", executor)
            return {"exact": [], "prefixes": [], "source": relative(executor, self.root)}

        tree = self.parse_python(executor, "task.executor.syntax")
        exact: set[str] = set()
        prefixes: set[str] = set()
        function_line: int | None = None
        if tree is not None:
            function = next(
                (
                    node
                    for node in tree.body
                    if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
                    and node.name == "run_task_action"
                ),
                None,
            )
            if function is None:
                self.issue(
                    "error",
                    "task.executor.function_missing",
                    "run_task_action is missing; supported operations cannot be derived",
                    executor,
                )
            else:
                function_line = function.lineno
                for node in ast.walk(function):
                    if (
                        isinstance(node, ast.Call)
                        and isinstance(node.func, ast.Attribute)
                        and node.func.attr == "startswith"
                        and isinstance(node.func.value, ast.Name)
                        and node.func.value.id == "operation"
                        and node.args
                        and isinstance(node.args[0], ast.Constant)
                        and isinstance(node.args[0].value, str)
                    ):
                        prefixes.add(node.args[0].value)
                    if isinstance(node, ast.Compare) and isinstance(node.left, ast.Name):
                        if node.left.id != "operation" or len(node.ops) != 1 or not isinstance(node.ops[0], ast.Eq):
                            continue
                        if len(node.comparators) == 1 and isinstance(node.comparators[0], ast.Constant):
                            value = node.comparators[0].value
                            if isinstance(value, str):
                                exact.add(value)

        if not exact and not prefixes:
            self.issue(
                "error",
                "task.executor.no_operations",
                "no supported operation checks were derived from run_task_action",
                executor,
                line=function_line,
            )
        return {
            "exact": sorted(exact),
            "prefixes": sorted(prefixes),
            "source": relative(executor, self.root),
        }

    @staticmethod
    def operation_supported(operation: str, rules: dict[str, Any]) -> bool:
        return operation in rules["exact"] or any(operation.startswith(prefix) for prefix in rules["prefixes"])

    def validate_action(
        self,
        action: Any,
        path: Path,
        pointer_parts: list[object],
        rules: dict[str, Any],
        observed: set[str],
    ) -> None:
        pointer = json_pointer(pointer_parts)
        if not isinstance(action, dict):
            self.issue("error", "task.action.type", "action must be an object", path, pointer=pointer)
            return
        operation = action.get("t")
        operation_pointer = json_pointer([*pointer_parts, "t"])
        if "t" not in action:
            self.issue("error", "task.operation.required", "action requires field 't'", path, pointer=pointer)
        elif not isinstance(operation, str):
            self.issue(
                "error",
                "task.operation.type",
                "operation 't' must be a string",
                path,
                pointer=operation_pointer,
            )
        else:
            observed.add(operation)
            if not self.operation_supported(operation, rules):
                self.issue(
                    "error",
                    "task.operation.unsupported",
                    f"operation {operation!r} is not handled by run_task_action",
                    path,
                    pointer=operation_pointer,
                )

        position_required = isinstance(operation, str) and (
            operation.startswith("click")
            or operation == "choose_and_change"
            or (operation.startswith("exchange") and "click" in operation)
        )
        if position_required and "p" not in action:
            self.issue(
                "error", "task.position.required", f"operation {operation!r} requires 'p'", path, pointer=pointer
            )
        if position_required and "p" in action:
            position = action["p"]
            if not (
                isinstance(position, list)
                and len(position) == 2
                and all(isinstance(item, (int, float)) and not isinstance(item, bool) for item in position)
            ):
                self.issue(
                    "error",
                    "task.position.type",
                    "'p' must be a two-number array",
                    path,
                    pointer=json_pointer([*pointer_parts, "p"]),
                )

        for field in BOOLEAN_ACTION_FIELDS:
            if field in action and not isinstance(action[field], bool):
                self.issue(
                    "error",
                    "task.field.boolean",
                    f"'{field}' must be a boolean",
                    path,
                    pointer=json_pointer([*pointer_parts, field]),
                )
        for field in WAIT_ACTION_FIELDS:
            value = action.get(field)
            if field in action and not (
                isinstance(value, (int, float)) and not isinstance(value, bool) and value >= 0
            ):
                self.issue(
                    "error",
                    "task.field.wait",
                    f"'{field}' must be a non-negative number",
                    path,
                    pointer=json_pointer([*pointer_parts, field]),
                )
        if "retreat" in action:
            retreat = action["retreat"]
            if not (
                isinstance(retreat, list)
                and retreat
                and all(isinstance(item, int) and not isinstance(item, bool) and item >= 0 for item in retreat)
            ):
                self.issue(
                    "error",
                    "task.field.retreat",
                    "'retreat' must be a non-empty array of non-negative integers",
                    path,
                    pointer=json_pointer([*pointer_parts, "retreat"]),
                )
        for field in ("desc", "description"):
            if field in action and not isinstance(action[field], str):
                self.issue(
                    "error",
                    "task.field.description",
                    f"'{field}' must be a string",
                    path,
                    pointer=json_pointer([*pointer_parts, field]),
                )

    def validate_task(self, task: dict[str, Any], path: Path, pointer_parts: list[object], rules: dict[str, Any], observed: set[str]) -> None:
        start = task.get("start")
        if "start" not in task:
            self.issue(
                "error", "task.start.required", "grid task requires field 'start'", path, pointer=json_pointer(pointer_parts)
            )
        elif not isinstance(start, list):
            self.issue(
                "error",
                "task.start.type",
                "'start' must be an array",
                path,
                pointer=json_pointer([*pointer_parts, "start"]),
            )
        else:
            for index, entry in enumerate(start):
                entry_parts = [*pointer_parts, "start", index]
                if not (isinstance(entry, list) and len(entry) == 2 and isinstance(entry[0], str)):
                    self.issue(
                        "error",
                        "task.start.entry",
                        "start entry must be [command, coordinates]",
                        path,
                        pointer=json_pointer(entry_parts),
                    )
                    continue
                command, coordinates = entry
                if command not in FORMATION_TYPES:
                    self.issue(
                        "error",
                        "task.start.command",
                        f"unsupported formation command {command!r}",
                        path,
                        pointer=json_pointer([*entry_parts, 0]),
                    )
                expected = 5 if command == "swipe" else 2
                if not (
                    isinstance(coordinates, list)
                    and len(coordinates) == expected
                    and all(isinstance(item, (int, float)) and not isinstance(item, bool) for item in coordinates)
                ):
                    self.issue(
                        "error",
                        "task.start.coordinates",
                        f"{command!r} coordinates must contain {expected} numbers",
                        path,
                        pointer=json_pointer([*entry_parts, 1]),
                    )

        actions = task.get("action")
        action_pointer = [*pointer_parts, "action"]
        if not isinstance(actions, list):
            self.issue(
                "error", "task.actions.type", "'action' must be an array", path, pointer=json_pointer(action_pointer)
            )
            return
        for index, action in enumerate(actions):
            self.validate_action(action, path, [*action_pointer, index], rules, observed)

    def validate_tasks(self, rules: dict[str, Any]) -> dict[str, Any]:
        directory = self.root / "src" / "explore_task_data"
        observed: set[str] = set()
        file_count = 0
        task_count = 0
        if not directory.is_dir():
            self.issue("error", "task.directory.missing", "explore_task_data directory is missing", directory)
            return {"file_count": 0, "grid_task_count": 0, "observed_operations": [], "supported": rules}

        for path in sorted(directory.rglob("*.json"), key=lambda item: relative(item, self.root)):
            file_count += 1
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as error:
                self.issue(
                    "error",
                    "task.json.invalid",
                    f"cannot decode JSON: {error}",
                    path,
                    line=getattr(error, "lineno", None),
                )
                continue
            if not isinstance(data, dict):
                self.issue("error", "task.document.type", "task document must be an object", path, pointer="")
                continue

            def walk(value: Any, parts: list[object]) -> None:
                nonlocal task_count
                if isinstance(value, dict):
                    if "grid_tasks_challenge" in value:
                        references = value["grid_tasks_challenge"]
                        references_pointer = [*parts, "grid_tasks_challenge"]
                        if not isinstance(references, list):
                            self.issue(
                                "error",
                                "task.references.type",
                                "'grid_tasks_challenge' must be an array",
                                path,
                                pointer=json_pointer(references_pointer),
                            )
                        else:
                            for index, reference in enumerate(references):
                                reference_pointer = json_pointer([*references_pointer, index])
                                if not isinstance(reference, str):
                                    self.issue(
                                        "error",
                                        "task.reference.type",
                                        "grid task reference must be a string",
                                        path,
                                        pointer=reference_pointer,
                                    )
                                elif not isinstance(value.get(reference), dict):
                                    self.issue(
                                        "error",
                                        "task.reference.missing",
                                        f"grid task reference {reference!r} does not name an object in the same scope",
                                        path,
                                        pointer=reference_pointer,
                                    )
                    if "action" in value:
                        task_count += 1
                        self.validate_task(value, path, parts, rules, observed)
                    for key, item in value.items():
                        walk(item, [*parts, key])
                elif isinstance(value, list):
                    for index, item in enumerate(value):
                        walk(item, [*parts, index])

            walk(data, [])

        return {
            "file_count": file_count,
            "grid_task_count": task_count,
            "observed_operations": sorted(observed),
            "supported": rules,
        }

    @staticmethod
    def literal_assignments(tree: ast.Module) -> tuple[dict[str, Any], dict[str, ast.AST], list[tuple[str, ast.AST]]]:
        values: dict[str, Any] = {}
        nodes: dict[str, ast.AST] = {}
        failures: list[tuple[str, ast.AST]] = []
        for statement in tree.body:
            if not (isinstance(statement, (ast.Assign, ast.AnnAssign))):
                continue
            targets = statement.targets if isinstance(statement, ast.Assign) else [statement.target]
            value_node = statement.value
            if value_node is None:
                continue
            for target in targets:
                if isinstance(target, ast.Name) and target.id in {"prefix", "path", "x_y_range"}:
                    try:
                        values[target.id] = ast.literal_eval(value_node)
                        nodes[target.id] = value_node
                    except (ValueError, TypeError, SyntaxError, MemoryError, RecursionError):
                        failures.append((target.id, value_node))
        return values, nodes, failures

    def validate_images(self) -> dict[str, Any]:
        images_root = self.root / "src" / "images"
        locales: dict[str, Any] = {}
        if not images_root.is_dir():
            self.issue("error", "image.directory.missing", "images directory is missing", images_root)
            return {"locales": locales}

        locale_dirs = sorted(
            (path for path in images_root.iterdir() if path.is_dir() and (path / "x_y_range").is_dir()),
            key=lambda path: path.name,
        )
        for locale_dir in locale_dirs:
            mappings: list[dict[str, Any]] = []
            missing_count = 0
            source_count = 0
            range_root = locale_dir / "x_y_range"
            for source in sorted(range_root.rglob("*.py"), key=lambda item: relative(item, self.root)):
                if source.name == "__init__.py":
                    continue
                source_count += 1
                tree = self.parse_python(source, "image.source.syntax")
                if tree is None:
                    continue
                values, nodes, failures = self.literal_assignments(tree)
                for name, node in failures:
                    self.issue(
                        "error",
                        "image.source.nonliteral",
                        f"{name!r} must be a literal for safe static inspection",
                        source,
                        line=getattr(node, "lineno", None),
                    )
                prefix = values.get("prefix")
                resource_path = values.get("path")
                ranges = values.get("x_y_range")
                if not isinstance(prefix, str) or not isinstance(resource_path, str) or not isinstance(ranges, dict):
                    self.issue(
                        "error",
                        "image.source.shape",
                        "image mapping module requires literal string prefix/path and dictionary x_y_range",
                        source,
                    )
                    continue

                key_lines: dict[Any, int] = {}
                range_node = nodes.get("x_y_range")
                if isinstance(range_node, ast.Dict):
                    for key_node in range_node.keys:
                        if isinstance(key_node, ast.Constant):
                            key_lines[key_node.value] = key_node.lineno

                for key, region in sorted(ranges.items(), key=lambda item: str(item[0])):
                    if not isinstance(key, str):
                        self.issue(
                            "error",
                            "image.key.type",
                            "image mapping key must be a string",
                            source,
                            line=key_lines.get(key),
                        )
                        continue
                    identifier = f"{prefix}_{key}"
                    resource = locale_dir / resource_path / f"{key}.png"
                    valid_region = region is None or (
                        isinstance(region, (list, tuple))
                        and len(region) in (0, 4)
                        and all(
                            isinstance(item, (int, float)) and not isinstance(item, bool)
                            for item in region
                        )
                    )
                    if not valid_region:
                        self.issue(
                            "error",
                            "image.region.type",
                            "image range must be null, empty, or four numbers",
                            source,
                            line=key_lines.get(key),
                        )
                    elif isinstance(region, (list, tuple)) and not region:
                        self.issue(
                            "warning",
                            "image.region.empty",
                            "image mapping has an empty crop range and cannot be used for region matching",
                            source,
                            line=key_lines.get(key),
                        )
                    exists = resource.is_file()
                    if not exists:
                        missing_count += 1
                        self.issue(
                            "error",
                            "image.resource.missing",
                            f"mapped image resource does not exist: {relative(resource, self.root)}",
                            source,
                            line=key_lines.get(key),
                        )
                    mappings.append(
                        {
                            "exists": exists,
                            "identifier": identifier,
                            "line": key_lines.get(key),
                            "region": json_value(region),
                            "resource": relative(resource, self.root),
                            "source": relative(source, self.root),
                        }
                    )
            mappings.sort(key=lambda item: (item["identifier"], item["source"], item["resource"]))
            locales[locale_dir.name] = {
                "mapping_count": len(mappings),
                "mappings": mappings,
                "missing_count": missing_count,
                "source_count": source_count,
            }
        return {"locales": locales}

    def validate_ocr(self) -> dict[str, Any]:
        definition = self.root / "core" / "ocr" / "ocr.py"
        methods: set[str] = set()
        tree = self.parse_python(definition, "ocr.definition.syntax") if definition.is_file() else None
        if not definition.is_file():
            self.issue("error", "ocr.definition.missing", "Baas_ocr definition is missing", definition)
        if tree is not None:
            class_node = next(
                (node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "Baas_ocr"), None
            )
            if class_node is None:
                self.issue("error", "ocr.class.missing", "class Baas_ocr is missing", definition)
            else:
                methods = {
                    node.name
                    for node in class_node.body
                    if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
                }

        calls: list[dict[str, Any]] = []
        module_root = self.root / "module"
        if not module_root.is_dir():
            self.issue("error", "ocr.module_directory.missing", "module directory is missing", module_root)
        else:
            for source in sorted(module_root.rglob("*.py"), key=lambda item: relative(item, self.root)):
                source_tree = self.parse_python(source, "ocr.caller.syntax")
                if source_tree is None:
                    continue
                for node in ast.walk(source_tree):
                    if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Attribute):
                        continue
                    owner = node.func.value
                    if not (
                        isinstance(owner, ast.Attribute)
                        and owner.attr == "ocr"
                        and isinstance(owner.value, ast.Name)
                        and owner.value.id == "self"
                    ):
                        continue
                    method = node.func.attr
                    exists = method in methods
                    calls.append(
                        {
                            "exists": exists,
                            "file": relative(source, self.root),
                            "line": node.lineno,
                            "method": method,
                        }
                    )
                    if not exists:
                        self.issue(
                            "error",
                            "ocr.method.missing",
                            f"Baas_ocr has no method {method!r}",
                            source,
                            line=node.lineno,
                        )
        calls.sort(key=lambda item: (item["file"], item["line"], item["method"]))
        return {"calls": calls, "declared_methods": sorted(methods)}

    def report(self) -> dict[str, Any]:
        rules = self.derive_operations()
        tasks = self.validate_tasks(rules)
        images = self.validate_images()
        ocr = self.validate_ocr()
        ordered = sorted(
            self.issues,
            key=lambda issue: (
                issue.severity,
                issue.code,
                issue.file,
                issue.line if issue.line is not None else -1,
                issue.pointer or "",
                issue.message,
            ),
        )
        errors = sum(issue.severity == "error" for issue in ordered)
        warnings = sum(issue.severity == "warning" for issue in ordered)
        return {
            "images": images,
            "issues": [issue.as_dict() for issue in ordered],
            "ocr": ocr,
            "schema_version": SCHEMA_VERSION,
            "summary": {
                "errors": errors,
                "image_mappings": sum(item["mapping_count"] for item in images["locales"].values()),
                "issues": len(ordered),
                "locales": len(images["locales"]),
                "ocr_calls": len(ocr["calls"]),
                "task_files": tasks["file_count"],
                "tasks": tasks["grid_task_count"],
                "warnings": warnings,
            },
            "tasks": tasks,
        }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python-repo", required=True, type=Path, help="path to the baas-dev checkout")
    parser.add_argument("--output", type=Path, help="write the JSON report to this path instead of stdout")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="fail on warnings as well as errors (errors always produce a non-zero exit)",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = args.python_repo.expanduser().resolve()
    if not root.is_dir():
        print(f"error: Python repository is not a directory: {root}", file=sys.stderr)
        return 2
    report = Validator(root).report()
    rendered = json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.output:
        output = args.output.expanduser()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(rendered)
    summary = report["summary"]
    if summary["errors"] or (args.strict and summary["warnings"]):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
