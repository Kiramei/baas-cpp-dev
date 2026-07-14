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

## Schema v2 and identity stability

Schema v2 keeps every discovered call, registry, route, and dispatch operation,
but separates two concepts previously conflated as `UNCLASSIFIED`:

- `source_scope` describes where a call occurs: script runtime, generated
  resources, C++ service migration, legacy GUI, tests, migration tooling, or
  deployment tooling;
- `disposition` describes what migration boundary applies:
  `HOST_BINDING_REQUIRED`, `SCRIPT_LANGUAGE_OR_MODULE`,
  `CPP_SERVICE_INTERNAL`, `TAURI_UI_REPLACED`, `MIGRATION_TOOLING_ONLY`,
  `TEST_ONLY`, `EXTERNAL_DEPENDENCY`, or `UNRESOLVED`.

One operation can occur in several source scopes, so JSON and the generated
matrix contain stable `operation-id@SOURCE_SCOPE` decisions. Dynamic/chained
expressions and statically unknown owners remain `UNRESOLVED` even in GUI,
test, and tooling directories. A directory name alone never closes a dynamic
or security-sensitive migration decision.

The report has schema version 2 but retains identity version 1. Of the 4,556 v1
identities, 4,448 remain byte-for-byte identical and retain the same ID. No
shared identity changed ID. Correct absolute/relative alias resolution plus
conservative branch/rebinding merges retired 108 incorrect identities and
produced 97 corrected identities, yielding 4,545 unique operations without
changing the 15,469 observed sites.

## Snapshot and deterministic evidence

| Evidence | Value |
| --- | --- |
| `baas-dev` commit | `75bbacb545bc87e9510d85cbe8034f9180397004` |
| Python source files | 569 |
| Unique operations | 4,545 |
| Observed operation sites | 15,469 |
| Operation/source-scope decisions | 5,128 |
| Unresolved disposition decisions | 1,842 across 1,649 operations |
| Host-binding-required decisions | 328 |
| Host contract gaps | 8 |
| Dynamic operations retained | 296 |
| Parse errors | 0 |
| Source snapshot SHA-256 | `76f974d77e7c63034296acefb86a707e150c68602db007f4dbec891c66f712ec` |
| Rules SHA-256 | `37dee8eea0163fc3e93e15dd9a37b31e7bd0e4a7531026ba61393c283d7ef431` |
| JSON report SHA-256 | `9a476f1662b98ec8c2200f7dbddb6dc80058194733e1d10f23d1e08dcb448be6` |

## Source-scope inventory

| Source scope | Decisions | Observed sites | Meaning |
| --- | ---: | ---: | --- |
| `SCRIPT_RUNTIME` | 1,508 | 5,463 | Python automation/runtime requiring Host, language, module, or dependency decisions |
| `CPP_SERVICE` | 1,331 | 3,435 | Python service implementation to migrate inside the C++ service |
| `LEGACY_GUI` | 1,498 | 4,298 | Legacy PyQt/UI implementation replaced at the Tauri boundary when statically resolved |
| `TEST` | 324 | 914 | Python-only test/support evidence when statically resolved |
| `DEPLOYMENT_TOOLING` | 342 | 1,045 | Packaging/deployment-only code when statically resolved |
| `MIGRATION_TOOLING` | 125 | 314 | Developer/migration tools when statically resolved |

The 253 generated `src` Python files contain data declarations but no indexed
calls, so they contribute to the source inventory without a scope decision.

## Disposition inventory

| Disposition | Decisions | Observed sites | Interpretation |
| --- | ---: | ---: | --- |
| `HOST_BINDING_REQUIRED` | 328 | 1,946 | Script-visible capability crossing a C++ Host boundary |
| `SCRIPT_LANGUAGE_OR_MODULE` | 768 | 2,813 | Language intrinsic, standard library behavior, or script/module rewrite; not a fabricated Host API |
| `CPP_SERVICE_INTERNAL` | 811 | 2,399 | C++ service implementation detail, not a script Host binding |
| `TAURI_UI_REPLACED` | 938 | 3,311 | Statically resolved legacy GUI work owned by the Tauri replacement boundary |
| `MIGRATION_TOOLING_ONLY` | 270 | 1,025 | Resolved deployment/developer tooling outside production script execution |
| `TEST_ONLY` | 161 | 497 | Resolved Python test/support operation |
| `EXTERNAL_DEPENDENCY` | 10 | 11 | Dependency replacement decision remains, without inventing a C++ binding |
| `UNRESOLVED` | 1,842 | 3,467 | Dynamic, rebound, shadowed, or otherwise unknown call owner |

Broad v1 rules for `builtins`, `json`, and similar standard-library operations
no longer manufacture Host bindings. Direct device libraries such as
`adbutils`, `uiautomator2`, `pyautogui`, and `mss` are explicitly routed to the
DeviceHost. Process/network calls are security-sensitive Host candidates and
remain gaps until their contract is assigned.

## Strict gaps and Phase 0 gate

The eight Host contract gaps are `psutil.Process`, `psutil.process_iter`,
`requests.get`, `requests.post`, `socket.socket`, `subprocess.Popen`,
`subprocess.check_output`, and `subprocess.run`. They have an explicit
`HOST_BINDING_REQUIRED` disposition but intentionally retain unassigned Host
binding, owner, and parity fields. The other 320 Host decisions have proposed
contracts only; `INVENTORIED` does not mean implemented or parity-complete.

Strict mode independently checks unresolved dispositions, Host contract gaps,
and parse failures. It therefore remains non-zero with 1,842 unresolved
scope decisions and eight Host gaps. Phase 0 remains incomplete alongside the
golden-trace, full performance, and smoke-test gates. The generated matrix is
an authoritative work queue, not proof that bindings or parity tests exist.
