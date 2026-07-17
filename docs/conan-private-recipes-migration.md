# BAAS Conan dependency workflow

## Goal

BAAS C++ dependencies are configured through BAAS-owned Conan 2 private recipes
under `deploy/conan`. CMake does not use repository-local `dll/` or `lib/`
dependency fallback logic. Any target that requests dependencies must be
configured with Conan-generated `CMakeToolchain` and `CMakeDeps` files.

The intended flow is:

```text
deploy/conan/recipes/<dependency>
  -> conan export
  -> conan install deploy/conan
  -> CMakeToolchain + CMakeDeps generated files
  -> find_package(baas-* CONFIG REQUIRED)
  -> BAAS::* imported targets
  -> CMake target link graph
```

## Common Commands

Export private recipes:

```powershell
python deploy/conan/scripts/manage_recipes.py export
```

Windows BAAS App, Release (CPU):

```powershell
conan install deploy/conan `
  -of build/conan/windows-msvc-release-baas `
  -pr:h=deploy/conan/profiles/windows-msvc-release `
  -pr:h=deploy/conan/profiles/dependency-versions-default `
  -pr:b=deploy/conan/profiles/windows-msvc-release `
  -pr:b=deploy/conan/profiles/dependency-versions-default `
  -o "&:onnxruntime_use_cuda=False" `
  -o "&:use_ffmpeg=True" `
  -o "&:use_benchmark=True" `
  --build=missing

cmake --preset conan-windows-msvc-release-baas
cmake --build --preset conan-windows-msvc-release-baas
```

For the CUDA artifact, use `onnxruntime_use_cuda=True`, output directory
`build/conan/windows-msvc-release-baas-cuda`, and the
`conan-windows-msvc-release-baas-cuda` configure/build preset. ONNX Runtime and
its provider runtime files are copied from the Conan package graph; the CI does
not download or splice individual DLLs into a repository-local directory.

Windows OCR Server, Release:

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

Linux OCR Server, GCC 13 Release:

```bash
conan install deploy/conan \
  -of build/conan/linux-gcc-release-ocr \
  -pr:h=deploy/conan/profiles/linux-gcc-release \
  -pr:h=deploy/conan/profiles/dependency-versions-default \
  -pr:b=deploy/conan/profiles/linux-gcc-release \
  -o "&:dependency_set=ocr" \
  -o onnxruntime_use_cuda=False \
  -o use_ffmpeg=False \
  -o use_benchmark=False \
  --build=missing \
  --no-remote

cmake --preset conan-linux-gcc-release-ocr
cmake --build --preset conan-linux-gcc-release-ocr
```

macOS OCR Server, arm64 Release:

```bash
conan profile detect --name baas-macos-release --force
conan install deploy/conan \
  -of build/conan/macos-appleclang-release-ocr \
  -pr:h=deploy/conan/profiles/macos-appleclang-release \
  -pr:h=deploy/conan/profiles/dependency-versions-default \
  -pr:b=deploy/conan/profiles/macos-appleclang-release \
  -o "&:dependency_set=ocr" \
  -o onnxruntime_use_cuda=False \
  -o use_ffmpeg=False \
  -o use_benchmark=False \
  --build=missing \
  --no-remote

cmake --preset conan-macos-appleclang-release-ocr
cmake --build --preset conan-macos-appleclang-release-ocr
```

Android OCR Server, arm64-v8a:

```powershell
conan install deploy/conan `
  -of build/conan/android-clang-release-ocr-arm64-v8a `
  -pr:h=deploy/conan/profiles/android-clang-arm64-v8a-release `
  -pr:h=deploy/conan/profiles/dependency-versions-default `
  -pr:b=deploy/conan/profiles/windows-msvc-release `
  -c:h tools.android:ndk_path="$env:ANDROID_NDK_LATEST_HOME" `
  -o "&:dependency_set=ocr" `
  -o onnxruntime_use_cuda=False `
  -o use_ffmpeg=False `
  -o use_benchmark=False `
  --build=missing `
  --no-remote

cmake --preset conan-android-clang-release-ocr-arm64-v8a
cmake --build --preset conan-android-clang-release-ocr-arm64-v8a
```

Android OCR Server, x86_64:

```powershell
conan install deploy/conan `
  -of build/conan/android-clang-release-ocr-x86_64 `
  -pr:h=deploy/conan/profiles/android-clang-x86_64-release `
  -pr:h=deploy/conan/profiles/dependency-versions-default `
  -pr:b=deploy/conan/profiles/windows-msvc-release `
  -c:h tools.android:ndk_path="$env:ANDROID_NDK_LATEST_HOME" `
  -o "&:dependency_set=ocr" `
  -o onnxruntime_use_cuda=False `
  -o use_ffmpeg=False `
  -o use_benchmark=False `
  --build=missing `
  --no-remote

