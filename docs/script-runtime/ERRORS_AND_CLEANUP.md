# BAAS Script structured errors and cleanup (Draft 0.1)

This document is the normative Draft 0.1 contract for diagnostics, structured
runtime errors, source references, script and host stack frames, cause chains,
`throw`, `try`/`catch`, `defer`, cleanup failure precedence, and translation at
the C++ host boundary. It refines Sections 2, 6, 9, and 14 of
`LANGUAGE_SPEC_DRAFT.md`, uses the syntax in `LANGUAGE_GRAMMAR.md`, and composes
with `VALUE_SEMANTICS.md`, `CONTROL_FLOW_AND_MODULES.md`, `ASYNC_TASKS.md`, and
`ADR-0002-vm-memory-management.md`.

The key words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative. The
ERR identifiers are stable conformance anchors. The checked-in lexer, parser,
immutable AST, semantic analyzer, source-location types, and heap error cell
provide static/foundation evidence only. The compiler, VM exception machinery,
stack capture, defer unwinder, module loader, and native host-error translator
remain pending; rules for them are requirements, not implementation claims.

## Normative clauses

### ERR-001 — Compile diagnostics and runtime errors are distinct

Lexer, parser, and semantic failures MUST be reported as non-catchable compile
diagnostics before module execution. A compile diagnostic contains severity,
stable phase code, safe UTF-8 message, and `SourceSpan`; it has no script stack,
cause, or heap identity. Recovery MAY emit multiple diagnostics. Package-level
diagnostics MUST be ordered by canonical module id, begin byte offset, phase
(`LEX`, `PAR`, then `SEM`), and stable emission order.

When serialized at a public runtime/package boundary, its form MUST use schema
`baas.script.diagnostic/v1` and field order `schema`, `severity`, `code`,
`message`, `source`; severity is exactly `error` or `warning`, and source is an
ERR-005 `SourceReference`. A single-file local checker MAY use an explicit
synthetic module/snapshot id supplied by its caller or wrap diagnostics in an
unversioned tool-specific report.

A failure after execution begins MUST be represented by the immutable Error
value defined in ERR-003. C++ exceptions and `RuntimeError` objects are boundary
implementation details and MUST NOT become catch bindings or cross a public
language/host ABI.

### ERR-002 — Diagnostic codes and source geometry

Existing `LEXnnn`, `PARnnn`, and `SEMnnn` spellings are stable. A code MUST NOT
be reused for a different condition; gaps remain reserved. Human-readable
messages MAY improve without changing the code. Draft 0.1 currently anchors
`LEX001`–`LEX006`, `PAR001`–`PAR011`, `PAR014`, `PAR018`, `PAR019`, `SEM001`–
`SEM004`, and `SEM006`–`SEM009`. `SEM009` reports a forbidden non-local or
suspending operation in a defer cleanup body under ERR-015.

Every location refers to the original UTF-8 byte sequence. `byte_offset` is
zero-based; `line` and `column` are one-based; columns count Unicode scalar
values, not bytes or display cells. A `SourceSpan` is the half-open range
`[begin, end)`. `CRLF` occupies two bytes but advances one line. Empty spans are
valid at insertion points and EOF. A present span MUST have ordered offsets and
locations within the immutable source; unavailable source MUST be `null`, never
a fabricated all-zero position.

### ERR-003 — Immutable Error value and envelope

An Error MUST be an identity-bearing, immutable, context-local heap value with
these logical fields:

| Field | Required contract |
| --- | --- |
| `schema` | exact string `baas.script.error/v1` in serialized envelopes |
| `code` | stable UpperCamelCase code from ERR-004 |
| `message` | safe UTF-8 text for people; not a branching contract |
| `origin` | exactly `script`, `runtime`, or `host` |
| `catchable` | Boolean fixed from ERR-004, not caller-controlled |
| `source` | primary `SourceReference` or `null` |
| `stack` | bounded immutable frames from ERR-007 |
| `cause` | one older causal Error or `null` |
| `suppressed` | ordered cleanup/secondary Errors from ERR-008 |
| `details` | insertion-ordered JSON-safe string-keyed map |
| `context` | allowlisted opaque task/session/package metadata |
| `truncated` | explicit omitted-count/byte flags from ERR-008 |

