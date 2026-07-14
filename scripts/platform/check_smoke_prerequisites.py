#!/usr/bin/env python3
"""Read-only BAAS platform smoke prerequisite inventory.

This checker only inspects repository declarations, environment variables,
filesystem entries, executable discovery, and version output.  It never starts
an emulator, invokes ``adb devices``, connects to a device, installs an APK, or
changes SDK/user configuration.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Callable, Iterable, Mapping, Sequence


SCHEMA_VERSION = 1
PROFILE_IDS = (
    "windows-foundation",
    "windows-desktop",
    "android-arm64",
    "android-x86_64",
    "linux-foundation",
    "macos-foundation",
)
STATUSES = ("discovered", "available", "missing", "not_run")
VERSION_RE = re.compile(r"(?<!\d)(\d+(?:\.\d+){1,3}(?:[-+._a-zA-Z0-9]*)?)")


def _normalize_host(system: str, machine: str) -> tuple[str, str]:
    os_name = {
        "windows": "windows",
        "linux": "linux",
        "darwin": "macos",
    }.get(system.lower(), system.lower() or "unknown")
    arch = {
        "amd64": "x86_64",
        "x86_64": "x86_64",
        "aarch64": "arm64",
        "arm64": "arm64",
    }.get(machine.lower(), machine.lower() or "unknown")
    return os_name, arch


def _safe_version(text: str) -> str | None:
    match = VERSION_RE.search(text)
    return match.group(1) if match else None


def _run_version(path: Path, args: Sequence[str]) -> str | None:
    try:
        completed = subprocess.run(
            [str(path), *args],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=8,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    return _safe_version(f"{completed.stdout}\n{completed.stderr}")


def _run_output(path: Path, args: Sequence[str]) -> str | None:
    try:
        completed = subprocess.run(
            [str(path), *args],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=8,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    return f"{completed.stdout}\n{completed.stderr}"


def _check(
    check_id: str,
    profile_id: str,
    status: str,
    evidence: str,
    *,
    required: bool = True,
    version: str | None = None,
) -> dict[str, object]:
    if status not in STATUSES:
        raise ValueError(f"invalid status: {status}")
    item: dict[str, object] = {
        "id": check_id,
        "profile": profile_id,
        "requirement": "required" if required else "optional",
        "status": status,
        "evidence": evidence,
    }
    if version:
        item["version"] = version
    return item


def _repo_declaration(
    root: Path,
    profile_id: str,
    check_id: str,
    relative_path: str,
    needle: str,
    evidence: str,
    *,
    required: bool = True,
) -> dict[str, object]:
    path = root / relative_path
    present = path.is_file() and needle in path.read_text(encoding="utf-8")
    return _check(
        check_id,
        profile_id,
        "discovered" if present else "missing",
        evidence if present else f"missing repository declaration: {relative_path}",
        required=required,
    )


def _find_executable(
    name: str,
    which: Callable[[str], str | None],
    candidates: Iterable[Path] = (),
) -> tuple[Path | None, str | None]:
    resolved = which(name)
    if resolved:
        return Path(resolved), "PATH"
    for candidate in candidates:
        if candidate.is_file():
            return candidate, "platform installation"
    return None, None


def _tool_check(
    profile_id: str,
    check_id: str,
    display_name: str,
    executable_name: str,
    which: Callable[[str], str | None],
    *,
    version_args: Sequence[str] = ("--version",),
    candidates: Iterable[Path] = (),
    required: bool = True,
) -> dict[str, object]:
    path, source = _find_executable(executable_name, which, candidates)
    if path is None:
        return _check(
            check_id,
            profile_id,
            "missing",
            f"{display_name} executable was not discovered",
            required=required,
        )
    version = _run_version(path, version_args)
    return _check(
        check_id,
        profile_id,
        "available",
        f"{display_name} executable discovered via {source}",
        required=required,
        version=version,
    )


def _python_check(profile_id: str) -> dict[str, object]:
    return _check(
        "tool.python",
        profile_id,
        "available",
        "running standard-library Python interpreter",
        version=".".join(str(part) for part in sys.version_info[:3]),
    )


def _android_sdk_candidates(environ: Mapping[str, str], host_os: str) -> list[Path]:
    candidates: list[Path] = []
    for variable in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
        value = environ.get(variable, "").strip()
        if value:
            candidates.append(Path(value))
    home = Path(environ.get("HOME") or environ.get("USERPROFILE") or "~").expanduser()
    if host_os == "windows" and environ.get("LOCALAPPDATA"):
        candidates.append(Path(environ["LOCALAPPDATA"]) / "Android" / "Sdk")
    candidates.extend((home / "Android" / "Sdk", home / "Library" / "Android" / "sdk"))
    unique: list[Path] = []
    for candidate in candidates:
        if candidate not in unique:
            unique.append(candidate)
    return unique


def _find_android_sdk(environ: Mapping[str, str], host_os: str) -> Path | None:
    for candidate in _android_sdk_candidates(environ, host_os):
        if (candidate / "platform-tools").is_dir() or (candidate / "ndk").is_dir():
            return candidate
    return None


def _version_key(value: str) -> tuple[int, ...]:
    numbers = [int(part) for part in re.findall(r"\d+", value)]
    return tuple(numbers) if numbers else (0,)


def _find_ndk(sdk: Path | None, environ: Mapping[str, str]) -> Path | None:
    explicit = environ.get("ANDROID_NDK_HOME", "").strip()
    if explicit and (Path(explicit) / "source.properties").is_file():
        return Path(explicit)
    if sdk is None or not (sdk / "ndk").is_dir():
        return None
    versions = [entry for entry in (sdk / "ndk").iterdir() if entry.is_dir()]
    return max(versions, key=lambda entry: _version_key(entry.name)) if versions else None


def _ndk_version(ndk: Path | None) -> str | None:
    if ndk is None:
        return None
    properties = ndk / "source.properties"
    if not properties.is_file():
        return _safe_version(ndk.name)
    match = re.search(
        r"^Pkg\.Revision\s*=\s*(\S+)",
        properties.read_text(encoding="utf-8", errors="replace"),
        flags=re.MULTILINE,
    )
    return match.group(1) if match else _safe_version(ndk.name)


def _android_host_tag(host_os: str, host_arch: str) -> str:
    if host_os == "windows":
        return "windows-x86_64"
    if host_os == "macos":
        return "darwin-arm64" if host_arch == "arm64" else "darwin-x86_64"
    return "linux-x86_64"


def _android_tool_candidates(sdk: Path | None, host_os: str, tool: str) -> list[Path]:
    if sdk is None:
        return []
    suffix = ".exe" if host_os == "windows" else ""
    if tool == "adb":
        return [sdk / "platform-tools" / f"adb{suffix}"]
    if tool == "emulator":
        return [sdk / "emulator" / f"emulator{suffix}"]
    if tool == "sdkmanager":
        name = "sdkmanager.bat" if host_os == "windows" else "sdkmanager"
        return [sdk / "cmdline-tools" / "latest" / "bin" / name]
    return []


def _java_candidates(environ: Mapping[str, str], host_os: str) -> list[Path]:
    executable = "java.exe" if host_os == "windows" else "java"
    candidates: list[Path] = []
    java_home = environ.get("JAVA_HOME", "").strip()
    if java_home:
        candidates.append(Path(java_home) / "bin" / executable)
    if host_os == "windows":
        candidates.extend(
            (
                Path("C:/Program Files/Android/Android Studio/jbr/bin/java.exe"),
                Path("C:/Program Files/Android/Android Studio/jre/bin/java.exe"),
            )
        )
    elif host_os == "macos":
        candidates.append(Path("/Applications/Android Studio.app/Contents/jbr/Contents/Home/bin/java"))
    return candidates


def _java_check(
    profile_id: str,
    environ: Mapping[str, str],
    host_os: str,
    which: Callable[[str], str | None],
) -> dict[str, object]:
    path, source = _find_executable("java", which, _java_candidates(environ, host_os))
    if path is None:
        return _check("tool.java", profile_id, "missing", "JDK 17+ executable was not discovered")
    version = _run_version(path, ("-version",))
    major = int(version.split(".", 1)[0]) if version and version.split(".", 1)[0].isdigit() else None
    valid = major is not None and major >= 17
    return _check(
        "tool.java",
        profile_id,
        "available" if valid else "missing",
        f"JDK executable discovered via {source}" if valid else "discovered Java does not report JDK 17+",
        version=version,
    )


def _has_system_image(sdk: Path | None, abi: str) -> bool:
    root = sdk / "system-images" if sdk else None
    if root is None or not root.is_dir():
        return False
    return any("android-36" in package.parts and abi in package.parts for package in root.rglob("package.xml"))


def _has_avd(environ: Mapping[str, str], abi: str) -> bool:
    home = Path(environ.get("HOME") or environ.get("USERPROFILE") or "~").expanduser()
    avd_home = Path(environ.get("ANDROID_AVD_HOME", "").strip()) if environ.get("ANDROID_AVD_HOME") else home / ".android" / "avd"
    if not avd_home.is_dir():
        return False
    for config in avd_home.glob("*.avd/config.ini"):
        text = config.read_text(encoding="utf-8", errors="replace")
        abi_matches = re.search(rf"^abi\.type\s*=\s*{re.escape(abi)}\s*$", text, flags=re.MULTILINE)
        api_matches = "android-36" in text
        if abi_matches and api_matches:
            return True
    return False


def _sibling_repository(root: Path, name: str) -> Path:
    candidates = (root.parent / name, root.parent.parent / name)
    return next((candidate for candidate in candidates if candidate.is_dir()), candidates[0])


def _rust_target_check(
    profile_id: str,
    target: str,
    which: Callable[[str], str | None],
) -> dict[str, object]:
    path, source = _find_executable("rustup", which)
    if path is None:
        return _check("rust.android_target", profile_id, "missing", f"rustup and target {target} were not discovered")
    output = _run_output(path, ("target", "list", "--installed"))
    installed = bool(output and target in {line.strip() for line in output.splitlines()})
    return _check(
        "rust.android_target",
        profile_id,
        "available" if installed else "missing",
        f"Rust target {target} is installed via {source}" if installed else f"Rust target {target} is not installed",
    )


def _host_requirement(profile_id: str, host_os: str, host_arch: str, expected_os: str) -> list[dict[str, object]]:
    applicable = host_os == expected_os and (expected_os != "windows" or host_arch == "x86_64")
    status = "available" if applicable else "not_run"
    return [
        _check(
            "host.platform",
            profile_id,
            status,
            f"host is {host_os}/{host_arch}" if applicable else f"requires {expected_os} host; current host is {host_os}/{host_arch}",
        )
    ]


def _foundation_checks(
    root: Path,
    profile_id: str,
    host_os: str,
    host_arch: str,
    expected_os: str,
    which: Callable[[str], str | None],
) -> list[dict[str, object]]:
    checks = _host_requirement(profile_id, host_os, host_arch, expected_os)
    checks.append(
        _repo_declaration(
            root,
            profile_id,
            "repository.foundation",
            "CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.22)",
            "CMake foundation requires 3.22 and exposes standalone script/service tests",
        )
    )
    optional_profile = {
        "windows": ("deploy/conan/profiles/windows-msvc-release", "compiler=msvc", "Windows full-build Conan profile discovered"),
        "linux": ("deploy/conan/profiles/linux-gcc-release", "compiler=gcc", "Linux full-build Conan profile discovered"),
        "macos": ("deploy/conan/profiles/macos-appleclang-release", "compiler=apple-clang", "macOS full-build Conan profile discovered"),
    }[expected_os]
    checks.append(
        _repo_declaration(
            root,
            profile_id,
            "repository.full_build_profile",
            optional_profile[0],
            optional_profile[1],
            optional_profile[2],
            required=False,
        )
    )
    if host_os != expected_os:
        for check_id, name in (("tool.cmake", "CMake"), ("tool.compiler", "host C++ compiler")):
            checks.append(_check(check_id, profile_id, "not_run", f"{name} is checked only on a matching hosted runner"))
        checks.append(_check("smoke.foundation", profile_id, "not_run", "foundation configure/build/CTest was not run"))
        return checks
    checks.extend((_python_check(profile_id), _tool_check(profile_id, "tool.cmake", "CMake", "cmake", which)))
    if expected_os == "windows":
        checks.extend(
            (
                _tool_check(profile_id, "tool.ninja", "Ninja", "ninja", which),
                _tool_check(profile_id, "tool.compiler", "MSVC cl", "cl", which, version_args=()),
            )
        )
    elif expected_os == "linux":
        checks.append(_tool_check(profile_id, "tool.compiler", "GCC 13", "g++-13", which))
    else:
        checks.append(_tool_check(profile_id, "tool.compiler", "AppleClang", "clang++", which))
    checks.append(_check("smoke.foundation", profile_id, "not_run", "checker does not configure, build, or run CTest"))
    return checks


def _windows_desktop_checks(
    root: Path,
    host_os: str,
    host_arch: str,
    which: Callable[[str], str | None],
) -> list[dict[str, object]]:
    profile_id = "windows-desktop"
    checks = _host_requirement(profile_id, host_os, host_arch, "windows")
    checks.extend(
        (
            _repo_declaration(root, profile_id, "repository.conan_profile", "deploy/conan/profiles/windows-msvc-release", "compiler=msvc", "Windows MSVC Conan profile declared"),
            _repo_declaration(root, profile_id, "repository.cmake_preset", "CMakePresets.json", "conan-windows-msvc-release-baas", "Windows BAAS Conan preset declared"),
        )
    )
    sibling = _sibling_repository(root, "baas-tauri")
    checks.append(
        _check(
            "repository.baas_tauri",
            profile_id,
            "discovered" if (sibling / "src-tauri" / "Cargo.toml").is_file() else "missing",
            "sibling baas-tauri source is present" if (sibling / "src-tauri" / "Cargo.toml").is_file() else "sibling baas-tauri source was not discovered",
        )
    )
    if host_os != "windows" or host_arch != "x86_64":
        checks.append(_check("smoke.desktop_pipe", profile_id, "not_run", "Windows desktop pipe/Tauri smoke requires a Windows x86_64 host"))
        return checks
    checks.extend(
        (
            _tool_check(profile_id, "tool.cmake", "CMake", "cmake", which),
            _tool_check(profile_id, "tool.ninja", "Ninja", "ninja", which),
            _tool_check(profile_id, "tool.msvc", "MSVC cl", "cl", which, version_args=()),
            _tool_check(profile_id, "tool.conan", "Conan", "conan", which, version_args=("--version",)),
            _tool_check(profile_id, "tool.bun", "Bun", "bun", which),
            _tool_check(profile_id, "tool.cargo", "Cargo", "cargo", which),
            _tool_check(profile_id, "tool.rustc", "Rust compiler", "rustc", which),
        )
    )
    checks.append(_check("smoke.desktop_pipe", profile_id, "not_run", "checker does not launch Tauri or the managed backend"))
    return checks


def _android_checks(
    root: Path,
    profile_id: str,
    abi: str,
    conan_arch: str,
    host_os: str,
    host_arch: str,
    environ: Mapping[str, str],
    which: Callable[[str], str | None],
) -> list[dict[str, object]]:
    suffix = "arm64-v8a" if abi == "arm64-v8a" else "x86_64"
    checks = [
        _repo_declaration(root, profile_id, "repository.conan_profile", f"deploy/conan/profiles/android-clang-{suffix}-release", "os.api_level=26", f"Android {abi} Conan profile declares API 26"),
        _repo_declaration(root, profile_id, "repository.conan_arch", f"deploy/conan/profiles/android-clang-{suffix}-release", f"arch={conan_arch}", f"Android profile maps {abi} to Conan {conan_arch}"),
        _repo_declaration(root, profile_id, "repository.cmake_preset", "CMakePresets.json", f"conan-android-clang-release-ocr-{suffix}", f"Android {abi} OCR CMake preset declared"),
    ]
    tauri = _sibling_repository(root, "baas-tauri")
    tauri_gradle = tauri / "src-tauri" / "gen" / "android" / "app" / "build.gradle.kts"
    tauri_manifest = tauri / "src-tauri" / "gen" / "android" / "app" / "src" / "main" / "AndroidManifest.xml"
    tauri_declared = (
        tauri_gradle.is_file()
        and "compileSdk = 36" in tauri_gradle.read_text(encoding="utf-8")
        and tauri_manifest.is_file()
        and "BaasForegroundService" in tauri_manifest.read_text(encoding="utf-8")
    )
    checks.append(
        _check(
            "repository.tauri_android",
            profile_id,
            "discovered" if tauri_declared else "missing",
            "sibling baas-tauri declares compile/target SDK 36 and the foreground service" if tauri_declared else "sibling baas-tauri Android Gradle/manifest assumptions were not discovered",
        )
    )
    sdk = _find_android_sdk(environ, host_os)
    ndk = _find_ndk(sdk, environ)
    checks.append(_check("android.sdk", profile_id, "available" if sdk else "missing", "Android SDK root discovered without recording its absolute path" if sdk else "Android SDK root was not discovered"))
    checks.append(
        _check(
            "android.platform_api36",
            profile_id,
            "available" if sdk and (sdk / "platforms" / "android-36").is_dir() else "missing",
            "Android platform 36 is installed for the Tauri compile/target SDK" if sdk and (sdk / "platforms" / "android-36").is_dir() else "Android platform 36 is not installed",
        )
    )
    checks.append(
        _check(
            "android.ndk",
            profile_id,
            "available" if ndk else "missing",
            "Android NDK discovered" if ndk else "Android NDK was not discovered",
            version=_ndk_version(ndk),
        )
    )
    host_tag = _android_host_tag(host_os, host_arch)
    wrapper_suffix = ".cmd" if host_os == "windows" else ""
    triple = "aarch64-linux-android" if abi == "arm64-v8a" else "x86_64-linux-android"
    wrapper = ndk / "toolchains" / "llvm" / "prebuilt" / host_tag / "bin" / f"{triple}26-clang++{wrapper_suffix}" if ndk else None
    checks.append(
        _check(
            "android.ndk_api26_wrapper",
            profile_id,
            "available" if wrapper and wrapper.is_file() else "missing",
            f"NDK {abi} API 26 compiler wrapper exists" if wrapper and wrapper.is_file() else f"NDK {abi} API 26 compiler wrapper is missing",
        )
    )
    clang_name = "clang++.exe" if host_os == "windows" else "clang++"
    clang = ndk / "toolchains" / "llvm" / "prebuilt" / host_tag / "bin" / clang_name if ndk else None
    clang_version = _run_version(clang, ("--version",)) if clang and clang.is_file() else None
    checks.append(
        _check(
            "android.ndk_clang21",
            profile_id,
            "available" if clang_version and clang_version.startswith("21.") else "missing",
            "NDK reports Clang 21 required by the Conan profile" if clang_version and clang_version.startswith("21.") else "NDK Clang 21 was not discovered",
            version=clang_version,
        )
    )
    checks.extend(
        (
            _tool_check(profile_id, "tool.cmake", "CMake", "cmake", which),
            _tool_check(profile_id, "tool.ninja", "Ninja", "ninja", which),
            _tool_check(profile_id, "tool.conan", "Conan", "conan", which),
            _java_check(profile_id, environ, host_os, which),
            _tool_check(profile_id, "tool.adb", "adb", "adb", which, candidates=_android_tool_candidates(sdk, host_os, "adb")),
            _tool_check(profile_id, "tool.emulator", "Android emulator", "emulator", which, version_args=("-version",), candidates=_android_tool_candidates(sdk, host_os, "emulator")),
            _tool_check(profile_id, "tool.sdkmanager", "Android sdkmanager", "sdkmanager", which, candidates=_android_tool_candidates(sdk, host_os, "sdkmanager")),
            _tool_check(profile_id, "tool.bun", "Bun", "bun", which),
            _tool_check(profile_id, "tool.cargo", "Cargo", "cargo", which),
            _tool_check(profile_id, "tool.rustc", "Rust compiler", "rustc", which),
        )
    )
    rust_target = "aarch64-linux-android" if abi == "arm64-v8a" else "x86_64-linux-android"
    checks.append(_rust_target_check(profile_id, rust_target, which))
    checks.append(
        _check(
            "android.system_image",
            profile_id,
            "available" if _has_system_image(sdk, abi) else "missing",
            f"an installed Android 36 system image declares {abi}" if _has_system_image(sdk, abi) else f"no installed Android 36 system image declares {abi}",
        )
    )
    checks.append(
        _check(
            "android.avd",
            profile_id,
            "available" if _has_avd(environ, abi) else "missing",
            f"an Android 36 AVD configuration declares {abi}" if _has_avd(environ, abi) else f"no Android 36 AVD configuration declares {abi}",
        )
    )
    checks.extend(
        (
            _check("smoke.android_emulator", profile_id, "not_run", "checker does not start an emulator or query a device"),
            _check("smoke.android_foreground", profile_id, "not_run", "foreground-service, notification, restart, and port 8190 checks were not run"),
            _check("smoke.android_jni", profile_id, "not_run", "JNI/native-library load and ABI packaging checks were not run"),
        )
    )
    return checks


def collect(
    root: Path,
    *,
    selected_profiles: Sequence[str] = PROFILE_IDS,
    environ: Mapping[str, str] | None = None,
    host_system: str | None = None,
    host_machine: str | None = None,
    which: Callable[[str], str | None] | None = None,
) -> dict[str, object]:
    environ = dict(os.environ if environ is None else environ)
    host_os, host_arch = _normalize_host(host_system or platform.system(), host_machine or platform.machine())
    which = shutil.which if which is None else which
    selected = tuple(dict.fromkeys(selected_profiles))
    unknown = sorted(set(selected) - set(PROFILE_IDS))
    if unknown:
        raise ValueError(f"unknown profiles: {', '.join(unknown)}")

    by_profile: dict[str, list[dict[str, object]]] = {
        "windows-foundation": _foundation_checks(root, "windows-foundation", host_os, host_arch, "windows", which),
        "windows-desktop": _windows_desktop_checks(root, host_os, host_arch, which),
        "android-arm64": _android_checks(root, "android-arm64", "arm64-v8a", "armv8", host_os, host_arch, environ, which),
        "android-x86_64": _android_checks(root, "android-x86_64", "x86_64", "x86_64", host_os, host_arch, environ, which),
        "linux-foundation": _foundation_checks(root, "linux-foundation", host_os, host_arch, "linux", which),
        "macos-foundation": _foundation_checks(root, "macos-foundation", host_os, host_arch, "macos", which),
    }
    checks = sorted(
        (item for profile_id in selected for item in by_profile[profile_id]),
        key=lambda item: (str(item["profile"]), str(item["id"])),
    )
    profiles: list[dict[str, object]] = []
    for profile_id in selected:
        items = by_profile[profile_id]
        required = [item for item in items if item["requirement"] == "required"]
        if any(item["status"] == "missing" for item in required):
            status = "missing"
        elif any(item["status"] == "not_run" for item in required):
            status = "not_run"
        elif any(item["status"] == "available" for item in required):
            status = "available"
        else:
            status = "discovered"
        profiles.append({"id": profile_id, "status": status})

    counts = {status: sum(item["status"] == status for item in checks) for status in STATUSES}
    return {
        "schema_version": SCHEMA_VERSION,
        "scope": "platform-smoke-prerequisites",
        "host": {"os": host_os, "arch": host_arch},
        "safety": {
            "starts_emulator": False,
            "queries_or_connects_device": False,
            "installs_apk": False,
            "modifies_user_configuration": False,
            "records_absolute_paths": False,
            "records_timestamp": False,
        },
        "profiles": profiles,
        "checks": checks,
        "summary": {"checks": len(checks), "status_counts": counts},
    }


def strict_failure(evidence: Mapping[str, object]) -> bool:
    checks = evidence.get("checks", [])
    return any(
        isinstance(item, Mapping)
        and item.get("requirement") == "required"
        and item.get("status") == "missing"
        for item in checks  # type: ignore[union-attr]
    )


def _json_text(evidence: Mapping[str, object]) -> str:
    return json.dumps(evidence, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def main(
    argv: Sequence[str] | None = None,
    *,
    environ: Mapping[str, str] | None = None,
    host_system: str | None = None,
    host_machine: str | None = None,
    which: Callable[[str], str | None] | None = None,
) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", action="append", choices=PROFILE_IDS, dest="profiles", help="profile to inspect; repeat to select multiple (default: all)")
    parser.add_argument("--output", type=Path, help="write deterministic JSON evidence to this path")
    parser.add_argument("--strict", action="store_true", help="return 1 when any selected required prerequisite is missing")
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[2]
    evidence = collect(
        root,
        selected_profiles=args.profiles or PROFILE_IDS,
        environ=environ,
        host_system=host_system,
        host_machine=host_machine,
        which=which,
    )
    rendered = _json_text(evidence)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(rendered)
    return 1 if args.strict and strict_failure(evidence) else 0


if __name__ == "__main__":
    raise SystemExit(main())
