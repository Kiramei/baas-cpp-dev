# Production service application

`BAAS_service` is the first real standalone C++ service executable. Its CMake
target and packaged filename are exactly `BAAS_service` (`BAAS_service.exe` on
Windows), matching `baas-tauri`'s `launch_cpp_backend_command`:

```text
BAAS_service --project-root <directory> --host 127.0.0.1 --port <1..65535> --runtime-repository-generation <64-lowercase-hex>
```

The executable identity and wire identity are intentionally separate.
`BAAS_service` is the process/package name and remains the `--version` prefix;
the HTTP `GET /version` response uses `"service":"BAAS Service"`. This is an
explicit cross-repository contract with `baas-tauri` revision `a1c8c837`,
`src-tauri/src/commands.rs`, function `cpp_backend_ready`, whose strict
readiness check requires `ok=true`, `api_version=1`, and the exact service
literal `BAAS Service` before it accepts `/health` as ready.

The runtime repository generation is a mandatory launch handoff from the
publisher. Composition activates `current.json` once, requires the retained
immutable snapshot to have that exact generation, and opens both repository
objects as one pathless read bundle. Their manifests, complete file sets, and
payload digests are validated on anchored native handles. Missing state or a
different valid generation returns `runtime_repository_generation_mismatch`;
malformed or tampered activation or repository bytes return
`runtime_repository_invalid`. These checks precede remote-resource, signal,
auth, worker, and socket side effects.

`--help` and `--version` succeed without constructing the service. Windows uses
`wmain` and converts bounded UTF-16 arguments to UTF-8; other hosts use `main`.
Stable failures use exit codes: command line `2`, unavailable Pipe `3`, signal
setup `4`, composition `5`, HTTP start `6`, readiness `7`, and internal `8`.

## Production composition

`ServiceApplication` uses the existing production owners rather than stubs:

- `ServiceShutdownCoordinator`, `ServiceSignalOwner`, and
  `HealthReadinessOwner`;
- `ProductionProviderBackend`, `FileResourceStore`, and the joined
  `ServiceRuntimeProviderBridge`/`FileResourceWatcher` lifecycle;
- `ServiceRuntimeRepositoryOwner`, which retains the validated immutable
  generation selected by the mandatory launch expectation, plus the single
  admitted resources/scripts read bundle retained by `ServiceApplication`;
- desktop `ProductionRemoteBackend`, `RemoteHandlerFactory`, and the same
  shared resource store/ADB smart-socket transport;
- real `status`, `add_config*`, `copy_config`, `remove_config*`, `export_config`,
  and `import_config` registrations,
  `TriggerDispatcher`, `TriggerExecutor`, and `TriggerHandlerFactory`;
- when and only when an embedding owner supplies
  `ServiceApplicationDependencies::production_runtime_script_provider`,
  `ProductionRuntimeScriptTaskFactory`, `RuntimeScriptTaskBackend`,
  `RuntimeTaskOwner`, `ProductionRuntimeTaskControl`, and all five runtime
  task Trigger descriptor groups;
- file auth storage, system clock, system random, and sodium password deriver;
- `ProductionHttpHost` on the exact CLI port.

The status source reads the thread-safe production provider snapshot.
Configuration triggers use the same durable `FileResourceStore` supplied to
the sync and remote channels. Archive export returns a JSON filename plus an
adjacent binary ZIP response; archive import consumes the ingress-declared
adjacent binary frame and returns JSON serial/name data after its atomic
staging/replacement claim. Other catalog triggers remain unregistered. On
desktop, remote policy is `desktop_only` and `/ws/remote` uses the production
ws-scrcpy backend. The pinned resource is read from
`<project-root>/service/remote/scrcpy-server.jar`; a missing file fails with
`remote_resource_unavailable` before auth/config side effects. Android keeps
the host-side remote route disabled.

The Pipe listener factory is not yet a complete application dependency. Any
parsed `--pipe-name` therefore returns exit `3` before signal ownership,
auth/config file creation, executor threads, or socket binding. This is a
fail-closed boundary, not a claim that Pipe works.