Script member access MUST expose these fields read-only. Equality and `is` use
error identity, not structural comparison. Error values, live frames, causes,
and suppressed errors MUST be traced heap edges and MUST NOT cross execution
contexts. A service boundary exports only the deterministic JSON envelope; it
does not transfer the Error heap cell.

All fields are fixed when an Error is published. Adding a cause or suppressed
failure MUST construct a new immutable Error that copies the primary fields;
the original identity and fields remain unchanged.

### ERR-004 — Stable language error catalog

The following codes and catchability are the minimum Draft 0.1 language
contract. New codes MAY be added compatibly; existing spelling, meaning, and
catchability MUST NOT change within language version 1.

| Stable code | Catchable | Required condition |
| --- | --- | --- |
| `ThrownValue` | yes | `throw` operand was not already an Error |
| `TypeMismatch` | yes | operation received the wrong value kind |
| `ArgumentInvalid` | yes | argument has the right kind but invalid value/shape |
| `NameNotFound` | yes | dynamic name/member lookup has no binding |
| `UninitializedBinding` | yes | declared slot was read before initialization |
| `NotCallable` | yes | call target is not callable |
| `CallArityMismatch` | yes | required/excess positional argument mismatch |
| `CallArgumentDuplicate` | yes | parameter supplied more than once |
| `CallArgumentUnknown` | yes | named argument does not name a parameter |
| `TaskCycle` | yes | adding an await edge would form a wait cycle |
| `IndexOutOfRange` | yes | index/slice/map lookup is outside its contract |
| `NumericOverflow` | yes | checked integer arithmetic overflow |
| `DivisionByZero` | yes | division, floor division, or modulo by zero |
| `InvalidUtf8` | yes | runtime or host supplied invalid UTF-8 |
| `JsonCycle` | yes | JSON-bound graph contains a cycle |
| `JsonNonFinite` | yes | JSON conversion encountered NaN or infinity |
| `JsonUnsupported` | yes | value kind is not JSON-safe |
| `JsonDuplicateKey` | yes | JSON object contains a repeated key |
| `JsonLimitExceeded` | yes | bounded JSON depth/node/string/byte/work limit |
| `ImportSpecifierInvalid` | yes | module specifier is non-canonical |
| `ImportCycle` | yes | loader encounters a loading-state cycle |
| `ImportDepthLimit` | yes | bounded dependency depth is exhausted |
| `ModuleInitializationFailed` | yes | imported module top-level execution failed |
| `ModuleMemberMissing` | yes | requested namespace member is private/absent |
| `CapabilityDenied` | yes | effective capability set rejects an operation |
| `HostValidationFailed` | yes | host argument/state validation failed safely |
| `HostUnavailable` | yes | selected host service/adapter is unavailable |
| `DeviceDisconnected` | yes | selected device disconnected during operation |
| `PackageMismatch` | yes | package/resource/runtime version or hash mismatch |
| `OcrModelUnavailable` | yes | required OCR engine/model is absent or invalid |
| `ResourceMissing` | yes | immutable resource id is absent |
| `Timeout` | yes | one bounded host operation timed out |
| `HostInternal` | yes | unexpected host operation failure after redaction |
| `Cancelled` | no | execution stop token was observed |
| `HumanTakeover` | no | policy/user requested automation surrender |
| `DeadlineExceeded` | no | execution-context absolute deadline expired |
| `InstructionLimitExceeded` | no | VM instruction budget was exhausted |
| `MemoryLimitExceeded` | no | context heap/external memory budget was exhausted |
| `StackLimitExceeded` | no | script call-frame budget was exhausted |
| `CleanupLimitExceeded` | no | defer count/work/emergency cleanup budget exhausted |
| `TaskLimitExceeded` | no | task/awaiter/timer/host-operation bound exhausted |
| `InternalInvariant` | no | stale/cross-context/corrupt runtime state detected |

