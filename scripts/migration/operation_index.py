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


SCHEMA_VERSION = 2
IDENTITY_VERSION = 1
GENERATOR_VERSION = "4.0.0"
RULES_VERSION = 4
BEGIN_MARKER = "<!-- BEGIN GENERATED OPERATION INDEX -->"
END_MARKER = "<!-- END GENERATED OPERATION INDEX -->"
DEFAULT_RULES = Path(__file__).with_name("operation_rules.v4.json")
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


def import_from_base(current_package: str, node: ast.ImportFrom) -> str | None:
    if node.level == 0:
        return node.module or ""
    package = current_package.split(".") if current_package else []
    ascend = node.level - 1
    if ascend >= len(package) and ascend:
        return None
    prefix = package[: len(package) - ascend] if ascend else package
    if node.module:
        prefix.extend(node.module.split("."))
    return ".".join(prefix)


def module_package(filename: str, source_module: str) -> str:
    package_init = filename == "__init__.py" or filename.endswith("/__init__.py")
    return source_module if package_init else source_module.rpartition(".")[0]


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


@dataclass(frozen=True)
class ParsedModule:
    filename: str
    source_module: str
    tree: ast.Module


class DefinitionCollector(ast.NodeVisitor):
    """Collect module definitions without treating nested bindings as global."""

    def __init__(self, source_module: str) -> None:
        self.source_module = source_module
        self.definitions: dict[str, str] = {}

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        self.definitions[node.name] = f"{self.source_module}.{node.name}"

    visit_AsyncFunctionDef = visit_FunctionDef

    def visit_ClassDef(self, node: ast.ClassDef) -> None:
        self.definitions[node.name] = f"{self.source_module}.{node.name}"


class LocalBindingCollector(ast.NodeVisitor):
    """Collect compile-time local names for one function-like scope."""

    def __init__(self) -> None:
        self.names: set[str] = set()
        self.globals: set[str] = set()
        self.nonlocals: set[str] = set()

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        self.names.add(node.name)

    visit_AsyncFunctionDef = visit_FunctionDef

    def visit_ClassDef(self, node: ast.ClassDef) -> None:
        self.names.add(node.name)

    def visit_Lambda(self, node: ast.Lambda) -> None:
        return

    def visit_ListComp(self, node: ast.ListComp) -> None:
        return

    visit_SetComp = visit_ListComp
    visit_GeneratorExp = visit_ListComp

    def visit_DictComp(self, node: ast.DictComp) -> None:
        return

    def visit_Import(self, node: ast.Import) -> None:
        for item in node.names:
            self.names.add(item.asname or item.name.split(".")[0])

    def visit_ImportFrom(self, node: ast.ImportFrom) -> None:
        for item in node.names:
            if item.name != "*":
                self.names.add(item.asname or item.name)

    def visit_Name(self, node: ast.Name) -> None:
        if isinstance(node.ctx, (ast.Store, ast.Del)):
            self.names.add(node.id)

    def visit_Global(self, node: ast.Global) -> None:
        self.globals.update(node.names)

    def visit_Nonlocal(self, node: ast.Nonlocal) -> None:
        self.nonlocals.update(node.names)

    def visit_ExceptHandler(self, node: ast.ExceptHandler) -> None:
        if node.name:
            self.names.add(node.name)
        for statement in node.body:
            self.visit(statement)


@dataclass
class ScopeFrame:
    kind: str
    aliases: dict[str, str]
    value_types: dict[str, str]
    local_names: set[str]
    global_names: set[str]
    nonlocal_names: set[str]


