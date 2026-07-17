# Runtime task status JSON

`BAAS_service_runtime_task_status_json` converts the service-owned runtime task
snapshots into the exact object used by Python `ServiceRuntime.current_status()`.
It is a pure, bounded serialization layer. It does not read config, resources,
scripts, or filesystem paths and it does not own task execution.

The top-level keys are config ids in deterministic bytewise order. Each value
contains `config_id`, `running`, `is_flag_run`, `button`, `current_task`,
`waiting_tasks`, `exit_code`, `run_mode`, and `timestamp` in that order. The C++
owner's internal `stopping` field is deliberately omitted from the v1 wire
contract. Explicit exit code zero remains distinct from `null`.

`button` is the legacy signal boundary: bounded valid JSON remains a JSON value,
while other valid UTF-8 text is encoded as a string. The encoder rejects invalid
UTF-8, duplicate config ids, timestamps outside JavaScript's safe-integer range,
invalid limits, and independently bounded config counts, waiting-task counts,
button JSON, and output bytes. It never substitutes `{}` after an encoding
failure; the future production composition must fail closed instead of erasing
the last observable task state.

This component prepares provider publication but does not claim that production
task execution is already connected. A later composition layer will encode a
stable `RuntimeTaskOwner::snapshots()` copy and pass the resulting JSON to the
production provider backend.
