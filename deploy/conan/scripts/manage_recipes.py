#!/usr/bin/env python3
"""Manage BAAS private Conan recipes."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml


ROOT = Path(__file__).resolve().parents[3]
CONAN_ROOT = ROOT / "deploy" / "conan"
RECIPES_ROOT = CONAN_ROOT / "recipes"
GENERATED_RECIPES_ROOT = ROOT / "build" / "conan" / "generated-recipes"

EXPORT_ORDER = [
    "baas-lz4",
    "baas-nlohmann-json",
    "baas-cpp-httplib",
    "baas-spdlog",
    "baas-simdutf",
    "baas-benchmark",
    "baas-opencv",
    "baas-ffmpeg",
    "baas-onnxruntime",
]


@dataclass(frozen=True)
class Recipe:
    directory: Path
    name: str
    version: str

    @property
    def reference(self) -> str:
        return f"{self.name}/{self.version}"

    @property
    def relative_directory(self) -> Path:
        return self.directory.relative_to(ROOT)

    def conandata(self) -> dict[str, Any]:
        path = self.directory / "conandata.yml"
        return yaml.safe_load(path.read_text(encoding="utf-8")) or {}

    def fixed_conandata_path(self, version: str) -> Path:
        return self.directory / "versions" / f"{version}.yml"

    @property
    def conf_key(self) -> str:
        return f"{self.name.removeprefix('baas-').replace('-', '_')}_version"

    def fixed_versions(self) -> list[str]:
        versions_dir = self.directory / "versions"
        return sorted((path.stem for path in versions_dir.glob("*.yml")), key=version_sort_key)


def parse_recipe(path: Path) -> Recipe:
    conanfile = path / "conanfile.py"
    text = conanfile.read_text(encoding="utf-8")
    name = re.search(r'^\s*name\s*=\s*["\']([^"\']+)["\']', text, re.MULTILINE)
    version = re.search(r'^\s*version\s*=\s*["\']([^"\']+)["\']', text, re.MULTILINE)
    if not name or not version:
        raise ValueError(f"cannot determine recipe reference from {conanfile}")
    return Recipe(path, name.group(1), version.group(1))


def discover_recipes() -> list[Recipe]:
    recipes: list[Recipe] = []
    for name in EXPORT_ORDER:
        path = RECIPES_ROOT / name
        if not (path / "conanfile.py").exists():
            raise FileNotFoundError(f"missing recipe: {path / 'conanfile.py'}")
        recipes.append(parse_recipe(path))
    return recipes


def selected_recipes(recipes: list[Recipe], only: list[str]) -> list[Recipe]:
    if not only:
        return recipes
    selected = []
    wanted = set(only)
    for recipe in recipes:
        if recipe.directory.name in wanted or recipe.name in wanted or recipe.reference in wanted:
            selected.append(recipe)
    found = {r.directory.name for r in selected} | {r.name for r in selected} | {r.reference for r in selected}
    missing = wanted - found
    if missing:
        raise ValueError("unknown recipe selector(s): " + ", ".join(sorted(missing)))
    return selected


def recipe_aliases(recipe: Recipe) -> set[str]:
    aliases = {recipe.directory.name, recipe.name, recipe.reference, recipe.conf_key}
    if recipe.conf_key.endswith("_version"):
        aliases.add(recipe.conf_key[: -len("_version")])
    return aliases


def find_recipe(recipes: list[Recipe], selector: str) -> Recipe | None:
    for recipe in recipes:
        if selector in recipe_aliases(recipe):
            return recipe
    return None


def parse_version_specs(recipes: list[Recipe], specs: list[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for spec in specs:
        if "=" not in spec:
            raise ValueError(f"version selector must be '<dependency>=<version>': {spec}")
        selector, version = spec.split("=", 1)
        selector = selector.strip()
        version = version.strip()
        if not selector or not version:
            raise ValueError(f"version selector must be '<dependency>=<version>': {spec}")
        recipe = find_recipe(recipes, selector)
        if recipe is None:
            raise ValueError(f"unknown dependency version selector: {selector}")
        validate_fixed_version(recipe, version)
        result[recipe.name] = version
    return result


def validate_fixed_version(recipe: Recipe, version: str) -> None:
    allowed = recipe.fixed_versions()
    if not allowed:
        raise FileNotFoundError(f"missing fixed source metadata directory for {recipe.name}: {recipe.directory / 'versions'}")
    if version not in allowed:
        raise ValueError(
            f"{recipe.name}/{version} is not an allowed fixed-source version. "
            f"Allowed versions: {', '.join(allowed)}"
        )
    metadata = recipe.fixed_conandata_path(version)
    if not metadata.exists():
        raise FileNotFoundError(f"missing fixed source metadata for {recipe.name}/{version}: {metadata}")


def version_sort_key(version: str) -> tuple[tuple[int, object], ...]:
    tokens = re.findall(r"\d+|[A-Za-z]+", version)
    return tuple((0, int(token)) if token.isdigit() else (1, token) for token in tokens)


def ensure_under(path: Path, parent: Path) -> None:
    resolved_path = path.resolve()
    resolved_parent = parent.resolve()
    try:
        resolved_path.relative_to(resolved_parent)
    except ValueError as exc:
        raise ValueError(f"refusing to write generated recipe outside {resolved_parent}: {resolved_path}") from exc


def patch_recipe_version(text: str, version: str) -> str:
    patched = re.sub(r'^(\s*version\s*=\s*)["\'][^"\']+["\']', rf'\1"{version}"', text, count=1, flags=re.MULTILINE)
    if patched == text:
        raise ValueError("cannot patch generated recipe version")
    return patched


def generated_recipe(recipe: Recipe, version: str, dry_run: bool) -> Recipe:
    dest = GENERATED_RECIPES_ROOT / recipe.name / version
    if dry_run:
        return Recipe(dest, recipe.name, version)

    ensure_under(dest, GENERATED_RECIPES_ROOT)
    if dest.exists():
        shutil.rmtree(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(recipe.directory, dest, ignore=shutil.ignore_patterns("versions", "__pycache__"))
    conanfile = dest / "conanfile.py"
    conanfile.write_text(patch_recipe_version(conanfile.read_text(encoding="utf-8"), version), encoding="utf-8")
    shutil.copy2(recipe.fixed_conandata_path(version), dest / "conandata.yml")
    return Recipe(dest, recipe.name, version)


def first_source(data: dict[str, Any], version: str) -> tuple[str, str]:
    version_data = data.get("sources", {}).get(version, {})
    if isinstance(version_data, dict) and "url" in version_data:
        return str(version_data.get("url", "")), str(version_data.get("sha256", ""))
    if isinstance(version_data, dict):
        for value in version_data.values():
            if isinstance(value, dict) and value.get("url"):
                return str(value.get("url", "")), str(value.get("sha256", ""))
    return "", ""


def inspect_rows(recipes: list[Recipe]) -> list[dict[str, str]]:
    result = []
    for recipe in recipes:
        url, sha256 = first_source(recipe.conandata(), recipe.version)
        result.append(
            {
                "recipe": recipe.directory.name,
                "reference": recipe.reference,
                "source_url": url,
                "sha256": sha256,
            }
        )
    return result


def add_recipe_selector(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--only", action="append", default=[], help="Select this recipe directory, package name, or reference. Can be repeated.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="List private recipe references.")
    add_recipe_selector(list_parser)

    inspect_parser = subparsers.add_parser("inspect", help="Print recipe source metadata.")
    add_recipe_selector(inspect_parser)
    inspect_parser.add_argument("--format", choices=("markdown", "json"), default="markdown")

    export_parser = subparsers.add_parser("export", help="Export private recipes into the Conan cache.")
    add_recipe_selector(export_parser)
    export_parser.add_argument(
        "--version",
        action="append",
        default=[],
        help="Export an additional fixed-source recipe version as '<dependency>=<version>'. Can be repeated.",
    )
    export_parser.add_argument("--dry-run", action="store_true", help="Print commands without executing them.")

    return parser.parse_args()


def cmd_list(recipes: list[Recipe]) -> int:
    for recipe in recipes:
        print(f"{recipe.reference}\t{recipe.relative_directory}")
    return 0


def cmd_inspect(recipes: list[Recipe], output_format: str) -> int:
    rows = inspect_rows(recipes)
    if output_format == "json":
        print(json.dumps(rows, indent=2, ensure_ascii=False))
        return 0

    print("| recipe | reference | first source URL | sha256 |")
    print("| --- | --- | --- | --- |")
    for item in rows:
        print("| {recipe} | {reference} | {source_url} | {sha256} |".format(**item))
    return 0


def export_recipes(recipes: list[Recipe], version_overrides: dict[str, str], dry_run: bool) -> list[Recipe]:
    selected_names = {recipe.name for recipe in recipes}
    unknown = sorted(set(version_overrides) - selected_names)
    if unknown:
        raise ValueError("--version specified dependencies excluded by --only: " + ", ".join(unknown))

    result: list[Recipe] = []
    seen: set[str] = set()
    for recipe in recipes:
        validate_fixed_version(recipe, recipe.version)
        result.append(recipe)
        seen.add(recipe.reference)

    for recipe in recipes:
        version = version_overrides.get(recipe.name)
        if not version or version == recipe.version:
            continue
        generated = generated_recipe(recipe, version, dry_run)
        if generated.reference not in seen:
            result.append(generated)
            seen.add(generated.reference)
    return result


def cmd_export(recipes: list[Recipe], dry_run: bool, version_specs: list[str]) -> int:
    version_overrides = parse_version_specs(recipes, version_specs)
    for recipe in export_recipes(recipes, version_overrides, dry_run):
        command = ["conan", "export", str(recipe.directory)]
        print(f"{recipe.reference}: {' '.join(command)}")
        if dry_run:
            continue
        result = subprocess.run(command, cwd=ROOT)
        if result.returncode != 0:
            return result.returncode
    return 0


def main() -> int:
    args = parse_args()
    try:
        recipes = selected_recipes(discover_recipes(), args.only)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.command == "list":
        return cmd_list(recipes)
    if args.command == "inspect":
        return cmd_inspect(recipes, args.format)
    if args.command == "export":
        try:
            return cmd_export(recipes, args.dry_run, args.version)
        except (OSError, ValueError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 2

    print(f"error: unknown command: {args.command}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
