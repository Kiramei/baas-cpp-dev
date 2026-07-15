# Service file resource store

`FileResourceStore` is the production, project-root-backed implementation of
the sync `ResourceStore` contract. It exposes only JSON resources whose
ownership and projection are unambiguous.

## Resource projection

| Sync resource | Filesystem path |
| --- | --- |
| `config(id)` | `<project-root>/config/<id>/config.json` |
| `event(id)` | `<project-root>/config/<id>/event.json` |
| `gui` | `<project-root>/config/gui.json` |
| `static_data` | `<project-root>/config/static.json` |
| `setup_toml` | unsupported; returns `not_found` |

The config list is a sorted JSON array. An identifier is listed only when it is
a safe direct child directory of `config` and both `config.json` and
`event.json` are regular files. Reparse points/symlinks, traversal spellings,
path separators, drive separators, control bytes, invalid UTF-8, and identifiers
over the configured byte limit are rejected. Windows also rejects trailing-dot,
trailing-space, reserved-device, case, and 8.3 aliases that would address an
existing resource with a second cache key.

`setup.toml` is deliberately not parsed as JSON or reduced to ad-hoc key/value
pairs. Its migration, projection, defaulting, and merge rules are application
owned. A dedicated TOML projection adapter can be composed later without
silently changing this JSON store's contract.

## Validation and capacity

Every pull and external refresh reads the file as bytes, validates UTF-8, parses
strict JSON with duplicate-key rejection, and enforces `ResourceStoreLimits` for
input bytes, JSON depth, JSON nodes, resource count, resource-id bytes, origin
bytes, subscribers, and patch operations. Malformed, over-depth, over-node, or
over-size documents never enter the visible cache. Cancellation is checked
before filesystem or mutation work.

## Patch and commit boundary

Patch behavior matches `InMemoryResourceStore`: `add`, `remove`, and `replace`
use JSON Pointer paths, including root replacement; a stale timestamp returns a
conflict snapshot; invalid operations are isolated; and `static_data` is
immutable. Validation, patching, serialization, and update construction finish
before the writer is invoked while the per-store state lock serializes competing
patches.

All default reads and writes start from a persistent non-reparse/no-follow
project-root handle. Windows opens every component relative to that handle with
`NtCreateFile`, keeps the directory chain open without delete sharing, reads
from the final verified regular-file handle, and commits a sibling temporary
file with relative `NtSetInformationFile` after `FlushFileBuffers`. POSIX walks
every component from the persistent root FD with `openat(O_NOFOLLOW)`, reads the
final `fstat`-verified regular-file FD, writes a sibling temporary with `openat`,
calls `fsync` on it, commits with same-directory `renameat`, and calls `fsync` on
the parent directory. A pre-commit failure returns `not_committed`; cache and
subscribers remain unchanged. Once replacement/rename succeeds the file is
visible and the result is committed. A later POSIX directory fsync/close failure
is reported by the injected writer contract as
`committed_durability_uncertain`, so cache and publication still advance with
the already-visible file instead of diverging from disk.

Cache mutation and publication occur only after a committed writer result.
Subscriber callbacks run after the state lock is released.

## Subscriptions and external refresh

Subscription destruction first closes admission and removes the slot. It then
waits for callbacks that already entered, forming an entry barrier. A callback
may destroy its own subscription without waiting on itself; no later publication
can enter it. Callbacks run without the main state lock, may re-enter the store,
and exceptions from one callback do not stop other subscribers.

`refresh_and_publish(key, origin)` is the watcher integration point. It applies
the same path and JSON validation, updates the cache only for a changed valid
document, and publishes one root `replace` operation with the supplied bounded
origin. No filesystem watcher is embedded in this adapter; a host watcher calls
this method after observing an external change.

## Build and verification

Enable `BUILD_SERVICE_FILE_RESOURCE_STORE` for the static adapter target or
`BUILD_SERVICE_FILE_RESOURCE_STORE_TESTS` for the adapter plus
`BAAS_service_file_resource_store_tests`. The native suite covers safe listing
and pulls, traversal/reparse-point rejection, malformed and capacity-bounded
JSON, patch conflicts, pre-commit writer failure, post-commit durability
uncertainty, concurrent patches, subscription barriers and self-unsubscription,
callback exception isolation, external refresh, Windows physical aliases,
anchored-handle rename blocking, and Windows/POSIX ancestor-swap fail-closed
behavior.

The implementation is excluded from the legacy `BAAS_CORE_SOURCES` glob and is
compiled only through `cmake/ServiceFileResourceStore.cmake`.
