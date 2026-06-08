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
