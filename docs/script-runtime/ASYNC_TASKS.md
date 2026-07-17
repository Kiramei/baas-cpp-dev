# BAAS Script async tasks, cancellation, and deterministic scheduling (Draft 0.1)

This document is the normative Draft 0.1 contract for async functions, Task
values, `await`, structured task ownership, cancellation propagation and
masking, absolute deadlines, VM safe points, host asynchronous operations,
execution-context thread confinement, deterministic scheduler/clock hooks, and
task resource release. It refines Section 10 of `LANGUAGE_SPEC_DRAFT.md` and
composes with `ERRORS_AND_CLEANUP.md`, `VALUE_SEMANTICS.md`,
`CONTROL_FLOW_AND_MODULES.md`, ADR-0001, and ADR-0002.

The key words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative. The
ASY identifiers are stable conformance anchors. The checked-in parser/AST,
semantic analyzer, `ValueHeap` Task cell, `Environment`, and
`BoundedExecutor` provide static or C++ scheduling foundations only. The
compiler, VM, language Task transitions, continuations, task module,
execution-context strand, injected clocks, deterministic scheduler, and host
async bridge remain pending; rules for them are requirements, not completion
claims.

## Normative clauses

### ASY-001 — Language task boundary

An `async fn` call MUST return a first-class Task immediately after synchronous
call preparation and bounded admission. `await` MUST suspend a continuation
without blocking an executor worker. A Task is a language heap value owned by
one execution context; it is not `std::future`, an OS thread, a raw executor
handle, or a host callback.

All script execution for one context MUST be serialized by one logical strand,
even when that strand migrates between executor workers. `BoundedExecutor` MAY
run different contexts concurrently but MUST NOT run two VM/heap operations for
the same context concurrently.

### ASY-002 — Task identity and observable fields

A Task MUST be identity-bearing, immutable as a reference, and context-local.
Its runtime record contains opaque monotonic `task_id`, exact ASY-003 `state`,
parent scope id, creation sequence, effective monotonic deadline, cancellation
request/reason, suspended continuation, ordered awaiters, owned child tasks,
terminal result or Error, and retained/rooted values. Script may read `id` and
`state`; result/error delivery occurs through `await`, not mutable public fields.

Task equality and `is` use identity. Tasks MUST NOT cross execution contexts or
the JSON boundary. A context-local deterministic `task_id` is unique until
teardown and MUST NOT encode an address, OS thread, process-global counter, or
secret. Dropping the last script reference does not detach a live task because
its owning task scope remains a root.

### ASY-003 — State machine and terminal publication

The language state inventory is exactly `Pending`, `Running`, `Succeeded`,
`Failed`, and `Cancelled`, matching `TaskState` in `ValueHeap.h`. Only these
transitions are legal:

```text
Pending -> Running
Pending -> Cancelled
Running -> Succeeded
Running -> Failed
Running -> Cancelled
```

States are monotonic; terminal states never transition. `Succeeded` owns one
result value. `Failed` owns one Error, including `DeadlineExceeded` and safety
limit errors. `Cancelled` owns `Cancelled` or `HumanTakeover`. A cancellation
request against a Running task is a separate monotonic flag and MUST NOT change
state until the request is observed under ASY-011.

Terminal publication MUST occur exactly once on the context strand after the
task body, child-scope close, ERR-013 defer cleanup, and host-operation cleanup
complete. It atomically fixes state and result/Error, then queues awaiters in
registration order for later scheduler turns; it MUST NOT resume an awaiter
inline or expose a partially finalized outcome.

### ASY-004 — Async call preparation and admission

The callee, arguments, and default parameters MUST follow CTL-005 and complete
synchronously in the caller. A preparation error raises in the caller and no
Task is created. The runtime then atomically allocates a Pending Task, registers
it as a child of the current active task scope, and admits one runnable record.
Admission either succeeds completely or raises terminal `TaskLimitExceeded`
without publishing a Task or child edge.

The async body starts on a later scheduler turn, never re-entrantly inside the
call. Its Pending-to-Running transition occurs immediately before function-body
entry. Body return/fallthrough proposes success; an escaping catchable Error
proposes failure; observed cancellation proposes cancellation. No proposal is
published until ASY-003 finalization.

### ASY-005 — Await semantics and wait graph

`await expression` MUST evaluate its operand exactly once. A non-Task operand
raises `TypeMismatch`. Awaiting a Succeeded Task returns its rooted result;
awaiting Failed raises the exact stored Error; awaiting Cancelled raises its
exact terminal Error. Each completed await marks that child outcome observed
for ASY-007, even when raising. A Task MAY be awaited repeatedly and by multiple
tasks; every await observes the same terminal value/Error identity.

