# Platform and emulator smoke-test prerequisites

Status: Phase 0 prerequisite inventory; no platform smoke is claimed

Baseline: repository revision `82c632681b1d0c70be502a94c54639e42ed75265`

This document defines what must exist before BAAS platform smoke tests are run.
It records static repository declarations and a read-only observation of the
current Windows host. It does not prove a build, launch, device connection,
foreground-service lifecycle, JNI load, named-pipe exchange, or end-to-end
workflow.

## 1. Evidence vocabulary

The checker and this document use four statuses:

- `discovered`: a repository profile, preset, source declaration, or sibling
  contract was found. It says nothing about host executability.
- `available`: a host path or executable exists and, where safe, reports a
  version. It says nothing about a successful build or runtime behavior.
- `missing`: a required prerequisite was not found or did not meet the stated
  version/target requirement.
- `not_run`: a behavioral operation was deliberately excluded or requires a
  different host. It must not be read as pass, skip-pass, or not applicable.

`scripts/platform/check_smoke_prerequisites.py` is standard-library-only. Its
default action examines all profiles and writes JSON to stdout. `--output`
writes the same deterministic JSON; `--strict` returns 1 if any selected
required item is `missing`. `not_run` is preserved but is not itself a missing
filesystem/tool prerequisite.

The checker never:

- starts an emulator or runs `adb devices`;
- connects to or queries a device;
- installs or launches an APK;
- creates an AVD, accepts SDK licenses, or installs SDK packages;
- starts Tauri or a BAAS backend;
- changes environment variables or user configuration;
- records timestamps or absolute user paths.

Examples:

```powershell
python scripts/platform/check_smoke_prerequisites.py
python scripts/platform/check_smoke_prerequisites.py `
  --profile windows-foundation --strict `
  --output build/evidence/windows-foundation.json
python scripts/platform/check_smoke_prerequisites.py `
  --profile android-x86_64 --strict `
  --output build/evidence/android-x86_64.json
```

## 2. Profile definitions

| Profile | Required prerequisite boundary | Behavioral evidence intentionally outside the checker |
| --- | --- | --- |
| `windows-foundation` | Windows x86_64, Python 3.11+, CMake 3.22+, Ninja, MSVC x64, standalone foundation CMake declarations | configure, build, CTest |
| `windows-desktop` | Windows profile/preset, CMake, Ninja, MSVC, Conan 2, Bun, Cargo/Rust, sibling `baas-tauri` | Tauri launch, managed C++ backend, BPIP named pipe, health/readiness, shutdown |
| `android-arm64` | Android SDK platform 36, NDK with Clang 21 and `aarch64-linux-android26-clang++`, Conan/CMake/Ninja, JDK 17+, adb/emulator/sdkmanager, Bun/Rust and `aarch64-linux-android`, Android 36 arm64 image/AVD | build/package/install, JNI load, foreground service, notification, port/socket, restart |
| `android-x86_64` | Same boundary with `x86_64-linux-android26-clang++`, `x86_64-linux-android`, and an Android 36 x86_64 image/AVD | same Android runtime evidence |
| `linux-foundation` | Linux x86_64 hosted runner, CMake 3.22+, GCC 13, Python when vector checks run | configure, build, CTest on Linux |
| `macos-foundation` | macOS arm64 hosted runner, CMake 3.22+, AppleClang 17, Python when vector checks run | configure, build, CTest on macOS |

The Android C++ profiles intentionally use API 26 and C++20. The audited Tauri
Gradle application uses minSdk 24 and compile/target SDK 36. These are different
layers: the NDK wrapper proves the C++ artifact can target API 26, while the SDK
platform proves the current Tauri application can compile against API 36.

## 3. Authoritative repository declarations

The checker reads, but does not modify:

- `CMakeLists.txt`: CMake 3.22 minimum and standalone script/service targets;
- `CMakePresets.json`: Windows BAAS/OCR/ISA/NMS, Linux/macOS OCR, and Android
  OCR presets;
- `deploy/conan/profiles/windows-msvc-release`: MSVC 194, x86_64;
- `deploy/conan/profiles/linux-gcc-release`: GCC 13, x86_64;
- `deploy/conan/profiles/macos-appleclang-release`: AppleClang 17, armv8;
- `deploy/conan/profiles/android-clang-arm64-v8a-release`: Clang 21,
  armv8, API 26, `c++_shared`;
- `deploy/conan/profiles/android-clang-x86_64-release`: Clang 21,
  x86_64, API 26, `c++_shared`;
- `.github/workflows/foundation-runtime.yml`: Windows/Ubuntu/macOS Debug and
  Release foundation matrix. This workflow is hosted build evidence only after
  a job runs; merely discovering the YAML is not a smoke pass.

The sibling `baas-tauri` audit found these current assumptions:

- desktop `tauri dev` runs `bun dev:tauri`; managed desktop backend startup
  selects a loopback port and defaults to a unique Windows named pipe;
- desktop launch code still prepares a Python backend command. A native C++
  service executable and launch adapter are not yet integrated;
