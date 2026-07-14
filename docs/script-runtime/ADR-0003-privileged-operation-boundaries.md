# ADR-0003: Privileged Python operation boundaries

Status: Accepted for migration inventory; adapters and parity remain pending.

## Context

The Phase 0 operation index previously left 26 security-sensitive operation
identities unresolved at 29 Python call sites. The review set covered Windows
shortcut, registry, monitor and native calls; notification callbacks; raw IPC;
OCR updater/archive operations; socket bind and descriptor access; and video
codec calls. A generic script-language fallback is unsafe for these operations:
resolution of a Python symbol does not grant scripts the underlying platform
authority.

This ADR fixes ownership and the static proof boundary. It does not claim that
the C++ Host interfaces, service internals, migration tooling, or parity tests
have been implemented.

## Decision

The following boundaries are normative for taxonomy v4. Rules are evaluated
before dynamic, unresolved, external-dependency, and standard-library defaults.
Source-qualified rules apply per source file before equivalent decisions are
aggregated, so an identical symbol in another file does not inherit authority.

| Legacy evidence | Exact migration owner | Rule and contract |
| --- | --- | --- |
| `win32com.client.Dispatch` and `shell.CreateShortCut` in `core/Baas_thread.py` | migration/bootstrap tooling | `windows-shortcut-tooling-boundary-v4`; no runtime Host capability |
| `winreg.*` and `bst_read_registry_key` below `core/device/emulator_manager/` | device emulator discovery | `emulator-registry-device-boundary-v4`; `DeviceHost`, `baas/device` |
| `get_monitor_info`, `monitor_from_window`, and `user32.SystemParametersInfoA` in the two named window/input sources | device capture/input adapter | `window-input-device-boundary-v4`; `DeviceHost`, `baas/device` |
| `ctypes.*` and the enumerated native allocation expression forms in `core/device/nemu_client.py` | Nemu native device adapter | `nemu-native-device-boundary-v4`; `DeviceHost`, `baas/device` |
| `file_err.fileno` and `file_out.fileno` in `core/device/nemu_client.py` | Nemu descriptor redirection, internal to the device adapter | `device-descriptor-boundary-v4`; descriptors never cross the Host ABI |
| `_notify` and `_toast` in `core/notification.py` | runtime notification adapter | `notification-host-boundary-v4`; `NotifyHost`, canonical module `baas/notify` |
| `context.send` in `core/exception.py` and the named shared-memory release in `core/ipc_manager.py` | C++ service IPC internals | `raw-ipc-service-boundary-v4`; no script-visible raw pipe or shared-memory handle |
| every operation in `core/ocr/baas_ocr_client/server_installer.py`, including `pygit2` and `ZipFile.extractall` | offline migration/updater tooling | `ocr-updater-tooling-boundary-v4`; the updater moves as one unit and is not a runtime capability |
| `s.bind` in `core/ocr/ocr.py` | C++ service listener and port-probe internals | `socket-listener-service-boundary-v4`; `baas/socket` remains connect-only |
| `av.*`, `codec.decode`, `codec.parse`, and `frame.to_ndarray` | bounded vision decoding | `vision-host-v2`; `VisionHost`, `baas/vision` |

The notification boundary deliberately does not assign legacy notification UI
to Tauri. `baas/notify.show` preserves one-way notification behavior and
`baas/notify.prompt` preserves ordered action/response behavior used by the
runtime thread. Platform UI and callbacks remain behind `NotifyHost`, while the
script-visible module contract remains available on every supported frontend.

## Static proof checklist

Receiver and owner inference may use only facts proven by source and exact
versioned rules:

1. exact imported names, aliases, local class constructors, and lexical nested
   definition binding in Python evaluation order;
2. exact constructor/factory return rules, including `adbutils.AdbDevice`,
   `logging.Handler`, `psutil.Process`, `socket.socket`, `subprocess.Popen`,
   `threading.Timer`, and `re.Match`;
3. `with` target propagation only for a proven `io.IOBase` context result;
4. iterable element propagation only when the exact call rule declares an
   `element_type`, currently `psutil.process_iter` and `re.finditer`;
5. conservative lexical and control-flow merges: disagreement becomes unknown.

The index MUST NOT infer ownership from a final identifier segment, receiver
suffix, wildcard resemblance, runtime attribute lookup, or an optimistic branch
merge. In particular, ordinary `obj.bind`, `obj.fileno`, `obj.extractall`, local
classes named `Process`, the same dynamic expression outside the exact Nemu
source, and updater-like calls outside the exact installer source remain under
their independently proven classification.

## Consequences

- Resolved standard-library or third-party names can no longer fall through to
  `SCRIPT_LANGUAGE_OR_MODULE` when an earlier privileged boundary applies.
- A single operation identity may have multiple `SCRIPT_RUNTIME` scope
  decisions when source-qualified evidence produces different owners. Decision
  IDs receive deterministic suffixes only in that case.
- `baas/socket` still forbids listening and descriptor export. Device and
  notification adapters expose values, not native handles.
- Strict generation is expected to remain non-zero while unrelated unresolved
  operations exist. That result is an inventory gate, not a failure of this
  boundary decision.

## Verification

Regression fixtures cover every boundary group above, ordinary-name and
wrong-source negative cases, exact factory returns, context-manager returns,
iterable element types, lexical nested definitions, and deterministic output.
The generated index, migration matrix, catalog taxonomy mappings, and audit are
validated together by the migration and documentation test suites.
