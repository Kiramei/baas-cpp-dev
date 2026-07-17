# Service runtime task Trigger registrations

## Scope

`BAAS_service_runtime_task_triggers` supplies the protocol-to-runtime boundary
for these Python-compatible Trigger descriptors:

| Descriptor | RuntimeTaskControl call | Required input |
|---|---|---|
| `start_scheduler` | `prepare_start_scheduler(config_id)` | non-empty `config_id` |
| `stop_scheduler` | `prepare_stop_scheduler(config_id)` | non-empty `config_id` |
| `solve` | `prepare_start_task(config_id, payload.task)` | non-empty `config_id` and non-empty string `task` |
| `start_*` | `prepare_start_task(config_id, original_command)` | non-empty `config_id` |
| `stop_all_tasks` | `prepare_stop_all_tasks()` | none |

The catalog/Trigger ingress validates the `config_id` policy first. The
registration repeats that check at the business boundary so a future
transport cannot bypass it. The global `TriggerEnvelope` limit (1 MiB by
default) is the first DOM/allocation boundary. This adapter's 64 KiB default
plus UTF-8, depth, node-count and duplicate-key validation is a second,
stricter business boundary before control admission and before retaining its
secondary task-extraction DOM; it is not the initial wire parser boundary.

Python tests `if not task`, so unusual truthy non-string values can travel
farther before failing in runtime code. This adapter intentionally tightens
that edge to a non-empty string: supported task names are strings, and the
stricter type prevents ambiguous ownership reservations while preserving all
normal Python/Tauri requests.

## Ownership and cancellation

`RuntimeTaskControl` is an abstract service-owned two-phase lifecycle boundary.
A `prepare_*` method creates a reversible reservation and its response data but
must not start or stop real work. It completes every potentially failing
validation, conflict check, allocation, and resource reservation. Returning a
prepared operation is therefore a guarantee that the later ownership transfer
cannot fail or throw. The adapter validates response data and calls
`TriggerResponseSink::irrevocable_success` to atomically close the cancellation
window. Only a successful claim is followed by
`RuntimeTaskPreparedOperation::commit() noexcept`, which performs the
infallible, exactly-once ownership transfer and returns immediately without
waiting for the job.

Cancellation before claim destroys/aborts the reservation and never calls
`commit()`. Cancellation after claim cannot replace the terminal or cancel the
committed service-owned job. There is intentionally no error-correction branch
after claim: an implementation that can still fail has not completed prepare
and does not satisfy this interface. The control interface receives no Trigger
`stop_token`.

The `start_*` registration deliberately passes the original command to the
control owner. The later concrete RuntimeTaskOwner adapter is responsible for
normalizing Python aliases such as `start_hard_task` to
`explore_hard_task`. Results such as `already-running` are successful data
objects, not protocol errors.

## Wire result and errors

Successful prepare results provide the exact JSON object placed in
`command_response.data`; the adapter validates it against explicit byte,
depth, node and UTF-8 limits before claim. All control failures occur during
prepare and map to stable wire strings:

| Control error | Wire error |
|---|---|
| `invalid_config_id` | `runtime_task_invalid_config_id` |
| `invalid_task` | `runtime_task_invalid_task` |
| `conflict` | `runtime_task_conflict` |
| `capacity` | `runtime_task_control_capacity` |
| `unavailable` | `runtime_task_control_unavailable` |
| `internal_error` | `runtime_task_internal_error` |

Exceptions are redacted to `runtime_task_control_exception`. This slice adds
no request ID, durable result store, replay behavior, placeholder runtime, or
application composition. A concrete production `RuntimeTaskControl` must be
installed separately when RuntimeTaskOwner is available.

## Build and test

Configure with `BUILD_SERVICE_RUNTIME_TASK_TRIGGERS=ON` to build the library or
`BUILD_SERVICE_RUNTIME_TASK_TRIGGER_TESTS=ON` to build and register its native
test. The test covers every descriptor, Python response data, alias handoff,
the secondary bounded input/result boundary, stable errors, exception
redaction, cancel-before-claim under backpressure, cancel-after-claim, and
prepare-failure rollback before claim. Successful and claimed operations also
verify exactly one commit with no rollback after ownership transfer.
