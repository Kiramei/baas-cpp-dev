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
  -pr:b=deploy/conan/profiles/windows-msvc-release `
  -o "&:dependency_set=ocr" `
  -o onnxruntime_use_cuda=False `
  -o use_ffmpeg=False `
  -o use_benchmark=False `
  --build=missing `
  --no-remote

cmake --preset conan-windows-msvc-release-ocr
cmake --build --preset conan-windows-msvc-release-ocr
```

The final `--no-remote` install assumes that the exact public closure of
`baas-cpp-httplib/0.50.1` has already been provisioned. On a clean runner,
create the checked-in cpp-httplib recipe first with the same host/build
profiles and `--build=missing`; that recipe pins OpenSSL 3.5.7. This is an
explicit online provisioning phase, not an implicit system fallback.

The WebAssembly workflow uses `dependency_set=afwc`, which resolves only the
four dependencies linked by `BAAS_workflow_checker`. Its Emscripten host profile
is `profiles/emscripten-wasm-release`; the native Linux build context remains
`profiles/linux-gcc-release`. OpenCV, spdlog, and simdutf are static in the wasm
host context. The activated EMSDK must be exactly 6.0.3 so the profile's
`compiler.version` continues to identify the Emscripten ABI.

Linux, macOS, and Android OCR commands are documented in
`docs/conan-private-recipes-migration.md`.

Dependency versions are selected through top-level Conan conf values. Defaults
come from `profiles/dependency-versions-default`, which records the recommended
pinned versions. Only versions backed by `recipes/<dependency>/versions/*.yml`
can be selected.

ZIP archive support uses the pinned `baas-miniz/3.1.2` private recipe. It
verifies the immutable official `richgel999/miniz` tag archive with SHA-256
`98468f8924934b723276680f85238b6c78bf1f8b49b4459cc9b7214a20e2e9fb`,
builds a static library on Windows, Linux, and Android, and publishes only the
stable `BAAS::miniz` CMake target to consumers.

The authentication and key-management layer uses the private
`baas-libsodium/1.0.22` source recipe and links only its `BAAS::sodium` CMake
target. The recipe verifies the official release tarball with SHA-256
`adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349`, builds
the official MSBuild solution on MSVC, and uses the release tarball's generated
Autotools files on Linux, macOS, and Android. It does not substitute a Windows
prebuilt archive. Static packages propagate `SODIUM_STATIC` through the target.
The top-level dependency graph keeps libsodium opt-in so existing OCR-only
installs do not acquire an Autotools dependency. Pass
`-o "&:use_libsodium=True"` for service-auth builds.

Windows-hosted Android cross builds require a POSIX `bash` and `make` on PATH;
pass the local bash explicitly with
`-c:a tools.microsoft.bash:path=<path-to-bash.exe>` and identify it with
`-c:a tools.microsoft.bash:subsystem=msys2` (or the matching Conan subsystem).
These are host tools, not Conan requirements, so the recipe remains usable
under the mandatory `--no-remote` private-recipe policy.
Android builds force unversioned sonames, as required by upstream. Toolchain
contract generation has been checked for arm64-v8a and x86_64; actual Android
packages still require the POSIX tools above and are not claimed as built here.

The recommended cpp-httplib dependency is `0.50.1`, fetched from its immutable
GitHub tag with SHA-256
`6aabb9750df0a779c7470f3a22753cee3dfeec580c44201aff1bf057aa91fcbc`.
Older checked-in 0.18.0, 0.20.1, and 0.28.0 metadata remains selectable for
reproduction. `conan create deploy/conan/recipes/baas-cpp-httplib --build=missing`
also builds its `test_package`, which verifies that the public header is
consumable and the BAAS target propagates its required HTTPS and WebSocket
definitions. The package pins static OpenSSL 3.5.7 with applications and zlib
disabled; after that exact closure has been provisioned, downstream installs
can remain offline. The repository HTTP upgrade contract owns the exact 0.50.1 version
and multipart API assertions so generated legacy recipes remain reproducible
with their older headers.