- Android package id is `io.github.kiramei.baas_tauri`, minSdk is 24, and
  compile/target SDK is 36;
- `BaasForegroundService` runs in `:baas_backend`, owns notification id 8190,
  and starts the backend on loopback port 8190;
- the Android backend currently starts Python through Chaquopy. Native C++/JNI
  service bootstrap is not implemented;
- generated Tauri Android code loads `baas_tauri_lib` through JNI, but that is
  not evidence that a future BAAS C++ service/runtime library is packaged or
  loadable for both required ABIs.

## 4. Recorded Windows host observation

The committed deterministic record is
`evidence/platform_smoke_prerequisites.windows.json`. It was produced from an
x86_64 Windows shell after activating the repository development environment.
It contains no timestamp or absolute user path.

At the time of the observation:

| Area | Status | Sanitized evidence |
| --- | --- | --- |
| Windows foundation tools | available | Python 3.11.9, CMake 3.31.6, Ninja 1.12.1, MSVC 19.44 |
| Windows desktop tools | available | Conan 2.30.0, Bun 1.3.14, Cargo/Rust 1.96.1 and Tauri source discovered |
| Android SDK/NDK | available | platform 36, NDK 29.0 beta 3, Clang 21, both API 26 ABI wrappers |
| Android command tools | available | adb 1.0.41, emulator 36.6.11, sdkmanager executable |
| Android Rust targets | available | `aarch64-linux-android` and `x86_64-linux-android` |
| JDK 17+ | available | Eclipse Temurin 17.0.19 is discovered through `JAVA_HOME`/PATH |
| Android 36 system images | available | default arm64-v8a and x86_64 images are installed |
| Android 36 AVDs | available | dedicated `baas_api36_arm64` and `baas_api36_x86_64` configurations exist |
| Windows foundation/desktop smoke | not_run | checker does not build or launch |
| Android emulator/foreground/JNI smoke | not_run | checker does not operate a device |
| Linux/macOS hosted foundation | not_run | the audited host was Windows |

An ordinary unactivated shell did not expose CMake, Ninja, Conan, or MSVC on
PATH. `. .\scripts\dev\Enter-WindowsDevShell.ps1` supplies those tools for the current
process. That shell activation is therefore part of Windows reproduction; the
committed evidence does not encode its machine-specific location.

## 5. Future Windows smoke commands and evidence

### 5.1 Foundation smoke

These commands are safe to run once the prerequisite checker is strict-green:

```powershell
. .\scripts\dev\Enter-WindowsDevShell.ps1
python scripts\platform\check_smoke_prerequisites.py `
  --profile windows-foundation --strict `
  --output build\evidence\windows-foundation.json
cmake -S . -B build\foundation-smoke -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTING=ON `
  -DBUILD_SCRIPT_TESTS=ON `
  -DBUILD_SERVICE_PROTOCOL_TESTS=ON `
  -DBUILD_APP_BAAS=OFF `
  -DBUILD_APP_ISA=OFF `
  -DBUILD_BAAS_OCR=OFF `
  -DBUILD_BAAS_AW_CHECKER=OFF `
  -DBUILD_BAAS_NMS_BENCHMARK=OFF `
  -DBAAS_FETCH_RESOURCES=OFF
cmake --build build\foundation-smoke --parallel 8
ctest --test-dir build\foundation-smoke --output-on-failure --timeout 120
```

Required retained evidence: checker JSON, configure/build logs, CTest JUnit or
console log, compiler/CMake versions, exit codes, and the tested commit. This
document records none of those runtime outputs.

### 5.2 Desktop Tauri + named-pipe smoke

Current Tauri can be launched with the exact existing entrypoint:

```powershell
Push-Location ..\baas-tauri
bun run tauri dev
Pop-Location
```

That command currently exercises the Python backend assumption and therefore
cannot establish C++ migration parity. After a C++ service executable and Tauri
launch adapter exist, a non-interactive runner must own this exact future
contract:

```powershell
python scripts\platform\run_windows_desktop_pipe_smoke.py `
  --tauri-root ..\baas-tauri `
  --backend build\windows-desktop\bin\BAAS_service.exe `
  --transport pipe `
  --output build\evidence\windows-desktop-pipe.json
```

`run_windows_desktop_pipe_smoke.py` and `BAAS_service.exe` do not exist at this
baseline, so this command is a required runner interface, not an instruction
that passed. The future evidence must include process ownership, unique pipe
name without leaking it publicly, `/health` readiness, BPIP open/open_ok for
provider/sync/trigger, one correlated command, graceful Tauri exit, backend
termination, no stale PID/pipe, and bounded timeouts. Windows pipe ACL evidence
must prove current-user-only access.

## 6. Future Android build and emulator commands

The audited host now has JDK 17, both default Android 36 system images, and
dedicated AVD configurations. Provisioning remains an explicit operator step
outside the checker: do not make the checker call `sdkmanager`, `avdmanager`,
or accept licenses.

The audited host was provisioned with these explicit commands (the Android SDK
licenses were already accepted):

```powershell
winget install --id EclipseAdoptium.Temurin.17.JDK --exact --silent `
  --accept-package-agreements --accept-source-agreements

