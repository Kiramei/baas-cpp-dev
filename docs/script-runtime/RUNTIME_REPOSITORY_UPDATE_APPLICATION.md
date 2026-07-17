# Standalone runtime repository update publisher

`BAAS_runtime_repository_update` is the desktop standalone/WebUI publication
entry point. It is a separate process from `BAAS_service`: the publisher owns
network retrieval and mutable generation publication, while the service remains
a read-only consumer pinned to one exact generation for its entire lifetime.
The Tauri desktop package may continue to use its Rust publisher and the
existing `BAAS_service` exact-generation launch path without linking libgit2.

The publisher is built only when
`BUILD_SERVICE_RUNTIME_REPOSITORY_UPDATE_APP=ON`. That option forces the signed
plan owner and the static libgit2 backend into the product link. Configuration
requires `BAAS_RUNTIME_REPOSITORY_TRUSTED_PUBLIC_KEY_HEX` to be one fixed
64-character lowercase Ed25519 public key. This is product trust material, not
runtime configuration: the executable does not accept a replacement key from
the command line, environment, project directory, WebUI, or signed envelope.
The target is desktop-only; Android keeps its independently tested libgit2
library boundary but does not claim a standalone publisher product.

## Process contract

The only production invocation is:

```text
BAAS_runtime_repository_update --project-root <directory> < signed-plan.json
```

The project root is application-owned. Repository state is always written below
`<project-root>/.baas-updater/runtime-repositories`; no state path, URL, ref,
commit, manifest hash, current generation, or trust key is accepted as an
argument. The publisher reads exactly one opaque signed envelope from standard
input, bounded to 128 KiB before it constructs the updater or claims store
ownership. The envelope contains the independently signed resources and scripts
plan described by `RUNTIME_REPOSITORIES.md`.

Startup composes, in order:

1. `StrictRuntimeRepositoryTreeValidator`;
2. `Libgit2RuntimeRepositoryFetchBackend`;
3. `RuntimeRepositoryTrustedPlanUpdateOwner` with the fixed product key;
4. crash recovery and trusted-state reconciliation;
5. signature verification, exact two-repository fetch, validation, and atomic
   generation publication.

The browser never invokes libgit2 directly and never supplies repository
policy. A launcher or authenticated native IPC owner may pass only the opaque
signed envelope over stdin. Killing the publisher is a crash, not a successful
cancellation; the next invocation must perform recovery before it can process a
new plan.

## Result and restart handoff

Standard output contains one bounded JSON result. Every application result
retains `disposition` and the pinned `generation`; success otherwise has only
`ok`. Failure additionally contains stable, non-sensitive error names for the
application, plan, policy state, and updater. It never returns a repository URL,
filesystem path, exception text, or libgit2 diagnostic. In particular,
`committed_durability_uncertain` is not safe to replay: the launcher re-reads
the current generation and completes the exact-generation handoff when it
matches the returned generation. Standard error contains only a local top-level
classification. Stable exit classes distinguish command-line, input, recovery,
signature/plan, update, and internal failures.

After success, the launcher re-reads the published current generation, starts
`BAAS_service` with `--runtime-repository-generation <generation>`, and accepts
completion only after `/health` reports that exact generation. A running service
does not reload or switch its retained resources/scripts bundle. Updating a
running WebUI installation therefore publishes first and then restarts the
service through the exact-generation handoff.

## Build evidence

The `runtime-repository-git2` workflow builds the publisher and its tests in
Debug and Release on Windows, Linux, and macOS with pinned static libgit2,
libsodium, cpp-httplib, miniz, and nlohmann-json dependencies. Tests cover
bounded stdin before side effects, stable help/version output, fixed-key
signature rejection before network retrieval, strict recovery-lock failure,
and redacted machine-readable errors. The existing backend and updater suites
continue to cover real libgit2 materialization, cancellation, resource limits,
atomic publication, and crash recovery.
