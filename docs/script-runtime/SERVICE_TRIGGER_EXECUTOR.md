# Bounded trigger execution and connection ownership

`BAAS_service_trigger_executor` is the transport-independent asynchronous owner
between trigger ingress/dispatch and a connection's `TriggerSession`. It owns a
fixed-capacity worker pool, fixed task slots, a bounded ring queue, per-session
task ownership, cancellation signals, and terminal-response retry state. It has
no dependency on BAAS globals, the script VM, networking, devices, services, or
application lifecycle.

## Submission transaction

Every live connection has one exclusive `TriggerConnectionOwner`. `connect()`
requires `shared_ptr<TriggerSession>` and the session atomically rejects a
second owner even when it comes from another executor; closed sessions cannot be
claimed. State and active slots retain that shared lifetime through deferred
worker drain. Hosts submit
a completed `TriggerIngressItem` through that owner; direct session admission or
synchronous dispatcher execution is not a supported host path. Submission uses
this ordering:

1. resolve the immutable catalog registration;
2. reserve a global task slot, per-connection task charge, and queue charge;
3. call `TriggerSession::admit()` and retain its opaque receipt;
4. register the sealed request, receipt, stop source, handler, session owner,
   and timestamp in the reserved slot;
5. commit the slot to the worker ring and make it visible to workers.

Capacity and queue failures happen before admission. The post-admission commit
uses preallocated storage and cannot allocate. If shutdown wins the transaction,
the receipt is rolled back before the reservation is released. A worker can
never observe an admitted request before its task owner is registered.

## Bounded states and cancellation

`TriggerExecutorLimits` independently bounds connections, workers, all active
tasks, queued tasks, and tasks per connection. It also applies global and
per-connection byte budgets to retained ingress and pending terminal output.
Slots have `reserved`, `queued`, `running`, and `completed` states. `completed`
means the handler has ended but its terminal batch and small pre-encoded
cancelled fallback are retained for egress backpressure; the full ingress item
has already been released. Completed output still consumes exact pending-byte,
global-task, and connection-task capacity. Published terminals release it.

Handlers receive a read-only `std::stop_token`. `request_cancel()` records the
session cancellation and copies the matching task stop-state under its ownership
locks, then invokes the stop source outside those locks. A task cancelled while queued never enters business code;
the bridge creates its cancelled terminal. Cancellation also wins after a
handler returns but before its terminal is queued. If a success terminal is
already pending, the owner replaces it with a cancelled terminal without
rerunning the handler.

The handler may stage terminal output, but the dispatcher commits it only after
the handler truly returns. Exceptions discard staged success and become bounded
error terminals. If session cancellation lands between the final token check
and publication, the session's `cancellation_response_required` rejection is
recovered as a cancelled terminal. A stop request landing after a terminal has
become pending replaces its pre-encoded fallback under the executor state lock.
Because standard stop callbacks run synchronously, `request_stop()` is never
called while the executor or connection operation mutex is held. Shutdown first
commits and notifies the complete registry-close gate, then invokes its bounded,
preallocated stop-state snapshot. Callback reentry into stats, cancel, owner
shutdown, or executor shutdown cannot self-lock or wait on its own handler slot.
The copied stop-state is also the cancellation capability: deferred pending
replacement verifies its original slot index and stop-token identity, so a sent
command's timestamp can be reused without an old callback cancelling the new
generation. Session decisions other than `requested` or `already_requested`
never acquire or invoke a task stop capability. A thread-local linked scan chain
guards every handoff scan: direct close or shutdown of an owner already present
anywhere in the active chain returns without rescanning. Different owners may
nest once, while an A-to-B-to-A callback cycle terminates at the lower A frame.

## Egress and connection lifecycle

Terminal queue backpressure transfers one `PendingTriggerResponse` into the
task slot. `complete_send()` and `retry_pending()` serialize pending movement
with cancellation, close, and fail-send using the connection operation mutex.
They retry the same moved batch after capacity is released and never rerun the
handler or copy its large binary payload. The slot cannot be reused during that
operation, preventing pending-response ABA.

`TriggerConnectionOwner::observe_output_ready()` delegates the session's weak
RAII wakeup seam to connection hosts. Successful publication therefore wakes a
transport immediately on the empty-to-nonempty edge instead of waiting for a
heartbeat. Unsubscribe and connection close cancel future notifications; the
production WebSocket/Pipe host that drains send leases remains a separate
integration boundary.

`close()` and successful `fail_send()` consume the session's active-command
cancellation handoff and request stop on every matching owner. A fatal dispatch
sets `accepting=false` and `close_required=true` in the same executor critical
section that retires its task, so no later submission can reserve or admit.

`shutdown()` is idempotent and `noexcept`: it stops admission, requests stop,
closes active owned connections, drains owner cleanup, and joins workers. A
call from any executor worker context never joins a worker, preventing two
executor pools from joining each other. Detached workers remain counted, and
every external caller waits for both registry force-close completion and actual
worker exit. Thread handles are heap-owned so the exceptional case where both
join and detach fail can leak only the handle instead of terminating the process.
An owner shutdown called from any executor worker is likewise non-blocking to
avoid same-pool and cross-pool wait cycles; the shared session remains alive and
an external shutdown is the drain point. Active slots retain connection state,
each worker retains executor implementation state through its exit guard, and
facade objects may therefore be destroyed from a handler without use-after-free.

## Verification and remaining boundary

`BAAS_service_trigger_executor_tests` covers reserve-before-admit, global and
per-connection saturation, queued cancellation without side effects, running
stop-token races, staged terminal visibility, exception replacement, pending
binary retry without rerun, pending-success cancellation, close linearization,
shutdown, two-pool worker cross-shutdown, concurrent external registry gating,
close/backpressure and running/completed cancellation barriers, executor-before-owner
destruction, synchronous stop-callback reentry, input/pending byte budgets,
timestamp-reuse cancellation ABA, terminal-already-queued cancellation,
direct-close and cross-owner callback scan cycles, cross-executor session claims, receipt
owner/generation/rollback invariants, prefix identity, stream rules, ignored
progress backpressure, and concurrent independent connections.
The test-enabled link also contains a production-header consumer translation
unit without the hook macro; the unconditional test-access forward declaration
and friend keep `TriggerExecutor`'s class definition identical in both views.

Still pending are real catalog handlers, authenticated WebSocket and local Pipe
connection hosts that consume the egress wakeup seam, deadlines, BAAS runtime integration,
and end-to-end service execution. This target starts no service or application.