Awaiting Pending or Running stores the current continuation and roots its
frames, lexical environments, operand stack, defer stack, active errors, and
temporaries, then yields the context strand. Resumption occurs on a later turn
at an ASY-011 safe point. Adding an await edge that reaches the waiter through
existing await edges MUST raise catchable `TaskCycle` without suspending.

Cancellation propagates along ownership edges, not arbitrary await edges. If a
task awaits a non-child shared task and is cancelled, its continuation stops
waiting but the awaited task continues unless its own owner requests cancel.

### ASY-006 — Structured ownership and detached-task prohibition

Every top-level execution owns a root task scope. Each Running task owns a
default child scope, and `baas/task.scope` MAY create nested scopes. Direct
async calls and `spawn` register children in the currently active scope. A
normal function return does not close the caller task's active scope; a task or
explicit scope closes only under ASY-007.

A child MUST NOT outlive its owning scope. Script code cannot detach, abandon,
or transfer ownership of a Task. A privileged service host MAY detach only by
becoming the explicit lifetime, budget, cancellation, result, and logging owner;
that operation returns a capability-scoped host handle rather than a transferable
script Task.

### ASY-007 — Scope close, observation, and child failure

Before a task or explicit scope publishes normal completion, it MUST wait for
all live children. An awaited child failure is observed. An unawaited Failed
child is unobserved and MUST fail a normally closing scope; the earliest
terminal publication in deterministic scheduler event order becomes primary,
live siblings receive cancellation, and later child failures are attached as
suppressed in that same order. An explicitly cancelled child not awaited is a
handled scope outcome; `HumanTakeover`, `DeadlineExceeded`, and safety-limit
Errors are never silently handled.

When a parent is already failing or cancelled, it requests cancellation for all
live owned descendants, waits for their bounded defer/host cleanup, preserves
the parent Error as primary, and attaches safe child cleanup/failure Errors as
suppressed under ERR-014. A child result cannot publish after its scope has
published.

### ASY-008 — Required task operations

The versioned `baas/task` module MUST provide these semantics; exact host
registration remains Phase 2 work:

`baas/task` is reserved for capability-free language Task values and the table
below. Capability-scoped automation registration, dispatch, and scheduling use
the separate `baas/scheduler` Host module defined by
`HOST_CAPABILITY_CONTRACTS.md`; its opaque `host<ScheduledTask>` is not a Task,
and its `cancel(host<ScheduledTask>) -> null` MUST NOT overload, alias, or replace
`baas/task.cancel(Task) -> bool`.

| Operation | Required behavior |
| --- | --- |
| `spawn(async_callable, args...)` | equivalent to an eager async call in the active scope; rejects a non-async callable |
| `join(task)` | equivalent to `await task` |
| `all(tasks)` | await all, return results in input order; first scheduler-ordered failure cancels live siblings and remains primary |
| `race(tasks)` | select first terminal publication; cancel and drain losers; winner result/Error remains primary |
| `timeout(task, duration)` | if duration wins, cancel/drain the owned child and raise catchable `Timeout` |
| `cancel(task)` | idempotently request explicit cancellation; return true only for the first effective request |
| `scope(async_callable)` | create a nested structured scope and close it under ASY-007 |
| `shield(async_callable)` | apply the bounded mask in ASY-010 |

Collections are snapshotted in input order before `all`/`race` registers
awaiters. Non-Task entries raise `TypeMismatch`; empty `all` returns an empty
list and empty `race` raises catchable `ArgumentInvalid`. Operations that may
cancel inputs (`all`, `race`, `timeout`, and `cancel`) require owned descendants
of the current scope; otherwise they raise `ArgumentInvalid`. If a context
deadline and a catchable timeout compete, ASY-013 selects
`DeadlineExceeded`.

`all`, `race`, and `timeout` mark every drained child outcome observed. When a
race winner is an Error, safe loser failures are suppressed on its derived
immutable Error. When the winner is a success, loser cancellation remains
handled and any non-cancellation loser failure is emitted as a bounded
`race_loser_error` trace record; it does not replace an already linearized
success and MUST NOT disappear without trace evidence.

### ASY-009 — Cancellation sources and propagation

A cancellation request is a thread-safe, idempotent, monotonic signal carrying
one source: explicit task/scope cancel, ancestor failure/cancel, context
shutdown, human takeover, absolute deadline, or runtime safety limit. Requesting
cancel does not synchronously execute script, mutate the heap, run cleanup, or
guarantee a Running task has stopped.

