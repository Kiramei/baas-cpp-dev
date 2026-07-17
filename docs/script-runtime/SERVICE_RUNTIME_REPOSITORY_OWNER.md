# Service runtime repository owner

`ServiceRuntimeRepositoryOwner` is the C++ service boundary for the immutable
runtime repository activation metadata described in `RUNTIME_REPOSITORIES.md`.
The service remains a reader: this owner does not fetch, update, publish, roll
back, or reload repository state.

## Startup contract

During `ServiceApplication::open`, the owner examines
`<project-root>/.baas-updater/runtime-repositories/current.json` for the exact
64-lowercase-hex generation supplied at process launch.

- If `current.json` is absent, composition fails with the stable generation
  mismatch error.
- If `current.json` exists, the normal bounded activation reader must validate
  the pointer, immutable snapshot, generation identity, field set, and anchored
  paths. Any malformed, unsafe, or tampered activation fails composition.
- After that one activation, the owner compares the retained snapshot's
  generation directly with the expected launch generation. A different valid
  generation fails composition; the owner never re-reads `current.json` for
  this comparison.
- While holding the native publisher's `.trusted-plan-writer.lock`, the owner
  performs a strictly read-only comparison against the initialized
  `.trusted-plan-state.json`. Missing/malformed state, a different generation,
  an active writer, `.publish-journal.json`, or `.trusted-plan-journal.json`
  fails closed. Service startup never creates ownership, recovers publication,
  completes a policy journal, or rewrites the state store.
- A successful activation is retained as a
  `shared_ptr<const RuntimeRepositorySnapshot>`. Before any later startup
  dependency is composed, `ServiceApplication` opens and retains exactly one
  resources/scripts read bundle from that snapshot. The owner has no reload
  API.

Activation itself pins pointer and snapshot metadata only. The application
immediately calls the owner's `open_read_bundle()` during startup admission.
Opening succeeds only after both complete repository trees, manifests, and
payload digests validate from anchored native handles. The retained
capabilities expose no native path and return only manifest-listed, reverified
owned bytes. A failure occurs before remote-resource, signal, auth, worker, or
socket effects.

The absence check happens once. If a publisher creates or advances
`current.json` later, a successfully started service remains pinned to its
expected startup generation until it is restarted. Consumers retain the
shared pin, never a borrowed descriptor or a path recovered from a later
pointer read.

The factory also seals one native-only
`RuntimeScriptRepositoryTrustEvidence` containing the exact retained generation
and the scripts commit already bound into that generation digest. Its
`covers(generation, scripts_commit)` predicate accepts only that pair. A
snapshot-only embedding constructor deliberately exposes no evidence, and the
capability has no JSON/HTTP/Tauri representation. Consequently a browser,
script package, raw read bundle, or caller-provided generation cannot turn
repository readability into script execution trust.

## Public health projection

The existing `/health` response adds `statuses.runtime.repository`. A running
service has passed exact generation matching, so its service-owned fields are:

```json
{"generation":"<64 lowercase hexadecimal characters>","phase":"pinned"}
```

The projection never contains the project root, state root, repository object
paths, manifest names, remote URLs, or credentials. No update or reload HTTP
route is installed.

## Lifecycle invariant

One `ServiceRuntimeRepositoryOwner` and one read bundle belong to one
`ServiceApplication`. Readiness snapshots and all repository consumers refer
to that same generation. Removing or advancing the mutable activation files
cannot replace the already retained snapshot or bundle. A later hot-reload
design must use a separate, explicit generation handoff protocol; it must not
mutate this owner in place.

The publisher and service remain separate lifecycle stages. Only a completed
signed publication whose exact durable policy state is already committed may
be passed to a newly started service.
The service does not fetch, publish, or expose a repository update route.
