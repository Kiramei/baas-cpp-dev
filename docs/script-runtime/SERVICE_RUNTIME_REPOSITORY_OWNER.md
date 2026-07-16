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
- A successful activation is retained as a
  `shared_ptr<const RuntimeRepositorySnapshot>`. The owner has no reload API.

Activation pins pointer and snapshot metadata only. `activate()` deliberately
does not open repository object directories or manifests, so `phase=pinned`
does not itself claim payload readiness. Consumers call the owner's
`open_read_bundle()` to obtain resources and scripts capabilities bound to the
same retained generation. Those capabilities expose no native path and admit
only manifest-listed bytes verified from anchored native handles.

The absence check happens once. If a publisher creates or advances
`current.json` later, a successfully started service remains pinned to its
expected startup generation until it is restarted. Consumers retain the
shared pin, never a borrowed descriptor or a path recovered from a later
pointer read.

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

One `ServiceRuntimeRepositoryOwner` belongs to one `ServiceApplication`.
Readiness snapshots and all future repository consumers refer to that same
owner. Removing or advancing the mutable activation files cannot change an
already retained snapshot. A later hot-reload design must use a separate,
explicit generation handoff protocol; it must not mutate this owner in place.

The publisher and service remain separate lifecycle stages. Only a completed
publication's returned generation may be passed to a newly started service.
The service does not fetch, publish, or expose a repository update route.
