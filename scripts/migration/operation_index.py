#!/usr/bin/env python3
"""Generate a deterministic, static Python operation migration index.

The inspected repository is never imported or executed. Python files are read
as source and parsed with :mod:`ast`; malformed and dynamic forms remain visible
as unresolved evidence instead of being guessed.
"""

from __future__ import annotations

import argparse
import ast
import builtins
import fnmatch
import hashlib
import json
import os
import subprocess
import sys
import tokenize
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
GENERATOR_VERSION = "1.0.0"
RULES_VERSION = 1
BEGIN_MARKER = "<!-- BEGIN GENERATED OPERATION INDEX -->"
END_MARKER = "<!-- END GENERATED OPERATION INDEX -->"
DEFAULT_RULES = Path(__file__).with_name("operation_rules.v1.json")
IGNORED_DIRECTORIES = frozenset(
    {
        ".git",
        ".idea",
        ".mypy_cache",
        ".pytest_cache",
        ".ruff_cache",
        ".tox",
        ".venv",
        "__pycache__",
        "build",
        "dist",
        "htmlcov",
        "node_modules",
        # baas-dev/toolkit contains a vendored CPython distribution. It is a
        # runtime artifact, not BAAS source, and would otherwise contribute
        # thousands of standard-library calls and intentional syntax fixtures.
        "toolkit",
        "venv",
    }
)
REGISTRY_NAMES = frozenset({"func_dict", "_HANDLERS"})
DISPATCH_NAMES = frozenset({"channel", "command", "msg_type", "operation", "req_type"})
ROUTE_METHODS = frozenset({"delete", "get", "head", "options", "patch", "post", "put", "websocket"})
BUILTIN_NAMES = frozenset(dir(builtins))


def normalize_path_text(value: str | os.PathLike[str]) -> str:
    """Return a deterministic slash-separated relative path representation."""
    return str(value).replace("\\", "/")


def stable_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def module_name(relative_path: str) -> str:
    parts = relative_path.removesuffix(".py").split("/")
    if parts[-1] == "__init__":
        parts.pop()
    return ".".join(parts) or "__root__"


def relative(path: Path, root: Path) -> str:
    return normalize_path_text(path.relative_to(root))


def discover_python_sources(root: Path) -> list[Path]:
    sources: list[Path] = []
    for directory, directories, filenames in os.walk(root, topdown=True, followlinks=False):
        directories[:] = sorted(
            name
            for name in directories
            if name not in IGNORED_DIRECTORIES and not name.startswith(".codex")
        )
        base = Path(directory)
        for filename in sorted(filenames):
            if filename.endswith(".py"):
                sources.append(base / filename)
    return sorted(sources, key=lambda path: relative(path, root))


@dataclass(frozen=True)
class Location:
    file: str
    line: int
    column: int
    scope: str

    def as_dict(self) -> dict[str, Any]:
        return {
            "column": self.column,
            "file": self.file,
            "line": self.line,
            "scope": self.scope,
        }


@dataclass(frozen=True)
class Observation:
    symbol: str
    kind: str
    call_form: str
    resolution: str
    location: Location
    shape: str


@dataclass(frozen=True)
class ParseFailure:
    file: str
    line: int | None
    error_type: str
    message: str

    def as_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "error_type": self.error_type,
            "file": self.file,
            "message": self.message,
        }
        if self.line is not None:
            result["line"] = self.line
        return result


