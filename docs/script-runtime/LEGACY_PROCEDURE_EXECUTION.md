# Legacy Procedure Execution Boundary

This boundary is a compatibility seam between generation-bound procedure
activation and the existing Windows BAAS automation implementation. It does not
turn the legacy engine into an Android production engine.

## Trusted input and identity

`BAAS::run_procedure_definition` accepts an already parsed `BAASConfig` and
constructs one temporary procedure owned by the call. The entry point does not
read `BAAS_PROCEDURE_DIR`, create a temporary file, or insert into the ambient
`procedures` map. A native caller intended for the new runtime constructs `BAAS`
with `ProcedureCatalogMode::DirectDefinitionsOnly`, which skips the legacy
catalog scan entirely. The one-argument constructor retains the scan for old
BAAS APP callers.

The `procedure_id`, definition payload, declared effects, and allowed terminal
set are bound and verified by `RuntimeProcedureActivation`/`ProcedureDescriptor`
before this seam. This low-level compatibility entry checks that the supplied ID
is non-empty and that the payload has a typed `procedure_type`; it must not be
called with an untrusted ID/payload assembled outside that activation owner.
The direct seam mirrors the descriptor's lowercase path grammar and default
1,024-byte ID bound without linking the heavy snapshot implementation. A
successful result carries `source_terminal`, which the adapter must validate
against the already pinned descriptor terminal set.

## Typed result and control

`LegacyProcedureRunResult` distinguishes missing procedure, invalid definition,
cancellation, deadline, device disconnection, foreground package mismatch,
missing resource, business budget exhaustion, memory/resource exhaustion,
unavailable integration, and internal failure. Exception mapping is by C++ type
only; exception text is never parsed. Deadline failures are not retryable by
default, matching the generic Script Host callback boundary and resource and
procedure hosts.

`LegacyProcedureExecutionControl` checks the absolute deadline before stop-token
or owner cancellation. `BAAS::request_stop()` publishes cancellation through an
atomic flag. `AppearThenClickProcedure` checkpoints each loop and before and
after capture, vision, input, and bounded wait slices, so cancellation cannot be
reported as an empty successful result.

Direct `legacy.appear_then_click/v1` definitions require `procedure_type=0` and
freeze the exact field set: required `procedure_type`/`ends`, with optional
`max_stuck_time`, `max_execute_time`, `max_click_times`, `show_log`, `possibles`,
and `tentative_click`. Unknown fields, duplicate terminals, embedded NUL, and
control characters in terminal/feature keys are rejected rather than silently
ignored. Definitions are validated before the legacy constructors
narrow values: terminals and possible-action shapes are bounded, all times must
be finite and within explicit ranges, click budgets/counts are positive, and
offset type/size are constrained. Invalid direct payloads return
`InvalidDefinition`; the old ambient catalog path keeps its historical leniency.
In explicit compatibility mode, every `ends` and `possibles[*][0]` feature is
also checked against the initialized legacy feature registry before any effect;
a missing key returns retryable `ResourceNotFound` instead of aging into a
stuck/budget failure. This check is specific to the legacy
AppearThenClick adapter and does not define future locale/template miss rules.

## Effect honesty and current production status

`LegacyProcedureEffectScope` is the RAII observer seam for Capture, Vision,
Input, Wait, and ForegroundCheck operations. The current legacy operation layer
does not yet instrument every foreground/device operation. Therefore
`legacy_procedure_production_ready()` is false and the new entry defaults to a
typed `Unavailable` result. `CompatibilityUninstrumented` is an explicit bridge
mode for validating the old engine; it is not a production completeness claim.

ProcedureHost effect reporting permits repeated complete operations of one
effect (`Began -> Committed -> Began -> Committed`). A duplicate `Began`, a
terminal report without an active `Began`, or any report after `Unknown` is
invalid and fails the Host call closed.

## Concurrency and platform limits

The legacy engine still depends on process-wide global logger/resource/feature
state. A process-wide execution gate is required before more than one real
legacy procedure can run concurrently. The pure
`BAAS_legacy_procedure_execution` target compiles for Android to keep typed
control and observer interfaces portable; Android builds do not claim that the
legacy screenshot, vision, input, or device engine is available.

`DirectDefinitionsOnly` skips only `BAAS_PROCEDURE_DIR`. Legacy feature lookup
still comes from the process-global `BAAS_FEATURE_DIR`; production readiness
therefore remains false until a pinned `ResourceSnapshot` feature bridge replaces
that ambient registry. Screenshot interval waits are sliced to at most 50 ms and
checkpointed, but a bottom-layer device I/O call is only as interruptible as its
device adapter timeout; this boundary does not claim arbitrary device calls can
be preempted.
