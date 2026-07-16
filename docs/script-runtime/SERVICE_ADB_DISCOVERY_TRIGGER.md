# Production ADB discovery trigger

`BAAS_service_adb_discovery_trigger` registers the canonical `detect_adb`
command. Its successful inner data value is always:

```json
{"addresses":["127.0.0.1:5557"]}
```

`TriggerResponseSink` owns the surrounding `command_response` identity,
status, and ingress timestamp.

## Python compatibility audit

The compatibility source was audited in `Kiramei/baas-dev` at revision
`75bbacb545bc87e9510d85cbe8034f9180397004`.

`service/runtime.py:609-620` has two runtime modes. When the exact lower-case
value of `BAAS_ANDROID` is `1`, `true`, `yes`, or `on`, it returns the trimmed
non-empty `BAAS_ANDROID_ADB_SERIAL` followed by `127.0.0.1:5555` and
`localhost:5555`. It preserves first occurrence and performs no connection
check. `BAAS_ANDROID` itself is deliberately not trimmed for compatibility.

Desktop Python does not call `adb devices` or inspect ADB state. It scans these
running emulator process names and derives loopback ports from their command
lines and vendor configuration:

| Emulator | Process | Instance marker | Address rule |
|---|---|---|---|
| BlueStacks | `HD-Player.exe` | `--instance` | `BlueStacks.conf` `status.adb_port` |
| Nox | `nox.exe` | `Nox_` | `62001` or the instance port |
| MuMu | `MuMuPlayer.exe`, `MuMuNxDevice.exe` | `-v` | valid `MuMuManager.exe adb` JSON; formula `16384 + 32 * instance` only when that object omits `adb_host`/`adb_port` |
| LDPlayer | `dnplayer.exe` | `index=` | `5555 + 2 * instance` |
| MEmu | `MEmu.exe` | `MEmu_` | `21503 + 10 * instance` |

Processes are observed in PID order. Python then groups them by emulator type
in first-seen type order, retains PID order within each group, and removes
duplicate addresses without sorting. C++ preserves that policy. In particular,
an LDPlayer instance may be visible from ADB as both `127.0.0.1:5557` and
`emulator-5556`; discovery returns only the process-derived loopback address.

## Intentional contract hardening

The Python scanner has observable failure cases: no process implicitly returns
`None`; Nox and Chinese BlueStacks can index missing regex entries; unavailable
command lines can throw; and unresolved vendor data can insert `None` or a
`127.0.0.1:None` string. Those values contradict the annotated `List[str]` and
the UI's array contract. C++ intentionally repairs them:

- no supported process returns `{"addresses":[]}`;
- malformed, missing, or out-of-range instance data uses a bounded default or
  skips that candidate; a process whose command line cannot be read is always
  skipped rather than guessed as instance zero;
- all returned data is a JSON array of strings;
- untrusted Android serial text is fully JSON-escaped;
- enumeration, allocation, cancellation, and capacity failures produce one
  correlated error/cancelled terminal with no sensitive exception text.

These are documented safety and schema corrections, not byte-for-byte
preservation of Python bugs.

## Bounds and cancellation

`AdbDiscoveryLimits` defaults to 4,096 processes, 32 KiB per command line or
executable path, 1,024 unique addresses, and a 512 KiB inner JSON value. Hard
ceilings are 16,384 processes, 128 KiB per string, 4,096 addresses, and 4 MiB
of JSON. Invalid limit sets are rejected before registration. Process mapping checks the stop token between
records and groups. Windows uses a native Toolhelp process snapshot plus a
bounded native command-line query, avoiding synchronous WMI server calls. The
five-second total scan deadline and three-second per-vendor-query deadline have
hard ceilings of thirty and ten seconds respectively. MuMu children inherit
only their output pipe and are terminated on every abnormal exit. Each
BlueStacks region config is read at most once, capped by `max_json_bytes`, and
scanned with cancellation/deadline checks. Known Global/CN executable paths do
not fall back across regions; hidden paths are accepted only when the available
region results are unambiguous. Before synchronous file access or process
creation, vendor paths must resolve to ordinary files on a fixed local drive;
local NTFS junctions/symlinks are resolved recursively and accepted when their
final target remains on a fixed local drive. UNC/device namespaces,
remote/removable drives, offline files, cloud recall files, and unsafe reparse
targets skip only that vendor candidate. A shutdown request therefore does not
wait behind a remote/offline path, unbounded result fetch, vendor process, or
configuration scan, while another safe emulator candidate remains discoverable.

Stable terminal errors are:

| Boundary | Error |
|---|---|
| source or result capacity | `adb_discovery_source_capacity` |
| platform enumeration unavailable | `adb_discovery_source_unavailable` |
| unexpected source exception | `adb_discovery_source_exception` |
| outer response rejection | `adb_discovery_response_rejected` |

## Build and evidence

The production library is enabled by
`BUILD_SERVICE_ADB_DISCOVERY_TRIGGER`; deterministic tests use
`BUILD_SERVICE_ADB_DISCOVERY_TRIGGER_TESTS`. The production application
explicitly constructs the platform source and registers `detect_adb` beside
`status` and the configuration commands.

`BAAS_service_adb_discovery_trigger_tests` covers exact success/empty
envelopes, JSON escaping, Android ordering and de-duplication, LDPlayer, MEmu,
MuMu, BlueStacks and Nox mapping, Python grouping order, cancellation, hard
bounds, MuMuManager override, error mapping, and exception redaction. The hook-free application test
also submits `detect_adb` through the real production dispatcher. Host Debug
and Release CI build the tests on Windows, Linux, and macOS; the Android auth
cross-build compiles the production registration and its environment branch.