class BindingCollector(ast.NodeVisitor):
    """Collect conservative file-level aliases and top-level definitions."""

    def __init__(self, source_module: str) -> None:
        self.source_module = source_module
        self.aliases: dict[str, str] = {}
        self.definitions: dict[str, str] = {}
        self.rebound: set[str] = set()
        self.depth = 0

    def visit_Import(self, node: ast.Import) -> None:
        for item in node.names:
            local = item.asname or item.name.split(".")[0]
            resolved = item.name if item.asname else item.name.split(".")[0]
            self.aliases[local] = resolved

    def visit_ImportFrom(self, node: ast.ImportFrom) -> None:
        if node.module is None or node.level:
            return
        for item in node.names:
            if item.name == "*":
                continue
            self.aliases[item.asname or item.name] = f"{node.module}.{item.name}"

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        if self.depth == 0:
            self.definitions[node.name] = f"{self.source_module}.{node.name}"
        self.depth += 1
        self._record_arguments(node.args)
        for statement in node.body:
            self.visit(statement)
        self.depth -= 1

    visit_AsyncFunctionDef = visit_FunctionDef

    def visit_ClassDef(self, node: ast.ClassDef) -> None:
        if self.depth == 0:
            self.definitions[node.name] = f"{self.source_module}.{node.name}"
        self.depth += 1
        for statement in node.body:
            self.visit(statement)
        self.depth -= 1

    def visit_Name(self, node: ast.Name) -> None:
        if isinstance(node.ctx, (ast.Store, ast.Del)):
            self.rebound.add(node.id)

    def _record_arguments(self, arguments: ast.arguments) -> None:
        for argument in [*arguments.posonlyargs, *arguments.args, *arguments.kwonlyargs]:
            self.rebound.add(argument.arg)
        if arguments.vararg:
            self.rebound.add(arguments.vararg.arg)
        if arguments.kwarg:
            self.rebound.add(arguments.kwarg.arg)

    def resolved_aliases(self) -> dict[str, str]:
        return {name: target for name, target in self.aliases.items() if name not in self.rebound}


