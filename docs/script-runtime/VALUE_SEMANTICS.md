# BAAS Script value semantics (Draft 0.1)

This document is the normative Draft 0.1 contract for runtime values,
conversions, equality, collections, mutability, heap isolation, and in-memory
JSON interoperability. It refines the summaries in `LANGUAGE_SPEC_DRAFT.md` and
uses the syntax defined by `LANGUAGE_GRAMMAR.md`. If a summary in the draft
language specification conflicts with this document, this document controls for
the subjects in its title.

The key words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.
The clause identifiers are stable conformance anchors; renumbering one is a
specification compatibility change. This document describes the checked-in
`ValueHeap` and `JsonBridge` foundation plus the bounded synchronous AST
evaluator. It does not claim that the production VM/loader or JSON text
parser/serializer exists.

## Normative clauses

### VAL-001 — Runtime value universe

An implementation MUST expose exactly these language-visible value kinds:

| Language kind | Runtime kind | Storage and observable role |
| --- | --- | --- |
| `null` | `Null` | inline singleton absence value |
| `bool` | `Boolean` | inline `true` or `false` |
| `int` | `Integer` | inline signed 64-bit integer (`int64`) |
| `float` | `Float` | inline IEEE-754 binary64 value (`double`) |
| `string` | `String` | immutable, valid UTF-8 byte sequence in a heap cell |
| `list` | `List` | mutable, ordered sequence of values in a heap cell |
| `ordered-map` | `OrderedMap` | mutable, insertion-ordered, unique string-keyed map |
| `function` | `Function` | identity-bearing callable metadata and captured values |
| `module` | `Module` | identity-bearing namespace name and ordered exports |
| `error` | `Error` | identity-bearing code, message, optional span, and details |
| `task` | `Task` | identity-bearing task id, state, and retained values |
| `host-handle` | `HostHandle` | identity-bearing adapter handle with explicit release state |
| `bytes` | `Bytes` | immutable, arbitrary binary byte sequence in a heap cell |

`HeapReference` is an internal representation tag, not a fourteenth language
kind. A heap reference consists of heap identity, slot, and generation; scripts
MUST NOT forge or inspect those components.

### VAL-002 — Strings, keys, and UTF-8

Language strings, ordered-map keys, module names/export names, and structured
error code/message fields MUST contain well-formed UTF-8. Overlong sequences,
surrogates, values above U+10FFFF, truncated sequences, and invalid continuation
bytes are rejected as `RT011_INVALID_UTF8`. U+0000 is valid in a runtime string.

Strings and keys MUST preserve their exact UTF-8 bytes. The runtime MUST NOT
normalize, case-fold, or locale-transform them. String equality and map-key
matching are therefore byte-exact. A source BOM remains a lexer error under
`LANGUAGE_GRAMMAR.md`; it is not part of a decoded source string.

Byte values are distinct from strings: they permit every octet, perform no
UTF-8 validation, and have no mutable buffer API. Host conversion copies their
payload so neither side can mutate storage owned by the other side.

String length, indexing, slicing, and substring membership MUST operate on
Unicode scalar values, not storage bytes or grapheme clusters. Concatenation
and slicing produce new immutable valid-UTF-8 strings. String indices are
zero-based and non-negative, and slices use the positive-step/default/clipping
rules in VAL-007. A wrong operand kind is `RT001_TYPE_MISMATCH`; an invalid
single index or non-positive step is `RT016_INDEX_OUT_OF_RANGE`.

### VAL-003 — Allowed conversions and type mismatch

Draft 0.1 MUST perform no general implicit coercion. In particular, `bool` is
not an `int`; strings do not implicitly become numbers; numbers, collections,
and identity objects do not implicitly become strings; and a non-string map key
is invalid. An operation requiring another kind MUST fail with
`RT001_TYPE_MISMATCH` unless a more specific stable error is named below.

Only these conversions are implicit:

1. a value is converted to a Boolean decision by the truthiness rules in
   VAL-005 when used by a conditional or `not`;
2. an `int` operand is converted to binary64 when paired with a `float` by a
   mixed numeric operation or equality comparison;
3. the explicit in-memory JSON bridge applies VAL-012 through VAL-016.

The `and` and `or` operators MUST short-circuit and return a selected original
operand, not a newly coerced Boolean. `+` MUST concatenate only two strings or
two lists; mixed string/number or list/other operands are type errors.

### VAL-004 — Numeric domain and boundaries

An `int` MUST represent every value from -9,223,372,036,854,775,808 through
9,223,372,036,854,775,807 exactly. The current decimal integer token contains
digits only and is parsed before unary `-`; consequently a source integer token
greater than 9,223,372,036,854,775,807 produces `PAR018`, including the positive
token inside `-9223372036854775808`. The minimum int64 value MAY still enter the
runtime through a host value, JSON value, or a future checked arithmetic path.

A source float literal MUST decode to a finite IEEE-754 binary64 value or
produce `PAR019`. Runtime host code can construct infinities or NaNs, but those
values remain floats, are truthy except for signed zero, compare with IEEE
`==`, and are forbidden by JSON interoperation. `+0.0` and `-0.0` compare equal
and are both falsey; a NaN compares unequal to every float, including itself.

Same-kind integer comparison is exact. Mixed int/float comparison MUST convert
the int to binary64 first. Integers in the inclusive range
-9,007,199,254,740,992 through 9,007,199,254,740,992 are exactly representable;
outside that range conversion can round, so mixed equality follows the rounded
binary64 values. Boolean operands MUST NOT participate as numbers.

Checked integer arithmetic MUST eventually report the draft language error
`NumericOverflow` instead of wrapping, and division by zero MUST report
`DivisionByZero`; these evaluator errors are reserved by
`LANGUAGE_SPEC_DRAFT.md` and are not implemented or assigned an RT number by
the current heap foundation.

### VAL-005 — Truthiness

Every value MUST have the following deterministic truthiness:

| Value | False exactly when |
| --- | --- |
| `null` | always |
| `bool` | the value is `false` |
| `int` | the value is zero |
| `float` | the value compares equal to `0.0` |
| `string` | it has zero UTF-8 bytes |
| `bytes` | it has zero bytes |
| `list` | it has zero elements |
| `ordered-map` | it has zero entries |
| `function`, `module`, `error`, `task`, `host-handle` | never |

Truthiness MUST NOT traverse collection children and therefore remains defined
for cyclic collections.

### VAL-006 — Equality and identity

`==` MUST be cycle-aware and obey these rules; `!=` is its negation:

- null equals only null; Boolean equality requires the same Boolean;
- ints compare exactly, floats use binary64 `==`, and mixed int/float equality
  uses the promotion rule in VAL-004;
- strings compare exact UTF-8 bytes;
- bytes compare exact binary payload bytes;
- lists compare structurally by length and corresponding element order;
- ordered maps compare structurally by unique key/value membership, independent
  of insertion order;
- function, module, error, task, and host-handle values compare by exact heap
  reference identity, not by their metadata or children.

The cycle algorithm MUST terminate by remembering already compared reference
pairs. Two independently allocated acyclic collections MAY be equal without
being identical. Shared subgraphs do not change structural equality.

There are two deliberate work-accounting layers. Each `ValueHeap::equals` call
is cycle-aware and bounded by the heap's `HeapLimits` and reachable cell graph.
`SynchronousEvaluator::EvaluationStats.collection_work` separately counts its
own collection materialization, iteration, lookup, and preflight work; it does
not cumulatively add nested heap equality traversal. This boundary is explicit
until a future heap API exposes per-comparison traversal accounting.