`catchable = no` identifies terminal errors. Terminal status is part of the
code contract, not inferred from a prefix or message.

`ErrorMetadata.details` is validated recursively as JSON-safe at construction.
Identity-bearing values, including `host<T>` at any nested list/map depth, are
rejected with `JsonUnsupported`; an error envelope can therefore never retain,
serialize, or forge a native handle capability.

### ERR-005 — SourceReference identity and privacy

A present `SourceReference` MUST contain immutable package snapshot identity,
canonical extensionless module id, and one ERR-002 `SourceSpan`. Its exact field
order is `snapshot_id`, `module`, `span`; `span` contains `begin` then `end`,
and each location contains `byte_offset`, `line`, `column`. It MUST NOT
contain a host filesystem path, user profile, URL with credentials, source
excerpt, pointer, or mutable active-package alias. Built-in/host failures with
no script call site use `source = null`; host failures caused by a script call
use the call expression's source reference.

The snapshot identity and module id select the exact source bytes even after
package activation or rollback. Renderers MAY derive an excerpt from that
immutable snapshot under redaction policy, but the excerpt is not stored in or
hashed into the Error value.

### ERR-006 — Primary span attribution

Runtime attribution MUST use existing immutable AST spans and the corresponding
verified-bytecode instruction span. Explicit `throw` uses the
`ThrowStatement.span`; call binding and host failures use `CallExpression.span`;
unary/binary/type/numeric failures use the complete expression node span;
index/member failures use their access node span; binding initialization uses
the declaration/reference span; import errors use `ImportStatement.span`.

A cleanup failure retains the failing operation's primary span. Its cleanup
stack frame additionally carries the registering `DeferStatement.span`. A host
adapter MUST NOT replace a known script call span with a C++ location. Compiler
generated instructions inherit the narrowest responsible AST span and MUST NOT
invent line/column values.

### ERR-007 — Stack frame contract and capture

The stack MUST be captured when an Error is first raised, before unwinding, and
ordered innermost frame first. Re-throwing the same Error preserves its source,
stack, identity, causes, and suppressed list unless ERR-014 must derive a new
immutable Error to record cleanup failures. Each script frame contains:

| Frame field | Required contract |
| --- | --- |
| `kind` | `script` or `host` |
| `module` | canonical script id or versioned `baas/` host module |
| `function` | declared name, `<anonymous>`, or `<module>` |
| `phase` | `body`, `module_init`, `cleanup`, or `host` |
| `call_source` | caller `CallExpression` reference or `null` at root |
| `definition_source` | function declaration/expression reference or `null` for host |
| `defer_source` | registration reference only for cleanup frames, else `null` |

A host call prepends one allowlisted host frame and retains
the script caller frames. Native C++ function names, exception types, return
addresses, thread ids, and OS stacks MUST NOT appear. The default
`max_stack_frames` is 128. On overflow, keep the 96 innermost and 32 outermost
frames in their respective order and record the middle omission in
`truncated.stack_frames`.

### ERR-008 — Causes, suppressed failures, and bounds

`cause` means “this older Error led to this Error”; `suppressed` means “this
secondary Error occurred while preserving a primary outcome.” These roles MUST
NOT be interchanged. Graphs MUST be acyclic. Error construction rejects a
self/cycle edge as `InternalInvariant`; serialization stops safely even if
corrupt input bypassed construction.

Defaults are `max_cause_depth = 16`, `max_suppressed_errors = 16`,
`max_error_message_bytes = 4096`, and `max_error_detail_bytes = 65536` per
Error. Truncation MUST preserve the primary Error, nearest causes, and earliest
observed suppressed failures, then record omitted counts/bytes in `truncated`.
Truncation is deterministic and MUST NOT allocate beyond the context/error
budget. A truncated message MUST end at a valid UTF-8 scalar boundary. Details
and context use the bounded JSON rules in `VALUE_SEMANTICS.md`.
The `context` map contains nullable, opaque strings in exact order: `task_id`,
`session_id`, `package_id`, `snapshot_id`, `language_version`,
`correlation_id`. The `truncated` map contains `stack_frames`, `cause_errors`,
`suppressed_errors`, `message_bytes`, and `detail_bytes` as nonnegative omitted
counts, followed by Boolean `details_replaced` and `fallback`.

