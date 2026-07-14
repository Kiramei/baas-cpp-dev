# Script LogHost foundation

`baas/log.emit` now has a production-capable synchronous Host path. The
dependency-free runtime owns `QueuedLogHost`; the primary BAAS application
compiles `BAASLoggerLogSink` as a separate adapter so foundation builds do not
inherit spdlog or application-global state.

This is a real bounded logging adapter foundation, but not yet application
activation. Package manifest activation, construction from a live script task,
and propagation of real task/session/config identifiers into each evaluator are
still required before the roadmap's complete logging/structured-events item can
be checked.

## Ownership and effect boundary

`QueuedLogHost` retains one `StructuredLogSink`, immutable host identity,
redaction secrets, fixed limits, and one `BoundedExecutor` worker. The callback
does only the following synchronous work:

1. validate the exact `host.log.emit.v1` level/message/ordered-fields shape;
2. copy host-owned task, session, and configuration identity separately from
   script fields;
3. recursively redact messages, identity strings, field keys, and field string
   values under explicit byte/node/work limits;
4. validate fixed-key bounded JSON serialization; and
5. attempt one non-blocking queue insertion.

Successful queue insertion is the Host effect boundary. The single worker
preserves accepted order and owns all sink calls. A full queue returns retryable
`HOST016_BACKPRESSURE` with `effect_state=not_started`; shutdown returns
`HOST006_UNAVAILABLE`; invalid levels/shapes or redaction key collisions return
`HOST002_INVALID_ARGUMENT`; redaction/node/serialized-byte exhaustion returns
`HOST005_BUDGET_EXCEEDED` with `budget_scope=host_operation`. Allocation failure
continues through the allocation-free callback boundary and becomes
`MemoryLimitExceeded`.

An exception from the asynchronous sink cannot be returned to a call whose
enqueue already succeeded. It is contained at the worker boundary and counted
as `sink_failures`. `accepted`, `delivered`, backpressure, unavailable, and sink
failure counters are observable for embedding diagnostics. `shutdown()` stops
admission and drains every accepted event; it never reopens the queue.

## Identity, redaction, and serialization

Scripts cannot set or overwrite `task_id`, `session_id`, or `config_name`.
Those values are supplied when the host is constructed and occupy fixed
top-level fields outside the script's ordered `fields` object. Empty or invalid
UTF-8 secrets are rejected before worker creation. Secrets are normalized
longest-first, and every scan and replacement shift is charged to the redaction
work budget. If redaction would collapse two keys into one, the event is
rejected before enqueue rather than emitting duplicate JSON keys.

`serialize_structured_log_event()` emits this stable key order:

```json
{"level":"info","message":"ready","task_id":"task-7","session_id":"session-3","config_name":"primary","fields":{"attempt":1}}
```

Strings use JSON escaping, object insertion order is preserved, non-finite or
invalid native values are rejected, and the caller supplies the maximum output
size. This format is the compatibility bridge for text loggers; it is not a
script-selectable filesystem destination.

## BAAS application adapter

When `BUILD_APP_BAAS=ON`, CMake also enables `BUILD_SCRIPT_RUNTIME`, compiles
`BAAS_script_baas_logger_adapter`, and links it into `BAAS_APP`.
`BAASLoggerLogSink` serializes the structured event and maps levels to the
existing `GlobalLogger::_out` trace/debug/info/warn/error/critical surface.
`make_baas_logger_log_binding()` owns the queue and returns the exact catalog
binding. `GlobalLogger` remains application-owned and must outlive that binding;
destroying the binding drains the queue first.

The adapter does not initialize global logging, choose an output directory, or
start a script evaluator by itself. Runtime composition must pass an already
owned logger and live execution identity.

## Verification boundary

`BAAS_script_log_host_tests` covers evaluator-to-queue execution, immutable
identity, nested redaction, duplicate-key rejection, fixed JSON, deterministic
backpressure, drain/shutdown, asynchronous sink containment, and configuration
limits. The normal BAAS application build compiles the real `BAASLogger` sink.

Still pending are live package activation, real task/session/config propagation,
service structured-log streaming, Python parity traces for event metadata and
redaction, and end-to-end Tauri display. No filesystem, service, device, or UI
process is started by these foundation tests.
