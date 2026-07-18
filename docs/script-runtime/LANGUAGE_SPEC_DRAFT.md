# BAAS Script Language Specification (Draft 0.1)

This document defines the intended language contract. Items marked *draft* may
change before language version 1 is frozen; implementations must not infer
missing semantics from JavaScript or Python.

## 1. Goals and boundaries

BAAS Script is a dynamically typed, UTF-8, module-based orchestration language.
It is designed to keep frequently updated automation logic outside the C++
binary while routing stable image, OCR, device, configuration, logging, task,
and service capabilities through versioned host modules.

The language must be Turing-complete, deterministic under injected host inputs,
cancellation-aware, and safe to run in a long-lived concurrent service. It is
not a Python syntax clone and does not expose raw C++ pointers, arbitrary native
library loading, raw threads, unrestricted filesystem, or unrestricted network
access.

Files use the `.baas` suffix. Package manifests select the language version,
host API version, entrypoint, required capabilities, resources, and integrity
hashes. The concrete negotiation, manifest, detached-signature, atomic
activation, rollback, and cache rules are defined in `PACKAGE_VERSIONING.md`.

## 2. Source text and diagnostics

The complete Draft 0.1 lexical and syntactic grammar is defined in
`LANGUAGE_GRAMMAR.md`. The normative compile-diagnostic, source-reference,
runtime Error, stack, cause, serialization, and redaction contract is defined
in `ERRORS_AND_CLEANUP.md`.

- Source is valid UTF-8 and a byte-order mark is rejected rather than stripped.
- Line and column diagnostics are one-based. Byte offsets are zero-based.
- A source span records start/end byte offsets and start/end line/column.
- Newline is whitespace; semicolons terminate simple statements.
- `#` begins a line comment. `/* ... */` is a block comment. `//` is reserved
  for floor division and is never a comment marker.
- Identifiers preserve their exact UTF-8 bytes; the draft Unicode acceptance
  profile and its pre-1.0 normalization decision are explicit in the grammar.
- Invalid UTF-8, invalid escapes, unterminated strings/comments, and unknown
  characters produce recoverable diagnostics with source spans.

### 2.1 Keywords

`let`, `fn`, `if`, `else`, `while`, `for`, `in`, `return`, `break`,
`continue`, `import`, `as`, `true`, `false`, `null`, `and`, `or`, `not`,
`is`, `try`, `catch`, `throw`, `defer`, `async`, and `await`.

## 3. Values

The normative value, conversion, equality, collection, mutability, heap, and
JSON rules are defined in `VALUE_SEMANTICS.md`. The remainder of this section
is a summary and must be read consistently with that contract.

The required value set is:

- `null`;
- Boolean;
- signed 64-bit integer;
- IEEE-754 binary64 float;
- immutable UTF-8 string;
- mutable ordered list;
- mutable insertion-ordered string-keyed map;
- function/closure;
- module namespace;
- host handle with an unforgeable capability and explicit lifetime;
- task/future;
- structured error.

Runtime ownership, cycle collection, context isolation, and memory-budget rules
are defined by `ADR-0002-vm-memory-management.md`.

Integer overflow throws `NumericOverflow`; division by zero throws
`DivisionByZero`. Numeric operations promote an integer to float only when one
operand is float. There is no implicit string-to-number conversion.

Maps preserve insertion order because Python `co_detect` reaction order is
observable. Equality is value/structural equality for primitive values, lists,
and maps (cycle-aware), and identity equality for functions, modules, errors,
tasks, and host handles. `is` tests representation identity, including
`value is null`.

Truthiness is compatible with migration needs: `null`, `false`, numeric zero,
empty string, empty list, and empty map are false; other values are true.

## 4. Lexical scope and state

The normative scope, initialization, function, closure, recursion, branch,
loop, non-local return/break/continue, and module-loading rules for Sections 4,
6, 7, and 8 are defined in `CONTROL_FLOW_AND_MODULES.md`.

`let name = expression;` creates a mutable binding in the current lexical
scope. Reading an uninitialized or unknown name is an error. Assignment updates
the nearest existing lexical binding; implicit globals are forbidden.

