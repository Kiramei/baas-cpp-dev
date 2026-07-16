# Production configuration triggers

This slice registers one coherent five-command Python-compatible configuration
surface without registering placeholders for the rest of the catalog.

| Wire command | Python evidence | C++ production behavior |
| --- | --- | --- |
| `add_config*` | `service/api/commands.py` → `ServiceRuntime.add_config` → `ConfigInitializer.check_config` | requires non-empty payload strings `name` and `server`; creates one decimal millisecond `serial`; applies the complete user/event/switch/static/manufacturing defaults; returns exactly `{serial}` |
| `copy_config` | `service/api/commands.py` → `ServiceRuntime._copy_config_sync` → `ConfigInitializer.check_config` | requires payload string `id`; recursively copies one safe config tree; generates a decimal millisecond `serial`; applies Python name/default/event/switch/static/manufacturing migration; returns `{serial,name}` |
| `remove_config*` | `service/api/commands.py` → `ServiceRuntime.remove_config` | requires payload string `id`; atomically withdraws the directory and returns `{}`; an absent id is an idempotent success |
| `export_config` | `service/api/commands.py` → `ServiceRuntime.export_config` | requires payload string `id`; emits a deterministic bounded ZIP of the complete profile; returns `{filename}` plus the ZIP as the adjacent response binary |
| `import_config` | `service/api/commands.py` → `ServiceRuntime.import_config` → `ConfigInitializer.check_config` | requires the ingress-declared adjacent request binary; accepts a root archive or one top-level wrapper containing `config.json`; migrates defaults, assigns a decimal millisecond `serial`, atomically replaces complete profiles with the same stripped name, and returns `{serial,name}` |

`update_setup_toml` remains unregistered because setup changes are currently
owned by the sync patch/TOML projection path. The archive commands use the
pinned `baas-miniz/3.1.2` dependency; they do not delegate extraction to an
external process or to a caller-controlled filesystem path.

## Archive protocol and safety

The Tauri trigger protocol keeps JSON and bulk bytes separate. `export_config`
returns data `{filename}` and one adjacent binary response frame. `import_config`
requires the command payload to declare binary ingress and consumes exactly the
adjacent request frame; success returns JSON `{serial,name}` without a binary
response. Existing envelope binary size metadata is produced and validated by
the shared protocol layer.

Both encode and decode enforce a 64 MiB ZIP limit, 4,096 entries, 16 MiB per
entry, 64 MiB total uncompressed data, 1,024-byte normalized paths, 32 path
components, and a maximum 1,024:1 uncompressed/compressed ratio. Import rejects
invalid UTF-8; absolute, drive, UNC, traversal, ADS, trailing-dot/space, control,
or Windows-reserved paths; separator/case aliases; file/directory prefix
collisions; encrypted or unsupported methods; symlinks and other special
entries; invalid CRCs; and duplicate normalized names. Export walks only
anchored regular files, applies the same portable-name rules, sorts archive
members, and fails closed on reparse points/symlinks or any exceeded bound.

## Bounds and filesystem ownership

`ConfigurationTriggerRegistration` parses the already ingress-validated payload
again under a smaller 64 KiB / depth 16 / 1,024-node command budget. Duplicate
keys are rejected by trigger ingress. Add additionally bounds `name` to 1 KiB
and `server` to 256 bytes and requires both to be non-empty JSON strings, matching
the service command boundary rather than accepting coercive placeholders. The
`id` is a direct UTF-8 resource id;
separators, traversal, Windows device names, control bytes, and oversized ids
fail before filesystem access. The `.baas-` id prefix is implementation-reserved
for unaddressable transaction files. Cancellation is checked before and during
tree work and is atomically ordered against the irreversible rename commit.

Structural operations live on `FileResourceStore` and share its mutation gate
with JSON patches. Copy and archive operations use the limits above; copy is
bounded to 4,096 total tree entries (regular files plus directories), 64 MiB
total, and 32 directory levels. Source files are read
through the persistent Windows/POSIX root anchor, reparse points and symlinks
fail closed, and the actual copy pass itself accounts for every byte, entry,
and depth transition before creating the corresponding destination. New files
use an anchored create-exclusive durable writer on Windows and POSIX;
replacement uses the existing atomic writer. A complete tree is built as an
auth-protected `config/.baas-copy-*` sibling and becomes visible with one
same-volume rename.
Create and copy admission reserves two cache slots per resulting configuration
(config and event), the required static/setup global slots, and the optional
GUI slot when `gui.json` is an anchored safe regular file. A mutation that
would make the next watcher scan exceed `max_resources` fails with `capacity`
before staging or static metadata changes.
Create builds the complete three-file config in an auth-protected
`config/.baas-create-*` sibling. Its clock-derived millisecond id is selected
under the same mutation gate, so simultaneous creates deterministically receive
distinct ids. It and copy transactionally refresh `config/static.json` with a
single-file atomic root-metadata upgrade before publishing the directory. That
idempotent upgrade remains durable if a later response claim, cancellation, or
directory rename fails; it is not part of a misleading two-path rollback
promise. The config directory itself becomes visible with one same-parent
anchored rename.
Remove first renames the validated tree to a tombstone, invalidates cached
config/event snapshots, then performs best-effort reclamation outside the
mutation gate.

