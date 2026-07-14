# BAAS Script scope, control flow, functions, and modules (Draft 0.1)

This document is the normative Draft 0.1 contract for lexical scope,
initialization, functions, closures, recursion, branching, loops, non-local
control flow, and module loading. It refines the corresponding summaries in
`LANGUAGE_SPEC_DRAFT.md`, uses the concrete syntax in `LANGUAGE_GRAMMAR.md`,
and composes with the value contract in `VALUE_SEMANTICS.md` and the package
contract in `PACKAGE_VERSIONING.md`. Structured errors and cleanup are defined
by `ERRORS_AND_CLEANUP.md`; async execution and cancellation are defined by
`ASYNC_TASKS.md`.

The key words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.
The CTL identifiers are stable conformance anchors. Static behavior in this
document is implemented by the checked-in parser, immutable AST, and lexical
semantic analyzer. Dynamic evaluation, bytecode, the VM, and the module loader
remain pending; normative rules for them are requirements, not completion
claims.

## Normative clauses

### CTL-001 — Compilation and execution boundary

The source pipeline MUST be UTF-8 source to tokens to immutable source-spanned
AST to lexical semantic analysis. A module containing any lexer, parser, or
semantic error MUST NOT execute. The future production pipeline MUST continue
from the validated AST to verified bytecode and a stack VM as selected by
`ADR-0001-runtime-architecture.md`; an AST evaluator MAY exist only as a
conformance oracle.

The AST MUST preserve distinct nodes for block, binding, expression statement,
branch, while loop, for loop, function declaration/expression, return, break,
continue, import, throw, try/catch, and defer. This document defines the first
eleven control/module forms. `ERRORS_AND_CLEANUP.md` defines structured
throw/catch/defer unwinding, and `ASYNC_TASKS.md` defines async suspension and
cancellation; their VM execution remains pending.

### CTL-002 — Lexical scope regions

Name resolution MUST use a lexical scope tree with the module scope as its
root. These constructs create scopes:

1. every explicit `{ ... }` `BlockStatement` creates a child scope;
2. a function declaration or expression creates one function scope;
3. a `for` statement creates one loop scope containing its iteration binding;
4. a `catch` clause creates a scope containing its catch binding.

The braces forming a function body delimit the function scope itself and MUST
NOT add another scope between parameters and top-level body declarations. The
explicit block used as a `for` body is still a child of the loop scope. `if` and
`while` MUST NOT introduce an implicit scope around a non-block body. Exiting a
scope makes all of its bindings inaccessible, while captured bindings remain
alive through their closure environments.

### CTL-003 — Binding kinds, declaration order, and shadowing

The runtime MUST support `Let`, `Parameter`, `Function`, `Import`, `For`, and
`Catch` bindings. A declaration is visible from its source position to the end
of its lexical scope; declarations are not hoisted. A read or write for which
no visible declaration exists MUST produce `SEM001` during lexical semantic
analysis. The future VM MUST NOT create an implicit global for such a write.

Two declarations with the same exact UTF-8 name in one scope MUST produce
`SEM002`, except duplicate parameters use `SEM004` in semantic analysis and
`PAR004` in parser validation. A declaration in a child scope MAY shadow an
outer declaration. Each reference and identifier assignment MUST resolve to
the nearest visible binding and record its lexical distance. Member/index
assignment reads its object/index expressions and mutates the referenced
object; it is not a lexical write to the member spelling.

### CTL-004 — Initialization and assignment

A `let` binding MUST be declared uninitialized, then evaluate its initializer,
then become initialized. Reading it from its own initializer MUST produce
`SEM003`. Plain assignment is a write and does not first read the old value;
compound assignment is a read followed by a write and therefore requires an
initialized binding. Function declarations and import aliases are initialized
when declared; required parameters, for bindings, and catch bindings are
initialized when their scope is entered.

Only an explicit block changes lexical scope. Consequently a naked declaration
used as an `if` or `while` body belongs to the surrounding scope. Its statement
runs only when control reaches it; a later runtime read on a path where it never
ran MUST fail with the future language error `UninitializedBinding`. The current
semantic analyzer detects direct self-initialization but is intentionally not a
complete control-flow definite-assignment analysis. The VM MUST preserve the
runtime check rather than treating static acceptance as proof of initialization.

