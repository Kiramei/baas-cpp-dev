# Python-to-C++ Script Runtime Migration Matrix

This matrix is the authoritative coverage ledger. Each Python operation family
must ultimately resolve to concrete source locations, a versioned script API,
a C++ host binding, and executable parity evidence.

| Domain | Python source/operation | Current C++ capability | Planned script API | Parity evidence | Status |
| --- | --- | --- | --- | --- | --- |
| Language semantics | Inventory in progress | None | Specification pending | Conformance corpus pending | In progress |
| Logging | Inventory in progress | Existing BAAS logger | Pending | Golden event trace pending | In progress |
| Configuration | Inventory in progress | Existing JSON configuration | Pending | Round-trip parity pending | In progress |
| Resources | Inventory in progress | Existing CMake/resource loaders | Pending | Manifest/update tests pending | In progress |
| Screenshot | Inventory in progress | ADB/scrcpy/Nemu implementations | Pending | Image/hash parity pending | In progress |
| Image processing | Inventory in progress | OpenCV feature utilities | Pending | Numeric/image parity pending | In progress |
| OCR | Inventory in progress | ONNX Runtime OCR server/core | Pending | OCR corpus parity pending | In progress |
| Device input/control | Inventory in progress | ADB/scrcpy/Nemu controls | Pending | Emulator trace pending | In progress |
| Feature/procedure | Inventory in progress | Feature and procedure classes | Pending | Workflow trace pending | In progress |
| Scheduler/workflow | Inventory in progress | Partial app workflows | Pending | Retry/cancel/state parity pending | In progress |
| Service/Tauri | Inventory in progress | OCR-only HTTP service | Pending | Shared protocol suite pending | In progress |
| Updates/rollback | Inventory in progress | Resource fetch at configure time | Pending | Atomic update/rollback pending | In progress |

## Entry completion rule

An entry is complete only when all relevant Python call sites are enumerated,
the host API and script signature are documented, platform/threading/error
semantics are explicit, and automated parity evidence passes. A similar C++
class name or a successful narrow smoke test is not sufficient evidence.

