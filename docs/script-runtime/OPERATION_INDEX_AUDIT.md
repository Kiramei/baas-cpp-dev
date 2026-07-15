# Python Operation Index Audit

This is Phase 0 inventory evidence, not a parity or migration-completion claim.
The index statically parses the `baas-dev` snapshot; no inspected Python module
is imported or executed.

## Reproduction

From a `baas-cpp-dev` checkout with `baas-dev` as a sibling directory:

```powershell
python scripts\migration\operation_index.py `
  --python-repo ..\baas-dev `
  --output docs\script-runtime\evidence\operation-index.json `
  --matrix docs\script-runtime\MIGRATION_MATRIX.md `
  --evidence-link evidence/operation-index.json

python scripts\migration\operation_index.py `
  --python-repo ..\baas-dev `
  --output build\operation-index-strict.json `
  --strict
```

Both commands return 0. Two normal generations and the strict report are
byte-identical.

## Taxonomy v5 core/script boundary

Schema v2 and identity version 1 remain unchanged. The bounded static
owner-resolution pass runs before source-scope disposition defaults. It accepts
only facts visible in the AST or exact rules in `operation_rules.v5.json`:

- absolute and relative imports, aliases, local wildcard exports, definitions,
  and conservative lexical rebinding/branch merges;
- declared concrete annotations, local class constructors, literal/container
  types, `*args`, `**kwargs`, exact destructuring, and positive `isinstance`
  narrowing inside the guarded branch;
- exact constructor, factory-return, property-return, context-manager-return,
  and iterable-element rules.

The hardened v4 pass adds only source-provable facts: lexical nested definitions
in evaluation order; exact returns for selected device, logging, process,
socket, subprocess, timer, and regular-expression factories; proven
`io.IOBase` context-manager targets; and exact iterable element rules for
`psutil.process_iter` and `re.finditer`.

The audited-module pass then resolves only exact receiver operations whose
meaning was checked at the pinned Python revision. Module-local text,
collection, and class behavior remains in the script layer, while NumPy array
reductions, the `self.u2()` pinch operation, and the thread result queue retain
their existing Vision, Device, and Scheduler Host ownership. The rule is
restricted to `module/*` plus an enumerated symbol list; it is not a receiver
suffix heuristic and does not classify an identical unknown call in `core/*`.

Generator 4.1 propagates container element types only when they are proven by
a concrete generic annotation, a homogeneous literal, or an exact factory rule
such as `str.split`. It also recognizes positive `type(value) is ConcreteType`
and `type(value) == ConcreteType` guards. Element facts participate in the same
branch-state intersection as value types, so they do not escape a guarded branch
or survive an ambiguous control-flow merge. This resolves the typed `main.py`
argument conversion and merges equivalent operation identities without adding a
symbol-tail heuristic.

Unknown call results, subscripts, ambiguous unions, rebinding across possible
control-flow paths, `getattr` with runtime names, and untyped parameters remain
dynamic unless an authoritative source or exact rule fixes their boundary.
Module export discovery is AST-only; external wildcard imports are not
expanded. Receiver suffixes and final identifier segments are never ownership
evidence.

Taxonomy v5 adds `CPP_RUNTIME` for `core/*`; its default disposition is
`CPP_RUNTIME_INTERNAL`. Six non-script source scopes now fix the migration
boundary independently of receiver ownership: C++ runtime, C++ service, legacy
GUI/Tauri replacement, tests, deployment tooling, and migration tooling.
`module/*`, `main.py`, and `cli.example.py` remain `SCRIPT_RUNTIME`; their
dynamic calls require an exact audited rule rather than inheriting the core
default.
Source-qualified rules are classified per file before equal conclusions are
aggregated, so an unrelated `obj.bind`, `obj.fileno`, `obj.extractall`, or
dynamic expression in another file cannot inherit authority.

`ADR-0003-privileged-operation-boundaries.md` records the high-priority
Windows, notification, IPC, updater, and listener/descriptor decisions.
`ADR-0004-core-runtime-boundary.md` fixes the native core/script split and
supersedes v4 where a core implementation detail had been mistaken for a script
operation. Notification retains the language-level `baas/notify` action and
response contract through `NotifyHost`; only platform callbacks and UI remain
inside the adapter.

All 15,469 observed sites are preserved. Generator 5.0 aggregates them into 4,340
operations and 5,060 operation/source decisions. Compared with the checked-in
v4 baseline, 3,916 operation IDs remain shared, 403 less-specific identities
retire, and 447 proven identities are created. One operation can now have more
than one decision in the same source scope when exact per-file evidence gives
different owners; only those decisions receive a deterministic ID suffix.

## v2 unresolved baseline analysis

The v2 starting point was 1,842 unresolved decisions across 3,467 observed
sites. Its rule and call-form split was:

| Dimension | v2 decisions | v2 sites |
| --- | ---: | ---: |
| `unresolved-expression-v2` | 1,512 | 2,884 |
| `dynamic-expression-v2` | 330 | 583 |
| member | 1,425 | — |
| chained | 323 | — |
| name | 87 | — |
| dynamic | 7 | — |

The largest unresolved member families by observed site were `get` (329),
`append` (203), `addWidget` (111), `split` (99), `strip` (85), `replace` (77),
`startswith` (60), `setattr` (60), `tr` (57), `exists` (54), `lower` (53), and
`connect` (53). This separated provable container/string/path operations from
still-unknown GUI owners, pytest fixtures, configuration objects, and runtime
call results.

The source-scope split was 560 legacy-GUI, 520 C++-service, 402 script-runtime,
163 test, 149 deployment-tooling, and 48 migration-tooling decisions.

## Snapshot and deterministic evidence

| Evidence | Value |
| --- | --- |
| `baas-dev` commit | `75bbacb545bc87e9510d85cbe8034f9180397004` |
| Python source files | 569 |
| Unique operations | 4,340 |
| Observed operation sites | 15,469 |
| Operation/source decisions | 5,060 |
| Unresolved disposition decisions | 0 |
| Unresolved observed sites | 0 |
| Host-binding-required decisions | 145 |
| Host contract gaps | 0 |
| Dynamic operations retained | 254 |
| Parse errors | 0 |
| Source snapshot SHA-256 | `76f974d77e7c63034296acefb86a707e150c68602db007f4dbec891c66f712ec` |
| Rules SHA-256 | `513b43e1d5291be727794410ece393667dc3588317d6cfdd9abf70cc1675874c` |
| JSON report SHA-256 | `2228b3294e84c2efcc01b7c041fd1692081b02b140541ce26ac1e6196f0f2c13` |

## Before/after unresolved inventory

| Measure | Taxonomy v2 | Taxonomy v3 | v4 baseline | Hardened v4 | Audited modules | Generator 4.1 | Taxonomy v5 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Unresolved decisions | 1,842 | 1,279 | 240 | 183 | 119 | 109 | 0 |
| Unresolved observed sites | 3,467 | 2,302 | 378 | 304 | 183 | 171 | 0 |
| Operations with unresolved disposition | 1,649 | 1,170 | 240 | 183 | 119 | 109 | 0 |
| Static unresolved decisions | 1,512 | 1,008 | 167 | 116 | 76 | 69 | 0 |
| Dynamic unresolved decisions | 330 | 271 | 73 | 67 | 43 | 40 | 0 |
| Dynamic operations retained | 296 | 257 | 257 | 258 | 258 | 254 | 254 |

No unresolved call was assigned from a symbol-tail guess. The final 109 v4.1
gaps became `CPP_RUNTIME_INTERNAL` only after `core/*` received its independent
source scope; the six mixed core/module identities were then audited on the
module side before strict mode was allowed to pass.

## Disposition inventory

| Disposition | Decisions | Observed sites |
| --- | ---: | ---: |
| `CPP_RUNTIME_INTERNAL` | 761 | 2,177 |
| `HOST_BINDING_REQUIRED` | 145 | 1,201 |
| `SCRIPT_LANGUAGE_OR_MODULE` | 546 | 1,900 |
| `CPP_SERVICE_INTERNAL` | 1,256 | 3,438 |
| `TAURI_UI_REPLACED` | 1,469 | 4,298 |
| `MIGRATION_TOOLING_ONLY` | 548 | 1,541 |
| `TEST_ONLY` | 335 | 914 |

## Assigned contracts and Phase 0 gate

The eleven taxonomy-v3 Process, HTTP, and Socket gaps retain stable reserved
capability, proposed C++ interface, owner, and parity identities, while taxonomy
v5 correctly classifies their observed `core/*` occurrences as
`CPP_RUNTIME_INTERNAL`. They are no longer claimed as legacy script Host
requirements. Hardened v4 exact exceptions continue to assign Device and Notify
Host contracts while keeping listener, raw IPC, shortcut, and updater work
outside the script Host surface. All Host decisions remain `INVENTORIED`; no
binding or parity implementation is claimed.

Strict mode independently checks unresolved dispositions, Host contract gaps,
and parse failures. It now returns zero for all three gates, completing the
operation-inventory item in Phase 0. Other Phase 0 and later implementation,
parity, service, and platform-smoke items remain incomplete. The generated
matrix is an authoritative work queue, not proof that bindings or parity tests
exist.
