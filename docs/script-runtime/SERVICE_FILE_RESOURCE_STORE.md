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

Configuration-tree copy and archive processing additionally limit the actual
traversal/decoded set to 4,096 entries across regular files and directories,
64 MiB of file data, and 32 directory levels. Archives additionally allow at
most 64 MiB of ZIP input, 16 MiB per file, 1,024-byte portable paths, and a
1,024:1 compression ratio. Each entry is charged before its destination is
created, so wide trees of empty directories cannot consume unbounded
enumeration, inode, or disk capacity. Capacity and cancellation failures remove
private staging without publishing a partial configuration.

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
publishes one root `replace` operation with the supplied bounded origin. Any
load failure for a previously cached document invalidates the cache and
publishes one root `remove`, so later pulls and subscriber replay both expose
the resource as absent after a delete, malformed replacement, capacity overflow,
or internal read failure. Invalid bytes are never published.
`invalidate_and_publish(key, origin)` applies the same root-remove barrier
without consulting the current path. The config-list watcher uses it to retire
both config/event keys when their pair disappears, even if one sibling remains.
It also retires a deleted optional `gui.json` before admitting newly discovered
pairs, so a stale GUI cache slot cannot trap a capacity-bound provider in a
permanent degraded retry loop. The watcher retains each structurally valid id
list before pair refresh, allowing a later scan to retire a one-sibling partial
cache left by a capacity failure.
`refresh_and_publish` remains the compatibility convenience for callers that
only need to know whether a replacement or invalidation was published.
Filesystem lifecycle is owned by the separate `FileResourceWatcher` rather
than this durable adapter.

## Configuration command operations

The production `add_config*`, `copy_config`, `remove_config*`, `export_config`,
and `import_config` trigger
registrations use explicit structural operations on this store. They share the patch mutation
gate, use bounded auth-protected siblings under `config/` plus same-volume rename,
and invalidate affected config/event cache entries on every successful outcome;
create and copy also invalidate `static_data` on every success, including when the disk
file already matches the current default and requires no replacement.
This includes idempotent removal of an already-absent directory and reuse of a
clock-derived identifier whose old files disappeared externally. Recursive copy
reads only regular files through the persistent root anchor and commits
destination files through anchored create-exclusive writers. Every staging and
nested directory is also created relative to the persistent anchor; POSIX
creation verifies `O_NOFOLLOW`/mode `0700` and syncs the parent directory.
Create initializes user, event, and switch documents from the same immutable
runtime-resource defaults used by copy, assigns `name`/`server`, rebuilds the server-specific
manufacturing quantities, and makes the private staging directory visible with
one durable rename. Millisecond id selection is serialized by the mutation gate,
while one atomic open/cancelled/claimed phase gives cancellation and the
irreversible response claim an exact winner; failed claims, invalid static
targets, and initialization failures reclaim all `.baas-create-*` siblings.
Production construction injects those owned documents from the service's pinned
repository read bundle. Tests use synthetic private fixtures; absence of the injected
defaults fails closed for create, copy, and import rather than selecting compiled data.

Export holds the mutation gate for a coherent snapshot, recursively reads only
anchored regular files, sorts normalized forward-slash member names, and
returns a bounded in-memory ZIP. Import validates and decodes ZIP bytes before
the mutation gate, but performs every filesystem write within it. Portable path
validation rejects traversal, absolute/drive/UNC/ADS spellings, invalid UTF-8,
reserved or trailing-dot/space components, normalized duplicates, symlinks,
special/encrypted/unsupported entries, invalid CRCs, excessive compression,
and all declared size/count/path/depth ceilings.

Import builds the migrated user/event/switch files and preserved auxiliary
regular files under an anchored `.baas-import-*` staging directory. Complete
existing config/event pairs with the same Python-stripped name are renamed to
private tombstones only after a durable `.baas-import-journal-*` records the
staging, target, and complete rename set and the response sink claims the
irreversible terminal. Failure before target publication rolls those renames
back in reverse order and removes staging; success atomically publishes the new
id, invalidates new/retired config and event cache entries plus static metadata,
and lets the watcher observe only the final pair set. A marker that travels with
staging distinguishes a published target from an unrelated directory. The
journal filename's canonical transaction is required to match its target,
staging, ordered tombstones, and marker byte-for-byte. A project-root
`.baas-config-import.lock` serializes recovery and live imports across store
instances and processes. On the next store construction, a strictly bound
journal deterministically restores
all old trees when publication did not happen, or finishes tombstone cleanup
when it did. The journal is removed before the commit marker only after cleanup,
so process exit or power loss at any rename/cleanup boundary remains recoverable.
Windows create, rename, and delete barriers explicitly close the mutated handle
and flush the anchored parent-directory metadata through the native NT flush API.

The `config/` root is a production precondition: `create_config` never creates
an unprotected root. `ServiceApplication`'s auth owner creates and tightens that
root before commands are accepted. POSIX staging uses anchored `mkdirat(0700)`
plus `openat(O_NOFOLLOW)` verification; Windows uses handle-relative directory
creation under the anchored, protected parent and verifies the exact physical
path. Directory publication is likewise anchored and atomically no-replace
(`renameat2(RENAME_NOREPLACE)` on Linux/Android, `renameatx_np(RENAME_EXCL)` on
macOS, or handle-relative `NtSetInformationFile` on Windows), so a pathname swap
cannot redirect it and a concurrent serial claimant is never overwritten.

`config/static.json` is current project-root metadata, not part of the new
directory. Create repairs or upgrades it first through the single-file durable
atomic writer. That idempotent upgrade may remain if the subsequent directory
claim or rename fails; no old-static backup/restore transaction is claimed.
Pulls acquire the mutation gate before the cache gate, so no reader can observe
the static upgrade/cache invalidation or directory publication mid-boundary.
After success, `FileResourceWatcher` discovers the new config/event pair within
its configured poll interval (250 ms in `ServiceRuntimeProviderBridge`) and
publishes both replacements to provider subscribers.

Structural command methods invalidate caches but do not synthesize subscriber
updates. Their trigger response owns command acknowledgement; a host filesystem
watcher that needs a subscription update calls `refresh_and_publish` after
observing the committed change, using that method's validation and publication
barrier. See
`SERVICE_CONFIGURATION_TRIGGERS.md` for
the Python parity boundary, adjacent-binary archive protocol, and remaining
deferred command.

## Build and verification

Enable `BUILD_SERVICE_FILE_RESOURCE_STORE` for the static adapter target or
`BUILD_SERVICE_FILE_RESOURCE_STORE_TESTS` for the adapter plus
`BAAS_service_file_resource_store_tests` and
`BAAS_service_config_archive_codec_tests`. The native suites cover safe listing
and pulls, traversal/reparse-point rejection, malformed and capacity-bounded
JSON, setup alias/proxy projection, canonical null/global identity, replayable
committed publications, quoted-key/comment/multiline-preserving TOML commits, patch
conflicts, pre-commit writer failure, post-commit durability
uncertainty, concurrent patches, subscription barriers and self-unsubscription,
callback exception isolation, external refresh, Windows physical aliases,
anchored-handle rename blocking, and Windows/POSIX ancestor-swap fail-closed
behavior. They also cover bounded deterministic ZIP round trips,
path/case/separator collisions, encrypted/symlink/unsupported entries, CRC
corruption, compression ratio, archive/entry/total/path/depth/count limits, and
cancellation.

The implementation is excluded from the legacy `BAAS_CORE_SOURCES` glob and is
compiled only through `cmake/ServiceFileResourceStore.cmake`.
