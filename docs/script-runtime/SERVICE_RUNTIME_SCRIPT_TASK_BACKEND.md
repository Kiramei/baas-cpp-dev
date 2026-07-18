# Service runtime script task backend

## Scope

`BAAS_service_runtime_script_task_backend` is the explicit native
`RuntimeTaskBackend` boundary for production BAAS Script execution. It is an
additive composition target. Building it does not register a service command,
change the Tauri launch path, or replace the legacy Python backend.

The first slice deliberately separates service task ownership from runtime
construction. `RuntimeScriptTaskRuntimeFactory::create()` is the only seam that
may resolve a user-created configuration, open the pinned runtime repository
view, build the catalog resolution/execution plan and procedure activation, and
construct Procedure/Log/Resource Hosts. It returns a fresh task-local runtime
which retains all of those immutable owners until `execute()` returns.

## Identity and ownership contract

Before execution, the backend requires the returned runtime to identify:

- the exact requested config id and immutable config snapshot id;
- profile and concrete device id;
- nonzero runtime repository generation plus exact scripts/resources commits;
- requested run mode/task and the resolved canonical task.

Missing, empty, oversized, or request-mismatched identity fails closed before
user script or native device code executes. A factory must not reread a mutable
config, catalog, repository pin, or adapter registry after it returns. The
returned runtime owns its execution plan, `RuntimeProcedureActivation`, config
snapshot, per-task native procedure adapter, evaluator and Host lifetimes.

The factory is shared and its `create()` method may run concurrently. It must
be thread-safe. Every call returns a distinct `unique_ptr` runtime so different
configs have isolated mutable evaluator/adapter state. Physical-device
serialization, where required, belongs to the injected production factory's
shared device coordinator rather than global backend scratch state.

## Stop, deadline, progress and terminal mapping

The backend selects `current_task`, or the first waiting task when current is
absent, and publishes one deterministic initial progress projection. A `false`
progress result only means that the weak reporter lease is stale; cancellation
is owned by the supplied stop token.

`RuntimeScriptTaskExecutionControl` gives both factory and runtime the same
stop token and absolute monotonic deadline. They must poll at bounded safe
points and pass the same cancellation/deadline context into evaluator and Host
calls. The backend also checks both boundaries before and after execution:

- exception, allocation failure, invalid identity or invalid terminal: `1`;
- deadline exceeded: `124`;
- cooperative cancellation: `130`;
- otherwise the runtime's exact terminal exit code (including null or zero).

Exceptions never cross the `RuntimeTaskOwner` worker boundary. A runtime result
with `is_flag_run=true` is not terminal and therefore fails closed.

## Runtime-state boundary

No resources, config defaults, BAAS Script packages, procedure definitions, or
repository checkout paths are compiled into this target. User config is
created and edited at runtime. Desktop Tauri may continue to own repository
updates; native WebUI/service composition may supply a libgit2-backed pinned
factory. Both routes inject runtime state through the same factory contract.

## Build and test

```sh
cmake -S . -B build/runtime-script-task-backend \
  -DBUILD_SERVICE_RUNTIME_SCRIPT_TASK_BACKEND_TESTS=ON
cmake --build build/runtime-script-task-backend --config Debug \
  --target BAAS_service_runtime_script_task_backend_tests
ctest --test-dir build/runtime-script-task-backend -C Debug \
  -R BAAS_service_runtime_script_task_backend_tests --output-on-failure
```

Foundation CI runs the tests on Windows, Linux and macOS in Debug/Release and
cross-compiles the production library for Android arm64-v8a and x86_64.
