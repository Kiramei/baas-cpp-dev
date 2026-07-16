# Service runtime repository owner

`ServiceRuntimeRepositoryOwner` is the C++ service boundary for the immutable
runtime repository activation metadata described in `RUNTIME_REPOSITORIES.md`.
The service remains a reader: this owner does not fetch, update, publish, roll
back, or reload repository state.

## Startup contract

During `ServiceApplication::open`, the owner examines
`<project-root>/.baas-updater/runtime-repositories/current.json`.

- If `current.json` is absent, composition continues with repository phase
  `unavailable`.
- If `current.json` exists, the normal bounded activation reader must validate
  the pointer, immutable snapshot, generation identity, field set, and anchored
  paths. Any malformed, unsafe, or tampered activation fails composition.
- A successful activation is retained as a
  `shared_ptr<const RuntimeRepositorySnapshot>`. The owner has no reload API.

Activation pins pointer and snapshot metadata only. `activate()` deliberately
does not open repository object directories or manifests, so `phase=pinned`
does not claim that payload bytes have been revalidated or that a future
resource/script consumer is ready. Such consumers must use anchored opens and
validate the selected tree manifest before admitting payload bytes.

The absence check happens once. If a publisher creates or advances
`current.json` later, the running service remains unavailable or pinned to its
startup generation until it is restarted. Consumers retain the shared pin,
never a borrowed descriptor or a path recovered from a later pointer read.

## Public health projection

The existing `/health` response adds `statuses.runtime.repository`. Its exact
service-owned fields are:

```json
{"phase":"unavailable"}
```

or:

```json
{"generation":"<64 lowercase hexadecimal characters>","phase":"pinned"}
```

The projection never contains the project root, state root, repository object
paths, manifest names, remote URLs, or credentials. No update or reload HTTP
route is installed.

## Lifecycle invariant

One `ServiceRuntimeRepositoryOwner` belongs to one `ServiceApplication`.
Readiness snapshots and all future repository consumers refer to that same
owner. Removing or advancing the mutable activation files cannot change an
already retained snapshot. A later hot-reload design must use a separate,
explicit generation handoff protocol; it must not mutate this owner in place.