Import performs bounded ZIP decoding before entering the mutation gate, then
builds a fully migrated profile in an anchored, auth-protected
`config/.baas-import-*` staging directory. Only complete existing config/event
pairs whose stripped name matches are eligible for replacement. Before the
new tree becomes visible, a durable `.baas-import-journal-*` record names the
staging, target, and every source/tombstone pair, then those pairs move to
private same-parent tombstones. Any ordinary pre-publication failure restores
them in reverse order and removes staging. If the process or machine exits at
any rename boundary, the next `FileResourceStore` construction strictly binds
the journal filename to its target, staging, ordered tombstones, and commit
marker, then either completes the same rollback or
finishes committed tombstone cleanup before serving config state.
Recovery and live import share a project-root interprocess lock, so a second
store cannot mistake another live transaction for a crashed one. The
irreversible response terminal is claimed through one atomic phase before
retirement/publication, so cancellation wins before mutation or the committed
result wins after the claim. After publication, config/event/static cache entries for the new and
retired ids are invalidated while still inside the mutation boundary; the
filesystem watcher subsequently publishes only the final complete pair set.
Tombstone reclamation is attempted after releasing the mutation gate; the
journal is retained until reclamation finishes, so an interrupted cleanup is
retried on the next start instead of becoming an unowned hidden tree.

Create and copy run the observable Python initializer projection before publication. The
four initializer documents are admitted from the pinned resources repository generation
before service composition; no business JSON fallback is compiled into the executable.
The currently published runtime data retains the audited 97-key user default,
26-event default, 11-entry switch default, and complete 25-key static default.
Create rejects names that are empty after
Python-compatible Unicode stripping but preserves the accepted name verbatim;
copy uses the stripped source name when deriving its suffix. Only Python's
documented server labels are accepted; manufacturing quantities
are rebuilt to 154 CN or 157 Global/JP entries; missing/invalid events recover
to server-adjusted defaults, including Python's whole-file fallback when a
`daily_reset` element is a null/boolean/number scalar. Strings and objects are
normalized to a three-element reset array instead of preserving Python's
accidental `len(...) == 3` shapes; this is intentional type-safety hardening.
Deprecated `display.json` is removed; and
`config/static.json` is atomically upgraded from that admitted runtime document.
Missing, malformed, oversized, or structurally invalid defaults fail startup before
the resource store, transport, worker, or listening socket is created.

Windows file authentication keeps a protected DACL on `<project-root>/config`.
Its allow-list ACEs inherit to child directories and files, so starting the
auth owner cannot revoke access to pre-existing `config/<id>` resources.

## Errors and response ownership

Stable wire errors distinguish invalid payload/id, missing source, invalid
tree, capacity, conflict, internal failure, and cancellation. Success/error
encoding remains owned by `TriggerResponseSink`. Before rename, the response is
proved encodable and the Session atomically claims an irrevocable terminal.
Cancellation that wins first prevents mutation; commit that wins first cannot
be rewritten to cancelled by dispatch, shutdown, or deferred backpressure.
Completed terminals retain bounded retry behavior without rerunning mutation.

## Build and verification

Enable `BUILD_SERVICE_CONFIGURATION_TRIGGERS` for the production registration
library or `BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS` for
`BAAS_service_configuration_trigger_tests`. The tests cover exact
add/copy/remove/export/import envelopes, adjacent binary ingress/egress,
create and remove prefix dispatch, unique ids/names, recursive files, absent removal,
payload/id limits, duplicate rejection, deep and wide tree ceilings, cleanup of
capacity/cancellation staging, pre/post-commit cancellation, Python initializer
vectors, complete static-default hash, import rollback/claim correction, and
exact five-command registration scope. `BAAS_service_config_archive_codec_tests`
separately exercise deterministic round trips, portable-path aliases, ZIP
metadata hardening, CRC failure, cancellation, and every archive capacity bound.
`BAAS_service_application_tests` executes the configuration commands through the
hook-free production dispatcher/executor, while auth-owner tests preserve
access to a pre-existing config tree. CI runs Debug and Release application
coverage on Windows, Linux, and macOS.