Each script execution has isolated module and task state. Mutable language
objects are never silently shared between independent execution contexts.
Cross-task transfer is explicit and either frozen, copied, or mediated by a
host concurrency primitive.

Supported assignment targets are identifiers, list indices, map keys, and map
member sugar. Compound assignments cover arithmetic operators.

## 5. Expressions and operators

Required expression forms include literals, identifiers, list/map literals,
member/index access, slices, calls, anonymous functions, assignment, unary
operators, and binary operators.

From lower to higher precedence:

1. assignment (`=`, `+=`, `-=`, `*=`, `/=`, `//=`, `%=`);
2. `or` (short circuit, returns the selected operand);
3. `and` (short circuit, returns the selected operand);
4. equality and identity (`==`, `!=`, `is`, `not is`);
5. ordering and membership (`<`, `<=`, `>`, `>=`, `in`, `not in`);
6. addition/subtraction (`+`, `-`);
7. multiplication/division/floor/modulo (`*`, `/`, `//`, `%`);
8. exponentiation (`**`, right associative);
9. unary (`-`, `+`, `not`);
10. call, member, index, and slice.

`+` concatenates strings or lists only when both operands share that type.
`in` tests list membership, map key membership, or string substring membership.

## 6. Statements and control flow

```text
let total = 0;
for (item in values) {
    if (item < 0) { continue; }
    total += item;
}

while (condition) {
    if (done()) { break; }
}
```

Required statements are block, binding, expression, `if`/`else`, `while`,
`for` over deterministic iterables, `break`, `continue`, `return`, `throw`,
`try`/`catch`, and `defer`.

The normative throw/catch/defer registration, unwind, cleanup failure, and
terminal-error rules are defined in `ERRORS_AND_CLEANUP.md`.

`defer statement;` registers cleanup in the current function scope and executes
in last-in-first-out order on normal return, throw, and cancellation. A host
stop/cancellation signal cannot be swallowed by a broad catch; it may be
observed for cleanup and is then rethrown unless an explicit privileged host
operation converts it.

## 7. Functions and closures

```text
fn factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

fn make_counter(start = 0) {
    let value = start;
    return fn() { value += 1; return value; };
}
```

Functions are first-class closures with lexical capture and recursion. Required
call semantics include positional arguments, named arguments, default values,
and precise duplicate/missing/unknown argument errors. Host APIs should prefer
an ordered options map over unstable long positional signatures.

This combination of unbounded loops (subject to configured execution budgets),
mutable state, conditionals, and recursive functions establishes the language's
computational completeness; runtime limits constrain a deployment, not the
language model.

## 8. Modules and packages

```text
import "baas/log" as log;
import "baas/device" as device;
import "tasks/common" as common;
```

- Imports resolve through the package manifest and immutable package snapshot.
- Package imports use canonical root-relative extensionless logical ids and
  cannot escape or probe outside the immutable package snapshot.
- Host modules use the reserved `baas/` namespace.
- A module initializes at most once per execution context. Import cycles produce
  a deterministic `ImportCycle` diagnostic in language version 1.
- Top-level names are exposed on the imported namespace in version 1; names
  beginning with `_` are private and inaccessible through imports.
- Package validation parses every module, resolves every import/resource, and
  checks version/capability declarations before atomic activation.

## 9. Structured errors

The complete normative structured-error contract is defined in
`ERRORS_AND_CLEANUP.md`; this section is a summary.

Every error carries a stable code, message, source span when applicable, stack
frames, task/session metadata, and an optional JSON-safe detail map.

Required categories include syntax/type/name/index/numeric/import/capability,
host validation, device disconnected, package mismatch, OCR/model, timeout,
cancelled/human takeover, resource missing, and internal host failure.

C++ exceptions do not cross the language ABI. Native bindings translate them
to structured script errors and preserve safe causal details.

## 10. Tasks, cancellation, and time

The normative Task state machine, structured ownership, await/suspension,
cancellation/deadline priority, safe-point, thread-confinement, host async, and
deterministic scheduler/clock contract is defined in `ASYNC_TASKS.md`; this
section is a summary.

`async fn` returns a task. `await` suspends without blocking an executor worker.
The standard task module provides structured `spawn`, `join/all`, `race`,
`timeout`, and cancellation scopes. Detached fire-and-forget tasks are forbidden
unless a privileged service host explicitly owns their lifetime.