The native script composition is explicitly opt-in. The ordinary
`BAAS_service` CLI, including the process launched by the existing Tauri
integration, calls the dependency-free overload and registers no runtime task
commands. It does not invent a config snapshot, repository, device, or
provider. An embedding WebUI owner may pass the complete production provider;
that provider must pin the user-owned config and immutable runtime-repository
capabilities at task execution time. Resource files, scripts, and user config
therefore remain external dynamic data and are never compiled into the
executable.

Runtime repository ownership is independent of `FileResourceStore` and
`ServiceRuntimeProviderBridge`. A missing pointer or a valid activation for a
different generation fails with `runtime_repository_generation_mismatch`; an
invalid activation fails with `runtime_repository_invalid`. Both occur before
auth/config side effects. An exact activation and its validated, pathless
resources/scripts bundle are pinned for the entire service lifetime. `/health`
exposes only `phase=pinned` and its generation; it never exposes paths,
manifests, or repository entries.

## Lifecycle

After CLI validation, the process rejects Pipe, fixes the runtime repository
activation, and admits its full resources/scripts read bundle before it checks
the desktop remote payload, blocks shutdown signals, publishes `starting`,
constructs the production graph, and starts the loopback host. The listener initially
returns `503 health_starting`. After start, the
runtime/provider bridge must complete its real config/static/setup scan;
`ServiceApplication` then reads `AuthOwner::password_state()` and
`signing_public_key()`, base64url-encodes the key, and atomically publishes the
ready snapshot; `/health` then returns `200`.

The main thread waits for the first immutable shutdown reason. HTTP
`POST /shutdown` only publishes intent. Teardown withdraws readiness, calls
`ProductionHttpHost::stop()`, drains `TriggerExecutor` so no prepare/claim
window remains, closes `RuntimeTaskOwner` admission and cooperatively joins
all native tasks while their provider/device pins are alive, stops the remote
backend and its ADB/session owners, joins the runtime resource watcher and
resets the Provider initialized flag, and finally stops the signal owner.
Concurrent `stop()` calls are idempotent. Failures publish failed readiness
and use the same release sequence.

Auth state lives below `<project-root>/config`. Production storage keeps an
exclusive installation lock, rejects a concurrent second owner, and preserves
the signing identity across orderly restarts.

## Build and evidence

The executable is opt-in and excluded from the legacy `BAAS_CORE` glob:

```text
-DBUILD_SERVICE_APP=ON
-DBUILD_SERVICE_APP_TESTS=ON
```

`BAAS_service_application_tests` links hook-free production targets. It covers
real loopback `/version`, health `503` to ready `200`, auth routing, HTTP
shutdown, fixed-port conflict, real status, `detect_adb`, and durable
configuration create/copy commands,
persistent auth restart and second-instance locking, and Pipe rejection before
filesystem side effects. It also verifies desktop remote policy and missing
ws-scrcpy-resource failure before composition. The dedicated runtime repository
owner test covers exact generation matching, missing current,
malformed/tampered activation, retained pin lifetime, and concurrent readers
observing one startup generation. Application tests also verify that missing
or mismatched generations and invalid payload bytes fail before composition
side effects, that the application retains both same-generation read
capabilities, and that health does not expose repository paths or metadata.
Startup also reads and validates the user, event, switch, and static initializer
documents through the retained resources capability before constructing the mutable
user resource store. These documents remain external runtime data and are never
compiled into `BAAS_service`.
The separate `BAAS_service_production_runtime_path_tests` exercises the real
Trigger ingress/dispatcher/executor path through the production factory,
backend, owner, and control with synthetic pinned repositories and providers.
It covers absent opt-in, successful script/log execution, provider failure,
deadline, duplicate config admission, keyed cancellation, and concurrent
application shutdown.
Separate CTest entries execute
the actual binary for `--help` and `--version`. CI provisions pinned
cpp-httplib, libsodium, nlohmann-json, miniz, and OpenCV recipes, builds the
bounded ZIP codec test, and runs Debug and Release on Windows, Linux, and
macOS.

This slice does not claim Pipe composition, packaged Tauri runtime resources,
or an Android service executable. The production control and its dependency
closure are nevertheless compile-checked for both supported Android ABIs.
