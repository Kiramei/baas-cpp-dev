#!/usr/bin/env python3
"""Fetch BAAS runtime resources from the locked release assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import time
import urllib.error
import urllib.request
import zipfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOCK = ROOT / "resources.lock.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Fetch BAAS runtime resources.")
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK, help="Path to resources.lock.json.")
    parser.add_argument("--output-root", type=Path, required=True, help="Aggregated resource output root.")
    parser.add_argument("--download-root", type=Path, required=True, help="Downloaded asset cache root.")
    parser.add_argument(
        "--resource",
        action="append",
        default=[],
        help="Resource name to fetch. Repeatable. Defaults to all resources.",
    )
    return parser.parse_args()


def load_lock(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as handle:
        data = json.load(handle)
    resources = data.get("resources")
    if not isinstance(resources, dict):
        raise ValueError(f"{path} does not contain a resources object")
    return resources


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_sha256(path: Path, expected: str) -> bool:
    return sha256_file(path).lower() == expected.lower()


def asset_name(url: str) -> str:
    name = url.rsplit("/", 1)[-1].split("?", 1)[0]
    if not name:
        raise ValueError(f"Cannot determine asset name from URL: {url}")
    return name


def download(url: str, destination: Path, expected_sha256: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and verify_sha256(destination, expected_sha256):
        print(f"[cache] {destination}")
        return

    tmp = destination.with_suffix(destination.suffix + ".tmp")
    if tmp.exists():
        tmp.unlink()

    request = urllib.request.Request(url, headers={"User-Agent": "BAAS-Cpp-resource-fetcher"})
    last_error: Exception | None = None
    for attempt in range(1, 4):
        try:
            print(f"[download] {url}")
            with urllib.request.urlopen(request, timeout=60) as response, tmp.open("wb") as handle:
                shutil.copyfileobj(response, handle)
            if not verify_sha256(tmp, expected_sha256):
                actual = sha256_file(tmp)
                raise RuntimeError(f"SHA256 mismatch for {url}: expected {expected_sha256}, got {actual}")
            tmp.replace(destination)
            return
        except (OSError, urllib.error.URLError, RuntimeError) as exc:
            last_error = exc
            if tmp.exists():
                tmp.unlink()
            if attempt < 3:
                time.sleep(2 * attempt)
    raise RuntimeError(f"Failed to download {url}: {last_error}")


def clean_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def safe_extract_zip(archive: Path, target: Path) -> None:
    target_resolved = target.resolve()
    with zipfile.ZipFile(archive) as zip_file:
        for member in zip_file.infolist():
            member_target = (target / member.filename).resolve()
            if not str(member_target).startswith(str(target_resolved) + os.sep) and member_target != target_resolved:
                raise RuntimeError(f"Unsafe archive member path: {member.filename}")
        zip_file.extractall(target)


def copy_file(source: Path, target_dir: Path) -> None:
    clean_dir(target_dir)
    shutil.copy2(source, target_dir / source.name)


def validate_outputs(output_root: Path, resource_name: str, resource: dict[str, Any]) -> None:
    outputs = resource.get("outputs", {})
    validation = resource.get("validation", {})
    required_groups = validation.get("required_outputs", [])
    missing: list[str] = []
    for group in required_groups:
        for relative in outputs.get(group, []):
            if not (output_root / relative).exists():
                missing.append(relative)
    if missing:
        missing_text = "\n  ".join(missing)
        raise RuntimeError(f"Resource {resource_name} is missing required outputs:\n  {missing_text}")


def fix_permissions(output_root: Path, resource_name: str) -> None:
    if resource_name in {"platform_tools_macos", "platform_tools_linux"}:
        adb = output_root / ("bin/MacOS/platform-tools/adb" if resource_name.endswith("macos") else "bin/Linux/platform-tools/adb")
        if adb.exists():
            adb.chmod(adb.stat().st_mode | 0o755)


def fetch_resource(name: str, resource: dict[str, Any], output_root: Path, download_root: Path) -> None:
    expected_sha256 = str(resource["sha256"])
    url = str(resource["url"])
    version = str(resource.get("version", "unknown"))
    archive = download_root / name / version / asset_name(url)
    download(url, archive, expected_sha256)

    target_dir = output_root / str(resource["target_dir"])
    resource_type = resource.get("type", "archive")
    if resource_type == "file":
        copy_file(archive, target_dir)
    elif resource_type == "archive":
        clean_dir(target_dir)
        safe_extract_zip(archive, target_dir)
    else:
        raise RuntimeError(f"Unsupported resource type for {name}: {resource_type}")

    fix_permissions(output_root, name)
    validate_outputs(output_root, name, resource)
    print(f"[ready] {name} -> {target_dir}")


def main() -> int:
    args = parse_args()
    resources = load_lock(args.lock)
    selected = args.resource or sorted(resources)
    unknown = [name for name in selected if name not in resources]
    if unknown:
        print(f"Unknown resources: {', '.join(unknown)}", file=sys.stderr)
        return 2

    args.output_root.mkdir(parents=True, exist_ok=True)
    args.download_root.mkdir(parents=True, exist_ok=True)
    for name in selected:
        fetch_resource(name, resources[name], args.output_root, args.download_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