class OperationVisitor(ast.NodeVisitor):
    def __init__(
        self,
        filename: str,
        source_module: str,
        definitions: dict[str, str],
        tree: ast.Module,
        rules: "RuleSet",
        known_classes: set[str],
        module_exports: dict[str, dict[str, str]],
    ) -> None:
        self.filename = filename
        self.source_module = source_module
        self.definitions = definitions
        self.rules = rules
        self.known_classes = known_classes
        self.module_exports = module_exports
        self.observations: list[Observation] = []
        self.scope: list[str] = []
        self.current_package = module_package(filename, source_module)
        self.frames = [ScopeFrame("module", {}, {}, set(), set(), set())]
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
        bindings = LocalBindingCollector()
        for statement in node.body:
            bindings.visit(statement)
        local_names = set(bindings.names)
        for argument in [*node.args.posonlyargs, *node.args.args, *node.args.kwonlyargs]:
            local_names.add(argument.arg)
        if node.args.vararg:
            local_names.add(node.args.vararg.arg)
        if node.args.kwarg:
            local_names.add(node.args.kwarg.arg)
        local_names.difference_update(bindings.globals)
        local_names.difference_update(bindings.nonlocals)
        argument_types: dict[str, str] = {}
        for argument in [*node.args.posonlyargs, *node.args.args, *node.args.kwonlyargs]:
            value_type = self._annotation_type(argument.annotation)
            if value_type is not None:
                argument_types[argument.arg] = value_type
        if node.args.vararg:
            argument_types[node.args.vararg.arg] = "builtins.tuple"
        if node.args.kwarg:
            argument_types[node.args.kwarg.arg] = "builtins.dict"
        self.scope.append(node.name)
        self.frames.append(
            ScopeFrame(
                "function",
                {},
                argument_types,
                local_names,
                bindings.globals,
                bindings.nonlocals,
            )
        )
        for statement in node.body:
            self.visit(statement)
        self.frames.pop()
        self.scope.pop()

    def visit_ClassDef(self, node: ast.ClassDef) -> None:
        for decorator in node.decorator_list:
            self.visit(decorator)
        for base in node.bases:
            self.visit(base)
        for keyword in node.keywords:
            self.visit(keyword.value)
        self.scope.append(node.name)
        self.frames.append(ScopeFrame("class", {}, {}, set(), set(), set()))
        for statement in node.body:
            self.visit(statement)
        self.frames.pop()
        self.scope.pop()

    def visit_Lambda(self, node: ast.Lambda) -> None:
        local_names = {
            argument.arg
            for argument in [*node.args.posonlyargs, *node.args.args, *node.args.kwonlyargs]
        }
        if node.args.vararg:
            local_names.add(node.args.vararg.arg)
        if node.args.kwarg:
            local_names.add(node.args.kwarg.arg)
        for default in [*node.args.defaults, *node.args.kw_defaults]:
            if default is not None:
                self.visit(default)
        argument_types: dict[str, str] = {}
        for argument in [*node.args.posonlyargs, *node.args.args, *node.args.kwonlyargs]:
            value_type = self._annotation_type(argument.annotation)
            if value_type is not None:
                argument_types[argument.arg] = value_type
        if node.args.vararg:
            argument_types[node.args.vararg.arg] = "builtins.tuple"
        if node.args.kwarg:
            argument_types[node.args.kwarg.arg] = "builtins.dict"
        self.frames.append(
            ScopeFrame("function", {}, argument_types, local_names, set(), set())
        )
        self.visit(node.body)
        self.frames.pop()

    def visit_Import(self, node: ast.Import) -> None:
        for item in node.names:
            local = item.asname or item.name.split(".")[0]
            target = item.name if item.asname else item.name.split(".")[0]
            self._bind_alias(local, target)

    def visit_ImportFrom(self, node: ast.ImportFrom) -> None:
        base = self._import_from_base(node)
        if base is None:
            return
        for item in node.names:
            if item.name == "*":
                for name, target in sorted(self.module_exports.get(base, {}).items()):
                    if not name.startswith("_"):
                        self._bind_alias(name, target)
                continue
            target = f"{base}.{item.name}" if base else item.name
            self._bind_alias(item.asname or item.name, target)

    def _import_from_base(self, node: ast.ImportFrom) -> str | None:
        return import_from_base(self.current_package, node)

    def _target_frame(self, name: str) -> ScopeFrame:
        current = self.frames[-1]
        if name in current.global_names:
            return self.frames[0]
        if name in current.nonlocal_names:
            for frame in reversed(self.frames[:-1]):
                if frame.kind != "class" and name in frame.local_names:
                    return frame
        return current

    def _bind_alias(self, name: str, target: str) -> None:
        frame = self._target_frame(name)
        frame.local_names.add(name)
        frame.aliases[name] = target
        frame.value_types.pop(name, None)

    def _bind_unresolved(self, name: str, value_type: str | None = None) -> None:
        frame = self._target_frame(name)
        frame.local_names.add(name)
        frame.aliases.pop(name, None)
        if value_type is None:
            frame.value_types.pop(name, None)
        else:
            frame.value_types[name] = value_type

    def _bind_target(self, node: ast.AST, value_type: str | None = None) -> None:
        if isinstance(node, ast.Name):
            self._bind_unresolved(node.id, value_type)
        elif isinstance(node, (ast.List, ast.Tuple)):
            for item in node.elts:
                self._bind_target(item)
        elif isinstance(node, ast.Starred):
            self._bind_target(node.value)

    def _lookup_alias(self, name: str) -> tuple[bool, str | None]:
        inside_function = any(frame.kind == "function" for frame in self.frames[1:])
        for frame in reversed(self.frames):
            if inside_function and frame.kind == "class":
                continue
            if name in frame.aliases:
                return True, frame.aliases[name]
            if name in frame.local_names:
                return True, None
        return False, None

    def _lookup_value_type(self, name: str) -> str | None:
        inside_function = any(frame.kind == "function" for frame in self.frames[1:])
        for frame in reversed(self.frames):
            if inside_function and frame.kind == "class":
                continue
            if name in frame.value_types:
                return frame.value_types[name]
            if name in frame.local_names:
                return None
        return None

    def _annotation_type(self, node: ast.AST | None) -> str | None:
        if node is None:
            return None
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            try:
                node = ast.parse(node.value, mode="eval").body
            except SyntaxError:
                return None
        if isinstance(node, ast.Tuple):
            value_types = {self._annotation_type(item) for item in node.elts}
            value_types.discard(None)
            return next(iter(value_types)) if len(value_types) == 1 else None
        if isinstance(node, ast.Subscript):
            owner, _, resolution = self.resolve_expression(node.value)
            normalized = {
                "typing.Dict": "builtins.dict",
                "typing.FrozenSet": "builtins.frozenset",
                "typing.List": "builtins.list",
                "typing.Set": "builtins.set",
                "typing.Tuple": "builtins.tuple",
            }.get(owner, owner)
            if resolution in {"resolved", "builtin"} and normalized in {
                "builtins.dict",
                "builtins.frozenset",
                "builtins.list",
                "builtins.set",
                "builtins.tuple",
            }:
                return normalized
            return None
        owner, _, resolution = self.resolve_expression(node)
        if resolution in {"resolved", "builtin"}:
            normalized = {
                "typing.Dict": "builtins.dict",
                "typing.FrozenSet": "builtins.frozenset",
                "typing.List": "builtins.list",
                "typing.Set": "builtins.set",
                "typing.Tuple": "builtins.tuple",
            }.get(owner, owner)
            if normalized.startswith("typing."):
                return None
            return normalized
        return None

    def infer_value_type(self, node: ast.AST | None) -> str | None:
        if node is None:
            return None
        literal_types: tuple[tuple[type[ast.AST], str], ...] = (
            (ast.List, "builtins.list"),
            (ast.ListComp, "builtins.list"),
            (ast.Dict, "builtins.dict"),
            (ast.DictComp, "builtins.dict"),
            (ast.Set, "builtins.set"),
            (ast.SetComp, "builtins.set"),
            (ast.Tuple, "builtins.tuple"),
            (ast.GeneratorExp, "types.GeneratorType"),
            (ast.JoinedStr, "builtins.str"),
        )
        for node_type, value_type in literal_types:
            if isinstance(node, node_type):
                return value_type
        if isinstance(node, ast.Constant):
            if node.value is None:
                return None
            return f"builtins.{type(node.value).__name__}"
        if isinstance(node, ast.Name):
            return self._lookup_value_type(node.id)
        if isinstance(node, ast.IfExp):
            body_type = self.infer_value_type(node.body)
            return body_type if body_type == self.infer_value_type(node.orelse) else None
        if isinstance(node, ast.UnaryOp):
            if isinstance(node.op, ast.Not):
                return "builtins.bool"
            operand_type = self.infer_value_type(node.operand)
            if isinstance(node.op, (ast.UAdd, ast.USub)) and operand_type in {
                "builtins.complex",
                "builtins.float",
                "builtins.int",
            }:
                return operand_type
            if isinstance(node.op, ast.Invert) and operand_type == "builtins.int":
                return operand_type
            return None
        if isinstance(node, ast.BinOp):
            left_type = self.infer_value_type(node.left)
            right_type = self.infer_value_type(node.right)
            if isinstance(node.op, ast.Div) and left_type == "pathlib.Path":
                return "pathlib.Path"
            if (
                isinstance(node.op, ast.Add)
                and left_type == right_type
                and left_type
                in {
                    "builtins.bytearray",
                    "builtins.bytes",
                    "builtins.complex",
                    "builtins.float",
                    "builtins.int",
                    "builtins.list",
                    "builtins.str",
                    "builtins.tuple",
                }
            ):
                return left_type
            numeric = {"builtins.complex", "builtins.float", "builtins.int"}
            if isinstance(node.op, ast.Div) and left_type in numeric and right_type in numeric:
                return (
                    "builtins.complex"
                    if "builtins.complex" in {left_type, right_type}
                    else "builtins.float"
                )
            if (
                isinstance(node.op, (ast.Sub, ast.Mult))
                and left_type == right_type
                and left_type in numeric
            ):
                return left_type
            if (
                isinstance(node.op, ast.FloorDiv)
                and left_type == right_type
                and left_type in {"builtins.float", "builtins.int"}
            ):
                return left_type
            if isinstance(node.op, ast.Mod) and left_type == "builtins.str":
                return left_type
            if (
                isinstance(node.op, ast.Mod)
                and left_type == right_type
                and left_type in {"builtins.float", "builtins.int"}
            ):
                return left_type
            if (
                isinstance(node.op, (ast.BitAnd, ast.BitOr, ast.BitXor))
                and left_type == right_type
                and left_type in {"builtins.int", "builtins.set", "builtins.frozenset"}
            ):
                return left_type
            return None
        if isinstance(node, ast.Call):
            symbol, _, resolution = self.resolve_expression(node.func)
            if resolution not in {"resolved", "builtin", "inferred", "structural"}:
                return None
            if symbol in self.known_classes:
                return symbol
            if symbol == "os.getenv":
                default = node.args[1] if len(node.args) > 1 else None
                if default is None:
                    default = next(
                        (item.value for item in node.keywords if item.arg == "default"),
                        None,
                    )
                if self.infer_value_type(default) == "builtins.str":
                    return "builtins.str"
            return self.rules.infer_call_type(symbol)
        if isinstance(node, ast.Attribute):
            owner_type = self.infer_value_type(node.value)
            if owner_type is not None:
                return self.rules.infer_attribute_type(f"{owner_type}.{node.attr}")
        return None

    def _bind_assignment_target(self, target: ast.AST, value: ast.AST | None) -> None:
        if (
            isinstance(target, (ast.List, ast.Tuple))
            and isinstance(value, (ast.List, ast.Tuple))
            and len(target.elts) == len(value.elts)
        ):
            for target_item, value_item in zip(target.elts, value.elts):
                self._bind_assignment_target(target_item, value_item)
            return
        self._bind_target(target, self.infer_value_type(value))

    def _iter_element_type(self, node: ast.AST) -> str | None:
        if isinstance(node, (ast.List, ast.Set, ast.Tuple)) and node.elts:
            types = {self.infer_value_type(item) for item in node.elts}
            return next(iter(types)) if len(types) == 1 else None
        if isinstance(node, ast.Call):
            symbol, _, resolution = self.resolve_expression(node.func)
            if resolution in {"resolved", "builtin"} and symbol == "builtins.range":
                return "builtins.int"
            if resolution in {"resolved", "builtin"} and symbol == "builtins.enumerate":
                return "builtins.tuple"
        return None

    def _isinstance_narrowings(self, node: ast.AST) -> dict[str, str]:
        if isinstance(node, ast.BoolOp) and isinstance(node.op, ast.And):
            result: dict[str, str] = {}
            for value in node.values:
                result.update(self._isinstance_narrowings(value))
            return result
        if not (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "isinstance"
            and len(node.args) == 2
            and isinstance(node.args[0], ast.Name)
        ):
            return {}
        value_type = self._annotation_type(node.args[1])
        return {node.args[0].id: value_type} if value_type is not None else {}

    def _apply_value_types(self, values: dict[str, str]) -> None:
        for name, value_type in values.items():
            self._bind_unresolved(name, value_type)

    def _snapshot_frames(self) -> list[ScopeFrame]:
        return [
            ScopeFrame(
                frame.kind,
                dict(frame.aliases),
                dict(frame.value_types),
                set(frame.local_names),
                set(frame.global_names),
                set(frame.nonlocal_names),
            )
            for frame in self.frames
        ]

    def _restore_frames(self, frames: list[ScopeFrame]) -> None:
        self.frames = self._copy_frames(frames)

    @staticmethod
    def _copy_frames(frames: list[ScopeFrame]) -> list[ScopeFrame]:
        return [
            ScopeFrame(
                frame.kind,
                dict(frame.aliases),
                dict(frame.value_types),
                set(frame.local_names),
                set(frame.global_names),
                set(frame.nonlocal_names),
            )
            for frame in frames
        ]

    def _merge_frame_states(self, states: list[list[ScopeFrame]]) -> None:
        if not states or any(len(state) != len(states[0]) for state in states):
            raise ValueError("cannot merge incompatible lexical scope states")
        merged: list[ScopeFrame] = []
        for index in range(len(states[0])):
            frames = [state[index] for state in states]
            common_aliases: dict[str, str] = {}
            for name, target in frames[0].aliases.items():
                if all(frame.aliases.get(name) == target for frame in frames[1:]):
                    common_aliases[name] = target
            merged.append(
                ScopeFrame(
                    frames[0].kind,
                    common_aliases,
                    {
                        name: value_type
                        for name, value_type in frames[0].value_types.items()
                        if all(
                            frame.value_types.get(name) == value_type
                            for frame in frames[1:]
                        )
                    },
                    set().union(*(frame.local_names for frame in frames)),
                    set().union(*(frame.global_names for frame in frames)),
                    set().union(*(frame.nonlocal_names for frame in frames)),
                )
            )
        self.frames = merged

    def visit_Assign(self, node: ast.Assign) -> None:
        for target in node.targets:
            if isinstance(target, ast.Name):
                self._record_registry(target.id, node.value, node)
        self.visit(node.value)
        for target in node.targets:
            self._bind_assignment_target(target, node.value)
            self.visit(target)

    def visit_AnnAssign(self, node: ast.AnnAssign) -> None:
        if isinstance(node.target, ast.Name) and node.value is not None:
            self._record_registry(node.target.id, node.value, node)
        self.visit(node.annotation)
        if node.value is not None:
            self.visit(node.value)
        value_type = self._annotation_type(node.annotation) or self.infer_value_type(node.value)
        self._bind_target(node.target, value_type)
        self.visit(node.target)

    def visit_AugAssign(self, node: ast.AugAssign) -> None:
        self.visit(node.target)
        self.visit(node.value)
        self._bind_target(node.target)

    def visit_NamedExpr(self, node: ast.NamedExpr) -> None:
        self.visit(node.value)
        self._bind_assignment_target(node.target, node.value)

    def visit_Delete(self, node: ast.Delete) -> None:
        for target in node.targets:
            self._bind_target(target)

    def visit_If(self, node: ast.If) -> None:
        self.visit(node.test)
        initial = self._snapshot_frames()
        states: list[list[ScopeFrame]] = []
        self._restore_frames(initial)
        narrowings = self._isinstance_narrowings(node.test)
        for item in ast.walk(node.test):
            if isinstance(item, ast.Name) and isinstance(item.ctx, ast.Store):
                narrowings.pop(item.id, None)
        self._apply_value_types(narrowings)
        for statement in node.body:
            self.visit(statement)
        states.append(self._snapshot_frames())
        self._restore_frames(initial)
        for statement in node.orelse:
            self.visit(statement)
        states.append(self._snapshot_frames())
        self._merge_frame_states(states)

    def visit_For(self, node: ast.For) -> None:
        self.visit(node.iter)
        zero_iterations = self._snapshot_frames()
        self._bind_target(node.target, self._iter_element_type(node.iter))
        for statement in node.body:
            self.visit(statement)
        one_or_more_iterations = self._snapshot_frames()
        self._merge_frame_states([zero_iterations, one_or_more_iterations])
        before_else = self._snapshot_frames()
        for statement in node.orelse:
            self.visit(statement)
        self._merge_frame_states([before_else, self._snapshot_frames()])

    visit_AsyncFor = visit_For

    def visit_While(self, node: ast.While) -> None:
        self.visit(node.test)
        zero_iterations = self._snapshot_frames()
        for statement in node.body:
            self.visit(statement)
        one_or_more_iterations = self._snapshot_frames()
        self._merge_frame_states([zero_iterations, one_or_more_iterations])
        before_else = self._snapshot_frames()
        for statement in node.orelse:
            self.visit(statement)
        self._merge_frame_states([before_else, self._snapshot_frames()])

    def _bind_match_pattern(self, node: ast.pattern) -> None:
        if isinstance(node, ast.MatchAs):
            if node.pattern is not None:
                self._bind_match_pattern(node.pattern)
            if node.name is not None:
                self._bind_unresolved(node.name)
        elif isinstance(node, ast.MatchStar):
            if node.name is not None:
                self._bind_unresolved(node.name, "builtins.list")
        elif isinstance(node, ast.MatchMapping):
            for pattern in node.patterns:
                self._bind_match_pattern(pattern)
            if node.rest is not None:
                self._bind_unresolved(node.rest, "builtins.dict")
        elif isinstance(node, (ast.MatchSequence, ast.MatchOr)):
            for pattern in node.patterns:
                self._bind_match_pattern(pattern)
        elif isinstance(node, ast.MatchClass):
            for pattern in [*node.patterns, *node.kwd_patterns]:
                self._bind_match_pattern(pattern)

    def visit_Match(self, node: ast.Match) -> None:
        self.visit(node.subject)
        initial = self._snapshot_frames()
        states = [initial]
        for case in node.cases:
            self._restore_frames(initial)
            self.visit(case.pattern)
            self._bind_match_pattern(case.pattern)
            if case.guard is not None:
                self.visit(case.guard)
            for statement in case.body:
                self.visit(statement)
            states.append(self._snapshot_frames())
        self._merge_frame_states(states)

    def visit_Try(self, node: ast.Try) -> None:
        initial = self._snapshot_frames()
        states: list[list[ScopeFrame]] = []
        self._restore_frames(initial)
        for statement in node.body:
            self.visit(statement)
        for statement in node.orelse:
            self.visit(statement)
        states.append(self._snapshot_frames())
        for handler in node.handlers:
            self._restore_frames(initial)
            self.visit(handler)
            states.append(self._snapshot_frames())
        self._merge_frame_states(states)
        for statement in node.finalbody:
            self.visit(statement)

    visit_TryStar = visit_Try

    def visit_With(self, node: ast.With) -> None:
        for item in node.items:
            self.visit(item.context_expr)
            if item.optional_vars is not None:
                context_type = self.infer_value_type(item.context_expr)
                self._bind_target(
                    item.optional_vars,
                    context_type if context_type == "io.IOBase" else None,
                )
        for statement in node.body:
            self.visit(statement)

    visit_AsyncWith = visit_With

    def visit_ExceptHandler(self, node: ast.ExceptHandler) -> None:
        if node.type is not None:
            self.visit(node.type)
        if node.name:
            self._bind_unresolved(node.name, self._annotation_type(node.type))
        for statement in node.body:
            self.visit(statement)

    def _visit_comprehension(
        self,
        generators: list[ast.comprehension],
        values: list[ast.AST],
    ) -> None:
        if not generators:
            for value in values:
                self.visit(value)
            return
        self.visit(generators[0].iter)
        local_names: set[str] = set()
        collector = LocalBindingCollector()
        for generator in generators:
            collector.visit(generator.target)
        local_names.update(collector.names)
        self.frames.append(ScopeFrame("comprehension", {}, {}, local_names, set(), set()))
        for index, generator in enumerate(generators):
            if index:
                self.visit(generator.iter)
            self._bind_target(generator.target, self._iter_element_type(generator.iter))
            for condition in generator.ifs:
                self.visit(condition)
        for value in values:
            self.visit(value)
        self.frames.pop()

    def visit_ListComp(self, node: ast.ListComp) -> None:
        self._visit_comprehension(node.generators, [node.elt])

    visit_SetComp = visit_ListComp
    visit_GeneratorExp = visit_ListComp

    def visit_DictComp(self, node: ast.DictComp) -> None:
        self._visit_comprehension(node.generators, [node.key, node.value])

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
            bound, alias = self._lookup_alias(node.id)
            if alias is not None:
                return alias, "alias", "resolved"
            value_type = self._lookup_value_type(node.id)
            if value_type is not None:
                return value_type, "instance", "inferred"
            if bound:
                return node.id, "name", "unresolved"
            if node.id in self.definitions:
                return self.definitions[node.id], "name", "resolved"
            if node.id in BUILTIN_NAMES:
                return f"builtins.{node.id}", "name", "builtin"
            return node.id, "name", "unresolved"

        if isinstance(node, ast.Attribute):
            if isinstance(node.value, ast.Call) and self._is_super_call(node.value):
                return f"super().{node.attr}", "super", "structural"
            owner_type = self.infer_value_type(node.value)
            if owner_type is not None:
                return f"{owner_type}.{node.attr}", "instance-member", "inferred"
            base, base_form, base_resolution = self.resolve_expression(node.value)
            if base.startswith("dynamic:"):
                return f"{base}.{node.attr}", "chained", "dynamic"
            if base_form == "name" and base == "self":
                return f"self.{node.attr}", "self", "structural"
            if base.startswith("self."):
                return f"{base}.{node.attr}", "self", "structural"
            if base_form == "name" and base == "cls":
                return f"cls.{node.attr}", "cls", "structural"
            if base.startswith("cls."):
                return f"{base}.{node.attr}", "cls", "structural"
            if base_form in {"alias", "alias-member"}:
                return f"{base}.{node.attr}", "alias-member", "resolved"
            if base_form == "instance":
                return f"{base}.{node.attr}", "instance-member", "inferred"
            if base_form == "instance-member":
                return f"dynamic:attribute-result({base}).{node.attr}", "chained", "dynamic"
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
            value_type = self.infer_value_type(node)
            if value_type is not None:
                return value_type, "instance", "inferred"
            inner, _, _ = self.resolve_expression(node.func)
            return f"dynamic:call-result({inner})()", "chained", "dynamic"

        if isinstance(node, ast.Subscript):
            owner, _, _ = self.resolve_expression(node.value)
            return f"dynamic:subscript({owner})", "dynamic", "dynamic"
        if isinstance(node, ast.Lambda):
            return "dynamic:lambda", "dynamic", "dynamic"
        value_type = self.infer_value_type(node)
        if value_type is not None:
            return value_type, "instance", "inferred"
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
    REQUIRED_FIELDS = frozenset({"disposition", "id", "symbol_patterns"})
    VALID_DISPOSITIONS = frozenset(
        {
            "HOST_BINDING_REQUIRED",
            "SCRIPT_LANGUAGE_OR_MODULE",
            "CPP_SERVICE_INTERNAL",
            "TAURI_UI_REPLACED",
            "MIGRATION_TOOLING_ONLY",
            "TEST_ONLY",
            "EXTERNAL_DEPENDENCY",
            "UNRESOLVED",
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
        scope_rules = document.get("source_scope_rules")
        if not isinstance(scope_rules, list) or not scope_rules:
            raise ValueError("source_scope_rules must be a non-empty array")
        scope_identifiers: set[str] = set()
        for index, rule in enumerate(scope_rules):
            if not isinstance(rule, dict) or not {
                "id",
                "source_patterns",
                "source_scope",
            }.issubset(rule):
                raise ValueError(f"source scope rule {index} is missing required fields")
            if rule["id"] in scope_identifiers:
                raise ValueError(f"duplicate source scope rule id: {rule['id']}")
            scope_identifiers.add(rule["id"])
            if not isinstance(rule["source_patterns"], list) or not rule["source_patterns"]:
                raise ValueError(f"source scope rule {rule['id']} requires source_patterns")
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
            if rule["disposition"] not in self.VALID_DISPOSITIONS:
                raise ValueError(f"rule {rule['id']} has invalid disposition")
            if not isinstance(rule["symbol_patterns"], list) or not rule["symbol_patterns"]:
                raise ValueError(f"rule {rule['id']} requires symbol_patterns")
        value_type_rules = document.get("value_type_rules")
        if not isinstance(value_type_rules, list):
            raise ValueError("value_type_rules must be an array")
        type_rule_identifiers: set[str] = set()
        for index, rule in enumerate(value_type_rules):
            if not isinstance(rule, dict) or not {"id", "result_type"}.issubset(rule):
                raise ValueError(f"value type rule {index} is missing required fields")
            if rule["id"] in type_rule_identifiers:
                raise ValueError(f"duplicate value type rule id: {rule['id']}")
            type_rule_identifiers.add(rule["id"])
            call_patterns = rule.get("call_patterns")
            attribute_patterns = rule.get("attribute_patterns")
            if not call_patterns and not attribute_patterns:
                raise ValueError(
                    f"value type rule {rule['id']} requires call_patterns or attribute_patterns"
                )
            for field, patterns in (
                ("call_patterns", call_patterns),
                ("attribute_patterns", attribute_patterns),
            ):
                if patterns is not None and (
                    not isinstance(patterns, list)
                    or not patterns
                    or not all(isinstance(pattern, str) for pattern in patterns)
                ):
                    raise ValueError(f"value type rule {rule['id']} has invalid {field}")
            if not isinstance(rule["result_type"], str) or not rule["result_type"]:
                raise ValueError(f"value type rule {rule['id']} has invalid result_type")
        self.source_scope_rules = scope_rules
        self.rules = rules
        self.value_type_rules = value_type_rules

    def infer_call_type(self, symbol: str) -> str | None:
        for rule in self.value_type_rules:
            if any(
                fnmatch.fnmatchcase(symbol, pattern)
                for pattern in rule.get("call_patterns", [])
            ):
                return symbol if rule["result_type"] == "$callable" else rule["result_type"]
        return None

    def infer_attribute_type(self, symbol: str) -> str | None:
        for rule in self.value_type_rules:
            if any(
                fnmatch.fnmatchcase(symbol, pattern)
                for pattern in rule.get("attribute_patterns", [])
            ):
                return symbol if rule["result_type"] == "$attribute" else rule["result_type"]
        return None

    def source_scope(self, filename: str) -> str:
        matches = [
            rule["source_scope"]
            for rule in self.source_scope_rules
            if any(fnmatch.fnmatchcase(filename, pattern) for pattern in rule["source_patterns"])
        ]
        if not matches:
            return "UNRESOLVED_SOURCE"
        if len(set(matches)) != 1:
            return "UNRESOLVED_SOURCE"
        return matches[0]

    def classify(
        self,
        symbol: str,
        call_form: str,
        resolution: str,
        files: list[str],
        source_scope: str,
        local_module_roots: set[str],
    ) -> dict[str, Any]:
        if source_scope == "UNRESOLVED_SOURCE":
            return disposition_result(
                "UNRESOLVED",
                "unresolved-source-scope-v2",
                "no unique versioned source-scope rule matched",
            )
        for rule in self.rules:
            if not any(fnmatch.fnmatchcase(symbol, pattern) for pattern in rule["symbol_patterns"]):
                continue
            forms = rule.get("call_forms")
            if forms and call_form not in forms:
                continue
            scopes = rule.get("source_scopes")
            if scopes and source_scope not in scopes:
                continue
            sources = rule.get("source_patterns")
            if sources and not all(
                any(fnmatch.fnmatchcase(filename, pattern) for pattern in sources)
                for filename in files
            ):
                continue
            return disposition_result(
                rule["disposition"],
                rule["id"],
                rule.get("reason", "versioned disposition rule matched"),
                rule,
            )

        # These source roots already have an authoritative migration boundary.
        # Resolving the Python receiver may improve implementation inventories,
        # but cannot change which layer owns the call after migration. Apply the
        # boundary after explicit rules and before owner-resolution fallbacks so
        # dynamic/unresolved expressions do not manufacture false disposition
        # gaps outside the script runtime.
        defaults = {
            "CPP_SERVICE": ("CPP_SERVICE_INTERNAL", "service.internal", "C++ Service"),
            "DEPLOYMENT_TOOLING": (
                "MIGRATION_TOOLING_ONLY",
                "tooling.deployment",
                "Migration Tooling",
            ),
            "LEGACY_GUI": ("TAURI_UI_REPLACED", "ui.legacy", "Tauri UI"),
            "MIGRATION_TOOLING": (
                "MIGRATION_TOOLING_ONLY",
                "tooling.migration",
                "Migration Tooling",
            ),
            "TEST": ("TEST_ONLY", "test.support", "Test Infrastructure"),
        }
        if source_scope in defaults:
            disposition, family, owner = defaults[source_scope]
            unresolved_owner = resolution in {"dynamic", "unresolved"} or (
                call_form in {"dynamic", "chained"} and symbol.startswith("dynamic:")
            )
            return disposition_result(
                disposition,
                (
                    f"{source_scope.lower()}-boundary-v4"
                    if unresolved_owner
                    else f"{source_scope.lower()}-default-v2"
                ),
                (
                    f"{source_scope} fixes the migration boundary independently of call ownership"
                    if unresolved_owner
                    else f"resolved call belongs to the {source_scope} source scope"
                ),
                {"family": family, "owner": owner},
            )

        if resolution == "dynamic" or (
            call_form in {"dynamic", "chained"} and symbol.startswith("dynamic:")
        ):
            return disposition_result(
                "UNRESOLVED",
                "dynamic-expression-v3",
                "dynamic expression cannot be resolved statically",
            )
        if resolution == "unresolved":
            return disposition_result(
                "UNRESOLVED",
                "unresolved-expression-v3",
                "call owner cannot be resolved statically",
            )

        if source_scope in {"SCRIPT_RUNTIME", "GENERATED_RESOURCE"}:
            root = symbol.split(".", 1)[0]
            internal_roots = {
                "builtins",
                "self",
                "super()",
                *local_module_roots,
                *sys.stdlib_module_names,
            }
            if ":" not in symbol and root not in internal_roots:
                return disposition_result(
                    "EXTERNAL_DEPENDENCY",
                    "external-dependency-default-v2",
                    "resolved import root is neither local nor Python standard library",
                    {"family": "dependency.external", "owner": "Dependency Migration"},
                )
            return disposition_result(
                "SCRIPT_LANGUAGE_OR_MODULE",
                "script-language-module-default-v2",
                "resolved call remains in the script language or migrated module layer",
                {"family": "script.language-module", "owner": "Script Runtime"},
            )

        return disposition_result(
            "UNRESOLVED",
            "no-disposition-rule-v2",
            "no conservative disposition rule matched",
        )


def disposition_result(
    disposition: str,
    rule_id: str,
    reason: str,
    rule: dict[str, Any] | None = None,
) -> dict[str, Any]:
    rule = rule or {}
    host_required = disposition == "HOST_BINDING_REQUIRED"
    default_status = {
        "EXTERNAL_DEPENDENCY": "DEPENDENCY_DECISION_PENDING",
        "UNRESOLVED": "UNRESOLVED",
    }.get(disposition, "SCOPED")
    return {
        "classification_rule": rule_id,
        "cpp_host_binding": rule.get(
            "cpp_host_binding", "UNASSIGNED" if host_required else "NOT_APPLICABLE"
        ),
        "disposition": disposition,
        "disposition_reason": reason,
        "family": rule.get("family", "UNASSIGNED" if host_required else "UNRESOLVED"),
        "migration_status": rule.get("migration_status", default_status),
        "owner": rule.get("owner", "UNASSIGNED"),
        "parity_test_id": rule.get(
            "parity_test_id", "UNASSIGNED" if host_required else "NOT_APPLICABLE"
        ),
    }


def collect_known_classes(parsed_modules: list[ParsedModule]) -> set[str]:
    result: set[str] = set()

    def collect(statements: list[ast.stmt], prefix: str) -> None:
        for statement in statements:
            if isinstance(statement, ast.ClassDef):
                symbol = f"{prefix}.{statement.name}"
                result.add(symbol)
                collect(statement.body, symbol)

    for parsed in parsed_modules:
        collect(parsed.tree.body, parsed.source_module)
    return result


def collect_module_exports(
    parsed_modules: list[ParsedModule],
) -> dict[str, dict[str, str]]:
    exports: dict[str, dict[str, str]] = {}
    explicit_exports: dict[str, set[str] | None] = {}
    unknown_explicit_exports: set[str] = set()

    for parsed in parsed_modules:
        module_exports: dict[str, str] = {}
        selected: set[str] | None = None
        package = module_package(parsed.filename, parsed.source_module)
        for statement in parsed.tree.body:
            if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                module_exports[statement.name] = f"{parsed.source_module}.{statement.name}"
            elif isinstance(statement, ast.Import):
                for item in statement.names:
                    local = item.asname or item.name.split(".")[0]
                    module_exports[local] = item.name if item.asname else local
            elif isinstance(statement, ast.ImportFrom):
                base = import_from_base(package, statement)
                if base is None:
                    continue
                for item in statement.names:
                    if item.name == "*":
                        continue
                    else:
                        module_exports[item.asname or item.name] = (
                            f"{base}.{item.name}" if base else item.name
                        )
            elif isinstance(statement, (ast.Assign, ast.AnnAssign)):
                targets = (
                    statement.targets if isinstance(statement, ast.Assign) else [statement.target]
                )
                value = statement.value
                bindings = LocalBindingCollector()
                bindings.visit(statement)
                for name in bindings.names:
                    module_exports.pop(name, None)
                for target in targets:
                    if isinstance(target, ast.Name):
                        if target.id == "__all__" and isinstance(value, (ast.List, ast.Tuple)):
                            names = [
                                item.value
                                for item in value.elts
                                if isinstance(item, ast.Constant) and isinstance(item.value, str)
                            ]
                            if len(names) == len(value.elts):
                                selected = set(names)
                                unknown_explicit_exports.discard(parsed.source_module)
                            else:
                                selected = None
                                unknown_explicit_exports.add(parsed.source_module)
                        elif target.id == "__all__":
                            selected = None
                            unknown_explicit_exports.add(parsed.source_module)
            else:
                bindings = LocalBindingCollector()
                bindings.visit(statement)
                for name in bindings.names:
                    module_exports.pop(name, None)
                if "__all__" in bindings.names or any(
                    isinstance(item, ast.Name) and item.id == "__all__"
                    for item in ast.walk(statement)
                ):
                    selected = None
                    unknown_explicit_exports.add(parsed.source_module)
        exports[parsed.source_module] = module_exports
        explicit_exports[parsed.source_module] = selected

    def visible_exports(module: str) -> dict[str, str]:
        if module in unknown_explicit_exports:
            return {}
        selected = explicit_exports.get(module)
        if selected is None:
            return exports.get(module, {})
        return {
            name: symbol
            for name, symbol in exports.get(module, {}).items()
            if name in selected
        }

    for module in explicit_exports:
        exports[module] = dict(visible_exports(module))
    return exports


class OperationIndexer:
    def __init__(self, root: Path, rules: RuleSet) -> None:
        self.root = root
        self.rules = rules

    def generate(self) -> dict[str, Any]:
        sources = discover_python_sources(self.root)
        local_module_roots = {
            module_name(relative(source, self.root)).split(".", 1)[0]
            for source in sources
        }
        observations: list[Observation] = []
        failures: list[ParseFailure] = []
        parsed_modules: list[ParsedModule] = []
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
            parsed_modules.append(ParsedModule(filename, source_module, tree))

        known_classes = collect_known_classes(parsed_modules)
        module_exports = collect_module_exports(parsed_modules)
        for parsed in parsed_modules:
            filename = parsed.filename
            source_module = parsed.source_module
            tree = parsed.tree
            definitions = DefinitionCollector(source_module)
            definitions.visit(tree)
            visitor = OperationVisitor(
                filename,
                source_module,
                definitions.definitions,
                tree,
                self.rules,
                known_classes,
                module_exports,
            )
            visitor.visit(tree)
            observations.extend(visitor.observations)

        operations = self._aggregate(observations, local_module_roots)
        failures.sort(key=lambda item: (item.file, item.line or -1, item.error_type, item.message))
        decisions = [
            (operation, decision)
            for operation in operations
            for decision in operation["scope_decisions"]
        ]
        unresolved_ids = [
            decision["id"]
            for _, decision in decisions
            if decision["disposition"] == "UNRESOLVED"
        ]
        host_gap_ids = [
            decision["id"]
            for _, decision in decisions
            if decision["host_binding_gap_fields"]
        ]
        dynamic_ids = [
            operation["id"]
            for operation in operations
            if operation["resolution"] == "dynamic"
        ]
        disposition_counts: dict[str, int] = {}
        disposition_sites: dict[str, int] = {}
        disposition_ids: dict[str, set[str]] = {}
        source_scope_counts: dict[str, int] = {}
        source_scope_sites: dict[str, int] = {}
        for operation, decision in decisions:
            disposition = decision["disposition"]
            disposition_counts[disposition] = disposition_counts.get(disposition, 0) + 1
            disposition_sites[disposition] = (
                disposition_sites.get(disposition, 0) + decision["occurrences"]
            )
            disposition_ids.setdefault(disposition, set()).add(operation["id"])
            source_scope = decision["source_scope"]
            source_scope_counts[source_scope] = source_scope_counts.get(source_scope, 0) + 1
            source_scope_sites[source_scope] = (
                source_scope_sites.get(source_scope, 0) + decision["occurrences"]
            )
        return {
            "generator_version": GENERATOR_VERSION,
            "identity_version": IDENTITY_VERSION,
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
                "disposition_call_sites": dict(sorted(disposition_sites.items())),
                "disposition_scope_decisions": dict(sorted(disposition_counts.items())),
                "dynamic_operations": len(dynamic_ids),
                "host_binding_gaps": len(host_gap_ids),
                "host_binding_required_scope_decisions": disposition_counts.get(
                    "HOST_BINDING_REQUIRED", 0
                ),
                "operation_count": len(operations),
                "operations_with_host_binding_gap": len(
                    {
                        operation["id"]
                        for operation, decision in decisions
                        if decision["host_binding_gap_fields"]
                    }
                ),
                "operations_with_unresolved_disposition": len(
                    {
                        operation["id"]
                        for operation, decision in decisions
                        if decision["disposition"] == "UNRESOLVED"
                    }
                ),
                "parse_errors": len(failures),
                "python_files": len(sources),
                "scope_decision_count": len(decisions),
                "source_scope_call_sites": dict(sorted(source_scope_sites.items())),
                "source_scope_decisions": dict(sorted(source_scope_counts.items())),
                "unresolved_disposition_scope_decisions": len(unresolved_ids),
            },
            "disposition_operation_ids": {
                disposition: sorted(identifiers)
                for disposition, identifiers in sorted(disposition_ids.items())
            },
            "host_binding_gap_ids": host_gap_ids,
            "unresolved_disposition_ids": unresolved_ids,
            "unresolved_sources": [failure.as_dict() for failure in failures],
        }

    def _aggregate(
        self,
        observations: Iterable[Observation],
        local_module_roots: set[str],
    ) -> list[dict[str, Any]]:
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
            identity = f"v{IDENTITY_VERSION}\0{kind}\0{call_form}\0{symbol}".encode("utf-8")
            operation_id = f"op-{sha256_bytes(identity)[:16]}"
            files = sorted({location.file for location in locations})
            scope_items: dict[str, list[Observation]] = {}
            for item in items:
                scope_items.setdefault(
                    self.rules.source_scope(item.location.file), []
                ).append(item)
            scope_decisions: list[dict[str, Any]] = []
            for source_scope, scoped_items in sorted(scope_items.items()):
                scoped_locations = sorted(
                    {item.location for item in scoped_items},
                    key=lambda item: (item.file, item.line, item.column, item.scope),
                )
                scoped_files = sorted({location.file for location in scoped_locations})
                scoped_resolutions = sorted({item.resolution for item in scoped_items})
                scoped_resolution = (
                    scoped_resolutions[0] if len(scoped_resolutions) == 1 else "mixed"
                )
                decision = {
                    "id": f"{operation_id}@{source_scope}",
                    "occurrences": len(scoped_items),
                    "representative_locations": [
                        location.as_dict() for location in scoped_locations[:3]
                    ],
                    "resolution": scoped_resolution,
                    "source_file_count": len(scoped_files),
                    "source_scope": source_scope,
                }
                decision.update(
                    self.rules.classify(
                        symbol,
                        call_form,
                        scoped_resolution,
                        scoped_files,
                        source_scope,
                        local_module_roots,
                    )
                )
                decision["host_binding_gap_fields"] = host_binding_gap_fields(decision)
                scope_decisions.append(decision)
            entry: dict[str, Any] = {
                "call_form": call_form,
                "call_shapes": shapes,
                "id": operation_id,
                "kind": kind,
                "occurrences": len(items),
                "representative_locations": [location.as_dict() for location in locations[:3]],
                "resolution": resolution,
                "scope_decisions": scope_decisions,
                "source_file_count": len(files),
                "source_scopes": [decision["source_scope"] for decision in scope_decisions],
                "symbol": symbol,
            }
            for field in (
                "classification_rule",
                "cpp_host_binding",
                "disposition",
                "family",
                "migration_status",
                "owner",
                "parity_test_id",
            ):
                values = sorted({str(decision[field]) for decision in scope_decisions})
                entry[field] = values[0] if len(values) == 1 else "MULTIPLE"
            result.append(entry)
        return result


def host_binding_gap_fields(decision: dict[str, Any]) -> list[str]:
    if decision["disposition"] != "HOST_BINDING_REQUIRED":
        return []
    placeholders = {"", "NOT_APPLICABLE", "UNASSIGNED", "UNCLASSIFIED", "UNRESOLVED"}
    required = ("cpp_host_binding", "family", "owner", "parity_test_id")
    return [field for field in required if str(decision.get(field, "")) in placeholders]


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
            f"{summary['call_sites']} observed sites, {summary['scope_decision_count']} source-scope "
            f"decisions, {summary['unresolved_disposition_scope_decisions']} unresolved dispositions, "
            f"{summary['host_binding_gaps']} host-binding gaps, and {summary['parse_errors']} parse errors."
        ),
        "",
        "| Evidence ID | Source scope | Disposition | Kind/form | Operation symbol | Uses | Family | Owner / target | Proposed C++ binding | Parity test | Status | Representative source |",
        "| --- | --- | --- | --- | --- | ---: | --- | --- | --- | --- | --- | --- |",
    ]
    for operation in report["operations"]:
        for decision in operation["scope_decisions"]:
            location = decision["representative_locations"][0]
            source = f"{location['file']}:{location['line']}"
            lines.append(
                "| "
                + " | ".join(
                    markdown_escape(value)
                    for value in (
                        decision["id"],
                        decision["source_scope"],
                        decision["disposition"],
                        f"{operation['kind']}/{operation['call_form']}",
                        f"`{operation['symbol']}`",
                        decision["occurrences"],
                        decision["family"],
                        decision["owner"],
                        f"`{decision['cpp_host_binding']}`",
                        decision["parity_test_id"],
                        decision["migration_status"],
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
        help=(
            "fail for unresolved dispositions, host-binding gaps, or Python parse errors; "
            "the two gap counts remain separate in JSON"
        ),
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
    if args.strict and (
        summary["unresolved_disposition_scope_decisions"]
        or summary["host_binding_gaps"]
        or summary["parse_errors"]
    ):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
