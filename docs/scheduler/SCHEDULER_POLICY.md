# Scheduler policy and `event.json` contract

`BAAS_scheduler_policy` is the side-effect-free C++ migration of
`baas-dev/core/scheduler.py`. It parses one caller-provided `event.json`
snapshot, selects due work, builds a serial invocation plan, and produces the
updated document that a future scheduler owner may persist after successful
execution.

It does **not** read or write files, inspect a repository, start threads, open a
device, or execute a task. The default `event.json` and all other runtime
resources remain external data and are not compiled or copied into the target.

## Build

The library and its deterministic tests are opt-in:

```text
-DBUILD_SCHEDULER_POLICY=ON
-DBUILD_SCHEDULER_POLICY_TESTS=ON
```

The target is `BAAS_scheduler_policy`, with alias
`BAAS::scheduler_policy`. Enabling the tests also enables the library.

Because the parser uses the pinned `BAAS::nlohmann_json` package, CI ownership
belongs to `.github/workflows/service-application.yml`, whose dependency closure
already provides that package. Its existing Windows, Linux, and macOS
Debug/Release matrix builds and runs `BAAS_scheduler_policy_tests`. A separate
Android arm64-v8a/x86_64 job generates the real Conan cross dependency and
compile-checks the library. The dependency-free foundation job remains
dependency-free.

## Input boundary

The parser accepts a UTF-8 JSON array. Every event has the ten fields used by
the current Python scheduler:

```text
enabled, priority, interval, daily_reset, next_tick,
event_name, func_name, disabled_time_range, pre_task, post_task
```

Numbers that participate in time or ordering must be JSON integers. Times of
day are `[hour, minute, second]`, with the normal 23:59:59 upper bound.
Disabled ranges are `[[start], [end]]`. Event, per-list, string, aggregate
string, JSON byte, nesting, and node counts all have caller-configurable bounds
under non-overridable hard ceilings. Duplicate keys, malformed UTF-8, malformed
JSON, missing fields, extra fields, invalid types, invalid times, and values
outside the supported timestamp/interval domain fail closed.

The strict ten-key rule is intentionally safer than Python's permissive
`json.load`: Python ignores unknown event fields, while this parser rejects
them so that a later full-document persistence cannot silently discard data.
The serializer also rejects output larger than the hard JSON byte ceiling.

## Deterministic planning

The caller supplies one `EvaluationTime`:

- `unix_seconds` is compared with `next_tick` using `next_tick <= now`.
- `local_seconds_since_midnight` evaluates `disabled_time_range`.
- `utc_seconds_since_midnight` is used only when computing daily resets after
  success.

No platform clock or timezone API is called. The separation freezes Python's
current mixed-timezone behavior: disabled periods use local wall time, whereas
`daily_reset` uses UTC. `unix_seconds % 86400` must equal the supplied UTC
seconds; inconsistent snapshots fail closed. Disabled endpoints are inclusive. A reverse range
(`start > end`) never matches, just as in Python; it does not wrap midnight.

An event is due only when it is enabled, due by timestamp, and outside every
disabled range. Plans use ascending priority. Equal-priority entries retain
their source JSON order.

Python fills `Scheduler.funcs` only during construction and retains it across
later configuration reloads. C++ makes that state explicit:

1. Call `snapshot_function_inventory` on the initialization document.
2. Pass it and every reloaded document through `refresh_function_inventory`.
   An empty initialization remains uninitialized until the first non-empty
   reload, matching Python's `if self.event_map == {}` condition.
3. Retain the initialized inventory across later parsed snapshots and pass it
   to `plan_due_events`.

Unknown pre/post references are skipped against that frozen inventory. Each
plan exposes one ordered sequence: all admitted `pre_task` calls, then the
`current_task`, then all admitted `post_task` calls. The policy never runs this
sequence itself.

## Completion and persistence boundary

`apply_completion` is a pure copy transform. A failure outcome returns the
unchanged bounded document with `persist_required == false`. A success outcome
computes a new `next_tick`, updates only the selected event in the returned
copy, and sets `persist_required == true`. The caller can serialize that copy
with `serialize_event_json` and atomically replace its runtime-owned file.

The next time mirrors `Scheduler.systole`:

1. A positive task-requested delay wins (`now + delay`).
2. Otherwise a positive event interval is used; a non-positive interval means
   86,400 seconds.
3. If the nearest UTC daily reset is no later than that interval deadline, the
   reset wins. A valid earlier reset also wins if the interval deadline would
   overflow the supported timestamp domain. An empty reset list leaves the
   interval deadline unchanged.

Invalid time inputs, indices, delays, or timestamp overflow return the unchanged
bounded document and never request persistence.

There is deliberately no operation journal. Until the owner has durably
replaced `event.json`, the original `next_tick` remains due. Therefore a crash
after task side effects but before persistence causes the task to be eligible
again after restart: the recovery guarantee is **at-least-once**, not
exactly-once. Adding exactly-once operation identity or a journal is outside
this pure policy layer.

## Confirmed differences from Python

- Input validation is bounded and fail-closed instead of raising arbitrary
  `KeyError`, `TypeError`, JSON, or filesystem exceptions later.
- Unknown event fields and duplicate keys are rejected rather than accepted.
- Time is injected explicitly and checked for UTC consistency, so repeated
  calls cannot observe slightly different `time.time()` values inside one
  calculation.
- The transform returns persistence data; it never opens `event.json` itself.
- Integer seconds are the policy boundary. Python can briefly use fractional
  seconds before truncating the persisted `next_tick` with `int()`.

All selection order, enabled/due checks, inclusive same-day disabled periods,
frozen pre/post function filtering, serial plan order, interval/reset choice,
and success-only advancement are preserved.
