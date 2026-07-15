# BAAS Conan private recipes

This directory contains BAAS-owned Conan 2 recipes. BAAS CMake dependency
resolution is Conan-only; repository-local `dll/` and `lib/` fallback logic is
not used by CMake.

Export recipes:

```powershell
python deploy/conan/scripts/manage_recipes.py export
```

Install OCR dependencies and build with presets:

```powershell
conan install deploy/conan `
  -of build/conan/windows-msvc-release-ocr `
  -pr:h=deploy/conan/profiles/windows-msvc-release `
  -pr:h=deploy/conan/profiles/dependency-versions-default `
  -o onnxruntime_use_cuda=False `
  -o use_ffmpeg=False `
  -o use_benchmark=False `
  --build=missing `
  --no-remote

cmake --preset conan-windows-msvc-release-ocr
cmake --build --preset conan-windows-msvc-release-ocr
```

Linux, macOS, and Android OCR commands are documented in
`docs/conan-private-recipes-migration.md`.

Dependency versions are selected through top-level Conan conf values. Defaults
come from `profiles/dependency-versions-default`, which records the recommended
pinned versions. Only versions backed by `recipes/<dependency>/versions/*.yml`
can be selected.

The recommended cpp-httplib dependency is `0.50.1`, fetched from its immutable
GitHub tag with SHA-256
`6aabb9750df0a779c7470f3a22753cee3dfeec580c44201aff1bf057aa91fcbc`.
Older checked-in 0.18.0, 0.20.1, and 0.28.0 metadata remains selectable for
reproduction. `conan create deploy/conan/recipes/baas-cpp-httplib --no-remote`
also builds its `test_package`, which verifies that the public header is
consumable and the BAAS target propagates its required WebSocket payload
definition. The repository HTTP upgrade contract owns the exact 0.50.1 version
and multipart API assertions so generated legacy recipes remain reproducible
with their older headers.