A request for Pending atomically removes/invalidates runnable admission and
eventually publishes Cancelled without body entry. A request for Running posts
to its context strand and is raised at ASY-011. Parent cancellation recursively
requests every owned descendant. Child cancellation does not cancel ancestors
unless its Error is explicitly awaited or propagates as an unobserved terminal
failure under ASY-007.

Repeated or lower-priority requests do not replace the selected reason. Request
records and propagation order MUST be bounded and deterministic by
parent-before-child ownership order, then child creation sequence.

### ASY-010 — Cancellation masking and atomic boundaries

Ordinary `try`/`catch` MUST NOT mask terminal Errors. `shield` MAY delay only an
ordinary `Cancelled` request while its bounded async callable and already
registered cleanup complete. It MUST NOT delay `HumanTakeover`,
`DeadlineExceeded`, instruction/memory/stack/task/cleanup limits, or
`InternalInvariant`. The cancellation request remains pending and MUST be raised
at the first safe point after the outermost shield exits.

Shielding does not extend a deadline, reset budgets, detach children, or permit
unbounded host work. Nested shields coalesce. The only implicit masks are the
ERR-013 emergency cleanup allowance and a capability-checked host atomic commit
section that is bounded, non-blocking on script, and reports whether commit
linearized before cancellation.

### ASY-011 — VM and host safe points

Instruction accounting MUST occur at every bytecode dispatch. Cancellation,
deadline, and cooperative scheduling checks MUST occur at function/task entry,
call/return, loop body entry and backward branch, allocation/collection,
module/host boundary, before suspension, after resumption, and at compiler-
inserted checks so no straight-line path exceeds
`max_instructions_between_cancel_checks = 1024`.

At a safe point the VM first collects pending terminal reasons, selects ASY-013
priority, then either begins ERR-013 unwind or executes at most the remaining
instruction quantum. The default `max_instructions_per_turn = 1024`; quantum
expiration requeues the continuation at the ready-queue tail without changing
Task state. Blocking host adapters MUST have their own cancel/deadline wakeup
and MUST NOT defer observation until an unbounded native call returns.

### ASY-012 — Deadlines and injected clocks

Durations and deadlines MUST use an injected monotonic clock. The execution
context stores one absolute monotonic deadline; child/scope/host deadlines are
the minimum of parent deadline and requested deadline and cannot extend an
ancestor. A wall-clock instant is converted once at a public boundary using the
simultaneously injected wall and monotonic readings. Wall time is for display,
calendar policy, and trace metadata only; wall-clock jumps MUST NOT change an
active duration/deadline.

The clock tick is a checked signed 64-bit count of nanoseconds from a context-
local monotonic epoch. Public task/clock durations accept a nonnegative `int` or
finite `float` number of seconds and round upward to the next nanosecond so a
timer never expires early. Negative, non-finite, or overflowing input raises
`ArgumentInvalid`; zero expires at the next deadline check.

Deadline checks occur before admission, at body start, every ASY-011 safe point,
before/after suspension, and at every host boundary. `now >= deadline` is
expired. The scheduler represents timers as `(due_tick, kind_priority,
creation_sequence)`. At the same tick, terminal deadline/cancellation events are
processed before ordinary task/host completions unless that completion was
already atomically published, preserving ERR-011.

### ASY-013 — Deterministic outcome priority

Before unwind, the context strand atomically selects one primary outcome claim.
Simultaneously pending claims MUST use this exact priority; entries on one line
break ties left-to-right:

| Priority | Outcome |
| --- | --- |
| 1 | `InternalInvariant` |
| 2 | `MemoryLimitExceeded`, `StackLimitExceeded`, `InstructionLimitExceeded`, `CleanupLimitExceeded`, `TaskLimitExceeded` |
| 3 | `HumanTakeover` |
| 4 | `DeadlineExceeded` |
| 5 | `Cancelled` |
| 6 | escaping catchable body/host Error |
| 7 | normal success |

Selection is the internal linearization point used by ERR-011. The winner
remains primary through child and defer cleanup even though public ASY-003 Task
terminal publication occurs later. Later cleanup/child Errors follow ERR-014
and do not rerun the table. Safe losing Errors present at selection are
suppressed in scheduler event order; a losing success is discarded. A
`timeout` event that linearizes before its child produces catchable `Timeout`
and requests child cancellation, but any priority 1–5 context outcome overrides
it. Error message text, OS scheduling, and callback thread order MUST NOT decide
priority.

