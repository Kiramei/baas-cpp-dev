# BAAS C++ development environment

## Checked-out layout

The configured workspace uses sibling repositories so migration and protocol
tools can compare production sources without embedding machine-specific paths:

```text
D:\WorkSpace\pro\BAAS\
├── baas-cpp-dev\
├── baas-dev\
└── baas-tauri\
```

Run all C++ commands from `D:\WorkSpace\pro\BAAS\baas-cpp-dev`. Do not use the
old sibling path `D:\WorkSpace\pro\baas-cpp-dev`.

## Windows toolchain installed for this checkout

The repository-local environment currently resolves:

| Tool | Configured version |
| --- | --- |
| PowerShell | 7.6.3 |
| MSVC | 19.44 / Visual Studio 2022 Build Tools |
| CMake | 3.31.6 |
| Ninja | 1.12.1 |
| Python | 3.11.9 in `.venv` |
| Conan | 2.30.0 in `.venv` |
| Git | 2.54.0.windows.1 |
| JDK | Eclipse Temurin 17.0.19 |
| Android SDK | API 36, NDK 29.0.13846066, emulator, platform tools |

The local Python and Conan state is isolated in `.venv` and `.conan2`; the
activation script sets `CONAN_HOME` to the latter.

## Activate a shell

Initialize the repository-local environment once in PowerShell:

```powershell
Set-Location D:\WorkSpace\pro\BAAS\baas-cpp-dev
.\scripts\dev\Initialize-WindowsEnvironment.ps1
```

Then activate each shell:

```powershell
Set-Location D:\WorkSpace\pro\BAAS\baas-cpp-dev
. .\scripts\dev\Enter-WindowsDevShell.ps1
```

This imports the x64 MSVC environment, prepends the repository Python, bundled
CMake/Ninja, JDK, and Android SDK tools to `PATH`, and sets
`BAAS_CPP_DEV_ROOT`, `CONAN_HOME`, and `ANDROID_SDK_ROOT`. Activation only
changes the current PowerShell process.

Verify it with:

```powershell
cl
cmake --version
ninja --version
conan --version
python --version
```

## Build the dependency-free foundation

Lexer/parser/semantic/syntax-check/runtime-executor/value-heap/JSON-bridge and
BPIP framing targets do not require the full application dependency graph:

```powershell
cmake -S . -B build\foundation -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTING=ON `
  -DBUILD_SCRIPT_TESTS=ON `
  -DBUILD_SCRIPT_TOOLS=ON `
  -DBUILD_SERVICE_PROTOCOL_TESTS=ON `
  -DBUILD_APP_BAAS=OFF `
  -DBUILD_APP_ISA=OFF `
  -DBUILD_BAAS_OCR=OFF `
  -DBUILD_BAAS_AW_CHECKER=OFF `
  -DBUILD_BAAS_NMS_BENCHMARK=OFF `
  -DBAAS_FETCH_RESOURCES=OFF

cmake --build build\foundation --parallel 8
ctest --test-dir build\foundation --output-on-failure --timeout 120
```

Validate one or more draft scripts without executing them:

```powershell
build\foundation\bin\BAAS_script_check.exe --json path\to\task.baas
```

The checker returns 0 for valid input, 1 for language diagnostics, and 2 for
invocation or I/O failure. `--max-bytes`, `--max-ast-nodes`, and `--max-depth`
bound hostile input; `-` reads standard input once.

The same targets are checked by `.github/workflows/foundation-runtime.yml` on
Windows, Ubuntu, and macOS in Debug and Release. Hosted non-Windows results are
not a substitute for the still-pending full application and Android gates.

## Full Conan builds

BAAS-owned dependency recipes and pinned profiles live in `deploy/conan`. Export
them before the first full dependency install:

```powershell
python deploy\conan\scripts\manage_recipes.py export
```

Then follow `deploy/conan/README.md` and the matching `CMakePresets.json` entry.
For example, the CPU OCR release profile writes generators under
`build/conan/windows-msvc-release-ocr` and uses preset
`conan-windows-msvc-release-ocr`. Keep `--no-remote` when reproducing from the
checked-in private recipes and local Conan cache; remove it only as an explicit
dependency-source decision.

Known cross-platform dependency gaps are tracked in
`docs/conan-migration-gaps.md`. A successful foundation build does not claim
that the complete OCR, CUDA, Android, emulator, or desktop application stack is
configured or green.

## Python reference and service-vector checks

The repository-local `.venv` is configured from
`scripts/dev/requirements-foundation.txt`. It includes `fastapi`,
`cryptography`, and `PyNaCl`, which are needed when the production-anchored
service vectors import the sibling service. Refresh it with:

```powershell
.\scripts\dev\Initialize-WindowsEnvironment.ps1
```

The authoritative Python suite can still use the sibling Python environment:

```powershell
Push-Location ..\baas-dev
.\.venv\Scripts\python.exe -m pytest tests -q -rs
Pop-Location

python -m unittest discover -s tests\service_contract -p "test_*.py" -v
python scripts\service_contract\generate_vectors.py --check
```

Do not run an unscoped `pytest` from `baas-dev`: that repository currently has
no `testpaths` boundary and will collect the vendored CPython/toolkit tree.

## Safe parallelism

This host has 16 physical / 32 logical cores and about 31 GiB RAM. Foundation
builds use `--parallel 8`. Concurrent worktrees should divide that budget
explicitly; do not give every simultaneous dependency-heavy build all 32
logical CPUs.
