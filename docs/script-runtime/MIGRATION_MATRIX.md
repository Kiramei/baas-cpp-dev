# Python-to-C++ Script Runtime Migration Matrix

This matrix is the authoritative coverage ledger. Each Python operation family
must ultimately resolve to concrete source locations, a versioned script API,
a C++ host binding, and executable parity evidence.

| Domain | Python source/operation | Current C++ capability | Planned script API | Parity evidence | Status |
| --- | --- | --- | --- | --- | --- |
| Language semantics | 109 module files: 749 `if`, 177 `for`, 71 `while`, functions/callbacks, ordered maps, slices, comprehensions | No general runtime | Core language draft | Conformance corpus pending | Inventoried |
| Logging | 333 module `info`, 97 `warning`, 20 `error`; service streams scoped structured logs | Existing spdlog logger | `baas/log` host module | Golden event trace pending | Inventoried |
| Configuration | 97-field user config, 25-field static config, event and setup schemas; unknown fields must survive | JSON pointer based config classes | `baas/config` snapshot/transaction API | Round-trip and conflict parity pending | Inventoried |
| Resources | 2,961 PNG, 130 JSON, 253 generated coordinate modules; locale/activity dynamic imports | CMake resource fetch and image cache | `baas/resource` immutable snapshots | Validator/manifest/update tests pending | Inventoried |
| Screenshot | Cached screenshot, 0.3 s throttle, 1280x720 ratio; ADB/Nemu/u2/scrcpy/desktop backends | ADB/scrcpy and Windows Nemu/ldopengl | `baas/device.capture` | Image/hash/timing parity pending | Inventoried |
| Image processing | `co_detect` 199 calls; RGB range 90; OpenCV/template/search helpers | OpenCV utilities and feature classes | `baas/vision` plus ordered `co_detect` intrinsic | Numeric/image/state trace parity pending | Inventoried |
| OCR | Region OCR 17, integer OCR 8, language/candidate/filter/pass-method semantics | ONNX Runtime OCR core and tested OCR server | `baas/ocr` lifecycle/inference API | OCR corpus parity pending | Inventoried |
| Device input/control | Click 95, swipe 23, async click/wait-over distinction, jitter/ratio/clamp; app lifecycle | ADB/scrcpy/Nemu controls | `baas/device` capability API | Emulator input trace pending | Inventoried |
| Feature/procedure | Ordered `co_detect`, popup/tentative-click/loading/timeout behavior | Feature and one procedure type; AutoFight-specific workflow | Versioned host intrinsics plus script composition | Workflow trace pending | Inventoried |
| Scheduler/workflow | 36 registered tasks, 26 default events; priority, reset, interval, pre/post tasks, retries | Partial app-specific workflows | `baas/task` structured cancellation and scheduler API | Retry/cancel/state parity pending | Inventoried |
| Service/Tauri | 9 HTTP, 5 WS, provider/sync/trigger/remote channels, 4 pipe channels and secure handshake | OCR-only HTTP service | Versioned compatibility service | Shared wire/golden protocol suite pending | Inventoried |
| Updates/rollback | Git/updater plus setup.toml and mutable resources | Configure-time resource fetch only | Signed manifests, atomic switch, rollback | Atomic update/rollback pending | Inventoried |

## Entry completion rule

An entry is complete only when all relevant Python call sites are enumerated,
the host API and script signature are documented, platform/threading/error
semantics are explicit, and automated parity evidence passes. A similar C++
class name or a successful narrow smoke test is not sufficient evidence.
