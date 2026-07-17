# Runtime BAAS Script catalog

`BAAS_runtime_script_catalog` turns one pinned, external `scripts` repository
view into an immutable task-routing catalog. It validates metadata only. It
does not execute a task, report task acceptance, construct a native path, or
embed scripts/resources in the executable.

## Pin and capability boundary

The loader accepts only a pathless `RuntimeRepositoryReadView` whose repository
id is exactly `scripts`. The caller must also provide the expected generation
and scripts commit in `RuntimeScriptCatalogPin`. Both values are compared with
the view before the exact root entry `baas-script-catalog.json` is read and
again before publication. A stale generation, stale commit, resources view, or
missing root entry fails closed.

Generation and commit are deliberately not fields inside the catalog file.
Both identify content that includes the catalog bytes, so embedding either
derived identity into those bytes would create an unsatisfiable content-hash
cycle. The immutable read capability and explicit caller pin provide the
binding without that cycle. The successful catalog retains both identities.

The repository layer verifies the tree manifest and every payload digest using
anchored handles. Catalog references are matched against that verified entry
set byte-for-byte. The catalog target never accepts or returns a filesystem
path.

## Schema

The exact schema is `baas.runtime-script.catalog/v2`. Schema v2 makes
`package_root` mandatory; v1 lacked enough information to distinguish relative
package paths and is rejected instead of being guessed. This contract is still
pre-release and had not been published as an executable catalog. Unknown fields, duplicate
JSON object keys, non-UTF-8 input, decoded NUL, unsupported JSON value kinds,
invalid versions, duplicate routes, and missing references are errors.

```json
{
  "schema": "baas.runtime-script.catalog/v2",
  "tasks": [
    {
      "run_mode": "solve",
      "task": "main_story",
      "package_root": "packages/story",
      "package_manifest": "packages/story/baas.package.json",
      "entry_module": "main",
      "entry_export": "run",
      "language_version": {"major": 1, "minor": 0},
      "host_modules": [
        {
          "module": "baas/log",
          "major": 1,
          "min_minor": 0,
          "capabilities": ["log.emit"]
        }
      ],
      "legacy_aliases": ["start_main_story"]
    }
  ]
}
```

`package_root` is the exact canonical repository directory that contains one
package. `package_manifest` must be exactly
`<package_root>/baas.package.json`; neither value is derived from task input.
Manifest module paths and `entry_module` are canonical paths relative to that
root. The catalog verifies the exact joined entry source without stripping a
suffix, probing a parent, or case-folding. Host module ids,
API requirements, and dotted lowercase capability ids are declarative. A later
execution boundary must still negotiate the language version and resolve the
declared Host contract; catalog acceptance is not execution acceptance.

## Routing and legacy aliases

The lookup key is exactly `{run_mode, requested_task}`. Lookup never folds case,
matches a prefix, expands a wildcard, probes another path, guesses an alias, or
falls back to a default task. Canonical task names and every legacy alias share
the same collision domain for one run mode.

The current nine compatibility aliases are repository data, not native code:

| Requested legacy task | Canonical task |
| --- | --- |
| `start_hard_task` | `explore_hard_task` |
| `start_normal_task` | `explore_normal_task` |
| `start_fhx` | `de_clothes` |
| `start_main_story` | `main_story` |
| `start_group_story` | `group_story` |
| `start_mini_story` | `mini_story` |
| `start_explore_activity_story` | `explore_activity_story` |
| `start_explore_activity_mission` | `explore_activity_mission` |
| `start_explore_activity_challenge` | `explore_activity_challenge` |

The executable defines only the schema and validator. Removing or changing an
alias therefore requires a new verified repository catalog, not a C++ rebuild.

## Bounds, cancellation, and deterministic output

Independent limits cover manifest bytes, JSON depth/nodes, tasks, aliases,
Host modules, capabilities, individual/aggregate string bytes, module
specifier shape, and aggregate parse/validation work. Limits are validated
against hard ceilings. Repository reads, UTF-8/JSON stages, validation loops,
and publication boundaries observe `std::stop_token`; allocation exhaustion is
reported separately.

Successful tasks are sorted by `{run_mode, canonical_task}`. Aliases, Host
modules, and capabilities are sorted bytewise. Source ordering cannot alter
the resulting immutable catalog or lookup semantics.

## Build and CI

```text
-DBUILD_RUNTIME_SCRIPT_CATALOG=ON
-DBUILD_RUNTIME_SCRIPT_CATALOG_TESTS=ON
```

Foundation CI builds and runs the real pinned-repository tests on Windows,
Linux, and macOS in Debug and Release. Android arm64-v8a and x86_64 jobs compile
the production-only target. Test fixtures contain the compatibility alias data;
production C++ does not.