### ERR-009 — Throw semantics

`throw expression;` MUST evaluate the expression exactly once. If it is an
Error, the VM raises that same immutable Error without recapturing its stack. If
it is another value, the VM creates `ThrownValue` at the `ThrowStatement.span`,
records only the stable value kind in `details.thrown_kind`, and raises it. It
MUST NOT retain, stringify, or serialize an arbitrary identity-bearing thrown
value, because doing so could leak capability state or create hidden roots.

Throw immediately stops the current statement and begins ERR-013 unwinding.
The operand and resulting Error remain roots until caught, returned to the host,
or serialized and released.

### ERR-010 — Try/catch behavior

`try { ... } catch (error) { ... }` catches the first catchable Error raised
during the dynamic execution of the try block. Before entering the catch block,
the binding is initialized to the exact Error identity. Normal catch completion
handles that Error; `throw error;` rethrows the same identity. Errors raised by
the catch block follow normal rules and do not automatically acquire the caught
Error as a cause; callers that wrap an error MUST set an explicit cause through
the future standard error constructor.

Terminal errors in ERR-004 bypass catch blocks. Defers still run under ERR-013.
A defer registered inside a try block belongs to the function activation, so an
error raised when it later executes is not caught by the lexical catch that
surrounded registration.

### ERR-011 — Terminal failure and cancellation priority

`Cancelled`, `HumanTakeover`, `DeadlineExceeded`, instruction/memory/stack/
cleanup/task limit failures, and `InternalInvariant` MUST NOT be swallowed or
converted by ordinary script. They bypass catch and become the primary outcome
after cleanup. Only a separately capability-checked privileged host boundary
MAY convert cancellation/human-takeover into a new task outcome, outside the
terminating script context.

`ASYNC_TASKS.md` defines request propagation, deadline clocks, safe-point
observation, masking, and deterministic priority without changing catchability.

At a host safe point, a result/error already atomically published before a stop
request wins. Otherwise an observed stop request wins as `Cancelled` or
`HumanTakeover`; a concurrently discovered host failure is attached as
suppressed if safe. This publication rule, not thread timing or message text,
determines deterministic precedence.

### ERR-012 — Defer registration and lexical capture

Executing `defer statement` registers one cleanup thunk on the current function
activation; it does not execute the nested statement then. The parser MUST
continue to reject function-external defer as `PAR014`. Registration occurs
only when control reaches the defer, is atomic, and either pushes exactly one
entry or raises `CleanupLimitExceeded` without a partial entry.

The thunk contains code identity, `DeferStatement.span`, and lexical binding
cells required by the nested statement. Names, callees, member objects, and
arguments are evaluated at cleanup time, not registration time, so intervening
mutation is visible. A block nested under defer creates its ordinary lexical
scope when cleanup executes. Defers belong to function activations, not block
activations; leaving an inner block does not run them.

### ERR-013 — Unwind triggers and LIFO execution

On function fallthrough, `return`, catchable throw escaping the function, or
terminal failure, the VM MUST execute every registered cleanup for that
activation once in strict last-registered-first-executed order. A return
expression is evaluated and rooted before cleanup begins. Each nested function
activation owns an independent cleanup stack. Cleanup completes before the
return value or Error is delivered to the caller.

Cleanup runs at VM safe points with a bounded, non-cancellable emergency
cleanup allowance sufficient for already-registered language thunks and
idempotent host-handle close operations. It retains the original terminal state
without letting general work resume. Process termination or unrecoverable host
loss may prevent script cleanup; context teardown MUST still enqueue leaked
host-handle release records under ADR-0002.

If evaluator context close cannot finish those records, the embedder MUST
retain the release dispatcher and retry on its owner strand. The final owner
MUST reach `destruction_safe()` before destruction; an unsafe dispatcher
destructor fails fast and MUST NOT convert pending native ownership into
successful cleanup.

