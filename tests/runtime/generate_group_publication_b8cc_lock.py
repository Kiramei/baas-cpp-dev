"""Generate the frozen ten-variant publication lock from an exact Git commit.

This is an integration-test inventory tool, not runtime discovery.  The C++
production gate independently binds the resulting ordered member identities.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import pathlib
import subprocess
from typing import Any


COMMIT = "b8cc64705feb0067aba349892031a450d1bf8083"
PROFILES = ("CN", "Global_en-us", "Global_ko-kr", "Global_zh-tw", "JP")
RGB = {
    "navigation": (
        ("main_page", "rgb/main-page"),
        ("relationship_rank_up", "rgb/relationship-rank-up"),
        ("area_rank_up", "rgb/area-rank-up"),
        ("level_up", "rgb/level-up"),
        ("reward_acquired", "rgb/reward-acquired"),
        ("loadingNotWhite", "rgb/loading-not-white"),
        ("loadingWhite", "rgb/loading-white"),
    ),
    "group": (
        ("main_page", "rgb/main-page"),
        ("relationship_rank_up", "rgb/relationship-rank-up"),
        ("level_up", "rgb/level-up"),
        ("reward_acquired", "rgb/reward-acquired"),
        ("loadingNotWhite", "rgb/loading-not-white"),
        ("loadingWhite", "rgb/loading-white"),
    ),
}
IDS = {
    "navigation": "procedure-support/navigation.to-main-page/v1",
    "group": "procedure-support/group.open/v1",
}
_STORE: "GitObjectStore | None" = None
_DESCRIPTORS: dict[str, dict[str, Any]] = {}


class GitObjectStore:
    def __init__(self, repository: pathlib.Path) -> None:
        self.process = subprocess.Popen(
            ["git", "-C", str(repository), "cat-file", "--batch"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.cache: dict[str, tuple[str, bytes]] = {}

    def read(self, path: str) -> tuple[str, bytes]:
        if path in self.cache:
            return self.cache[path]
        assert self.process.stdin is not None and self.process.stdout is not None
        self.process.stdin.write(f"{COMMIT}:{path}\n".encode())
        self.process.stdin.flush()
        header = self.process.stdout.readline().decode().strip().split()
        if len(header) == 2 and header[1] == "missing":
            raise KeyError(path)
        if len(header) != 3 or header[1] != "blob":
            raise RuntimeError(f"unexpected git object for {path}: {header}")
        size = int(header[2])
        value = self.process.stdout.read(size)
        if self.process.stdout.read(1) != b"\n" or len(value) != size:
            raise RuntimeError(f"truncated git object for {path}")
        result = (header[0], value)
        self.cache[path] = result
        return result


def git(repository: pathlib.Path, *arguments: str, binary: bool = False) -> Any:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ).stdout
    return result if binary else result.decode("utf-8").strip()


def blob(repository: pathlib.Path, path: str) -> bytes:
    del repository
    assert _STORE is not None
    return _STORE.read(path)[1]


def descriptor(repository: pathlib.Path, path: str) -> dict[str, Any]:
    if path in _DESCRIPTORS:
        return _DESCRIPTORS[path]
    assert _STORE is not None
    oid, value = _STORE.read(path)
    result = {
        "path": path,
        "oid": oid,
        "size": len(value),
        "sha256": hashlib.sha256(value).hexdigest(),
    }
    _DESCRIPTORS[path] = result
    return result


def literal_assignments(source: bytes, function: str | None, name: str) -> list[Any]:
    tree = ast.parse(source.decode("utf-8"))
    root: ast.AST = tree
    if function is not None:
        root = next(
            node
            for node in ast.walk(tree)
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
            and node.name == function
        )
    result: list[Any] = []
    for node in ast.walk(root):
        if not isinstance(node, ast.Assign):
            continue
        if any(isinstance(target, ast.Name) and target.id == name for target in node.targets):
            try:
                result.append(ast.literal_eval(node.value))
            except (ValueError, TypeError):
                pass
    return result


def procedure_features(repository: pathlib.Path, profile: str) -> dict[str, set[str]]:
    server = profile if profile in ("CN", "JP") else "Global"
    baas_thread = blob(repository, "core/Baas_thread.py")
    picture = blob(repository, "core/picture.py")
    group_module = blob(repository, "module/group.py")
    navigation = set(literal_assignments(baas_thread, "to_main_page", "img_reactions")[0])
    navigation.update(literal_assignments(baas_thread, "to_main_page", "update")[0][server])
    popup_tables = [
        value
        for value in literal_assignments(
            picture, "deal_with_pop_ups", "common_task_img_reactions"
        )
        if isinstance(value, dict) and set(value) == {"CN", "JP", "Global"}
    ]
    navigation.update(popup_tables[0][server])
    group = set(literal_assignments(group_module, "to_group", "img_ends")[0])
    group.update(literal_assignments(group_module, "to_group", "img_possible")[0])
    group.update(literal_assignments(picture, None, "GAME_ONE_TIME_POP_UPS")[0][server])
    group.update(popup_tables[0][server])
    return {"navigation": navigation, "group": group}


def active_images(repository: pathlib.Path, profile: str) -> dict[str, dict[str, Any]]:
    prefix = f"src/images/{profile}/x_y_range/"
    paths = git(repository, "ls-tree", "-r", "--name-only", COMMIT, "--", prefix).splitlines()
    result: dict[str, dict[str, Any]] = {}
    for crop_path in paths:
        tree = ast.parse(blob(repository, crop_path).decode("utf-8"))
        values: dict[str, Any] = {}
        for node in tree.body:  # top-level active assignments only
            if not isinstance(node, ast.Assign):
                continue
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id in {
                    "prefix",
                    "path",
                    "x_y_range",
                }:
                    values[target.id] = ast.literal_eval(node.value)
        if set(values) != {"prefix", "path", "x_y_range"}:
            continue
        for name, crop in values["x_y_range"].items():
            feature = f"{values['prefix']}_{name}"
            png_path = f"src/images/{profile}/{values['path']}/{name}.png"
            try:
                assert _STORE is not None
                _STORE.read(png_path)
            except KeyError:
                continue
            result[feature] = {
                "crop": list(crop),
                "crop_path": crop_path,
                "png_path": png_path,
            }
    return result


def make_bundle(
    repository: pathlib.Path, profile: str, kind: str, features: set[str],
    images: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    bundle_id = IDS[kind]
    graph_id = (
        "feature/navigation.to-main-page" if kind == "navigation" else "feature/group.open"
    )
    members: list[dict[str, Any]] = [{"id": graph_id, "kind": "feature-graph"}]
    rgb_source = descriptor(repository, f"src/rgb_feature/{profile}.json")
    for feature, member_id in sorted(RGB[kind], key=lambda item: item[1]):
        members.append(
            {
                "id": member_id,
                "kind": "rgb-range-set",
                "feature": feature,
                "source": rgb_source,
                "source_key": feature,
            }
        )
    selected = sorted(features.intersection(images), key=lambda value: f"image/{value.lower()}")
    for feature in selected:
        image = images[feature]
        members.append(
            {
                "id": f"image/{feature.lower()}",
                "kind": "png-template",
                "feature": feature,
                "source": descriptor(repository, image["png_path"]),
                "crop_source": descriptor(repository, image["crop_path"]),
                "crop": image["crop"],
                "threshold_milli": 800,
                "mean_rgb_tolerance": 20,
            }
        )
    slug = "navigation.to-main-page" if kind == "navigation" else "group.open"
    return {
        "bundle_id": bundle_id,
        "locale": profile,
        "profile": profile,
        "output_path": f"payload/{slug}.{profile}.bundle",
        "member_count": len(members),
        "members": members,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    git(arguments.repository, "cat-file", "-e", f"{COMMIT}^{{commit}}")
    global _STORE
    _STORE = GitObjectStore(arguments.repository)
    bundles = []
    for profile in PROFILES:
        features = procedure_features(arguments.repository, profile)
        images = active_images(arguments.repository, profile)
        for kind in ("group", "navigation"):
            bundles.append(
                make_bundle(arguments.repository, profile, kind, features[kind], images)
            )
    bundles.sort(key=lambda item: (item["bundle_id"], item["profile"]))
    lock = {
        "schema": "baas.group-publication-lock/v1",
        "compiler": {"schema": "baas.runtime-publisher/v1", "version": 1},
        "source": {"commit": COMMIT},
        "bundles": bundles,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(lock, ensure_ascii=False, separators=(",", ":")), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