class OperationVisitor(ast.NodeVisitor):
    def __init__(
        self,
        filename: str,
        source_module: str,
        aliases: dict[str, str],
        definitions: dict[str, str],
        tree: ast.Module,
    ) -> None:
        self.filename = filename
        self.source_module = source_module
        self.aliases = aliases
        self.definitions = definitions
        self.observations: list[Observation] = []
        self.scope: list[str] = []
        self.awaited_calls = {
            id(node.value)
            for node in ast.walk(tree)
            if isinstance(node, ast.Await) and isinstance(node.value, ast.Call)
        }

    def current_scope(self) -> str:
        return ".".join(self.scope) if self.scope else "<module>"

    def location(self, node: ast.AST) -> Location:
        return Location(
            self.filename,
            getattr(node, "lineno", 0),
            getattr(node, "col_offset", 0) + 1,
            self.current_scope(),
        )

    def add(
        self,
        node: ast.AST,
        symbol: str,
        kind: str,
        call_form: str,
        resolution: str,
        shape: str = "declaration",
    ) -> None:
        self.observations.append(
            Observation(symbol, kind, call_form, resolution, self.location(node), shape)
        )

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        self._visit_function(node)

    def visit_AsyncFunctionDef(self, node: ast.AsyncFunctionDef) -> None:
        self._visit_function(node)

    def _visit_function(self, node: ast.FunctionDef | ast.AsyncFunctionDef) -> None:
        self._record_routes(node)
        for decorator in node.decorator_list:
            self.visit(decorator)
        for default in [*node.args.defaults, *node.args.kw_defaults]:
            if default is not None:
                self.visit(default)
        for argument in [*node.args.posonlyargs, *node.args.args, *node.args.kwonlyargs]:
            if argument.annotation is not None:
                self.visit(argument.annotation)
        if node.args.vararg and node.args.vararg.annotation is not None:
            self.visit(node.args.vararg.annotation)
        if node.args.kwarg and node.args.kwarg.annotation is not None:
            self.visit(node.args.kwarg.annotation)
        if node.returns is not None:
            self.visit(node.returns)
        self.scope.append(node.name)
        for statement in node.body:
            self.visit(statement)
        self.scope.pop()

    def visit_ClassDef(self, node: ast.ClassDef) -> None:
        for decorator in node.decorator_list:
            self.visit(decorator)
        for base in node.bases:
            self.visit(base)
        for keyword in node.keywords:
            self.visit(keyword.value)
        self.scope.append(node.name)
        for statement in node.body:
            self.visit(statement)
        self.scope.pop()

    def visit_Assign(self, node: ast.Assign) -> None:
        for target in node.targets:
            if isinstance(target, ast.Name):
                self._record_registry(target.id, node.value, node)
        self.generic_visit(node)

    def visit_AnnAssign(self, node: ast.AnnAssign) -> None:
        if isinstance(node.target, ast.Name) and node.value is not None:
            self._record_registry(node.target.id, node.value, node)
        self.generic_visit(node)

    def _record_registry(self, name: str, value: ast.AST, node: ast.AST) -> None:
        if name not in REGISTRY_NAMES or not isinstance(value, ast.Dict):
            return
        for key in value.keys:
            if isinstance(key, ast.Constant) and isinstance(key.value, str):
                self.add(
                    key,
                    f"registry:{self.source_module}.{name}:{key.value}",
                    "registration",
                    "registry-key",
                    "resolved",
                )

    def _record_routes(self, node: ast.FunctionDef | ast.AsyncFunctionDef) -> None:
        for decorator in node.decorator_list:
            if not isinstance(decorator, ast.Call) or not isinstance(decorator.func, ast.Attribute):
                continue
            method = decorator.func.attr.lower()
            if method not in ROUTE_METHODS or not decorator.args:
                continue
            path = decorator.args[0]
            if not isinstance(path, ast.Constant) or not isinstance(path.value, str):
                continue
            owner, _, _ = self.resolve_expression(decorator.func.value)
            self.add(
                decorator,
                f"route:{method.upper()}:{path.value}",
                "route",
                "decorator",
                "resolved" if owner else "structural",
            )

    def visit_Compare(self, node: ast.Compare) -> None:
        dispatch_name = self._dispatch_name(node.left)
        if dispatch_name and len(node.ops) == 1 and len(node.comparators) == 1:
            comparator = node.comparators[0]
            values: list[str] = []
            if (
                isinstance(node.ops[0], (ast.Eq, ast.Is))
                and isinstance(comparator, ast.Constant)
                and isinstance(comparator.value, str)
            ):
                values = [comparator.value]
            elif isinstance(node.ops[0], (ast.In, ast.NotIn)) and isinstance(
                comparator, (ast.List, ast.Set, ast.Tuple)
            ):
                if all(
                    isinstance(item, ast.Constant) and isinstance(item.value, str)
                    for item in comparator.elts
                ):
                    values = [item.value for item in comparator.elts]
            for value in values:
                self.add(
                    node,
                    f"dispatch:{dispatch_name}:{value}",
                    "dispatch",
                    "dispatch-exact",
                    "resolved",
                )
        self.generic_visit(node)

    def visit_Call(self, node: ast.Call) -> None:
        symbol, call_form, resolution = self.resolve_expression(node.func)
        shape = self.call_shape(node)
        self.add(node, symbol, "call", call_form, resolution, shape)

        if (
            isinstance(node.func, ast.Attribute)
            and node.func.attr == "startswith"
            and self._dispatch_name(node.func.value) is not None
            and node.args
            and isinstance(node.args[0], ast.Constant)
            and isinstance(node.args[0].value, str)
        ):
            self.add(
                node,
                f"dispatch:{self._dispatch_name(node.func.value)}:{node.args[0].value}*",
                "dispatch",
                "dispatch-prefix",
                "resolved",
            )
        self.generic_visit(node)

    def call_shape(self, node: ast.Call) -> str:
        positional = sum(not isinstance(item, ast.Starred) for item in node.args)
        star_args = sum(isinstance(item, ast.Starred) for item in node.args)
        keywords = sorted(item.arg for item in node.keywords if item.arg is not None)
        star_kwargs = sum(item.arg is None for item in node.keywords)
        awaited = id(node) in self.awaited_calls
        keyword_text = ",".join(keywords) if keywords else "-"
        return (
            f"pos={positional};kw={keyword_text};*args={star_args};"
            f"**kwargs={star_kwargs};await={str(awaited).lower()}"
        )

    def resolve_expression(self, node: ast.AST) -> tuple[str, str, str]:
        if isinstance(node, ast.Name):
            if node.id in self.aliases:
                return self.aliases[node.id], "alias", "resolved"
            if node.id in self.definitions:
                return self.definitions[node.id], "name", "resolved"
            if node.id in BUILTIN_NAMES:
                return f"builtins.{node.id}", "name", "builtin"
            return node.id, "name", "unresolved"

        if isinstance(node, ast.Attribute):
            if isinstance(node.value, ast.Call) and self._is_super_call(node.value):
                return f"super().{node.attr}", "super", "structural"
            base, base_form, base_resolution = self.resolve_expression(node.value)
            if base.startswith("dynamic:"):
                return f"{base}.{node.attr}", "chained", "dynamic"
            if base_form == "name" and base == "self":
                return f"self.{node.attr}", "self", "structural"
            if base.startswith("self."):
                return f"{base}.{node.attr}", "self", "structural"
            if base_form in {"alias", "alias-member"}:
                return f"{base}.{node.attr}", "alias-member", "resolved"
            if base_form == "chained" or "()" in base:
                return f"{base}.{node.attr}", "chained", base_resolution
            return f"{base}.{node.attr}", "member", base_resolution

        if isinstance(node, ast.Call):
            if self._is_getattr_call(node):
                owner, _, _ = self.resolve_expression(node.args[0]) if node.args else ("?", "", "")
                attribute = node.args[1] if len(node.args) > 1 else None
                if isinstance(attribute, ast.Constant) and isinstance(attribute.value, str):
                    return f"dynamic:getattr({owner},{attribute.value})", "dynamic", "dynamic"
                return f"dynamic:getattr({owner},?)", "dynamic", "dynamic"
            inner, _, _ = self.resolve_expression(node.func)
            return f"dynamic:call-result({inner})()", "chained", "dynamic"

        if isinstance(node, ast.Subscript):
            owner, _, _ = self.resolve_expression(node.value)
            return f"dynamic:subscript({owner})", "dynamic", "dynamic"
        if isinstance(node, ast.Lambda):
            return "dynamic:lambda", "dynamic", "dynamic"
        return f"dynamic:{node.__class__.__name__}", "dynamic", "dynamic"

    @staticmethod
    def _is_super_call(node: ast.Call) -> bool:
        return isinstance(node.func, ast.Name) and node.func.id == "super"

    @staticmethod
    def _is_getattr_call(node: ast.Call) -> bool:
        return isinstance(node.func, ast.Name) and node.func.id == "getattr"

    @staticmethod
    def _dispatch_name(node: ast.AST) -> str | None:
        if isinstance(node, ast.Name) and node.id in DISPATCH_NAMES:
            return node.id
        if isinstance(node, ast.Attribute) and node.attr in DISPATCH_NAMES:
            return node.attr
        return None