Every execution context contains:

- a stop token and absolute deadline;
- instruction and memory budgets;
- task/session/config/device identity;
- injected monotonic/wall clocks and RNG;
- capability set and immutable resource snapshot;
- structured event/trace sink.

Device operations for the same device are serialized by the host. CPU-only
vision/OCR work may use bounded shared pools. The language does not expose OS
thread primitives.

## 11. JSON interoperation

The normative in-memory JSON model, limits, rejection rules, and cross-context
copy semantics are defined in `VALUE_SEMANTICS.md`. JSON null, Boolean, signed
int64, finite binary64, valid UTF-8 string, array, and unique-key ordered object
map to the corresponding language values without scalar coercion. Conversion
rejects cycles, non-finite floats, duplicate JSON object keys, and functions,
modules, errors, tasks, and host handles with stable runtime errors. This
foundation does not provide JSON text parsing or serialization.

## 12. Versioned host modules

The full intended host surface is declared from the first stable API even when
adapters are implemented incrementally. Normative signatures, capability IDs,
errors, budgets, threading, and parity ownership for the Phase 1 surface are in
`HOST_CAPABILITY_CONTRACTS.md` and `host-capabilities.v1.json`:

| Module | Responsibility |
| --- | --- |
| `baas/log` | scoped structured logs and progress events |
| `baas/clock` | injected wall/monotonic readings for deterministic time access |
| `baas/random` | injected deterministic jitter/randomness |
| `baas/config` | immutable snapshots and atomic validated transactions |
| `baas/resource` | locale/activity-aware immutable resources and hashes |
| `baas/fs` | privileged policy-rooted filesystem reads and atomic mutations |
| `baas/device` | capture, click/swipe/long-click/scroll, app lifecycle |
| `baas/vision` | RGB/template/search helpers and ordered detect/reaction |
| `baas/ocr` | model lifecycle and OCR variants/options |
| `baas/procedure` | logical-ID composite device automation on the device/context strand |
| `baas/task` | capability-free structured concurrency and language Task operations |
| `baas/scheduler` | capability-scoped automation registration, dispatch, and scheduling |
| `baas/service` | bounded symbolic in-process service events and requests |
| `baas/process` | privileged allowlisted process inspection and execution |
| `baas/http` | privileged policy-checked HTTP requests |
| `baas/socket` | privileged policy-checked opaque stream sockets |
| `baas/notify` | user notifications and push adapters |
| `baas/trace` | deterministic fixture references and observable host effects |

Capabilities are granted per package and narrowed per execution. Raw filesystem,
network, process, native extension, and unsafe remote access require separate
explicit capabilities and are not part of the default automation API.

## 13. BAAS behavior profiles

Host APIs must encode, test, and version these existing semantics rather than
hide them inside global state:

- standard coordinate space 1280x720 and resolution ratio conversion;
- configurable click jitter and injected RNG;
- screenshot cache/interval/immediate-capture behavior;
- BGR/RGB conventions and ordered reaction matching;
- asynchronous click versus wait-for-completion behavior;
- desktop/service/Android differences, including Android clamping and local
  accessibility/device operations;
- locale/activity-specific resource snapshots;
- typed retry, timeout, package mismatch, and human-stop behavior.

## 14. Update and execution safety

Script packages and resources use signed/integrity-checked manifests. Update is
download-to-staging, validate, atomically activate, retain rollback, and only
then retire the old immutable snapshot. Running tasks keep their original
snapshot. New tasks select a compatible active version.

Malformed or hostile scripts are constrained by parser limits, import depth,
instruction/memory/task limits, bounded host queues, deadlines, and capability
checks. These limits must produce stable errors rather than terminate the
service.

## 15. Required conformance groups

- UTF-8 token/source-span and malformed-input tests.
- Operator/short-circuit/truth/equality/order tests.
- scope/closure/recursion/control/defer/error tests.
- ordered list/map/index/slice/JSON tests.
- module/import/cycle/version/capability tests.
- cancellation/deadline/task/budget tests.
- deterministic clock/RNG/fake-host trace tests.
- Python golden parity tests for each migration-matrix operation.