### ERR-014 — Cleanup failure precedence

All remaining cleanup thunks MUST be attempted even when one fails. If unwind
already has a primary Error, the primary Error remains primary and cleanup
failures are collected as `suppressed` in execution order. If unwind began with
normal fallthrough or return, the first cleanup failure becomes primary and
later cleanup failures are suppressed in execution order; the pending return
value is discarded. A terminal Error always remains primary.

After draining, a nonempty suppressed accumulator MUST construct one derived
immutable primary Error under ERR-003, copying code/message/origin/catchability,
source, stack, cause, details, and context, then appending bounded suppressed
entries and truncation counts. The Error that entered unwind is not mutated;
without cleanup failures its identity is preserved.

Suppressed overflow follows ERR-008 and MUST NOT replace the primary Error. A
cleanup failure is never assigned as `cause` merely because it occurred during
unwind. Host close is required to be idempotent; a duplicate close is successful
and MUST NOT manufacture an error.

### ERR-015 — Cleanup-safe statement restrictions and budgets

The nested statement of a defer MAY use any grammar statement, including calls,
assignments, blocks, branches, loops, declarations, `try`/`catch`, and `throw`,
except for the following restriction. Outside a nested function declaration or
expression it MUST NOT contain `return`, `break`, `continue`, `await`, or
another `defer`; the semantic validator MUST report `SEM009` at the forbidden
node. This prevents cleanup from replacing an unwind target,
suspending indefinitely, or growing the active cleanup stack while draining it.

Each frame/context MUST bound registered defer count, cleanup instructions,
cleanup wall time, nested call depth, heap use, and host-close queue work. The
default `max_defers_per_frame` is 1024. Exhaustion is terminal
`CleanupLimitExceeded`; it follows ERR-014 and cannot skip the host teardown
fallback. The synchronous conformance evaluator implements independent cleanup
step and nested-call allowances. Independent wall-time enforcement, production
heap reservation, and host-close fallback remain production bytecode-VM work.

### ERR-016 — Native host boundary

Every versioned host symbol MUST have one exception/status translation guard at
the outermost native ABI boundary. It validates/root arguments, checks effective
capabilities, invokes the adapter, translates success to language values, and
translates every failure to ERR-003 before returning control to the VM. A C++
exception, `exception_ptr`, platform error object, raw status integer, or
partially initialized output MUST NOT escape that guard.

Known adapters MUST publish a declarative mapping from safe domain statuses to
ERR-004 codes and allowlisted detail keys. Unknown `std::exception`, non-standard
exception, or undeclared status becomes `HostInternal`; its raw `what()`, type,
stack, and native payload are logged only to the protected host sink under an
opaque correlation id. `std::bad_alloc` becomes terminal
`MemoryLimitExceeded`.

### ERR-017 — Foundation C++ translation table

The existing foundation failures MUST translate as follows once a VM host
boundary exists. The `runtime_code` detail preserves the exact RT name from
`VALUE_SEMANTICS.md` without exposing native messages.

