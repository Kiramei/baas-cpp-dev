# Production service application

`BAAS_service` is the first real standalone C++ service executable. Its CMake
target and packaged filename are exactly `BAAS_service` (`BAAS_service.exe` on
Windows), matching `baas-tauri`'s `launch_cpp_backend_command`:

```text
BAAS_service --project-root <directory> --host 127.0.0.1 --port <1..65535>
```

The executable identity and wire identity are intentionally separate.
`BAAS_service` is the process/package name and remains the `--version` prefix;
the HTTP `GET /version` response uses `"service":"BAAS Service"`. This is an
explicit cross-repository contract with `baas-tauri` revision `a1c8c837`,
`src-tauri/src/commands.rs`, function `cpp_backend_ready`, whose strict
readiness check requires `ok=true`, `api_version=1`, and the exact service
literal `BAAS Service` before it accepts `/health` as ready.

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
- desktop `ProductionRemoteBackend`, `RemoteHandlerFactory`, and the same
  shared resource store/ADB smart-socket transport;
- real `status`, `add_config*`, `copy_config`, `remove_config*`, `export_config`,
  and `import_config` registrations,
  `TriggerDispatcher`, `TriggerExecutor`, and `TriggerHandlerFactory`;
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

## Lifecycle

After CLI validation, the process rejects Pipe, blocks shutdown signals,
publishes `starting`, constructs the production graph, and starts the loopback
host. The listener initially returns `503 health_starting`. After start, the
runtime/provider bridge must complete its real config/static/setup scan;
`ServiceApplication` then reads `AuthOwner::password_state()` and
`signing_public_key()`, base64url-encodes the key, and atomically publishes the
ready snapshot; `/health` then returns `200`.

The main thread waits for the first immutable shutdown reason. HTTP
`POST /shutdown` only publishes intent. Teardown withdraws readiness, calls
`ProductionHttpHost::stop()`, stops the remote backend and its ADB/session
owners, joins the runtime resource watcher and resets the Provider initialized
flag, calls `TriggerExecutor::shutdown()`, and finally
`ServiceSignalOwner::stop()`. Failures publish failed readiness and use the same
reverse release sequence.

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
ws-scrcpy-resource failure before composition. Separate CTest entries execute
the actual binary for `--help` and `--version`. CI provisions pinned
cpp-httplib, libsodium, nlohmann-json, and miniz recipes, builds the bounded ZIP
codec test, and runs Debug and Release on Windows, Linux, and macOS.

This slice does not claim Pipe composition, packaged Tauri runtime resources,
or an Android service executable.
