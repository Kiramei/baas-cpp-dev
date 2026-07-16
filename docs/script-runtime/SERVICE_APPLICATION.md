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
- real `status`, `copy_config`, and `remove_config*` registrations,
  `TriggerDispatcher`, `TriggerExecutor`, and `TriggerHandlerFactory`;
- file auth storage, system clock, system random, and sodium password deriver;
- `ProductionHttpHost` on the exact CLI port.

The status source reads the thread-safe production provider snapshot.
Configuration triggers use the same durable `FileResourceStore` supplied to
the sync channel. Other catalog triggers remain unregistered. Remote policy is explicitly
`disabled` and its factory is null. This release does not install a remote
route.

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
`ProductionHttpHost::stop()`, joins the runtime resource watcher and resets the
Provider initialized flag, calls `TriggerExecutor::shutdown()`, and finally
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
shutdown, fixed-port conflict, real status and durable `copy_config`,
persistent auth restart and second-instance locking, and Pipe rejection before
filesystem side effects. Separate CTest entries execute the actual binary for
`--help` and `--version`. CI provisions pinned cpp-httplib, libsodium, and
nlohmann-json recipes and runs Debug and Release on Windows, Linux, and macOS.

This slice does not claim remote, Pipe composition, Tauri packaging, or an
Android service executable.