The synchronous conformance evaluator applies this selection through one safe-
point operation. Each caller supplies only the safety claims already knowable
at that exact boundary: value-stack admission is combined with the requested
instruction, call-stack admission is combined with Host task/binding budgets,
and a collection charge supplies its preflight memory claim. The operation
collects deadline and cancellation observations before selecting
`MemoryLimitExceeded`, `StackLimitExceeded`, `InstructionLimitExceeded`,
`CleanupLimitExceeded`, and `TaskLimitExceeded` in the priority-2 order above;
it does not pretend that a later allocation failure was known at an earlier
statement safe point. Deadline then wins over cancellation, and either external
claim wins over normal success. During package initialization, `Cancelled` and
`DeadlineExceeded` roll every active nested module transaction back to
`Uninitialized`; they MUST NOT poison the deterministic module-failure cache. A
later non-interrupted `execute` retries those modules, while an already `Ready`
module retains its exact namespace and initialization count across interrupted
and repeated execution attempts.

### ASY-014 — Context strand and thread-safety boundary

VM frames, `Heap`, `Environment`, module state, Task records/continuations,
defer stacks, and language values MUST be accessed only on their
execution-context strand. The strand may migrate workers only after the
previous turn has fully released it; turns for one context never overlap.
Independent contexts MAY run concurrently because they share no mutable
language cells.

Cross-thread producers may touch only documented atomics/stop sources and post
immutable bounded completion records. They MUST NOT dereference `Value`,
`HeapRef`, `Environment`, a VM frame, or a host wrapper. Cross-context transfer
uses VAL-014 JSON-safe deep copy or a capability-scoped host primitive. Host
adapters MUST declare affinity (`context`, `device`, `cpu_pool`, or `external`)
and marshal completion back to the context strand.

### ASY-015 — Host asynchronous operation contract

An async host call MUST synchronously validate/root arguments and capabilities
on the context strand, then return a bounded suspension token containing
operation id, generation, affinity, deadline, cancel hook, and release hook.
The host may work on its declared strand/pool, but completion MUST be one
immutable status/value descriptor posted exactly once to the context strand.
Only there may it materialize language values or ERR-003 Errors and resume a
continuation.

Asynchronous `baas/scheduler` operations follow this bridge and may publish only
opaque `host<ScheduledTask>` handles. Language `baas/task` operations manipulate
context-local Task values directly and MUST NOT be routed through
`SchedulerHost`; neither handle kind may be converted to the other.

Cancellation invokes an idempotent non-blocking `request_cancel`; it is advisory
until completion/acknowledgement. A non-cancellable native operation MUST be
quarantined, remain charged to budgets, and have its late result discarded. An
operation id/generation prevents late completion from targeting a reused task
or destroyed context. Discard still runs release on the adapter's owning
affinity. A host callback MUST NOT re-enter script, publish twice, hold raw heap
pointers, or block an executor worker waiting for script.

### ASY-016 — Roots, completion, and resource release

Pending/Running tasks, suspended continuations, arguments/captures, child and
awaiter edges, results/Errors, host suspension tokens, and in-flight completion
handoffs MUST be exact heap roots/edges under ADR-0002. A terminal Task retains
its result/Error while reachable; unreachable task cycles are trace-collectable
after scope/awaiter roots are removed.

Cancellation and context teardown MUST first stop admission, request descendants
and host operations, drain bounded child/defer cleanup, process idempotent host
release records on their owning affinity, and only then invalidate heap/task
references. Late completions are discarded and released. Exhausted cleanup
budget preserves the ASY-013 primary Error and uses ADR-0002 teardown fallback;
it MUST NOT silently leak an external resource or revive a context.

### ASY-017 — Deterministic scheduler, clock, and trace hooks

Conformance MUST use injected `MonotonicClock`, `WallClock`, scheduler, and fake
host completion sources; it MUST NOT depend on `sleep_for`, worker count, thread
id, CPU timing, or ambient wall time. Task ids, scope ids, timer ids, host
operation ids, and creation sequences are monotonic within one context. The
scheduler uses a FIFO ready queue and total event order `(due_tick,
kind_priority, creation_sequence)` with ASY-012/013 priority.

