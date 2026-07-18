# Service runtime script task backend

## Scope

`BAAS_service_runtime_script_task_backend` is the explicit native
`RuntimeTaskBackend` boundary for production BAAS Script execution. It is an
additive composition target. Building it does not register a service command,
change the Tauri launch path, or replace the legacy Python backend.

The first slice deliberately separates service task ownership from runtime
construction. `RuntimeScriptTaskRuntimeFactory::create()` is the only seam that
may resolve a user-created configuration, open the pinned runtime repository
view, build every ordered catalog resolution/execution plan and procedure
activation, and construct Procedure/Log/Resource Hosts. It returns a fresh
request-plan-local runtime
which retains all of those immutable owners until `execute()` returns.

The production Trigger path uses `prepare_runtime_script_task_backend()`. The
owner starts a gated worker and runs factory creation on that same worker (the
evaluator is thread-affine), then waits for preparation to finish before it
returns a reversible reservation. Provider pinning, catalog/resource loading,
task resolution, identity validation, repository binding, evaluator creation,
and thread allocation therefore all finish before the response claim. A null
provider publication, missing task, allocation failure, invalid identity, or
stale repository bundle returns a stable wire failure and publishes no fake
successful task generation.

## Identity and ownership contract

Before execution, the backend derives the complete ordered requested task plan
as `current_task` followed by every `waiting_tasks` entry, or the waiting list
alone when no current task is present. It requires the returned runtime to
identify:

- the exact requested config id and immutable config snapshot id;
- profile and concrete device id;
- the exact 64-character lowercase-hex runtime repository generation plus
  exact 40- or 64-character lowercase-hex scripts/resources commits;
- requested run mode, the complete ordered requested task plan, and a
  same-length ordered resolved canonical task plan.

Missing, empty, malformed, embedded-NUL, oversized, or request/plan-mismatched
identity fails closed before user script or native device code executes. A
factory copies exact identity from its retained repository bundle, execution
plans and procedure activations rather than synthesizing a numeric generation
or an unverified commit label. It must not reread a mutable config, catalog,
repository pin, or adapter registry after it returns. The
returned runtime owns its execution plan, `RuntimeProcedureActivation`, config
snapshot, per-task native procedure adapter, evaluator and Host lifetimes.

The factory is shared and its `create()` method may run concurrently. It must
be thread-safe. Every call returns a distinct `unique_ptr` runtime so different
configs have isolated mutable evaluator/adapter state. Physical-device
serialization, where required, belongs to the injected production factory's
shared device coordinator rather than global backend scratch state.

## Stop, deadline, progress and terminal mapping

The backend publishes a deterministic initial progress projection for the first
entry and passes the reporter into `RuntimeScriptTaskRuntime::execute()`. The
runtime owns execution of the entire retained plan, in order, and publishes
intermediate `current_task`/`waiting_tasks` projections as it advances. It must
not retain the reporter after `execute()` returns. A `false` progress result
means either that the weak reporter lease is stale or that deadline/stop became
active across the report; the runtime checks control after every report.

`RuntimeScriptTaskExecutionControl` gives both factory and runtime the same
stop token and absolute monotonic deadline. They must poll at bounded safe
points and pass the same cancellation/deadline context into evaluator and Host
calls. The backend checks after factory creation, initial progress reporting,
runtime execution and exception translation. At every simultaneous boundary
the stable priority is deadline, then stop, then ordinary failure:

- exception, allocation failure or invalid identity: `1`;
- deadline exceeded: `124`;
- cooperative cancellation: `130`;
- otherwise the runtime's exact terminal state, preserving both
  `is_flag_run` and its exit code (including null or zero).

Exceptions never cross the `RuntimeTaskOwner` worker boundary. If an exception
coincides with deadline or stop, the same deadline-before-stop precedence is
preserved rather than rewriting the boundary as generic failure.

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