| C++/foundation failure | Script code |
| --- | --- |
| `RT001_TYPE_MISMATCH` | `TypeMismatch` |
| `RT002_CROSS_HEAP_REFERENCE` | `InternalInvariant` |
| `RT003_STALE_REFERENCE` | `InternalInvariant` |
| `RT004_CELL_KIND_MISMATCH` | `InternalInvariant` |
| `RT005_MEMORY_LIMIT_EXCEEDED` | `MemoryLimitExceeded` |
| `RT006_CELL_LIMIT_EXCEEDED` | `MemoryLimitExceeded` |
| `RT007_SINGLE_ALLOCATION_EXCEEDED` | `MemoryLimitExceeded` |
| `RT008_STRING_LIMIT_EXCEEDED` | `MemoryLimitExceeded` |
| `RT009_EXTERNAL_MEMORY_LIMIT_EXCEEDED` | `MemoryLimitExceeded` |
| `RT010_COLLECTION_WORK_LIMIT_EXCEEDED` | `MemoryLimitExceeded` |
| `RT011_INVALID_UTF8` | `InvalidUtf8` |
| `RT012_JSON_CYCLE` | `JsonCycle` |
| `RT013_JSON_NON_FINITE` | `JsonNonFinite` |
| `RT014_JSON_UNSUPPORTED` | `JsonUnsupported` |
| `RT015_HEAP_TORN_DOWN` | `InternalInvariant` |
| `RT016_INDEX_OUT_OF_RANGE` | `IndexOutOfRange` |
| `RT017_RELEASE_QUEUE_LIMIT_EXCEEDED` | `CleanupLimitExceeded` |
| `RT018_JSON_DEPTH_LIMIT_EXCEEDED` | `JsonLimitExceeded` |
| `RT019_JSON_NODE_LIMIT_EXCEEDED` | `JsonLimitExceeded` |
| `RT020_JSON_STRING_LIMIT_EXCEEDED` | `JsonLimitExceeded` |
| `RT021_JSON_BYTE_LIMIT_EXCEEDED` | `JsonLimitExceeded` |
| `RT022_JSON_WORK_LIMIT_EXCEEDED` | `JsonLimitExceeded` |
| `RT023_JSON_DUPLICATE_KEY` | `JsonDuplicateKey` |
| `TaskCancelled` | `Cancelled` |
| `SubmitTimeout` | `Timeout` |
| `ExecutorShutdown` | `HostUnavailable` |
| `std::bad_alloc` | `MemoryLimitExceeded` |
| other C++ exception/status | `HostInternal` |

`translate_runtime_error_code()` implements the complete allocation-free
RT001-RT023 code/catchability subset of this table. It does not build an Error
envelope, capture frames, translate executor/host exceptions, or unwind script
control flow; those boundaries remain pending.

### ERR-018 — Safe serialization and observability

Error-envelope serialization MUST be deterministic, non-throwing at the public
boundary, bounded by ERR-008, and ordered exactly as the fields in ERR-003.
Nested details use JSON-safe deep-copy rules. Invalid/non-JSON detail input is
replaced with an allowlisted kind marker and sets
`truncated.details_replaced = true`. Serialization failure MUST fall back to
the same twelve ERR-003 fields: `source`/`cause` are null; `stack`, `suppressed`,
and `details` are empty; every context value is null except an allowlisted
`correlation_id`; and `truncated.fallback` is true.

Messages, details, context, stack, and logs MUST redact source contents,
filesystem paths, credentials, tokens, pointer/address values, C++ RTTI, device
secrets, and unrestricted host payloads. Public task/session identifiers are
opaque bounded strings. Full native diagnostics MAY be written only to a
protected host sink linked by the opaque correlation id.

### ERR-019 — Normative conformance examples

The following source MUST remain syntactically and lexically semantically valid.
It is checked by the non-executing `BAAS_script_check`; it does not prove
unwinding or host translation.

<!-- conformance:error-cleanup-valid -->
```baas
fn guarded(handle) {
    let state = "open";
    defer {
        handle.close();
        state = "closed";
    }
    try {
        handle.run();
    } catch (error) {
        throw error;
    }
    return state;
}
```

Registration captures the `state` binding cell, `throw error` preserves Error
identity, and return evaluates `state` before the cleanup mutates it. A future
VM conformance test MUST therefore return `"open"` after closing the handle.

<!-- conformance:error-cleanup-invalid -->
```baas
defer cleanup();
```

The invalid example MUST retain `PAR014`. The semantic fixture below MUST retain
`SEM009`.

<!-- conformance:error-cleanup-semantic-invalid -->
```baas
fn invalid(cleanup) {
    defer return;
}
```

The semantic invalid example MUST parse successfully and then report `SEM009`.
The validator also rejects `break`, `continue`, `await`, and nested `defer`
inside a cleanup body, but resets the restriction across a nested function
declaration or expression as required by ERR-015.

### ERR-020 — Implementation and completion boundary

