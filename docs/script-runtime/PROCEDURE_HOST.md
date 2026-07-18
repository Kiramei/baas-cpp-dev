# Snapshot-owned Procedure Host foundation

Status: production-portable foundation implemented; legacy automation adapter,
task-backend composition, converted task packages, and Python parity remain
pending.

`BAAS_script_procedure_host` implements the exact `baas/procedure` v1 module and
`host.procedure.run.v1` binding specified by
`host-capabilities.v1.json`. Scripts supply an exact logical `procedure_id` and
an optional ordered map. Omission is the deterministic empty map. Success is
an ordered object beginning with the Host-owned `{end: string}` followed by the
executor payload fields declared by that procedure's immutable result schema.
An empty schema and `ProcedureExecutorOutcome::success(terminal)` retain the
historical terminal-only object. An undeclared terminal or payload mismatch
fails closed, and executor data can never replace or duplicate `end`.

## Descriptor-owned structured results

`ProcedureDescriptorInput::result_schema` recursively declares ordered object
fields and array item shapes using JSON `null`, `boolean`, `integer`, `float`,
`string`, `array`, and `object` types. Every named field is explicitly required
or optional. Publication rejects unknown schema keys in the production manifest,
duplicate or noncanonical field names, the reserved name `end`, malformed array
items, primitive children, excessive depth/nodes/strings/work, and digest drift.
The descriptor canonical domain is `baas.procedure.descriptor/v3`; requiredness,
field order, types, and complete nested shape therefore contribute to the
descriptor SHA and immutable snapshot identity.

The Host independently validates every returned payload. Unknown or duplicate
fields, missing required fields, schema-order drift, type mismatches, non-finite
floats, and attempts to forge `end` are adapter failures. Result depth, nodes,
string bytes, total logical bytes, and validation work each have separate
limits from input options. Allocation failure is contained as non-retryable
`HOST005_BUDGET_EXCEEDED`; no adapter or result-processing exception crosses the
Host ABI. Effect-trace invalidity remains first, and deadline then cancellation
postflight is repeated after bounded result processing before success publishes.

## Immutable activation boundary

`ProcedureSnapshot::build` accepts validated logical descriptors and an
externally supplied `resources::ResourceSnapshot`. It never accepts or stores a
filesystem path, native procedure pointer, Python module, user configuration,
embedded resource, or ambient global. Publication defensively owns descriptor
strings and vectors, checks lowercase logical IDs, ASCII case collisions,
duplicates, terminal order, effect/resource sets, SHA-256 descriptors, bounded
work, and the presence of every logical resource in the exact external
snapshot. Snapshot identity includes that resource snapshot identity, so a
resource generation cannot be substituted after activation. Descriptor
canonical domain v3 additionally binds `implementation_sha256` and the result schema; production
activation derives it from the engine, complete definition-file digest, and
ordered source-to-terminal mapping under `baas.procedure.implementation/v2`.
Definition-only changes therefore cannot
reuse a stale snapshot identity. See `RUNTIME_PROCEDURE_ACTIVATION.md`.

The injected `ProcedureExecutor` receives the owned snapshot, immutable
descriptor, frozen physical `device_id`, copied options, cancellation/deadline
probe, and allocation-free bounded effect reporter. It is an adapter interface,
not the legacy adapter: this target deliberately does not link or call
`BAAS::solve_procedure`, mutable procedure registries, process-global config, or
ambient resource loaders.

## Physical-device strand

Composition creates one `PhysicalDeviceCoordinator` and shares it across every
task/config `ProcedureHost` in the process. Calls for one exact physical device
are FIFO-serialized across Host instances. Calls for different devices may run
concurrently. Strand admission polls cancellation and deadline with a bounded
interval; deadline wins when both are observable. Saturation is
`HOST016_BACKPRESSURE`, shutdown is `HOST006_UNAVAILABLE`, and synchronous
same-thread reentry for the same device is rejected immediately as
`HOST006_UNAVAILABLE` instead of deadlocking. These coordinator conditions do
not invent public detail discriminators.