### CTL-005 — Function creation, parameters, and calls

Evaluating a function declaration or `fn` expression MUST create a first-class
closure containing code identity and the lexical environment required by
CTL-006. A named declaration makes its binding available before static body
analysis and MUST atomically install the created closure as initialized before
the body can execute. A function expression has no implicit self name. The
function body's top-level lets share a scope with its parameters.

Parameters are ordered. A required parameter MUST NOT follow a defaulted one
(`PAR005`), and names MUST be unique. At a call, the callee and arguments MUST
be evaluated left-to-right in source order. Positional arguments bind the next
unbound parameters; named arguments bind their exact parameter names. A
positional argument after a named argument is `PAR007`, and a repeated named
argument is `PAR006`. The VM MUST report stable call errors for too many,
missing, repeated, or unknown arguments without entering the body:
`CallArityMismatch`, `CallArgumentDuplicate`, or `CallArgumentUnknown` as
applicable. Calling another value kind is `NotCallable`.

Defaults MUST be evaluated at call time, only for omitted parameters, from left
to right in the callee's lexical environment after earlier parameters are
initialized. A default may read earlier parameters and outer bindings, but not
itself (`SEM003`) or a later parameter (`SEM001`). Default values are not cached
between calls. A call frame and all argument/default intermediates MUST be heap
roots until return or unwind.

### CTL-006 — Closure capture and mutation

A nested function reference to a binding owned by an enclosing function MUST
capture the binding cell, not a snapshot of its value. Reads and assignments by
the owner and every closure therefore observe the same mutable binding. Module
bindings are resolved through the context-local module environment and are not
listed as function captures.

Capture metadata MUST identify the referenced `BindingId` and mark both the
reference and binding as captured. If an inner function reads a binding across
one or more intermediate functions, the capture MUST propagate through every
intermediate `FunctionInfo` so each closure keeps the owning environment alive.
One binding appears at most once in each function's capture list. Closure
environment edges MUST be traced roots/heap edges under ADR-0002.

### CTL-007 — Recursion and declaration visibility

A named function MUST support direct recursion because its binding is
initialized before its body is analyzed or executed. A nested named function
captures its containing-scope function binding when it calls itself; a
module-level function resolves its recursive name through the module
environment. Recursive calls use ordinary call semantics. Draft 0.1 does not
require tail-call optimization; an implementation MAY perform it only when
observable stack traces, limits, cancellation, and error behavior remain
conformant.

There is no declaration hoisting. A function body that directly names a later
same-scope function MUST produce `SEM001`; Draft 0.1 therefore does not provide
direct forward-declared mutual recursion. Programs MAY build an explicit
already-declared indirection container before creating mutually recursive
closures. Recursion is limited only by configured call/instruction/memory and
cancellation budgets in a deployment, not by a semantic fixed depth.

### CTL-008 — Branching

`if (condition) consequent else alternate` MUST evaluate the condition exactly
once using `VALUE_SEMANTICS.md` truthiness, then execute exactly one selected
branch. An absent `else` does nothing when false. `else` binds to the nearest
unmatched `if` according to the parser. A branch statement MAY be a block or a
single statement, and only an explicit block creates a child lexical scope.

Short-circuit `and` and `or` MUST evaluate left-to-right and execute the right
operand only when selected by truthiness. They return the selected operand and
can therefore express guarded calls without adding an implicit scope.

### CTL-009 — While loops

A `while` loop MUST evaluate its condition before every iteration. A false
initial condition executes the body zero times. After a normal body completion
or `continue`, control returns to the condition; `break` exits the nearest
active loop in the current function. A block body creates a fresh block-scope
activation on each iteration; a non-block body uses the surrounding scope rules
in CTL-002 and CTL-004.

Condition checks, backward branches, and body entry MUST be VM safe points for
instruction limits, deadline, and cooperative cancellation. A deployment limit
failure is an execution error, not normal loop termination.

### CTL-010 — For loops and deterministic iteration

