# Script Runtime Migration Baseline Audit

This document records the initial read-only audit. It is evidence for planning,
not proof that parity has been achieved.

## Repository snapshots

| Repository | Audited branch/commit | Working tree |
| --- | --- | --- |
| `baas-dev` | `fix/uiautomator-apk-install` / `75bbacb545bc87e9510d85cbe8034f9180397004` | Clean |
| `baas-cpp-dev` | baseline `main` / `753901eb4678693992ef33e58abaa4d854bbdb63` | Clean before foundation work |
| `baas-tauri` | `dev/optim` / `711a09d4` | Three pre-existing user-modified files; audit made no changes |

The Tauri changes are `src/android/pages/ConfigurationPage.tsx`,
`src/components/ui/Modal.tsx`, and `src/pages/ConfigurationPage.tsx`.

## Python implementation inventory

- 3,851 tracked files: 569 Python, 2,961 PNG, and 130 JSON.
- Approximately 44,252 tracked Python lines.
- Source distribution: `core` 64 files/12,125 lines, `service` 58/8,081,
  `module` 109/8,024, `gui` 42/6,086, generated resource Python 253/3,953,
  and tests 21/1,891.
- `Baas_thread` registers 36 tasks. The default scheduler has 26 events, 22
  enabled at the audited snapshot.
- Module syntax contains at least 1,716 assignments, 749 `if`, 177 `for`, 71
  `while`, 23 `try`, 554 `return`, 42 `break`, 70 `continue`, 1,036 index
  operations, 32 slices, 10 lambdas, and 25 comprehensions.
- Production modules do not use `eval`/`exec`; dynamic behavior is concentrated
  in explicit imports, attribute lookup, and service dependency injection.

### Behavior-critical Python semantics

- `core/picture.py::co_detect` is the central state machine and has 199 module
  call sites. Ordered reaction maps determine match/click priority.
- It sequences screenshot, timeout/package checks, loading detection, ordered
  RGB/image matching, reaction clicks, popup handling, and tentative clicks.
- Click has asynchronous behavior unless `wait_over` is requested. Module code
  has 95 direct clicks and about 85 explicit waits; making all clicks synchronous
  would change observable behavior.
- Coordinates are authored for 1280x720, scaled by device ratio, and normally
  jittered by plus/minus five pixels. Android injects additional clamp/profile
  behavior. Screenshot caching and the 0.3 second throttle are observable.
- Clock, RNG, screenshot, OCR, device, and configuration effects therefore need
  injectable trace/replay seams before a rewrite can claim parity.

### Existing automation data

- `src/explore_task_data` contains 123 JSON workflows and 3,381 grid actions.
- Operation counts include click 2,220, exchange-and-click 397,
  choose-and-change 278, click-and-teleport 267, end-turn 107, and smaller
  exchange variants.
- Most activity wrappers are duplicates around shared helpers and should become
  manifest/data entries instead of one hand-translated script per file.
- Existing JSON is useful migration input but is not a Turing-complete language.

### Known baseline defects requiring explicit classification

- Two grid actions use an array where the current executor expects a string
  operation and calls `startswith`.
- Static analysis finds 82 coordinate mappings without a corresponding PNG
  across the five locale resource sets.
- Two legacy activity scripts call `recognize_number`, which is absent from the
  audited OCR class.
- Resource caches and locale initialization are process-global and unsynchronized.
- Service behavior is altered through extensive monkey patching; desktop,
  service, and Android are distinct behavior profiles.

The deterministic validator at `scripts/migration/validate.py` refines this
baseline to 93 errors and 99 warnings across 123 task JSON files, 359 discovered
grid tasks, 2,869 image mappings, and 31 OCR calls. In addition to the defects
above it reports six dangling grid-task references, one unsupported `nothing`
operation, and 99 empty crop-range warnings. Two consecutive real-repository
reports produced SHA-256
`d222ad7ef3a0530dc467c2af23b78dd900fde07e3eec61b7afcd8a35dd32cdd4`.

The opt-in Python trace foundation is committed on `baas-dev` branch
`feat/cpp-parity-trace` at `3a8f58585b69bf7cf54fe66115352b41f4094aa3`.
It records bounded JSONL begin/end/error/cancel effects for click, swipe, and
screenshot while preserving the disabled default path. It does not yet capture
`co_detect`, uiautomator calls, actual asynchronous click completion, or real
workflow golden fixtures.

## Reusable C++ inventory

### Strong reuse candidates

- OpenCV image utilities, feature primitives, image resource handling, NMS,
  RGB/template operations.
- ONNX Runtime RapidOCR pipeline and the existing OCR HTTP integration suite.
- ADB/scrcpy and Windows Nemu/ldopengl screenshot/control implementations.
- JSON pointer configuration classes and spdlog-backed logging.
- Existing feature/procedure and AutoFight behavior as parity fixtures.

### Foundations that must be replaced or isolated

- The existing `ThreadPool` has unsafe shutdown/wait semantics, an unbounded
  queue, no cancellation/deadline, and data-race risks.
- Logger, OCR, shared-memory, feature, screenshot, and runtime registries contain
  mutable global/singleton state without a sufficient concurrency contract.
- Procedure JSON and AutoFight are finite domain state machines, not a general
  script runtime.
- There is no reusable `BAAS_core` target; core sources are globbed and compiled
  into applications, which works against fast incremental multi-target builds.

### C++ build/test baseline

- C++20, CMake 3.22+, Conan 2, Ninja, and pinned private recipes.
- Presets cover Windows BAAS CPU/CUDA, Windows/Linux/macOS OCR, Android OCR for
  two ABIs, Windows ISA, and Windows NMS.
- Systematic runtime tests exist only for OCR: 16 integration tests in the
  current Windows baseline. Core/device/config/feature/procedure and BAAS app
  lack C++ unit tests and CTest integration.
- Current GitHub workflows still show a pre-Conan configure path, so workflow
  definitions cannot be treated as proof that the current build is green.

## Quantified host-operation priorities

| Operation family | Representative Python module count/use |
| --- | --- |
| Ordered detect/reaction | `co_detect` 199 |
| RGB checks | `rgb_in_range` 90; sweep availability 19 |
| Device input | click 95; swipe 23; uiautomator lifecycle/direct calls |
| Screenshot/cache | update 25; direct cached read 16 |
| Navigation | `to_main_page` 44 |
| OCR | region 17; integer 8; raw/specialized variants |
| Logging | info 333; warning 97; error 20 |
| Configuration | get/set/save plus scheduler state |

These counts prioritize adapter and parity work; they do not reduce the final
requirement to only the most frequent operations.

## Hardware baseline for local scheduling

- 16 physical cores / 32 logical processors.
- Approximately 31.1 GiB physical memory.
- Parallel builds must set explicit limits and avoid running multiple full LTO
  builds at 32 jobs simultaneously. One dependency/LTO build gets exclusive
  access; otherwise independent worktrees divide the available jobs.

## Immediate evidence work

1. Add an opt-in Python JSONL behavior trace with deterministic serialization.
2. Add resource/action/OCR-call validators and classify every reported defect.
3. Freeze service cryptographic and framing golden vectors.
4. Establish C++ CTest, lexer diagnostics, conformance fixtures, and fake hosts.
5. Capture Python latency, memory, startup, throughput, and package-size numbers.