The grammar's `is` operator MUST test representation identity: two heap-backed
values are identical only when heap identity, slot, and generation all match;
two inline values are identical only when they have the same runtime kind and
their same-kind scalar equality succeeds. Thus `1 == 1.0` can be true while
`1 is 1.0` is false, and `value is null` is the canonical null test.

### VAL-007 — List collections

A list MUST preserve element order and may contain any value from VAL-001,
including itself. Multiple references to one list are aliases: append or indexed
replacement through one alias MUST be visible through every alias in that heap.

The runtime foundation uses zero-based, non-negative indices. Replacement at an
index greater than or equal to the current length MUST fail with
`RT016_INDEX_OUT_OF_RANGE` without mutation. Append adds one final element.
List iteration and membership testing MUST proceed from index zero upward.
Membership uses VAL-006 equality. A list `+` or slice produces a new outer list
while retaining the selected element values, so nested heap objects remain
aliases. Slice start and stop default to zero and length, are clipped to that
range, and the step defaults to one; Draft 0.1 requires a positive step and
defines no negative-index wraparound. A wrong index/slice operand kind is
`RT001_TYPE_MISMATCH`; a negative index, non-positive step, or out-of-range
single-element access is `RT016_INDEX_OUT_OF_RANGE`.

### VAL-008 — Ordered-map collections

An ordered map MUST accept only valid UTF-8 string keys and MUST preserve the
position at which each unique key was first inserted. Updating an existing key
changes its value without moving it; inserting a new key appends it. Iteration
MUST follow that insertion order. Membership tests keys, not values.

Runtime map construction and map literals use deterministic last-value-wins for
a duplicate key while retaining the key's first insertion position. This rule
does not apply to JSON objects: VAL-013 rejects JSON duplicate keys. Map
equality follows VAL-006 and therefore ignores insertion order even though
iteration and JSON output preserve it.

The grammar accepts an expression before `:` in a map literal so parsing can
recover uniformly, but evaluation MUST require the result to be a string and
otherwise report `RT001_TYPE_MISMATCH`. Member syntax is string-key access
sugar. A missing lookup remains an absent optional result at the heap boundary;
the evaluator MUST translate bracket/member access for that result to
`RT016_INDEX_OUT_OF_RANGE` rather than silently insert a key. Map assignment is
an upsert and follows the stable-position rule above.

### VAL-009 — Identity-bearing runtime objects

Function, module, error, task, and host-handle values MUST use identity equality
and MUST remain outside structural JSON. Their traced child edges are:

- function captures;
- module export values;
- structured-error detail values;
- task retained values.

A host handle contains an opaque authenticated native key: handle and adapter
ids, fixed exact type, generation, context and snapshot ids, external-byte
charge, and closed state. Closing MUST be idempotent, MUST queue host release
work only once, and MUST NOT perform host I/O from the collector. Collection or
teardown of an open handle also queues a release record. Charges move with
reliable release ownership and remain live through retry and
native-released/awaiting-ACK. Teardown may detach records and their shared
ledger to the owning dispatcher; poisoned records remain visible and cannot
produce a false completed state.

The synchronous AST evaluator currently uses a bounded side table whose
`FunctionRecord` entries own rooted lexical `Environment` objects. Its heap
`FunctionMetadata.captures` vectors are intentionally empty, and records are
not reclaimed before evaluator teardown. Heap collection therefore cannot
invalidate a reachable callable, but this non-reclaiming transitional side
table MUST NOT be treated as ADR-0002's final traced closure representation.

### VAL-010 — Mutability, aliasing, and lifetime

Null, Boolean, integer, and float values are inline and immutable. Strings and
bytes are heap-backed but immutable. Lists and ordered maps are mutable. The
remaining heap kinds expose identity-bearing runtime state and MUST be mutated
only by their designated runtime/host operations.

