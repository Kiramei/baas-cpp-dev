# ADR-0002: VM Heap, Cycles, and Memory Budgets

- Status: Accepted
- Date: 2026-07-15

## Context

BAAS Script requires mutable lists/maps, closures, modules, tasks, and
structured errors. These values can form cycles. Reference counting alone would
leak normal language graphs, while process-global ownership would violate task
isolation and make concurrent service sessions unsafe.

The service must reject hostile packages predictably, preserve deterministic
traces, and run multiple execution contexts without allowing one context to
retain another context's mutable state. Host handles also own external
resources whose release may have thread-affinity and side effects; a garbage
collector must not invoke arbitrary script or host behavior.

## Decision

### Per-context non-moving tracing heap

Each `ExecutionContext` owns one non-moving tracing heap. Primitive `null`,
Boolean, integer, and float values are stored inline. Immutable strings and all
mutable/identity-bearing values are heap cells. The required cell kinds are:

- string;
- ordered list;
- insertion-ordered string-keyed map;
- closure/function;
- module namespace;
- structured error;
- task/future state;
- host handle wrapper.

The initial collector is stop-the-context mark-and-sweep. It runs only at VM
safe points on the execution context's strand; heap mutation and collection do
not run concurrently. A future incremental collector may replace it without
changing `Value` or bytecode semantics.

Cells do not contain owning `shared_ptr` links to other cells. Language edges
are heap references, so cycles are reclaimed by tracing. Native implementation
objects shared outside the language graph may use C++ ownership internally, but
must not make a script cell reachable after the heap has proven it dead.

### Stable generational references

A heap reference is an opaque `{slot, generation}` pair scoped to one heap. A
slot points to a non-moving cell while allocated. Reusing a swept slot increments
its generation. Every dereference validates heap identity, slot bounds,
generation, and expected cell kind; stale or cross-context references produce a
stable internal-boundary error rather than accessing freed memory.

Raw cell pointers are allowed only as short-lived implementation details while
the heap cannot collect. Public runtime APIs, native host calls, suspended
tasks, module caches, VM frames, and tests retain generational references or
registered roots, never raw pointers.

### Roots and tracing

The exact root set consists of:

- VM operand/call stacks and active frames;
- module namespaces and the current module initialization graph;
- closure environments reachable from frames or modules;
- active structured errors and deferred actions;
- suspended task continuations owned by the execution context;
- explicitly registered native-call roots for the duration of a host call;
- values in the result/event handoff until conversion or transfer completes.

Each cell kind implements a non-allocating child-reference visitor. Marking uses
an explicit bounded worklist rather than native recursion. Collection cannot
run while an unregistered `Value` exists only in a C++ local; allocation-capable
runtime functions use an RAII root scope before they may trigger collection.

### Budget accounting

Every context has hard limits for live heap bytes, cell count, single
allocation size, string bytes, collection work, and externally retained bytes.
Accounting includes cell headers and owned capacities for strings, list/map
storage, closure captures, errors, and task buffers. Container capacity growth
is charged before allocation and credited only after shrink/sweep succeeds.

When an allocation would exceed a soft collection threshold, the runtime
collects at the next safe point and retries once. If the hard limit would still
be exceeded, allocation fails with `MemoryLimitExceeded`. Integer overflow in
accounting is treated as a limit failure. A failed allocation leaves the heap
and destination container unchanged.

External libraries such as OpenCV and OCR keep their own budgets and bounded
queues. A small host-handle cell must not hide unbounded native memory; the host
adapter reports an external charge or uses a separately enforced capability
budget.

### Determinism and finalization

Collection timing is not language-observable. The collector never calls script
code and does not use user-defined finalizers or weak-reference callbacks.
Equality, iteration order, error traces, and task scheduling cannot depend on
slot addresses or sweep order.

Host handles require explicit `close`/`defer` semantics. Context teardown closes
any leaked handle through the owning host adapter as a safety fallback, on the
required device/host strand, after script execution has stopped. Sweep only
removes the language wrapper and queues an idempotent release record; it does
not synchronously perform arbitrary I/O.

### Context isolation and transfer

Heap references never cross execution contexts. Transfer is one of:

- checked deep copy of JSON-safe acyclic values;
- an explicitly frozen immutable representation owned outside both heaps;
- a host concurrency primitive represented by distinct capability-checked
  handles in each context.

Copy detects cycles and enforces destination budgets before publication.
Closures, modules, tasks, errors with live frames, and ordinary host handles are
not implicitly transferable.

### Task/threading relationship

An execution context may suspend and resume on different executor workers, but
all of its VM and heap operations are serialized by its strand. Cancellation
sets thread-safe state and is observed at VM/host safe points; it does not trace
or mutate the heap from the requesting thread. Independent contexts can collect
concurrently because they share no cells.

## Consequences

- Cyclic language values are reclaimable without recursive `shared_ptr` leaks.
- Non-moving cells simplify native boundaries and debugging, at the cost of
  fragmentation and a slot/generation indirection.
- Stop-the-context collection is simpler and deterministic enough for the
  initial runtime, but long collections require work/latency metrics and may
  motivate incremental tracing later.
- Native integrations must register temporary roots and report external memory;
  this is mandatory API discipline, not an optional optimization.
- Explicit host-handle close remains part of script correctness even though
  context teardown provides a safety net.

## Rejected alternatives

### Recursive `shared_ptr` object graphs

Rejected because ordinary list/map/closure cycles leak and memory budgets cannot
reliably determine reachability.

### Process-global garbage collector

Rejected because it couples unrelated service sessions, complicates bounded
latency, and permits accidental mutable sharing across execution contexts.

### Moving generational collector for v1

Rejected for the foundation because every native boundary would need relocation
barriers before the host API is stable. The opaque reference API preserves the
option for a future collector.

### Observable destructors/finalizers

Rejected because collection timing would change device actions, logs, and
network behavior. External lifetime is explicit and idempotent.

## Required validation

- allocate/mutate/collect every cell kind, including self and mutual cycles;
- stale generation and cross-context reference rejection;
- explicit-root, native-root, suspended-task, module, and defer reachability;
- deep graphs without native recursion overflow;
- exact budget boundaries, accounting overflow, failed-growth rollback, and
  repeated collect/retry behavior;
- deterministic ordered-map iteration and cycle-aware equality;
- concurrent independent heaps under separate strands and race instrumentation;
- explicit close, duplicate close, sweep release queue, and context teardown;
- JSON-safe transfer, cycle rejection, destination budget failure, and absence
  of source/destination mutable aliasing.
