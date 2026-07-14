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

The first command returns 0. The strict command returns 1 because unresolved
dispositions remain. Two normal generations and the strict report are
byte-identical.

## Taxonomy v4 resolution and privileged boundary

Schema v2 and identity version 1 remain unchanged. The bounded static
owner-resolution pass runs before source-scope disposition defaults. It accepts
only facts visible in the AST or exact rules in `operation_rules.v4.json`:

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

Unknown call results, subscripts, ambiguous unions, rebinding across possible
control-flow paths, `getattr` with runtime names, and untyped parameters remain
dynamic or unresolved. Module export discovery is AST-only; external wildcard
imports are not expanded. Receiver suffixes and final identifier segments are
never ownership evidence.

Five non-script source scopes already fix the migration boundary independently
of receiver ownership: C++ service, legacy GUI/Tauri replacement, tests,
deployment tooling, and migration tooling. Script-runtime dynamic/unresolved
calls remain strict gaps unless an explicit source-and-symbol boundary applies.
Source-qualified rules are classified per file before equal conclusions are
aggregated, so an unrelated `obj.bind`, `obj.fileno`, `obj.extractall`, or
dynamic expression in another file cannot inherit authority.

`ADR-0003-privileged-operation-boundaries.md` records the high-priority
Windows, notification, IPC, updater, listener/descriptor, and video codec
decisions. Notification retains the language-level `baas/notify` action and
response contract through `NotifyHost`; only platform callbacks and UI remain
inside the adapter.

All 15,469 observed sites are preserved. Hardened v4 aggregates them into 4,363
operations and 4,989 operation/source decisions. Compared with the checked-in
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
| Unique operations | 4,363 |
| Observed operation sites | 15,469 |
| Operation/source decisions | 4,989 |
| Unresolved disposition decisions | 183 across 183 operations |
| Unresolved observed sites | 304 |
| Host-binding-required decisions | 349 |
| Host contract gaps | 0 |
| Dynamic operations retained | 258 |
| Parse errors | 0 |
| Source snapshot SHA-256 | `76f974d77e7c63034296acefb86a707e150c68602db007f4dbec891c66f712ec` |
| Rules SHA-256 | `0a6bf758119537353c3e17926409a0bf1d273098e721fe6b2f573242d3bb7573` |
| JSON report SHA-256 | `c6223ebdf1117963834c56751815a6fff835378b2b59939049ca84891518598c` |

## Before/after unresolved inventory

| Measure | Taxonomy v2 | Taxonomy v3 | v4 baseline | Hardened v4 | baseline→hardened |
| --- | ---: | ---: | ---: | ---: | ---: |
| Unresolved decisions | 1,842 | 1,279 | 240 | 183 | -57 |
| Unresolved observed sites | 3,467 | 2,302 | 378 | 304 | -74 |
| Operations with unresolved disposition | 1,649 | 1,170 | 240 | 183 | -57 |
| Static unresolved decisions | 1,512 | 1,008 | 167 | 116 | -51 |
| Dynamic unresolved decisions | 330 | 271 | 73 | 67 | -6 |
| Dynamic operations | 296 | 257 | 257 | 258 | +1 |

The remaining 183 decisions are all script-runtime calls: 111 member, 65
chained, 5 name, and 2 dynamic decisions (116 statically unresolved and 67
dynamic). No unresolved call was assigned from a symbol-tail guess.

## Disposition inventory

| Disposition | Decisions | Observed sites |
| --- | ---: | ---: |
| `HOST_BINDING_REQUIRED` | 349 | 1,985 |
| `SCRIPT_LANGUAGE_OR_MODULE` | 832 | 2,983 |
| `CPP_SERVICE_INTERNAL` | 1,259 | 3,438 |
| `TAURI_UI_REPLACED` | 1,476 | 4,298 |
| `MIGRATION_TOOLING_ONLY` | 549 | 1,541 |
| `TEST_ONLY` | 335 | 914 |
| `EXTERNAL_DEPENDENCY` | 6 | 6 |
| `UNRESOLVED` | 183 | 304 |

## Assigned contracts and Phase 0 gate

The eleven taxonomy-v3 Process, HTTP, and Socket gaps retain stable capability,
proposed C++ interface, owner, and parity identities. Hardened v4 additionally
assigns Device and Notify Host contracts while keeping listener, raw IPC,
shortcut, and updater work outside the script Host surface. All Host decisions
remain `INVENTORIED`; no binding or parity implementation is claimed.

Strict mode independently checks unresolved dispositions, Host contract gaps,
and parse failures. It therefore remains non-zero with 183 unresolved decisions
and zero Host contract gaps. Phase 0 remains incomplete. The generated matrix
is an authoritative work queue, not proof that bindings or parity tests exist.