class RuleSet:
    REQUIRED_FIELDS = frozenset(
        {
            "cpp_host_binding",
            "family",
            "id",
            "migration_status",
            "owner",
            "parity_test_id",
            "symbol_patterns",
        }
    )

    def __init__(self, path: Path) -> None:
        raw = path.read_bytes()
        document = json.loads(raw.decode("utf-8"))
        canonical = json.dumps(
            document,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        self.sha256 = sha256_bytes(canonical)
        if document.get("rules_version") != RULES_VERSION:
            raise ValueError(f"rules_version must be {RULES_VERSION}")
        rules = document.get("rules")
        if not isinstance(rules, list):
            raise ValueError("rules must be an array")
        identifiers: set[str] = set()
        for index, rule in enumerate(rules):
            if not isinstance(rule, dict) or not self.REQUIRED_FIELDS.issubset(rule):
                raise ValueError(f"rule {index} is missing required fields")
            if rule["id"] in identifiers:
                raise ValueError(f"duplicate rule id: {rule['id']}")
            identifiers.add(rule["id"])
            if not isinstance(rule["symbol_patterns"], list) or not rule["symbol_patterns"]:
                raise ValueError(f"rule {rule['id']} requires symbol_patterns")
        self.rules = rules

    def classify(self, symbol: str, call_form: str, files: list[str]) -> dict[str, str]:
        if call_form in {"dynamic", "chained"} and symbol.startswith("dynamic:"):
            return unclassified("dynamic expression cannot be resolved statically")
        for rule in self.rules:
            if not any(fnmatch.fnmatchcase(symbol, pattern) for pattern in rule["symbol_patterns"]):
                continue
            forms = rule.get("call_forms")
            if forms and call_form not in forms:
                continue
            sources = rule.get("source_patterns")
            if sources and not all(
                any(fnmatch.fnmatchcase(filename, pattern) for pattern in sources)
                for filename in files
            ):
                continue
            return {
                "classification_rule": rule["id"],
                "cpp_host_binding": rule["cpp_host_binding"],
                "family": rule["family"],
                "migration_status": rule["migration_status"],
                "owner": rule["owner"],
                "parity_test_id": rule["parity_test_id"],
            }
        return unclassified("no versioned classification rule matched")


def unclassified(reason: str) -> dict[str, str]:
    return {
        "classification_rule": "UNCLASSIFIED",
        "cpp_host_binding": "UNCLASSIFIED",
        "family": "UNCLASSIFIED",
        "migration_status": "UNCLASSIFIED",
        "owner": "UNASSIGNED",
        "parity_test_id": "UNCLASSIFIED",
        "unclassified_reason": reason,
    }


class OperationIndexer:
    def __init__(self, root: Path, rules: RuleSet) -> None:
        self.root = root
        self.rules = rules

    def generate(self) -> dict[str, Any]:
        sources = discover_python_sources(self.root)
        observations: list[Observation] = []
        failures: list[ParseFailure] = []
        snapshot = hashlib.sha256()
        root_counts: dict[str, int] = {}

        for source in sources:
            filename = relative(source, self.root)
            root_name = filename.split("/", 1)[0] if "/" in filename else "."
            root_counts[root_name] = root_counts.get(root_name, 0) + 1
            try:
                with tokenize.open(source) as stream:
                    text = stream.read()
                # tokenize.open decodes the declared source encoding and uses
                # universal newlines. Hash that canonical source text so CRLF
                # checkout policy does not change otherwise identical evidence.
                snapshot.update(filename.encode("utf-8"))
                snapshot.update(b"\0")
                snapshot.update(text.encode("utf-8"))
                snapshot.update(b"\0")
                tree = ast.parse(text, filename=filename)
            except (OSError, UnicodeError, SyntaxError) as error:
                failures.append(
                    ParseFailure(
                        filename,
                        getattr(error, "lineno", None),
                        error.__class__.__name__,
                        str(error),
                    )
                )
                continue

            source_module = module_name(filename)
            bindings = BindingCollector(source_module)
            bindings.visit(tree)
            visitor = OperationVisitor(
                filename,
                source_module,
                bindings.resolved_aliases(),
                bindings.definitions,
                tree,
            )
            visitor.visit(tree)
            observations.extend(visitor.observations)

        operations = self._aggregate(observations)
        failures.sort(key=lambda item: (item.file, item.line or -1, item.error_type, item.message))
        unclassified_ids = [
            operation["id"]
            for operation in operations
            if operation["migration_status"] == "UNCLASSIFIED"
        ]
        dynamic_ids = [
            operation["id"]
            for operation in operations
            if operation["resolution"] == "dynamic"
        ]
        return {
            "generator_version": GENERATOR_VERSION,
            "operations": operations,
            "repository": {
                "revision": repository_revision(self.root),
                "source_snapshot_sha256": snapshot.hexdigest(),
            },
            "rules": {
                "rules_version": RULES_VERSION,
                "sha256": self.rules.sha256,
            },
            "schema_version": SCHEMA_VERSION,
            "source_roots": [
                {"file_count": count, "root": name}
                for name, count in sorted(root_counts.items())
            ],
            "summary": {
                "call_sites": sum(operation["occurrences"] for operation in operations),
                "classified_operations": len(operations) - len(unclassified_ids),
                "dynamic_operations": len(dynamic_ids),
                "operation_count": len(operations),
                "parse_errors": len(failures),
                "python_files": len(sources),
                "unclassified_operations": len(unclassified_ids),
            },
            "unclassified_operation_ids": unclassified_ids,
            "unresolved_sources": [failure.as_dict() for failure in failures],
        }

    def _aggregate(self, observations: Iterable[Observation]) -> list[dict[str, Any]]:
        grouped: dict[tuple[str, str, str], list[Observation]] = {}
        for observation in observations:
            key = (observation.kind, observation.call_form, observation.symbol)
            grouped.setdefault(key, []).append(observation)

        result: list[dict[str, Any]] = []
        for (kind, call_form, symbol), items in sorted(grouped.items()):
            items.sort(
                key=lambda item: (
                    item.location.file,
                    item.location.line,
                    item.location.column,
                    item.location.scope,
                    item.shape,
                )
            )
            locations = sorted(
                {item.location for item in items},
                key=lambda item: (item.file, item.line, item.column, item.scope),
            )
            shape_groups: dict[str, list[Observation]] = {}
            for item in items:
                shape_groups.setdefault(item.shape, []).append(item)
            shapes = []
            for shape, shape_items in sorted(shape_groups.items()):
                shape_locations = sorted(
                    {item.location for item in shape_items},
                    key=lambda item: (item.file, item.line, item.column, item.scope),
                )
                shapes.append(
                    {
                        "occurrences": len(shape_items),
                        "representative_location": shape_locations[0].as_dict(),
                        "shape": shape,
                    }
                )
            resolutions = sorted({item.resolution for item in items})
            resolution = resolutions[0] if len(resolutions) == 1 else "mixed"
            identity = f"v{SCHEMA_VERSION}\0{kind}\0{call_form}\0{symbol}".encode("utf-8")
            operation_id = f"op-{sha256_bytes(identity)[:16]}"
            files = sorted({location.file for location in locations})
            entry: dict[str, Any] = {
                "call_form": call_form,
                "call_shapes": shapes,
                "id": operation_id,
                "kind": kind,
                "occurrences": len(items),
                "representative_locations": [location.as_dict() for location in locations[:3]],
                "resolution": resolution,
                "source_file_count": len(files),
                "symbol": symbol,
            }
            entry.update(self.rules.classify(symbol, call_form, files))
            result.append(entry)
        return result


def repository_revision(root: Path) -> str:
    try:
        process = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return "UNAVAILABLE"
    value = process.stdout.strip()
    return value if process.returncode == 0 and len(value) == 40 else "UNAVAILABLE"


def markdown_escape(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def render_generated_matrix(report: dict[str, Any], evidence_link: str) -> str:
    summary = report["summary"]
    lines = [
        BEGIN_MARKER,
        "## Generated operation-level index",
        "",
        f"Deterministic JSON evidence: [{evidence_link}]({evidence_link}).",
        "",
        (
            f"Snapshot `{report['repository']['revision']}` contains "
            f"{summary['python_files']} Python files, {summary['operation_count']} unique operations, "
            f"{summary['call_sites']} observed sites, {summary['unclassified_operations']} unclassified "
            f"operations, and {summary['parse_errors']} parse errors."
        ),
        "",
        "| Evidence ID | Kind/form | Operation symbol | Uses | Family | Owner | Proposed C++ binding | Parity test | Status | Representative source |",
        "| --- | --- | --- | ---: | --- | --- | --- | --- | --- | --- |",
    ]
    for operation in report["operations"]:
        location = operation["representative_locations"][0]
        source = f"{location['file']}:{location['line']}"
        lines.append(
            "| "
            + " | ".join(
                markdown_escape(value)
                for value in (
                    operation["id"],
                    f"{operation['kind']}/{operation['call_form']}",
                    f"`{operation['symbol']}`",
                    operation["occurrences"],
                    operation["family"],
                    operation["owner"],
                    f"`{operation['cpp_host_binding']}`",
                    operation["parity_test_id"],
                    operation["migration_status"],
                    f"`{source}`",
                )
            )
            + " |"
        )
    lines.extend([END_MARKER, ""])
    return "\n".join(lines)


def update_matrix(path: Path, generated: str) -> None:
    original = path.read_text(encoding="utf-8") if path.exists() else ""
    begin = original.find(BEGIN_MARKER)
    end = original.find(END_MARKER)
    if (begin == -1) != (end == -1):
        raise ValueError("migration matrix contains only one generated marker")
    if begin != -1:
        if original.find(BEGIN_MARKER, begin + len(BEGIN_MARKER)) != -1:
            raise ValueError("migration matrix contains duplicate begin markers")
        if end < begin:
            raise ValueError("migration matrix generated markers are out of order")
        end += len(END_MARKER)
        updated = original[:begin] + generated.rstrip() + original[end:]
    else:
        separator = "" if not original else ("\n" if original.endswith("\n") else "\n\n")
        updated = original + separator + generated
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(updated, encoding="utf-8", newline="\n")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python-repo", required=True, type=Path, help="path to the Python checkout")
    parser.add_argument("--rules", type=Path, default=DEFAULT_RULES, help="versioned classification rules")
    parser.add_argument("--output", type=Path, help="write JSON evidence instead of stdout")
    parser.add_argument("--matrix", type=Path, help="update the generated marker block in this Markdown file")
    parser.add_argument(
        "--evidence-link",
        default="evidence/operation-index.json",
        help="relative JSON link written into the generated Markdown block",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="fail when any operation is UNCLASSIFIED or any Python source cannot be parsed",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = args.python_repo.expanduser().resolve()
    if not root.is_dir():
        print(f"error: Python repository is not a directory: {root}", file=sys.stderr)
        return 2
    try:
        rules = RuleSet(args.rules.expanduser().resolve())
        report = OperationIndexer(root, rules).generate()
        rendered = stable_json(report)
        if args.output:
            output = args.output.expanduser()
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(rendered, encoding="utf-8", newline="\n")
        else:
            sys.stdout.write(rendered)
        if args.matrix:
            update_matrix(
                args.matrix.expanduser(),
                render_generated_matrix(report, args.evidence_link),
            )
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    summary = report["summary"]
    if args.strict and (summary["unclassified_operations"] or summary["parse_errors"]):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
