# Production configuration triggers

This slice migrates two existing Python trigger commands without registering
placeholders for the rest of the catalog.

| Wire command | Python evidence | C++ production behavior |
| --- | --- | --- |
| `copy_config` | `service/api/commands.py` → `ServiceRuntime._copy_config_sync` | requires payload string `id`; recursively copies one safe config tree; generates a decimal millisecond `serial`; rewrites `config.json.name` to the first free `_copy`, `_copy2`, ... suffix; returns `{serial,name}` |
| `remove_config*` | `service/api/commands.py` → `ServiceRuntime.remove_config` | requires payload string `id`; atomically withdraws the directory and returns `{}`; an absent id is an idempotent success |

`add_config*`, `export_config`, `import_config`, and `update_setup_toml` remain
unregistered. They require, respectively, Python initializer/default semantics,
a bounded archive codec, archive validation/replacement semantics, and the
application-owned TOML projection. This slice does not claim them.

## Bounds and filesystem ownership

`ConfigurationTriggerRegistration` parses the already ingress-validated payload
again under a smaller 64 KiB / depth 16 / 1,024-node command budget. Duplicate
keys are rejected by trigger ingress. The `id` is a direct UTF-8 resource id;
separators, traversal, Windows device names, control bytes, and oversized ids
fail before filesystem access. Cancellation is checked before and during tree
work and immediately before publication.

Structural operations live on `FileResourceStore` and share its mutation gate
with JSON patches. Copy is bounded to 4,096 regular files, 64 MiB total, and 32
directory levels. Source files are read through the persistent Windows/POSIX
root anchor, reparse points and symlinks fail closed, and each destination file
uses the durable anchored writer. A complete tree is built under the project
staging directory and becomes visible with one same-volume directory rename.
Remove first renames the validated tree to a tombstone, invalidates cached
config/event snapshots, then performs best-effort reclamation outside the
mutation gate.

Windows file authentication keeps a protected DACL on `<project-root>/config`.
Its allow-list ACEs inherit to child directories and files, so starting the
auth owner cannot revoke access to pre-existing `config/<id>` resources.

## Errors and response ownership

Stable wire errors distinguish invalid payload/id, missing source, invalid
tree, capacity, conflict, internal failure, and cancellation. Success/error
encoding remains owned by `TriggerResponseSink`; completed terminals therefore
retain the executor's existing bounded backpressure retry behavior and handlers
never rerun a committed filesystem mutation.

## Build and verification

Enable `BUILD_SERVICE_CONFIGURATION_TRIGGERS` for the production registration
library or `BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS` for
`BAAS_service_configuration_trigger_tests`. The tests cover exact copy/remove
envelopes, unique names, recursive files, prefix dispatch, absent removal,
payload/id limits, duplicate rejection, cancellation, and exact registration
scope. `BAAS_service_application_tests` executes `copy_config` through the
hook-free production dispatcher/executor, while auth-owner tests preserve
access to a pre-existing config tree. CI runs Debug and Release application
coverage on Windows, Linux, and macOS.