sdkmanager "system-images;android-36;default;arm64-v8a" `
  "system-images;android-36;default;x86_64"

'no' | avdmanager create avd --name baas_api36_arm64 `
  --package "system-images;android-36;default;arm64-v8a" --device pixel_6
'no' | avdmanager create avd --name baas_api36_x86_64 `
  --package "system-images;android-36;default;x86_64" --device pixel_6
```

These commands modify the operator's JDK/Android SDK/AVD installation and are
not CI validation commands. The read-only checker verifies their result.

After those prerequisites are supplied, first run both strict profiles:

```powershell
. .\scripts\dev\Enter-WindowsDevShell.ps1
python scripts\platform\check_smoke_prerequisites.py `
  --profile android-arm64 --strict `
  --output build\evidence\android-arm64-prerequisites.json
python scripts\platform\check_smoke_prerequisites.py `
  --profile android-x86_64 --strict `
  --output build\evidence\android-x86_64-prerequisites.json
```

The exact C++ OCR build entrypoints are:

```powershell
conan install deploy/conan `
  -of build/conan/android-clang-release-ocr-arm64-v8a `
  -pr:h=deploy/conan/profiles/android-clang-arm64-v8a-release `
  -pr:h=deploy/conan/profiles/dependency-versions-default `
  -c:h tools.android:ndk_path="$env:ANDROID_NDK_LATEST_HOME" `
  -o onnxruntime_use_cuda=False -o use_ffmpeg=False -o use_benchmark=False `
  --build=missing --no-remote
cmake --preset conan-android-clang-release-ocr-arm64-v8a
cmake --build --preset conan-android-clang-release-ocr-arm64-v8a

conan install deploy/conan `
  -of build/conan/android-clang-release-ocr-x86_64 `
  -pr:h=deploy/conan/profiles/android-clang-x86_64-release `
  -pr:h=deploy/conan/profiles/dependency-versions-default `
  -c:h tools.android:ndk_path="$env:ANDROID_NDK_LATEST_HOME" `
  -o onnxruntime_use_cuda=False -o use_ffmpeg=False -o use_benchmark=False `
  --build=missing --no-remote
cmake --preset conan-android-clang-release-ocr-x86_64
cmake --build --preset conan-android-clang-release-ocr-x86_64
```

These commands build OCR libraries; they do not prove Tauri APK packaging or a
native BAAS service. For the current Tauri Android application, the existing
debug entrypoints are:

```powershell
Push-Location ..\baas-tauri
bun run tauri:android:build:debug
bun run tauri:android:dev
Pop-Location
```

The second command may start/select or connect to a device and is deliberately
never called by the prerequisite checker. Before automated emulator smoke, the
test job must select a named disposable Android 36 AVD and record its ABI. A
future runner must then retain:

- APK SHA-256 and package metadata;
- native library inventory for exactly the selected ABI, including
  `libc++_shared.so`, Tauri JNI, and the future BAAS native service library;
- absence of `UnsatisfiedLinkError`, linker, ABI, or JNI registration errors;
- `BaasForegroundService` process and foreground notification evidence;
- loopback port 8190 health response and app-private Unix-socket evidence;
- launch, background, process kill/restart, and relaunch results with deadlines;
- arm64-v8a result from an arm64 emulator/physical CI target and x86_64 result
  from an x86_64 emulator. One ABI cannot stand in for the other.

Chaquopy success is not JNI migration success. The native C++/JNI bootstrap and
foreground recovery path remain missing and must be implemented before these
runtime gates can pass.

## 7. Hosted Linux and macOS foundation smoke

On a matching hosted runner, use the same dependency-free configure contract as
the foundation workflow:

```sh
cmake -S . -B build/foundation-smoke \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DBUILD_SCRIPT_TESTS=ON \
  -DBUILD_SERVICE_PROTOCOL_TESTS=ON \
  -DBUILD_APP_BAAS=OFF \
  -DBUILD_APP_ISA=OFF \
  -DBUILD_BAAS_OCR=OFF \
  -DBUILD_BAAS_AW_CHECKER=OFF \
  -DBUILD_BAAS_NMS_BENCHMARK=OFF \
  -DBAAS_FETCH_RESOURCES=OFF
cmake --build build/foundation-smoke --parallel 4
ctest --test-dir build/foundation-smoke --output-on-failure --timeout 120
```

Linux x64 additionally expects GCC 13 for the declared full-build profile;
macOS arm64 expects AppleClang 17. Hosted evidence must identify runner OS/arch,
compiler/CMake versions, tested commit, configure/build/test logs, and exit
codes. The current Windows record leaves both hosted profiles `not_run`.

## 8. Completion boundary

This inventory completes only the Phase 0 task to record platform and emulator
smoke prerequisites. It does not complete the Phase 0 exit criteria because
performance baselines and operation classification remain open.
It does not change any Phase 6 platform pipeline item. Windows desktop pipe,
Android builds/emulators, JNI/foreground lifecycle, and hosted runtime evidence
remain missing or `not_run` until their future commands execute successfully.
