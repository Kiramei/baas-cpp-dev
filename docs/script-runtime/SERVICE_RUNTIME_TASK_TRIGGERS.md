# Service runtime task Trigger registrations

## Scope

`BAAS_service_runtime_task_triggers` supplies the protocol-to-runtime boundary
for these Python-compatible Trigger descriptors:

| Descriptor | RuntimeTaskControl call | Required input |
|---|---|---|
| `start_scheduler` | `start_scheduler(config_id)` | non-empty `config_id` |
| `stop_scheduler` | `stop_scheduler(config_id)` | non-empty `config_id` |
| `solve` | `start_task(config_id, payload.task)` | non-empty `config_id` and string `task` |
| `start_*` | `start_task(config_id, original_command)` | non-empty `config_id` |
| `stop_all_tasks` | `stop_all_tasks()` | none |

The catalog/Trigger ingress validates the `config_id` policy first. The
registration repeats that check at the business boundary so a future
transport cannot bypass it. `solve` payloads receive bounded UTF-8, depth,
node-count, duplicate-key and byte validation before a JSON DOM is retained.

## Ownership and cancellation

`RuntimeTaskControl` is an abstract service-owned lifecycle boundary. Each
method transfers or stops ownership and returns an immediate result object; it
must not wait for the scheduler or task job to complete. Accepted jobs remain
owned by the service rather than the Trigger connection. The interface does
not accept a Trigger `stop_token`, so closing or cancelling a connection cannot
become cancellation of an already transferred job. Cancellation may still win
the correlated response while admission is in flight.

The `start_*` registration deliberately passes the original command to the
control owner. The later concrete RuntimeTaskOwner adapter is responsible for
normalizing Python aliases such as `start_hard_task` to
`explore_hard_task`. Results such as `already-running` are successful data
objects, not protocol errors.

## Wire result and errors

Successful control results provide the exact JSON object placed in
`command_response.data`; the adapter validates it against explicit byte,
depth, node and UTF-8 limits before publication. Control failure categories
map to stable wire strings:

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
bounded input/result validation, stable errors, exception redaction, and
disconnect-independent service ownership.