Each execution context MUST own a separate non-moving tracing heap. Heap-backed
aliases remain valid while reachable from an explicit root, a temporary root,
or another reachable cell. Collection reclaims unreachable cycles. Reuse of a
slot increments its generation, and access through an obsolete generation MUST
fail as `RT003_STALE_REFERENCE`. Teardown invalidates the heap and later heap
operations MUST fail as `RT015_HEAP_TORN_DOWN`.
While an evaluator owns an active public execution boundary, including its
post-execution Host release drain, an attempted Heap teardown MUST fail as
`RT024_HEAP_BUSY` without invalidating cells or changing Host dispatcher
admission. The owning evaluator may tear down the Heap only after that boundary
has returned.

Allocation and collection may occur during mutation. The runtime MUST root
inputs and intermediates across those points. A failed append, map update,
allocation, JSON materialization, or collection preflight MUST NOT publish a
partial collection graph or a partially applied caller-visible mutation.

### VAL-011 — Heap isolation and cross-heap edges

A heap cell or root MUST NOT contain a reference owned by another heap. Reading,
rooting, allocating, or mutating such an edge MUST fail with
`RT002_CROSS_HEAP_REFERENCE`; no cross-heap edge may be installed. A reference
whose slot or generation is no longer live MUST fail with
`RT003_STALE_REFERENCE`.

Independent heaps MAY operate concurrently, but one heap belongs to one
execution-context strand. Transfer between contexts MUST use the explicit
JSON-safe deep-copy operation in VAL-014 or a future specified host concurrency
primitive. Passing a value through a queue does not transfer its heap ownership.

### VAL-012 — In-memory JSON value model and mapping

The bridge MUST use the dependency-free `JsonValue` model; it is not a JSON text
parser or serializer. Its kinds map without scalar coercion:

| JSON value kind | Language value kind |
| --- | --- |
| null | `null` |
| Boolean | `bool` |
| signed 64-bit integer | `int` |
| finite binary64 float | `float` |
| valid UTF-8 string | `string` |
| array | `list` |
| insertion-ordered, unique string-key object | `ordered-map` |

Object and map conversion MUST preserve entry insertion order. Integer and
float alternatives remain distinct even when numerically equal. JSON arrays
and objects own their children by value; the bridge MUST NOT introduce mutable
language-object sharing through that model.

### VAL-013 — JSON rejection and duplicate keys

Heap-to-JSON conversion MUST reject a cycle with `RT012_JSON_CYCLE`, a NaN or
infinity with `RT013_JSON_NON_FINITE`, and every function, module, error, task,
or host-handle occurrence with `RT014_JSON_UNSUPPORTED`. Foreign and stale heap
references retain `RT002` and `RT003` respectively.

JSON-to-heap conversion MUST reject invalid UTF-8 as `RT011_INVALID_UTF8`, a
non-finite float as `RT013_JSON_NON_FINITE`, and any repeated object key as
`RT023_JSON_DUPLICATE_KEY`. Duplicate-key checking MUST occur during the
iterative pre-materialization traversal and fail before any destination-heap
allocation. Object keys are compared by exact UTF-8 bytes.

These names specify a single-condition failure, not a global priority among
multiple simultaneous faults. Iterative depth/node/work/byte checks already
incurred while reaching a node MAY fail before a deeper semantic fault. Within
otherwise admitted input, traversal follows array/map insertion order and MUST
produce the stable semantic error named above when it reaches the bad value.

A failure MUST return no root value and expose no partial graph. Cells allocated
before a later heap-materialization failure remain unrooted and reclaimable;
existing caller roots and cross-heap invariants remain unchanged.

### VAL-014 — JSON-safe cross-context deep copy

`deep_copy_json_value` MUST require distinct source and destination heaps; using
one heap for both roles fails with `RT002_CROSS_HEAP_REFERENCE`. It first applies
heap-to-JSON validation and then destination materialization, so only the kinds
in VAL-012 can cross the boundary.