Each acquired lease also creates a private logical admission lineage. The
executor receives its opaque `HostAdmissionToken` through
`ProcedureExecutionRequest` and must propagate it in
`HostCallContext::admission` when helper threads make nested Host calls. A
token's recognized concrete identity is private to the coordinator, is bounded
by `max_admission_depth`, and becomes inert when its lease is released.
Consequently, cross-thread same-device logical reentry is rejected before it
can enter the FIFO queue; a caller cannot fabricate a token that the
coordinator recognizes. Multi-waiter non-reentrant calls retain exact ticket
FIFO order. If constructing a queued front admission runs out of memory, its
ticket and waiter accounting are removed before the allocation failure leaves
the coordinator, so later same-device calls cannot inherit a poisoned queue.

The executor must cooperatively poll during its own bounded work. Host
postflight checks prevent a late success from outranking a deadline or
cancellation. Effect reporting records `not_started`, `committed`, or `unknown`
without allocating. Each operation must report `Began` before `Committed` or
`Unknown`; a committed operation may be followed by another `Began` for the same
effect, while `Unknown` is permanently conservative. Duplicate `Began`, a
terminal report without an active operation, and every report after `Unknown`
invalidate the trace. Undeclared effects and terminals fail closed as
`HOST014_INTERNAL`; exceptions never cross the Host callback boundary. The
executor call is synchronous: adapters must join helper work and must not retain
the request or reporter beyond `execute()`.

Executor `BudgetExceeded` and `ResourceNotFound` preserve the adapter's typed
`retryable` value. `ResourceExhausted` is distinct in the executor ABI and maps
to `HOST005_BUDGET_EXCEEDED` with `retryable=false` and
`details.budget_scope=external_memory`; adapters must not throw `bad_alloc` to
forge this business result.

## Foreground mismatch

The typed foreground-package mismatch maps to `HOST006_UNAVAILABLE`, always
`retryable=true`, with the sole public discriminator
`details.unavailable_reason=foreground_package_mismatch`. Actual and expected
package names are not retained or published. The synchronous evaluator bridge
allowlists only that exact unavailable discriminator and preserves it in the
catchable script Error envelope together with the exact retryable flag and
effect state. Foreground mismatch effect state is derived exclusively from the
reporter's `input` trace: committed capture/vision/wait/foreground-check effects
remain `not_started`, committed input is `committed`, and begun/indeterminate
input is `unknown`. The executor error's supplied aggregate effect state is
deliberately ignored for this discriminator, so it cannot impersonate input.

## Build and evidence

Use `BUILD_SCRIPT_PROCEDURE_HOST=ON` for the production library or
`BUILD_SCRIPT_PROCEDURE_HOST_TESTS=ON` for native tests. Foundation CI builds the
test on Windows, Linux, and macOS and cross-compiles the production library for
Android `arm64-v8a` and `x86_64`. Native tests cover immutable ownership and
identity, strict malformed input, success/error mapping, lifetime, same-device
serialization, different-device concurrency, cancellation/deadline points,
same-thread and propagated cross-thread reentry, multi-waiter FIFO order,
queued admission allocation recovery, shutdown, exception/allocation failure,
64 repeated concurrency runs, nested shop plan/balance/formatted-text results,
strict schema failures, independent result limits, injected allocation failures,
and structured-result deadline/cancellation precedence.
Script evaluator tests cover the foreground discriminator through the language
Error envelope.

The exact CMake library target is `BAAS_script_procedure_host`; the executable
golden/unit target is `BAAS_script_procedure_host_tests`. A mock runner builds
external bytes with `resources::ResourceSnapshot::build`, publishes logical
procedures with `ProcedureSnapshot::build`, creates the shared strand with
`PhysicalDeviceCoordinator::create`, injects a `ProcedureExecutor`, and calls
`make_procedure_host_runtime`. The returned `ProcedureHostRuntime::metadata`
and `ProcedureHostRuntime::bindings` are immutable inputs for evaluator Host
composition. `ProcedureHostTests.cpp` copies the exact
`host.procedure.run.v1` binding into a combined Procedure/LogHost registry and
runs a real `SynchronousEvaluator` group golden trace. It also executes the
actual ProcedureHost foreground mismatch and checks ERR-016 publishes
`error.details.host_details.unavailable_reason` unchanged.
