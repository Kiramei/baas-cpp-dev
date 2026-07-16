# Service file resource store

`FileResourceStore` is the production, project-root-backed implementation of
the sync `ResourceStore` contract. JSON resources retain their complete data;
`setup.toml` uses an explicit Python/Tauri-compatible projection.

## Resource projection

| Sync resource | Filesystem path |
| --- | --- |
| `config(id)` | `<project-root>/config/<id>/config.json` |
| `event(id)` | `<project-root>/config/<id>/event.json` |
| `gui` | `<project-root>/config/gui.json` |
| `static_data` | `<project-root>/config/static.json` |
| `setup_toml(global)` | `<project-root>/setup.toml` |

The config list is a sorted JSON array. An identifier is listed only when it is
a safe direct child directory of `config` and both `config.json` and
`event.json` are regular files. Reparse points/symlinks, traversal spellings,
path separators, drive separators, control bytes, invalid UTF-8, and identifiers
over the configured byte limit are rejected. Windows also rejects trailing-dot,
trailing-space, reserved-device, case, and 8.3 aliases that would address an
existing resource with a second cache key.

`setup_toml` accepts no id or the exact compatibility id `global`; every public
entry point canonicalizes both spellings to the single cache/publication key
`global`. Pull returns
the seven-field Python `ConfigManager._project_setup_toml` view:
`transport`, `channel`, `updateMethod`, `repoUrl`, `shaMethod`, `mirrorcCdk`,
and `gitBackend`. Current snake-case, Tauri camel-case, and the relevant legacy
`[General]`/`[URLs]` aliases follow Python precedence. Invalid transport falls
back to `websocket`, channels normalize to `stable|dev`, and repository URLs
are channel-aware. Legacy direct, proxy, and CDN repository URLs reverse-map to
the same update methods that generate them for both stable and dev channels.

Setup patches still use the normal JSON Patch contract, but commit is not a
JSON serialization. The store updates only canonical keys in `[general]` and
preserves unrelated TOML tables, arrays-of-tables, comments, unknown fields,
quoted table/key spellings, multiline basic/literal strings, inline comments,
and the source newline convention. A cross-line lexer locates complete TOML
statements; an existing projected assignment changes only its value span, so
formatting and trailing comments remain byte-for-byte stable. Unsupported
syntax in a projected value, duplicate projected keys, invalid value types, or
a transport outside `websocket|pipe` fails closed before the atomic writer.
Dotted keys, subtables, or arrays-of-tables that reserve a projected canonical
key as a table also fail closed; the merge never emits an invalid TOML
scalar/table redefinition.
This deliberately retains more unknown TOML data than Python's whole-schema
rewrite.

## Validation and capacity

Every pull and external refresh reads the file as bytes and validates UTF-8.
JSON resources use strict duplicate-key-rejecting JSON; setup TOML uses the
bounded projection parser above. Both enforce `ResourceStoreLimits` for input
bytes, JSON depth, JSON nodes, resource count, resource-id bytes, origin bytes,
subscribers, and patch operations. Malformed, over-depth, over-node, or
over-size documents never enter the visible cache. Cancellation is checked
before filesystem or mutation work. `setup.toml` must already exist as an
anchored regular file; creation remains the launcher/installer's responsibility
so a racing pull can never replace a newly created user file with defaults.

Configuration-tree copy additionally limits the actual traversal to 4,096
entries across regular files and directories, 64 MiB of file data, and 32
directory levels. Each entry is charged before its destination is created, so
wide trees of empty directories cannot consume unbounded enumeration, inode,
or disk capacity. Capacity and cancellation failures remove private staging
without publishing a partial configuration.

## Patch and commit boundary

Patch behavior matches `InMemoryResourceStore`: `add`, `remove`, and `replace`
use JSON Pointer paths, including root replacement; a stale timestamp returns a
conflict snapshot; invalid operations are isolated; and `static_data` is
immutable. Validation, patching, serialization, and update construction finish
before the writer is invoked while the per-store state lock serializes competing
patches. For `setup_toml`, the visible/cache document is the JSON projection
while the writer receives a merge against the freshly anchored TOML bytes.
Because a merge may reproject removed/defaulted fields, a successful setup
commit publishes one root `replace` containing the committed projection rather
than forwarding the caller's pre-merge operations. Replaying that publication
therefore produces exactly the returned and subsequently pulled snapshot.

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

`refresh(key, origin)` is the exact watcher integration point. It applies the
same path and resource-specific validation and classifies unchanged, updated,
missing, invalid, capacity, and internal outcomes. A changed valid document
publishes one root `replace` operation with the supplied bounded origin. A disk
failure invalidates the cache without publishing invalid data, so later pulls
cannot return a stale document after an external delete or corrupt replacement.
When a previously cached file is cleanly removed, subscribers receive one root
`remove`; malformed data invalidates locally but is never published.
`refresh_and_publish` remains the compatibility convenience for callers that
only need to know whether a replacement was published. Filesystem lifecycle is
owned by the separate `FileResourceWatcher` rather than this durable adapter.

## Configuration command operations

The production `copy_config` and `remove_config*` trigger registrations use
explicit structural operations on this store. They share the patch mutation
gate, use bounded auth-protected siblings under `config/` plus same-volume rename,
and invalidate affected config/event cache entries on every successful outcome;
copy also invalidates `static_data` on every success, including when the disk
file already matches the current default and requires no replacement.
This includes idempotent removal of an already-absent directory and reuse of a
clock-derived identifier whose old files disappeared externally. Recursive copy
reads only regular files through the persistent root anchor and commits
destination files through anchored create-exclusive and replacement writers.

Structural command methods invalidate caches but do not synthesize subscriber
updates. Their trigger response owns command acknowledgement; a host filesystem
watcher that needs a subscription update calls `refresh_and_publish` after
observing the committed change, using that method's validation and publication
barrier. See
`SERVICE_CONFIGURATION_TRIGGERS.md` for
the Python parity boundary and deferred commands.

## Build and verification

Enable `BUILD_SERVICE_FILE_RESOURCE_STORE` for the static adapter target or
`BUILD_SERVICE_FILE_RESOURCE_STORE_TESTS` for the adapter plus
`BAAS_service_file_resource_store_tests`. The native suite covers safe listing
and pulls, traversal/reparse-point rejection, malformed and capacity-bounded
JSON, setup alias/proxy projection, canonical null/global identity, replayable
committed publications, quoted-key/comment/multiline-preserving TOML commits, patch
conflicts, pre-commit writer failure, post-commit durability
uncertainty, concurrent patches, subscription barriers and self-unsubscription,
callback exception isolation, external refresh, Windows physical aliases,
anchored-handle rename blocking, and Windows/POSIX ancestor-swap fail-closed
behavior.

The implementation is excluded from the legacy `BAAS_CORE_SOURCES` glob and is
compiled only through `cmake/ServiceFileResourceStore.cmake`.