`for (name in iterable) body` MUST evaluate `iterable` once before creating the
loop binding. Supported iteration plans are a shallow snapshot of list element
values in index order, ordered-map keys in insertion order, and string Unicode
scalar values in order. Another kind is a type error under
`VALUE_SEMANTICS.md`. Later mutation of the source collection MUST NOT change
the current loop's iteration plan, while referenced mutable element values
remain aliases.

The for statement creates one initialized mutable binding reused for every
iteration. Closures created by different iterations and capturing that binding
therefore share one cell. `continue` selects the next snapshotted item; `break`
discards the remaining plan. The iterable expression cannot see the loop
binding, and the binding is inaccessible after the for statement. An explicit
block body creates a fresh nested scope per iteration.

### CTL-011 — Return, break, and continue

`return expression;` MUST evaluate the expression, then leave only the current
function and deliver that value to its caller. Bare `return;` returns `null`.
`return` outside a function is `PAR010`. A nested function cannot return from an
enclosing function.

`break;` and `continue;` MUST target the nearest active `while` or `for` in the
current function. They are `PAR008` and `PAR009` respectively when no such loop
exists. Entering a nested function resets the parser's loop context, so it
cannot break or continue an enclosing function's loop. `ERRORS_AND_CLEANUP.md`
MUST define defer/error unwinding before VM execution is claimed; this document
does not mark throw/try/defer execution complete.

### CTL-012 — Import syntax and canonical resolution

An import MUST have the grammar `import string as identifier;`; the module
specifier is a source string literal, never a computed runtime expression.
Executing it resolves the module, obtains its namespace, and initializes the
alias as an `Import` binding at that source position.

A specifier beginning `baas/` names a versioned host module. Every other
specifier names a package module by a root-relative, extensionless logical id;
the resolver appends `.baas` and requires that exact path in the manifest's
`modules` list. Package specifiers MUST use `/`, valid NFC UTF-8, and nonempty
segments, and MUST NOT contain a leading slash, drive prefix, backslash, NUL,
`.` or `..` segment, or empty segment. Logical matching is case-sensitive on
every platform. Import resolution MUST NOT probe the host filesystem or apply
platform-specific case folding.

`ModuleSpecifier` implements this pure canonicalization boundary for the
current runtime foundation. It validates UTF-8 and bounded segments without
filesystem access, preserves accepted bytes and case, and appends `.baas` only
when producing a package manifest path. ASCII is inherently NFC; non-ASCII
input fails closed unless the embedding runtime supplies one shared
platform-independent NFC predicate, so a missing normalization dependency
cannot silently produce platform-specific identities.

### CTL-013 — Package graph validation and import cycles

Before activation, the package validator MUST parse and semantically analyze
every listed module, canonicalize every import, resolve all package and host
module edges, validate declared versions/capabilities, and build a deterministic
dependency graph. A missing package module is `PackageEntryMissing`; an
undeclared/incompatible host module uses the stable package errors in
`PACKAGE_VERSIONING.md`. No source from staging may execute during validation.

The package dependency graph MUST be acyclic. A self edge or multi-module cycle
MUST fail activation as `ImportCycle`/`PackageCompileFailed` and report one
deterministic cycle: begin with the lexicographically smallest canonical module
id in the discovered strongly connected component, follow source-order import
edges with canonical-id tie breaking, and repeat the first id at the end. The
runtime loader MUST also detect a `Loading` module edge defensively and fail
without publishing a partial namespace.

### CTL-014 — Module initialization, caching, and failure

Each execution context MUST own a module cache with `Loading`, `Ready`, and
`Failed` states. Package modules initialize lazily on first import and execute
top-level statements in source order. When execution reaches an import, its
dependency initializes depth-first before execution resumes after that import.
A module initializes at most once in one context. Re-import of a `Ready` module
returns the same namespace identity and state.

The cache key MUST include the immutable package snapshot identity, canonical
module path, and selected language version; host-module entries additionally
include the exact selected host API version. A failed initialization MUST NOT
publish a namespace as ready. The context caches the stable failure and later
imports rethrow it without repeating top-level side effects. A new execution
context has a new cache and MAY attempt initialization independently.