cmake --preset conan-android-clang-release-ocr-x86_64
cmake --build --preset conan-android-clang-release-ocr-x86_64
```

## Runtime Files

Recipes publish include, link, and runtime information through `self.cpp_info`.
`cmake/BAASDependency.cmake` calls `find_package(baas-* CONFIG REQUIRED)` and
applications link `BAAS::*` imported targets only.

Runtime libraries are copied with `baas_copy_conan_runtime_dependencies()`.
All platforms pass Conan package names to the helper, and the helper copies
runtime files from the package `bin` and `lib` directories generated by
CMakeDeps. Android additionally copies `libc++_shared.so` from the active NDK,
because that file belongs to the Android toolchain rather than a BAAS
dependency package.

## Options

- `dependency_set`: selects the exact application dependency surface. `ocr`
  contains OpenCV, ONNX Runtime, nlohmann-json, cpp-httplib, spdlog, and
  simdutf; `afwc` contains only OpenCV, nlohmann-json, spdlog, and simdutf.
  `full` preserves the complete legacy graph for existing consumers.
- `onnxruntime_use_cuda`: selects ONNX Runtime CPU or CUDA provider package.
- `use_ffmpeg`: include FFmpeg dependency for BAAS App and ISA. OCR sets this to
  `False`.
- `use_benchmark`: include Google Benchmark. OCR sets this to `False`.
- `use_opencv_dnn`: build OpenCV with DNN support for the NMS benchmark.
- `use_libsodium`: include the private libsodium source package for service
  authentication. It defaults to `False`, so OCR-only installs do not require
  Autotools on a Windows host.

## Clean-runner and WebAssembly contracts

OCR uses cpp-httplib 0.50.1, whose private recipe precisely requires OpenSSL
3.5.7. A clean CI runner must first create that checked-in recipe with the same
host/build profiles and `--build=missing`; only after that explicit public
closure is present may the application-level install use `--no-remote`.

AFWC does not use cpp-httplib or OpenSSL. Its Emscripten install uses
`profiles/emscripten-wasm-release` as the host profile and
`profiles/linux-gcc-release` as the build profile. The EMSDK is pinned to 6.0.3,
matching `compiler.version`, and the three compiled private dependencies are
static for the wasm target. The Conan-generated toolchain composes the pinned
Emscripten platform toolchain; CMake is never configured directly with a
system-only dependency fallback.

CUDA Toolkit itself is not packaged by Conan. CUDA builds still require
`find_package(CUDAToolkit 12.2 REQUIRED)` during CMake configure.

## Current Limits

- OCR Linux, macOS, Windows, Android arm64-v8a, and Android x86_64 are the
  Conan migration targets for this stage.
- OCR Linux/macOS/Android use ONNX Runtime CPU only.
- BAAS App macOS remains blocked until `baas-ffmpeg` has locked macOS metadata.
- ISA remains Windows-only.

## Verification Notes

Checked in this workspace:

- `python deploy/conan/scripts/manage_recipes.py list`
- `python deploy/conan/scripts/manage_recipes.py export --dry-run`
- `python deploy/conan/scripts/manage_recipes.py inspect`
- `cmake -P cmake/BAASDependency.cmake`
- `cmake -P cmake/BAASConanRuntime.cmake`

The private libsodium source package is additionally verified with:

```powershell
conan create deploy/conan/recipes/baas-libsodium `
  -pr:h=deploy/conan/profiles/windows-msvc-release `
  -pr:b=deploy/conan/profiles/windows-msvc-release `
  -c:a tools.build:jobs=4 --build=missing --no-remote

conan create deploy/conan/recipes/baas-libsodium `
  -pr:h=deploy/conan/profiles/windows-msvc-debug `
  -pr:b=deploy/conan/profiles/windows-msvc-release `
  -c:a tools.build:jobs=4 --build=missing --no-remote
```

Its test package links `BAAS::sodium` and exercises SHA-256, Argon2id, X25519,
Ed25519, IETF ChaCha20-Poly1305, and secretstream. Cross-built test packages are
compiled and linked but are not executed unless Conan's `can_run()` permits it.
Android uses the NDK compiler through Conan's Autotools toolchain. When Android
is cross-built from Windows, a local POSIX `bash` and `make` must be available;
set `-c:a tools.microsoft.bash:path=<path-to-bash.exe>`. No public Conan tool
package is fetched, preserving the `--no-remote` policy. Also set
`-c:a tools.microsoft.bash:subsystem=msys2` for Git Bash/MSYS2, or choose the
corresponding Conan subsystem value for another shell.

Service-auth installs must add `-o "&:use_libsodium=True"`. Android forces
unversioned sonames as required by libsodium. In this workspace, arm64-v8a and
x86_64 toolchain/Autotools host contracts were generated, but the actual
Android packages were not built because the Windows host lacks POSIX `make`.

Linux OCR must also be verified in WSL with the commands above.