The deterministic trace MUST record monotonically sequenced events:
`task_create`, `task_start`, `task_suspend`, `task_resume`, `cancel_request`,
`cancel_observed`, `timer_set`, `timer_fire`, `host_start`, `host_complete`,
`host_discard`, `race_loser_error`, and `task_complete`. Each record contains
virtual monotonic tick, context/task/operation ids, reason/error code, and source
span when applicable; it MUST NOT contain addresses, OS thread ids, secrets, or
unrestricted values. Replay injects the same external completion/timer sequence
and MUST produce the same trace and terminal Error envelope or bounded JSON-safe
result projection.
Production external arrival ticks are explicit trace inputs; replay does not
infer them from callback thread timing.

### ASY-018 — Limits and BoundedExecutor mapping

Defaults are `max_tasks_per_context = 4096`, `max_ready_queue = 4096`,
`max_awaiters_per_task = 4096`, `max_timers_per_context = 4096`, and
`max_host_operations_per_context = 1024`. Accounting includes Pending/Running
tasks, continuations, timers, child/awaiter edges, completion records, and
quarantined operations. Overflow raises terminal `TaskLimitExceeded` before
partial publication.

The existing C++ foundation maps as follows, but does not implement language
Task semantics:

| Foundation state/result | Future language mapping |
| --- | --- |
| `detail::TaskPhase::Queued` | `Pending` |
| `detail::TaskPhase::Running` | `Running` |
| `detail::TaskPhase::Finished` plus value | `Succeeded` after language cleanup |
| `detail::TaskPhase::Finished` plus exception | translated `Failed`/`Cancelled` after language cleanup |
| `detail::TaskPhase::Cancelled` | `Cancelled`; callable body never enters |
| `TrySubmit` full | `TaskLimitExceeded` |
| `SubmitTimeout` | catchable `Timeout` |
| `ExecutorShutdown` | `HostUnavailable`, except context shutdown already selected `Cancelled` |

`RequestCancel` for a Running foundation task only requests its `stop_token`;
the callable may still succeed if it publishes before observation. `Drain` and
`CancelPending` retain their C++ meanings. Language context teardown MUST also
request every running handle and drain ASY-016; `BoundedExecutor` shutdown alone
is not that protocol. Real `steady_clock` admission in `SubmitUntil` is a host
foundation, not the injected deterministic language clock.

### ASY-019 — Normative static conformance examples

The valid example MUST remain syntactically and lexically semantically valid.
The non-executing `BAAS_script_check` fixture proves async/await static context
only; it does not prove Task execution, cancellation, timing, or scheduling.

<!-- conformance:async-tasks-valid -->
```baas
import "baas/task" as task;
import "baas/clock" as clock;

async fn child(value) {
    await clock.sleep(1);
    return value;
}

async fn workflow() {
    let first = child(1);
    let second = task.spawn(child, 2);
    defer task.cancel(second);
    let values = await task.all([first, second]);
    let winner = await task.race([child(values[0]), child(values[1])]);
    return await task.timeout(child(winner), 5);
}
```

<!-- conformance:async-tasks-invalid -->
```baas
fn sync() {
    return await work();
}
```

The invalid example MUST retain `PAR011`; nested synchronous functions reset
the parser async context even when declared inside an async function.

### ASY-020 — Implementation and completion boundary

The lexer/parser/AST implement `async fn`, `AwaitExpression`, contextual
`PAR011`, and source spans. `SemanticAnalyzer` traverses await operands.
`ValueHeap` implements a traced Task cell placeholder with exact `TaskState` and
retained values. `BoundedExecutor` implements bounded C++ callables, queue
backpressure, queued cancellation, running `stop_token` requests, steady-clock
admission waits, and drain/cancel-pending shutdown. `Environment`, heap, and
executor tests are foundation evidence only.

The repository does not yet implement async call execution, language Task state
transitions/member access, VM continuations or `await`, ownership scopes,
combinators, cancellation propagation/masking, execution-context deadlines or
strand, safe-point scheduling, injected clocks, deterministic trace/replay, or
the host async bridge. These MUST remain pending in Phase 2 until executable
conformance evidence exists. Completing this normative Phase 1 specification
MUST NOT be described as implementing task primitives, VM execution, host
modules, the general conformance corpus, or Phase 1 as a whole.

## Machine-checked evidence

`tests/docs/test_async_tasks_spec.py` uses only the Python standard library and
MUST verify ASY-001 through ASY-020, the exact Task state graph, stable async
errors, await/parser/AST anchors, Task heap metadata, BoundedExecutor phase and
cancel/deadline/shutdown anchors, safe points, deterministic hooks, static
fixtures/CTest, explicit missing runtime boundary, the single ROADMAP checkbox,
and Foundation CI path wiring.
