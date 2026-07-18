# Production runtime script task factory

## Scope

`BAAS_service_production_runtime_script_task_factory` is the concrete,
explicitly selected implementation of `RuntimeScriptTaskRuntimeFactory`. It is
additive: building it does not register a command, change the Tauri startup
path, or replace the legacy Python backend.

The provider pins one immutable publication per request: the user-created
config snapshot, one runtime repository read bundle, trust evidence, the exact
device session, a structured log sink, and optional extensions. No config,
resource payload, BAAS Script package, procedure definition, or repository
checkout path is embedded in the executable. Tauri may keep updating the
repositories; a WebUI composition may use the libgit2-backed updater. Both
publish the same pinned read boundary to this factory.

## Construction and identity

For the complete ordered request plan the factory loads the resource snapshot
and script catalog once, then resolves and builds every package execution plan.
Each task receives a fresh ResourceHost, ProcedureHost, LogHost, composed Host
registry, evaluator, and engine adapter. Only the physical-device coordinator
is shared, so different runtimes keep evaluator and Host state isolated while
operations against one device remain serialized.

The returned identity retains the exact config and snapshot ids, device and
profile, 64-character repository generation, scripts/resources commits, run
mode, requested plan, and canonical plan. Trust or device identity mismatch,
unsupported procedure engines, missing dynamic support bundles, duplicate Host
modules/bindings, and extension identity mismatch all fail closed.

The built-in production procedure engine is
`co_detect.python-compat/v1`. Its definition and support archive are read from
the pinned scripts/resources repositories at runtime. The configured support
resource must also be an exact member of that procedure activation descriptor's
declared resource ids. An immutable extension may provide request-local Host
contributions and the already activation-supported
`legacy.appear_then_click/v1` executor. It is not an arbitrary engine registry:
unknown or future engines fail closed during activation. Extension identity
must cover the exact `config_id`, config snapshot id, and repository commits;
matching snapshot labels from different configs are not interchangeable.

## Execution and cleanup

Tasks execute in plan order. Success requires the entry export to return the
exact Boolean value `true`; `false`, `null`, numbers, exceptions, or Host
failure map to ordinary failure. Boundary precedence is deadline, then stop,
then ordinary failure.

Every task owns an RAII cleanup boundary. On normal completion and the supported
adapter failure paths, a later plan build failure, cancellation, extension
exception, final device drift, or destruction without execution performs
evaluator close, retries detached typed-handle releases, shuts down and drains
LogHost, and finally releases Host lifetime owners. Internal dispatcher
invariant corruption remains fail-fast; this boundary does not claim to turn an
unsafe dispatcher state into recoverable success.

## Build and test

```sh
cmake -S . -B build/production-runtime \
  -DBUILD_SERVICE_PRODUCTION_RUNTIME_SCRIPT_TASK_FACTORY_TESTS=ON
cmake --build build/production-runtime --config Debug \
  --target BAAS_service_production_runtime_script_task_factory_tests
ctest --test-dir build/production-runtime -C Debug \
  -R BAAS_service_production_runtime_script_task_factory_tests \
  --output-on-failure
```

Foundation CI executes the production factory suite with the pinned
nlohmann-json, miniz, and OpenCV dependency closure on Windows, Linux, and
macOS, and cross-compiles the production library for Android arm64-v8a and
x86_64.
