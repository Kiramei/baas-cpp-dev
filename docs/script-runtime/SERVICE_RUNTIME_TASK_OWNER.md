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
                  prepare_start
                        |
                        v
        +-------- reserved (worker gated) --------+
        |                                         |
      abort                                    commit
        |                                         |
        v                                         v
   prior/absent  completed <--worker exit-- stopping <--request_stop-- running
                                      ^                          |
                                      +-------natural exit-------+
```

- `prepare_start()` performs all request validation, bounded copies, capacity
  reservation, response construction, and worker-thread creation. Its move-only
  RAII reservation is not returned until those fallible steps have succeeded.
- A prepared worker waits at a private gate. Public snapshots continue to show
  the prior completed generation (or no config for a newly reserved id), never
  a false `running` state. Another prepare for the same config returns the
  deterministic `reservation-conflict` decision.
- Destroying or move-assigning over a pending reservation releases the gate as
  cancelled, joins the worker, and only then removes its slot. The slot remains
  reserved and capacity-charged until that join completes, so shutdown cannot
  return early and a same-config prepare cannot overlap an aborting worker. The
  backend is never invoked. `commit() noexcept` atomically installs the new
  generation and releases the gate exactly once; it performs no allocation and
  cannot fail.
- Failure to create the gated worker rolls a public `prepare_start()` back to
  the prior completed generation, or erases a newly allocated config slot. It
  returns a terminal failure snapshot to the caller but publishes no state for
  an operation that can never be committed.
- The legacy `start()` API is implemented as `prepare_start()` followed
  immediately by `commit()`. Its private compatibility path alone preserves
  the existing visible terminal thread-creation-failure snapshot.
- A start while `running` returns `already-running`.
- A start while `stopping` returns `stopping`. The old worker remains owned and
  the config cannot restart until it has actually exited. This closes the
  Python implementation's thread-handle stop/start race.
- `runtime_task_terminal_from_result(false)` and an exception produce exit code
  `1`; `runtime_task_terminal_from_result(true)` preserves the Python `null`.
- The richer `RuntimeTaskTerminal` is authoritative: its final `is_flag_run`
  and optional exit code are preserved exactly, including explicit zero.
- A cooperative stop does not rewrite that terminal. The backend decides
  whether cancellation is normal (`null`) or has a meaningful exit code.
- External `shutdown()` closes admission, requests stop on every live job, and
  joins all owned worker threads before returning. It also waits for each
  outstanding reservation to be explicitly committed or rolled back. If
  shutdown closes admission before commit, commit remains valid: it installs
  the worker, pre-requests its stop token while the backend is still gated, and
  then releases the gate. The backend therefore executes exactly once with an
  already-requested token instead of silently losing a protocol-claimed start.
  A call from an owned worker is initiation-only, so simultaneous self-shutdown
  cannot self-join or make workers wait on each other. Later external shutdown
  or destruction drains.

The `noexcept` shutdown path does not build temporary vectors or allocate per
job. It closes admission, copies one existing stop state at a time, delivers it
without the state mutex held, and drains one already-owned thread at a time.
Stop delivery never holds the external drain mutex: nested callbacks may stop
another config. A TLS delivery guard makes callback-reentrant shutdown
initiation-only; normal `std::stop_callback` destruction supplies callback
completion synchronization.

Owned-worker detection is also TLS execution context established at the worker
entry point. It never reads `std::thread::joinable()` or `get_id()` while an
external shutdown may concurrently call `join()`, avoiding a data race on the
thread object itself.

The owner never detaches workers. Disconnecting a transport therefore cannot
cancel a service-owned job, and destroying the owner reliably drains it. C++
cannot safely force-kill an arbitrary thread: every production backend must
have provable stop-safe points, observe its `std::stop_token`, and return after
cancellation. A backend that violates this contract will intentionally keep
`shutdown()` waiting rather than leave an unowned worker accessing torn-down
service state.

An external owner must resolve reservations before destroying the
`RuntimeTaskOwner` on the same thread. External shutdown intentionally waits
for outstanding reservations so a concurrent irrevocable protocol claim can
still commit; retaining a reservation while synchronously calling shutdown on
that same thread would be an application-level deadlock.

Destruction is stricter than public shutdown: `RuntimeTaskOwner` must be
destroyed by its external service owner, never inside its own backend callback.
That precondition is fail-fast enforced with `std::terminate()` so a joinable
`std::thread` can never be silently detached or abandoned.

## Backend and progress contract

The injected `RuntimeTaskBackend` receives the immutable start request, a
`std::stop_token`, and a progress reporter. Different configs call the backend
concurrently, so the implementation and captured dependencies must be
thread-safe. Long-running implementations must poll or register against the
stop token and return after cancellation. The reporter updates `is_flag_run`,
the bounded raw `button` JSON/string payload, `current_task`, and
`waiting_tasks`; invalid or oversized progress is rejected without mutating the
last valid snapshot.

Every backend-owned `std::stop_callback` must be `noexcept`. The standard
invokes it inside `std::stop_source::request_stop() noexcept`; an escaping
exception terminates the process before `RuntimeTaskOwner` can translate it.
Callbacks that call potentially throwing service APIs must catch internally and
convert failure to backend state rather than let it escape.

The reporter is a weak lifetime lease. A backend may accidentally retain a
copy, but calls after terminal publication or owner destruction return `false`
without dereferencing released owner/job state.

Snapshots include `config_id`, `running`, `stopping`, `is_flag_run`, `button`,
`current_task`, `waiting_tasks`, `exit_code`, `run_mode`, and a monotonically
advancing millisecond timestamp capped at JavaScript's 53-bit safe-integer
maximum. Multi-config snapshots are sorted by config id for deterministic
service serialization.

`wait_for_idle()` waits for terminal snapshot publication, not for thread
reaping. Unknown or invalid config ids are already idle. Completed threads are
reaped by the next same-config start, external shutdown, or destruction.

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

Foundation CI builds and executes the target on Windows, Linux, and macOS in
Debug and Release. Its Android arm64-v8a and x86_64 jobs compile the library
without executing host tests.

The tests execute injected backends on real worker threads and cover keyed
concurrency, gated prepare/commit, rollback without backend entry, deterministic
same-config reservation conflicts, forced thread-creation failure with public
rollback and legacy-only publication, abort visibility through worker join,
shutdown vs commit/rollback linearization, duplicate admission, stop/start
linearization, explicit terminal states, timestamp ordering, bounded progress,
escaped reporters, worker/self shutdown, nested/reentrant stop callbacks,
concurrent external drains, capacity enforcement, and external drain.