The copy MUST preserve scalar values, collection shape, map insertion order,
and valid UTF-8 bytes. It MUST duplicate each occurrence of a mutable list or
map. Therefore a source DAG is allowed, but two source edges to one mutable
object become two independent destination objects; mutating one destination
occurrence cannot change another occurrence or the source. Cycles remain
rejected.

All destination intermediates MUST be temporarily rooted across allocation and
soft collection. Publication occurs only after the full destination root has
been built, and no allocation occurs between publication and return.

### VAL-015 — Heap budgets and transactional behavior

Every heap MUST enforce its configured limits. The current defaults are:

| `HeapLimits` field | Default | Stable failure |
| --- | ---: | --- |
| `max_live_bytes` | 64 MiB | `RT005_MEMORY_LIMIT_EXCEEDED` |
| `max_cells` | 1,000,000 | `RT006_CELL_LIMIT_EXCEEDED` |
| `max_single_allocation` | 8 MiB | `RT007_SINGLE_ALLOCATION_EXCEEDED` |
| `max_string_bytes` | 16 MiB | `RT008_STRING_LIMIT_EXCEEDED` |
| `max_external_bytes` | 64 MiB | `RT009_EXTERNAL_MEMORY_LIMIT_EXCEEDED` |
| `soft_collect_threshold` | 48 MiB | triggers collection, then the applicable hard limit |
| `max_collection_work` | 2,000,000 cells | `RT010_COLLECTION_WORK_LIMIT_EXCEEDED` |
| `max_pending_release_records` | 1,000,000 | `RT017_RELEASE_QUEUE_LIMIT_EXCEEDED` |

Live-byte and string-byte accounting MUST use owned capacities, including cell,
vector, string, and byte-buffer capacities, rather than only logical payload
lengths. Byte buffers count toward live bytes but not string bytes. The
soft threshold is clamped to `max_live_bytes`; exceeding it attempts collection
before a hard-limit decision. Accounting overflow is a memory-limit failure.
Limit checking and native allocation failure MUST leave accounting and the
caller-visible object unchanged.

### VAL-016 — JSON bridge budgets

Both bridge directions and cross-context copying MUST enforce these default
`JsonBridgeLimits` independently of the destination heap limits:

| `JsonBridgeLimits` field | Default | Stable failure |
| --- | ---: | --- |
| `max_depth` | 256 | `RT018_JSON_DEPTH_LIMIT_EXCEEDED` |
| `max_nodes` | 100,000 | `RT019_JSON_NODE_LIMIT_EXCEEDED` |
| `max_string_bytes` | 16 MiB | `RT020_JSON_STRING_LIMIT_EXCEEDED` |
| `max_total_bytes` | 64 MiB | `RT021_JSON_BYTE_LIMIT_EXCEEDED` |
| `max_work` | 500,000 units | `RT022_JSON_WORK_LIMIT_EXCEEDED` |

The root is depth one, and configured depth is additionally clamped to the hard
ceiling of 1,024. Work is one unit per visited node plus one per child edge.
String bytes aggregate all string values and object keys. Logical total bytes
are one kind byte per node, primitive payload bytes, UTF-8 string/key bytes, and
`sizeof(size_t)` per array/object value slot; the last quantity follows the
target ABI. Counter overflow MUST report the corresponding limit error. Heap
limits in VAL-015 still apply while JSON is materialized.

### VAL-017 — Stable runtime error names

The RT name, not the human-readable message, MUST be the stable foundation
error contract:

| Stable name | Required condition |
| --- | --- |
| `RT001_TYPE_MISMATCH` | operation receives the wrong value kind |
| `RT002_CROSS_HEAP_REFERENCE` | foreign edge or invalid same-heap transfer |
| `RT003_STALE_REFERENCE` | dead slot or obsolete generation |
| `RT004_CELL_KIND_MISMATCH` | heap cell accessor uses the wrong cell kind |
| `RT005_MEMORY_LIMIT_EXCEEDED` | live/native memory or accounting overflow |
| `RT006_CELL_LIMIT_EXCEEDED` | live-cell or slot-index limit |
| `RT007_SINGLE_ALLOCATION_EXCEEDED` | one accounted allocation is too large |
| `RT008_STRING_LIMIT_EXCEEDED` | heap-accounted string capacity limit |
| `RT009_EXTERNAL_MEMORY_LIMIT_EXCEEDED` | open host external-byte limit |
| `RT010_COLLECTION_WORK_LIMIT_EXCEEDED` | tracing work limit |
| `RT011_INVALID_UTF8` | invalid runtime string or metadata bytes |
| `RT012_JSON_CYCLE` | cycle in a JSON-bound heap graph |
| `RT013_JSON_NON_FINITE` | NaN or infinity in JSON conversion |
| `RT014_JSON_UNSUPPORTED` | identity-bearing value in JSON conversion |
| `RT015_HEAP_TORN_DOWN` | operation after heap teardown |
| `RT016_INDEX_OUT_OF_RANGE` | invalid string/list index/slice or missing map key |
| `RT017_RELEASE_QUEUE_LIMIT_EXCEEDED` | host release backpressure |
| `RT018_JSON_DEPTH_LIMIT_EXCEEDED` | bridge depth limit |
| `RT019_JSON_NODE_LIMIT_EXCEEDED` | bridge node limit |
| `RT020_JSON_STRING_LIMIT_EXCEEDED` | bridge aggregate string/key bytes |
| `RT021_JSON_BYTE_LIMIT_EXCEEDED` | bridge logical total bytes or overflow |
| `RT022_JSON_WORK_LIMIT_EXCEEDED` | bridge node/edge work limit |
| `RT023_JSON_DUPLICATE_KEY` | repeated in-memory JSON object key |
| `RT024_HEAP_BUSY` | teardown attempted during a protected evaluator boundary |

Implementations MUST preserve these exact spellings in
`runtime_error_code_name`. Adding a new stable error requires a new number; an
existing number MUST NOT be silently repurposed.

### VAL-018 — Grammar and implementation boundary

`LANGUAGE_GRAMMAR.md` MUST remain the concrete syntax contract for these value
forms and operators.

The literals `null`, `true`, `false`, decimal integer, finite decimal float,
string, list literal, and map literal MUST produce the corresponding kinds in
VAL-001. Map syntax MAY parse a non-string key expression, but VAL-008 applies
at evaluation. Postfix index/member syntax and `in`, `==`, `!=`, `is`, `not is`,
`and`, `or`, and `not` MUST use this document's collection, equality, identity,
and truthiness rules.

This specification freezes semantics, not implementation completion. The
checked-in parser, `ValueHeap`, `JsonBridge`, and `SynchronousEvaluator` provide
evidence for the clauses they implement. The evaluator covers synchronous
lookup/arithmetic, collections, functions, package imports, and the bounded
scalar/JSON side of the synchronous `baas/log.emit` conformance bridge. Bytecode,
production VM/loader execution, real Host adapters,
structured unwinding, async, and JSON text I/O remain separately pending ROADMAP
work. Those pending components
MUST conform to these clauses rather than infer Python or JavaScript coercion
behavior. The specialized ERR-018 Error-envelope writer does not change this
boundary: it emits only the fixed ERR-003 schema and is not a general
`JsonValue` text parser/serializer.

## Machine-checked evidence

`tests/docs/test_value_semantics_spec.py` uses only the Python standard library
and MUST verify the complete VAL-001 through VAL-018 clause set, the runtime
type inventory, the exact RT001 through RT023 names, default heap/bridge limits,
the grammar linkage, the ROADMAP checkbox, and Foundation runtime CI wiring.
The C++ `ValueHeapTests` and `JsonBridgeTests` remain the executable behavioral
evidence for implemented runtime paths.