Module namespaces, initialization frames, and the current dependency chain
MUST be heap roots. Cache insertion/state transition MUST be transactional: a
budget, cancellation, validation, or initialization failure cannot expose a
partially populated ready namespace.

### CTL-015 — Module namespaces and visibility

A package module namespace MUST expose its successfully initialized top-level
declaration bindings in declaration order, except names beginning with `_` are
private. Because Draft 0.1 has no separate export statement, public top-level
lets, functions, and import aliases are exports. Duplicate same-scope names
remain `SEM002`; there is one binding and one namespace entry per public name.

Consumers access exports through the import alias and member syntax. A private
or missing export MUST produce `ModuleMemberMissing` and MUST NOT fall back to
another module, global, filesystem entry, or host registry name. The namespace
object has identity equality and is not JSON-safe under
`VALUE_SEMANTICS.md`.

### CTL-016 — Context isolation, versions, and activation

Module bindings and mutable module state MUST be isolated per execution
context. Two contexts importing the same immutable source snapshot receive
distinct namespace objects and heaps. Closures, module namespaces, tasks, and
ordinary host handles MUST NOT cross contexts; JSON-safe explicit deep copy is
the only currently implemented value transfer foundation.

An execution context MUST retain the exact immutable package/resource snapshot,
language version, host-module versions, and capability set selected at start.
Activation or rollback of another package version affects only new contexts.
Cache entries from different snapshots or version selections MUST NOT alias.
Language/host compatibility and deprecation follow `PACKAGE_VERSIONING.md`.

### CTL-017 — Module safety and bounded execution

Import authority MUST be the intersection of the signed manifest, immutable
snapshot, registered versioned host modules, package-declared capabilities,
service/user policy, platform availability, and per-task narrowing. The loader
MUST NOT expose raw filesystem, process, network, native extension, singleton,
or unrestricted device access. `baas/` host imports require a declared
compatible module and every capability required by accessed symbols.

Validation and execution MUST bound source bytes, module count, import depth,
AST nodes/depth, bytecode size, call depth, instructions, heap/external bytes,
host queues, and initialization work. Limit exhaustion and cancellation MUST
produce stable errors and leave the active package record and ready cache
entries unchanged. These dynamic loader/VM limits are pending implementation;
existing `SEM006` and `SEM007` provide the AST node/depth foundation.

The checked-in static diagnostic anchors are:

| Code | Required static condition |
| --- | --- |
| `SEM001` | unknown or not-yet-declared name |
| `SEM002` | duplicate declaration in one scope |
| `SEM003` | statically evident read before initialization |
| `SEM004` | duplicate parameter |
| `SEM005` | reserved for future declaration/type validation |
| `SEM006` | AST node limit |
| `SEM007` | semantic nesting limit, hard ceiling 1,024 |
| `SEM008` | malformed immutable AST |
| `SEM009` | forbidden non-local or suspending operation in a defer cleanup body |

The future VM/loader MUST use these stable dynamic categories. Their structured
payload, stack, and source-span schema follows `ERRORS_AND_CLEANUP.md`; runtime
implementation remains pending:

| Future stable category | Required dynamic condition |
| --- | --- |
| `UninitializedBinding` | execution reads a declared slot before its declaration ran |
| `NotCallable` | call target is not a function/native callable |
| `CallArityMismatch` | missing required or excessive positional arguments |
| `CallArgumentDuplicate` | one parameter is supplied more than once |
| `CallArgumentUnknown` | named argument does not name a parameter |
| `ImportSpecifierInvalid` | import string violates CTL-012 canonical form |
| `ImportCycle` | defensive runtime loading-state cycle detection |
| `ImportDepthLimit` | bounded loader dependency depth exhausted |
| `ModuleInitializationFailed` | imported module top-level execution failed |
| `ModuleMemberMissing` | private or absent namespace member access |

### CTL-018 — Constructive Turing-completeness argument

Ignoring configurable deployment limits, BAAS Script MUST admit unbounded heap
growth, repeated `while` execution, mutable bindings, conditionals, and list
construction/indexing. These features constructively simulate a deterministic
two-counter Minsky machine, which is Turing-complete.

