# Service runtime task owner

## Boundary

`BAAS_service_runtime_task_owner` owns long-running C++ runtime jobs at the
service lifetime, not at a Trigger or WebSocket request lifetime. The component
does not register commands and does not provide a placeholder production
backend. A later composition layer must inject the real BAAS/script backend.

This preserves the Python service's externally useful model: a start call
returns immediately while the job continues in the background, one config has
at most one live job, and different configs can run concurrently.

## State machine

Each retained config occupies one bounded status slot:

```text
completed --start--> running --request_stop--> stopping --worker exit--> completed
                         |                            ^
                         +------natural exit---------+
```

- A start while `running` returns `already-running`.
- A start while `stopping` returns `stopping`. The old worker remains owned and
  the config cannot restart until it has actually exited. This closes the
  Python implementation's thread-handle stop/start race.
- A natural backend `false` return or exception produces exit code `1`.
- A backend `true` return preserves the Python `null` exit code.
- A requested cooperative stop preserves the Python `null` exit code after the
  backend returns, even when cancellation makes the backend return `false`.
- `shutdown()` closes admission, requests stop on every live job, and joins all
  owned worker threads before returning.

The owner never detaches workers. Disconnecting a transport therefore cannot
cancel a service-owned job, and destroying the owner reliably drains it. C++
cannot safely force-kill an arbitrary thread: every production backend must
have provable stop-safe points, observe its `std::stop_token`, and return after
cancellation. A backend that violates this contract will intentionally keep
`shutdown()` waiting rather than leave an unowned worker accessing torn-down
service state.

## Backend and progress contract

The injected `RuntimeTaskBackend` receives the immutable start request, a
`std::stop_token`, and a progress reporter. Different configs call the backend
concurrently, so the implementation and captured dependencies must be
thread-safe. Long-running implementations must poll or register against the
stop token and return after cancellation. The reporter updates `is_flag_run`,
the bounded raw `button` JSON/string payload, `current_task`, and
`waiting_tasks`; invalid or oversized progress is rejected without mutating the
last valid snapshot.

Snapshots include `config_id`, `running`, `stopping`, `is_flag_run`, `button`,
`current_task`, `waiting_tasks`, `exit_code`, `run_mode`, and a monotonically
advancing millisecond timestamp capped at JavaScript's 53-bit safe-integer
maximum. Multi-config snapshots are sorted by config id for deterministic
service serialization.

## Bounds

`RuntimeTaskLimits` caps retained configs, config ids, run modes, task names,
and waiting-task counts and lengths. Completed status is retained so clients
can observe the terminal exit code. Once `max_configs` distinct configs have
been retained, a new config is rejected with `capacity-exceeded`; restarting a
known completed config remains allowed.

## Build and test

Configure the standalone component with:

```sh
cmake -S . -B build/runtime-task-owner \
  -DBUILD_SERVICE_RUNTIME_TASK_OWNER_TESTS=ON
cmake --build build/runtime-task-owner --config Debug \
  --target BAAS_service_runtime_task_owner_tests
ctest --test-dir build/runtime-task-owner -C Debug \
  -R BAAS_service_runtime_task_owner_tests --output-on-failure
```

The tests execute injected backends on real worker threads and cover keyed
concurrency, duplicate admission, stop/start linearization, bounded progress,
failure translation, capacity enforcement, shutdown cancellation, and drain.
