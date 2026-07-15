# Production status trigger registration

`BAAS_service_status_trigger` supplies the first production trigger command
registration. It returns exactly one `TriggerHandlerRegistration` named
`status`; the other catalog commands remain unregistered and therefore retain
the existing correlated `unregistered_command` failure.

## Python source audit

The compatibility source was audited in `Kiramei/baas-dev` at revision
`75bbacb545bc87e9510d85cbe8034f9180397004`:

- `service/api/commands.py:139-140` returns
  `{"status": "ok", "data": context.runtime.current_status()}` without an
  intermediate wrapper inside `data`.
- `service/runtime.py:200-202,257-260` owns `_statuses` as
  `Dict[str, Dict[str, Any]]`; `current_status()` holds `_status_lock` and
  returns a deep copy.
- `service/runtime.py:166-177` establishes each config status object with
  `config_id`, `running`, `is_flag_run`, `button`, `current_task`,
  `waiting_tasks`, `exit_code`, `run_mode`, and integer-millisecond `timestamp`.
  Signal payloads and later updates may change nested values, so this slice does
  not freeze the inner object schema.
- Before any config session exists the value is `{}`. Once sessions exist the
  top level is always an object mapping config id to a status object.
- Scheduler/solver and signal callbacks update status from background threads.
  The Python lock plus deep copy makes the snapshot stable. C++ trigger workers
  can call the injected source concurrently, so the C++ source has the same
  explicit thread-safety obligation.

The Python trigger channel adds `type`, `command`, `status`, and `timestamp`
outside this value. The C++ handler therefore calls `sink.success(data_json)`;
the existing trigger codec, not this registration, owns that outer envelope.

## Source and capability boundary

`StatusSource::current_status(stop_token)` or a `StatusSourceCallback` is
injected by the application runtime adapter. There is no default source,
provider-backend substitution, application singleton lookup, or synthetic
snapshot. A source must:

- return one owning JSON string representing a stable current-status snapshot;
- be safe for concurrent calls from `TriggerExecutor` workers;
- observe the stop token around blocking work;
- bound its own snapshot/copy work before returning.

The registration revalidates returned data. The top level must be an object and
the complete text must be valid UTF-8 JSON with no duplicate object keys. Bytes,
recursive depth, and value-node count use independent explicit limits, with
hard ceilings of 4 MiB, depth 256, and 262,144 nodes. The default status byte
limit is 512 KiB so the status value plus the outer response fit beneath the
default 1 MiB trigger response budget.

The shared trigger codec performs this bounded grammar validation and a probe
encoding before business success is staged. Its JSON number support includes
fraction/exponent forms, which preserves possible JSON-compatible Python signal
payloads. A successful snapshot is then passed unchanged to `sink.success` and
the codec emits the deterministic outer response.

The trigger protocol reserves a top-level `data.binary` member for an
immediately-following byte frame. Normal BAAS config ids are `default_config` or
generated timestamp ids, so audited `current_status()` output does not use that
member. A manually introduced config id exactly equal to `binary` fails closed
at the protocol codec instead of forging a byte-frame promise.

## Failure and cancellation

Failures publish one correlated terminal with stable non-sensitive text:

| Boundary | Result |
|---|---|
| source capacity | `status_source_capacity` |
| source unavailable | `status_source_unavailable` |
| source exception | `status_source_exception` |
| invalid/non-object JSON | `status_invalid_json` |
| byte/depth/node/work capacity | `status_json_capacity` |
| success rejected by the outer response budget | `status_response_rejected` |

An already-stopped token never calls the source. Source-reported cancellation or
cancellation observed after the source/validation publishes a cancelled
terminal; a late snapshot cannot win. The dispatcher stages the terminal until
the handler returns. If session egress is full, `TriggerExecutor` retains and
retries the owned terminal without calling the source again.

## Build and evidence

The library is opt-in through `BUILD_SERVICE_STATUS_TRIGGER`; tests use
`BUILD_SERVICE_STATUS_TRIGGER_TESTS` and enable the existing dispatcher and
executor owners. `BAAS_service_status_trigger_tests` exercises the exact Python
status data envelope, empty status, source-interface and callback factories,
invalid/scalar/duplicate JSON, byte/depth/node limits, exception and source
errors, cooperative running cancellation, concurrent calls, response encoding
recovery, and retained-terminal backpressure without rerun. Foundation CI
builds and runs the target on Windows, Linux, and macOS in Debug and Release.