Represent natural-number zero as `null` and successor `n + 1` as the one-element
list `[n]`. Increment is `counter = [counter];`. Zero-test is
`counter is null`. Decrement of a nonzero counter is
`counter = counter[0];`. Store the finite machine control location in `pc` and
compile every instruction into an `if` branch inside `while (pc != halt)`.
Two such linked-list counters plus finite control implement `INC` and `DECJZ`,
and therefore any two-counter program.

<!-- conformance:turing-machine -->
```baas
let c0 = null;
let c1 = null;
let pc = 0;
let halt = -1;
while (pc != halt) {
    if (pc == 0) {
        c0 = [c0];
        pc = 1;
    } else {
        if (c0 is null) {
            pc = halt;
        } else {
            c0 = c0[0];
            c1 = [c1];
            pc = 0;
        }
    }
}
```

Real executions remain finite when instruction, deadline, call, or heap budgets
are finite. Those policy bounds restrict one run; they do not reduce the
abstract language model because limits are configurable rather than a semantic
constant.

### CTL-019 — Normative conformance examples

The following examples MUST remain syntactically and lexically semantically
valid. The checked-in combined fixture is executed by `BAAS_script_check` in
CTest; the documentation checker verifies its required anchors.

<!-- conformance:closure-recursion -->
```baas
fn make_counter(seed = 0) {
    let value = seed;
    fn next() { value += 1; return value; }
    return next;
}
fn factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}
let counter = make_counter();
let first = counter();
let answer = factorial(5);
```

The `next` closure captures the one `value` binding by reference, and the named
`factorial` binding is available to its own body but not before its declaration.

<!-- conformance:loop-branch-import -->
```baas
import "baas/log" as log;
import "tasks/common" as common;
let total = 0;
for (item in [1, 2, 3]) {
    if (item == 2) { continue; }
    total += item;
}
while (total < 10) {
    total += 1;
    if (total == 9) { break; }
}
if (total > 0) { log.info(total); } else { common.run(); }
```

Module path existence, host registration, capability checks, initialization,
and execution are intentionally not claimed by this static fixture.

These invalid forms MUST retain their current diagnostics:

| Source shape | Diagnostic |
| --- | --- |
| `let self = self;` | `SEM003` |
| `later(); fn later() { return; }` | `SEM001` |
| same-scope duplicate declaration | `SEM002` |
| duplicate parameter | `PAR004` and/or `SEM004` |
| `return;` outside a function | `PAR010` |
| `break;` outside the current function's loop | `PAR008` |
| `continue;` outside the current function's loop | `PAR009` |

### CTL-020 — Implementation status and completion boundary

The checked-in lexer/parser implement the concrete forms and contextual parser
diagnostics. `Ast.h` implements immutable source-spanned nodes.
`SemanticAnalyzer` implements source-order lexical binding, initialization
checks, nearest resolution, binding kinds, closure capture propagation, and
bounded AST traversal. `Environment` implements rooted lexical binding cells.
`ModuleSpecifier` implements bounded, filesystem-independent canonical import
specifier validation. `SyntaxCheck` composes the non-executing compile stages.

The repository does not yet implement closure execution,
bytecode/compiler/VM evaluation, runtime recursion/control transfer, package
graph validation, manifest membership/host-version resolution, import-cycle
detection, namespace publication, module initialization/cache, or native module
registration. These items MUST remain pending in Phase 2 until executable
conformance evidence exists. Completing this normative Phase 1 specification
MUST NOT be described as completing the VM, module loader, Turing-machine
fixture execution, the general conformance corpus, or Phase 1 as a whole.

## Machine-checked evidence

`tests/docs/test_control_modules_spec.py` uses only the Python standard library
and MUST verify CTL-001 through CTL-020, conformance example ids, grammar and AST
forms, binding/diagnostic inventories, semantic implementation anchors, the
syntax-check fixture/CTest gate, canonical module-specifier foundation,
module/package links, pending implementation statements, the ROADMAP status,
and Foundation CI path wiring.
