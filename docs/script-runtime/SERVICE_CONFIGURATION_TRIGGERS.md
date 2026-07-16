# Production configuration triggers

This slice migrates three existing Python trigger command families without registering
placeholders for the rest of the catalog.

| Wire command | Python evidence | C++ production behavior |
| --- | --- | --- |
| `add_config*` | `service/api/commands.py` → `ServiceRuntime.add_config` → `ConfigInitializer.check_config` | requires non-empty payload strings `name` and `server`; creates one decimal millisecond `serial`; applies the complete user/event/switch/static/manufacturing defaults; returns exactly `{serial}` |
| `copy_config` | `service/api/commands.py` → `ServiceRuntime._copy_config_sync` → `ConfigInitializer.check_config` | requires payload string `id`; recursively copies one safe config tree; generates a decimal millisecond `serial`; applies Python name/default/event/switch/static/manufacturing migration; returns `{serial,name}` |
| `remove_config*` | `service/api/commands.py` → `ServiceRuntime.remove_config` | requires payload string `id`; atomically withdraws the directory and returns `{}`; an absent id is an idempotent success |

`export_config`, `import_config`, and `update_setup_toml` remain unregistered.
They require, respectively, a bounded archive codec, archive
validation/replacement semantics, and the application-owned TOML projection.
This slice does not claim them and does not introduce a ZIP dependency.

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
with JSON patches. Copy is bounded to 4,096 total tree entries (regular files
plus directories), 64 MiB total, and 32 directory levels. Source files are read
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

Create and copy run the observable Python initializer projection before publication. They
use the audited 97-key user default, 26-event default, 11-entry switch default,
and complete 25-key static default. Create rejects names that are empty after
Python-compatible Unicode stripping but preserves the accepted name verbatim;
copy uses the stripped source name when deriving its suffix. Only Python's
documented server labels are accepted; manufacturing quantities
are rebuilt to 154 CN or 157 Global/JP entries; missing/invalid events recover
to server-adjusted defaults, including Python's whole-file fallback when a
`daily_reset` element is a null/boolean/number scalar. Strings and objects are
normalized to a three-element reset array instead of preserving Python's
accidental `len(...) == 3` shapes; this is intentional type-safety hardening.
Deprecated `display.json` is removed; and
`config/static.json` is atomically upgraded. The embedded static vector
has canonical SHA-256
`4b31c708fbbcd88300eb00e1ec71a556bc22f596467e5af356330b5496d2b247`.

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
`BAAS_service_configuration_trigger_tests`. The tests cover exact add/copy/remove
envelopes, create and remove prefix dispatch, unique ids/names, recursive files, absent removal,
payload/id limits, duplicate rejection, deep and wide tree ceilings, cleanup of
capacity/cancellation staging, pre/post-commit cancellation, Python initializer
vectors, complete static-default hash, and exact registration scope.
`BAAS_service_application_tests` executes `add_config*` and `copy_config` through the
hook-free production dispatcher/executor, while auth-owner tests preserve
access to a pre-existing config tree. CI runs Debug and Release application
coverage on Windows, Linux, and macOS.
