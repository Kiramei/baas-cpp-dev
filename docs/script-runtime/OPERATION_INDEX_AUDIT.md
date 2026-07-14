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

The first command returned 0. The strict command returned 1 because unresolved
dispositions and unassigned Host contracts remain. Two normal generations and
the strict report were byte-identical.

## Taxonomy v3 resolution boundary

Schema v2 and identity version 1 remain unchanged. Taxonomy v3 adds a bounded
static owner-resolution pass before the existing source-scope disposition
rules. It accepts only facts visible in the AST or exact rules in
`operation_rules.v3.json`:

- absolute and relative imports, aliases, local wildcard exports, definitions,
  and conservative lexical rebinding/branch merges;
- declared concrete annotations, local class constructors, literal/container
  types, `*args`, `**kwargs`, exact destructuring, and positive `isinstance`
  narrowing inside the guarded branch;
- exact constructor, factory-return, and property-return rules for selected
  builtins and standard-library APIs, plus `requests.Response`.

Unknown call results, subscripts, ambiguous unions, rebinding across possible
control-flow paths, `getattr` with runtime names, and untyped parameters remain
dynamic or unresolved. Module export discovery is AST-only; external wildcard
imports are not expanded. No naming heuristic such as assuming every `config`
is a dictionary is used.

All 15,469 observed sites are preserved. v3 aggregates them into 4,319
operations and 4,908 operation/source-scope decisions. Of the v2 IDs, 4,020
remain shared with byte-identical identities and no shared ID changed meaning;
525 old unresolved identities were retired and 299 proven identities were
created.

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
| Unique operations | 4,319 |
| Observed operation sites | 15,469 |
| Operation/source-scope decisions | 4,908 |
| Unresolved disposition decisions | 1,279 across 1,170 operations |
| Unresolved observed sites | 2,302 |
| Host-binding-required decisions | 343 |
| Host contract gaps | 11 |
| Dynamic operations retained | 257 |
| Parse errors | 0 |
| Source snapshot SHA-256 | `76f974d77e7c63034296acefb86a707e150c68602db007f4dbec891c66f712ec` |
| Rules SHA-256 | `d8dbb97b8bfe63d392a20bb4ee0fdc073f94e5beb0980e6c8fc70bd74ba477eb` |
| JSON report SHA-256 | `9249cb288b78fb4503409fba94f330bf6f95a8ab0ecdf386e982e26fc91f9684` |

## Before/after unresolved inventory

| Measure | Taxonomy v2 | Taxonomy v3 | Delta |
| --- | ---: | ---: | ---: |
| Unresolved decisions | 1,842 | 1,279 | -563 |
| Unresolved observed sites | 3,467 | 2,302 | -1,165 |
| Operations with unresolved disposition | 1,649 | 1,170 | -479 |
| Static unresolved decisions | 1,512 | 1,008 | -504 |
| Dynamic unresolved decisions | 330 | 271 | -59 |
| Dynamic operations | 296 | 257 | -39 |

The dynamic reduction is limited to call results whose exact return type is
now proven by a v3 rule; genuinely runtime-dependent forms remain unresolved.
The remaining v3 call-form split is 925 member, 264 chained, 83 name, and 7
dynamic decisions. The remaining source-scope split is 471 legacy-GUI, 335
C++-service, 240 script-runtime, 108 deployment-tooling, 96 test, and 29
migration-tooling decisions.

## Disposition inventory

| Disposition | Decisions | Observed sites |
| --- | ---: | ---: |
| `HOST_BINDING_REQUIRED` | 343 | 1,994 |
| `SCRIPT_LANGUAGE_OR_MODULE` | 814 | 3,080 |
| `CPP_SERVICE_INTERNAL` | 920 | 2,819 |
| `TAURI_UI_REPLACED` | 1,005 | 3,499 |
| `MIGRATION_TOOLING_ONLY` | 316 | 1,127 |
| `TEST_ONLY` | 221 | 637 |
| `EXTERNAL_DEPENDENCY` | 10 | 11 |
| `UNRESOLVED` | 1,279 | 2,302 |

## Strict gaps and Phase 0 gate

The eight v2 Host gaps remain: `psutil.Process`, `psutil.process_iter`,
`requests.get`, `requests.post`, `socket.socket`, `subprocess.Popen`,
`subprocess.check_output`, and `subprocess.run`. Proven socket annotations also
expose three previously hidden capabilities: `socket.socket.recv`,
`socket.socket.send`, and `socket.socket.setblocking`. They remain explicit
unassigned Host contracts rather than being relabeled as completed work.

Strict mode independently checks unresolved dispositions, Host contract gaps,
and parse failures. It therefore remains non-zero with 1,279 unresolved
decisions and 11 Host gaps. Phase 0 remains incomplete. The generated matrix is
an authoritative work queue, not proof that bindings or parity tests exist.
