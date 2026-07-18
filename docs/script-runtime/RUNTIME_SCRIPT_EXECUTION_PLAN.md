# Runtime BAAS Script execution plan

`BAAS_runtime_script_execution_plan` is the immutable composition boundary
between a resolved catalog route and a later task backend. It does not execute
source. It reads the catalog-selected package manifest, validates the
source-only schema-1/schema-2 subset, loads the exact package graph, and
publishes owned inputs for later evaluator and Host-runtime construction.

## Identity and lifetime

The input `RuntimeScriptCatalogResolution` retains its originating catalog and
exposes that catalog's generation and scripts commit. Both must exactly match
the pathless `RuntimeRepositoryReadView` before the package manifest is read
and again before publication. Resolutions and views from different snapshots
cannot be combined merely because task names match.
The catalog also supplies an explicit `package_root`; the package manifest must
be exactly `<package_root>/baas.package.json`. Manifest paths remain relative
to that root and are joined lexically once, without stripping suffixes or
probing parents.

A successful plan owns the route, descriptor, package identity/version, exact
module metadata, source bytes, validated graph, Host requirements, and
capabilities. For schema 2 it also owns the canonical sorted set of procedure
IDs forming the package's explicit procedure closure. It can outlive the
catalog, resolution, read bundle, repository
snapshot, and backing cache handles. It never contains a native path, user
configuration, resource payload, or compiled-in automation source.

## Strict source-only manifest subset and procedure closure

The selected `package_manifest` uses manifest schema 1 or 2 from
`PACKAGE_VERSIONING.md`, with these constraints:

- `resources` and `profiles` must be empty;
- resource limits, when present, must be zero;
- detached `baas.package.sig` files and inline signature fields are rejected in
  repository-bundled mode because this target does not pretend to verify them;
- every unknown or duplicate field, unsupported JSON value, non-UTF-8 byte,
  decoded NUL, invalid semantic version, path alias, and case-fold collision is
  rejected;
- every module path must be the exact `.baas` path produced from its canonical,
  extensionless package module id;
- module sizes and lowercase SHA-256 values must exactly match the pinned
  repository tree entry.

Schema 1 remains compatible for packages that do not require or import
`baas/procedure`. A schema-1 package that declares that Host requirement fails
closed with `RSE028_PROCEDURE_REQUIREMENTS_MISSING`; it cannot activate an
unbounded implicit procedure namespace. Schema 2 adds the required exact
top-level `procedures` array. Its entries are nonempty canonical lowercase
logical IDs, bounded by independent count/string/work limits, deduplicated with
ASCII case-collision rejection, and published as an immutable sorted set.
Declaring/importing `baas/procedure` requires a nonempty procedure closure;
without that Host requirement, the closure must be empty so metadata cannot
grant an implicit privilege. This closure names definitions that a later
activation stage must load and bind; it is not itself definition evidence.

Repository-bundled mode additionally requires a native
`RuntimeScriptRepositoryTrustEvidence`. It attests that the signed update plan
covered the exact generation and scripts commit; the read view then verifies
the signed tree-manifest hash and each exact entry size/digest. A read view by
itself, absent/mismatched evidence, or browser-provided strings cannot activate
a plan. This native capability is not serializable and never contains a URL,
ref, key, or native path. Standalone package artifacts remain governed by the
detached Ed25519 contract and are rejected here until that verifier is composed.
Only native/service composition may supply this evidence, after
`RuntimeRepositoryTrustedPlanVerifier` has verified the signed update and the
repository owner has published that exact generation. A permissive in-process
implementation is outside the trusted boundary and is forbidden in production;
test doubles are private to native fixtures.

## Catalog consistency and package confinement

Manifest and catalog information is never unioned or guessed:

- language major must match and the catalog-selected minor must satisfy the
  manifest minimum minor;
- entrypoint must exactly equal the catalog entry module's `.baas` path;
- Host module ids, majors, and minimum minors must match exactly;
- manifest capabilities must exactly equal the sorted union of catalog Host
  capabilities;
- the actually imported Host set must exactly equal the declared Host set.

The package loader's strict overload accepts the exact module manifest as an
allowlist. Imports outside the allowlist fail before repository lookup, even if
another scripts-tree manifest entry exists. Every declared source must be
reachable from the entry and parsed/semantically analyzed. Output follows the
deterministic dependency-before-importer order.
Two packages may each contain `main.baas`; their explicit repository logical
paths remain different and an import cannot cross from one root into the other.

## Bounds and cancellation

Independent hard-capped limits cover manifest bytes, JSON depth/nodes, module,
procedure, and Host counts, capabilities, strings, validation work, and all
existing parser/semantic/graph budgets. Manifest `source_bytes` and
`module_count` may
only lower caller limits. One `std::stop_token` crosses repository reads,
JSON/UTF-8 validation, consistency loops, source analysis, graph validation,
and publication.

Errors use stable `RSEnnn_*` names. Package failures retain the narrow
`RSPnnn_*` reason, canonical module id, and structured language diagnostics.

## Build and CI

```text
-DBUILD_RUNTIME_SCRIPT_EXECUTION_PLAN=ON
-DBUILD_RUNTIME_SCRIPT_EXECUTION_PLAN_TESTS=ON
```

Foundation CI tests Windows, Linux, and macOS in Debug and Release. Android
arm64-v8a and x86_64 compile the production closure. Fixtures alone contain
scripts, manifests, resources, and compatibility alias data.