`SourceLocation.h`, `Diagnostic.h`, lexer/parser diagnostics, source-spanned AST
nodes, catch binding analysis, `PAR014`, the `SEM009` cleanup-control validator,
and `ValueHeap` Error cells are implemented foundations. `LanguageErrorCode`
contains the complete ERR-004 inventory and is the sole source of code spelling
and catchability. `ErrorMetadata` and `allocate_error` implement context-local,
identity-bearing, read-only-after-publication heap records for origin,
`SourceReference`, bounded stack frames, cause, ordered suppressed Errors,
JSON-safe details, allowlisted context, and explicit truncation metadata. All
retained `Value` edges are same-heap validated and traced. Construction applies
the shared `ModuleSpecifier` boundary to source/frame module IDs (including its
fail-closed NFC policy), requires source IDs to name package modules, and keeps
script/Host frame kinds consistent with their module kind. It also applies
the ERR-008 message/stack/suppressed/detail defaults, rejects duplicate,
wrong-kind, stale, cross-heap, self/obvious-cycle edges, and rejects a cause
chain already beyond `max_error_cause_depth`; VM Error builders remain
responsible for preserving nearest causes while deriving a bounded replacement.
`derive_error` copies a published primary into a new identity instead of
mutating it. `BAAS_script_structured_error_heap_tests` verifies these heap-only
contracts, including GC and allocation accounting.

`ErrorEnvelope.h` and `ErrorEnvelope.cpp` implement the dependency-free ERR-018
serialization foundation. `serialize_error_envelope` is a `noexcept`,
caller-buffer API: it writes the twelve ERR-003 fields in normative order,
derives `catchable` from `LanguageErrorCode`, recursively serializes bounded
cause/suppressed/detail graphs, and applies independent depth/node/output-byte/
string/work plus ERR-008 cause/suppressed/message/detail limits. Invalid detail
values become allowlisted kind markers. Cycles, stale/cross-heap references,
budget exhaustion, and native allocation failures fail closed to a complete
redacted fallback when the caller buffer can hold it; otherwise the API reports
zero published bytes. Allocation-free checked Heap views keep detail traversal
from copying attacker-expanded containers. `BAAS_script_error_envelope_tests`
fix stable bytes, nested ordering, every budget boundary, fallback privacy,
invalid-detail markers, and rooted/stale GC behavior.

The bounded `SynchronousEvaluator` now implements the Draft 0.1 synchronous
conformance slice: runtime and Host failures are materialized as immutable Error
values, `throw Error` preserves identity, non-Error operands become
`ThrownValue`, catchable and terminal failures are separated, and script-visible
Error members are read-only projections. Each function activation owns a
bounded `defer` stack; normal completion, return, Error propagation, and terminal
failure drain every registered cleanup in LIFO order. Return values are rooted
before cleanup, terminal failures cannot be swallowed, and ERR-014 derives a
new primary only when suppressed cleanup failures must be recorded. Native
Host failures carry `origin = host` plus an allowlisted Host frame. Uncaught
Errors cross the evaluator boundary as a bounded `baas.script.error/v1` JSON
snapshot retained by `EvaluationError` and emitted by `BAAS_script_run`. Native
tests plus the versioned process corpus provide executable conformance evidence.

This is not the production bytecode VM. Language-level Task/cancellation
integration, production package activation, full asynchronous Host adapters,
cause-chain normalization across task/service boundaries, and service diagnostic
transport remain pending. The heap snapshot is not itself an ERR-003 serialized
envelope; only the bounded `serialize_error_envelope`
boundary produces one.
Those production VM/async/service components MUST remain pending in Phase 2,
and this synchronous slice MUST NOT be described as completing Phase 1 as a
whole.

## Machine-checked evidence

`tests/docs/test_errors_cleanup_spec.py` uses only the Python standard library
and MUST verify ERR-001 through ERR-020, exact Error fields and stable language
codes, source/diagnostic anchors, stack/cause/truncation rules, the complete
RT001–RT023 translation table, parser/AST/semantic foundations, tagged static
fixtures and CTest wiring, structured Error heap/API/test anchors, the
implemented synchronous unwinder and serialization boundary, explicit pending
production VM/async/service integration, the single ROADMAP checkbox, and
Foundation CI path wiring.
