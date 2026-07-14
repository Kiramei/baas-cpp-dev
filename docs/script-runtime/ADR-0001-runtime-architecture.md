# ADR-0001: Script Runtime and Host Architecture

- Status: Proposed
- Date: 2026-07-15

## Context

The existing Python automation code requires complete control flow and mutable
state. Existing C++ procedure/AutoFight JSON is domain-specific and finite. The
new service must execute multiple isolated configurations concurrently while
serializing operations on a single device and preserving deterministic parity
evidence. Existing global singletons and `ThreadPool` do not provide the needed
ownership, cancellation, or queue bounds.

## Decision

### Compilation/execution pipeline

Use UTF-8 source -> tokens -> immutable AST -> validated bytecode -> stack VM.
Keep source spans on AST and bytecode instructions. A small reference AST
evaluator may be used only as a conformance oracle; production execution targets
bytecode so orchestration loops and dispatch avoid repeated tree walking.

The bytecode format is internal until a separate persistence/versioning ADR is
accepted. Script packages ship source in the first release, allowing the active
runtime to validate and compile atomically.

### Target boundaries

- `BAAS_script_runtime`: lexer, parser, diagnostics, values, compiler, bytecode,
  VM, module loader, native registry, and execution context.
- `BAAS_host_interfaces`: dependency-free capability interfaces and trace value
  types.
- Domain adapters: configuration, resource, image, OCR, device, scheduler,
  logging, and notification implementations that wrap reusable BAAS C++ code.
- `BAAS_script_cli`: local validate/run/disassemble/replay tool.
- `BAAS_service`: versioned HTTP plus compatible WS/pipe transports and a
  bounded task manager.

The runtime never reads `BAASGlobals` and never receives raw singleton pointers.

### Execution context and concurrency

Each top-level task gets an `ExecutionContext` containing stop/deadline/budgets,
identity, capabilities, immutable package/resource/config snapshots, injected
clock/RNG, host registry, and trace/event sink.

A bounded executor owns task scheduling. A per-device strand/actor serializes
stateful device operations. CPU vision/OCR jobs use explicitly bounded pools.
Module state is per execution context unless a host service exposes a safe
shared abstraction. Cancellation is cooperative at VM safe points and every
blocking host boundary.

### Values and memory

Primitive values are stored inline where practical. Lists, ordered maps,
closures, errors, tasks, and host handles live in a VM-owned heap. The heap must
support cycles and enforce a context memory budget. ADR-0002 selects a
per-context non-moving tracing heap with generational references, exact roots,
and budget accounting. Native handles use explicit close/defer semantics and
safe host ownership rather than relying on nondeterministic script finalizers.

### Host API and parity

Native functions are registered by versioned module and capability. Arguments
and results cross a checked value boundary. Host exceptions become structured
errors. Fake hosts use the same interfaces and produce deterministic traces.
Every migrated operation links to Python source, its host function, and golden
parity evidence in `MIGRATION_MATRIX.md`.

### Service transport

`cpp-httplib` is used for HTTP. The existing secure WebSocket, Windows named
pipe, Unix socket, and secret-stream protocols require dedicated transport
components or a mature audited library; they are not simulated with HTTP. Wire
compatibility is proven by golden vectors and cross-implementation tests before
the C++ service replaces Python in Tauri startup.

## Consequences

Positive:

- Language semantics stay independent from BAAS global state and platform code.
- Bytecode provides a credible performance path without hard-coding automation
  into C++.
- Fake hosts and injected nondeterminism make Python parity reproducible.
- Bounded queues, per-device serialization, and snapshot isolation support the
  concurrency goal.
- Target boundaries improve incremental and parallel worktree builds.

Costs and risks:

- Parser, compiler, VM, memory management, adapters, and wire compatibility are
  substantial components and require staged review.
- A safe cyclic heap/collector needs its own ADR and stress tests.
- Existing BAAS core code must be disentangled from globals before adapters can
  be considered thread-safe.
- Production bytecode requires verifier, budget accounting, malformed-input
  tests, fuzzing, and stable diagnostics.

## Rejected alternatives

- **Keep Python embedded:** retains runtime size, dependency, performance, and
  update problems that motivate the project.
- **Use only JSON action lists:** cannot express the audited loops, functions,
  callbacks, mutable state, exceptions, and concurrency.
- **Compile every automation script to C++:** removes independent rolling script
  updates and makes migration/release iteration expensive.
- **Expose existing singletons directly to a tree-walk interpreter:** creates an
  easy prototype but preserves unsafe global/concurrency architecture and does
  not meet the performance target.
- **Replace all service transports with REST:** breaks current Tauri wire and
  streaming/remote contracts.

## Validation required before acceptance

- Language conformance corpus proves required semantics and Turing completeness.
- VM benchmark demonstrates acceptable dispatch/hot-loop behavior.
- Fake-host traces and first real screenshot->detect->control vertical slice
  match Python golden traces.
- Bounded executor cancellation/backpressure and per-device serialization tests
  pass under concurrency.
- Service cryptographic/pipe golden vectors pass across Python, C++, and Tauri.
