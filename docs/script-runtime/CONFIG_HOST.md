# Request-local ConfigHost foundation

`ConfigHost` is the synchronous `baas/config` 1.0 boundary. It does not load a
file, embed a default configuration, consult a global `ConfigSet`, or know where
the application persists user data. The application pins one immutable
`ConfigHostSnapshot` for a task and supplies a narrow `ConfigHostPort` for
validated compare-and-swap writes.

## Script API

```text
snapshot() -> { revision: int, snapshot_id: string }
get(path: string, default?: json) -> json
transact(expected_revision: int, patch: ordered-map<string,json>)
  -> { revision: int, snapshot_id: string }
```

`get` always reads the object retained when the task was created. Results and
defaults cross the Host boundary by value, so mutable list/map results cannot
alter the pinned snapshot. A successful `transact` updates application state but
does not retarget reads in the current task; a later task must pin the newly
published snapshot.

Paths are strict, non-root RFC 6901 pointers. Empty segments and invalid `~`
escapes are rejected. Traversal is through JSON objects only. Patch values are
replacements; JSON `null` is stored as `null`, not treated as deletion. One patch
cannot contain equal or ancestor/descendant-overlapping decoded paths.

## Production ownership

`ConfigHostPort::transact` owns application-schema validation, persistence, and
one atomic expected-revision compare-and-swap on the `config_id` strand. The
request owns its patch values, cancellation probe, and immutable identity; the
port must not retain references into the request. Conflicts return
`HOST009_CONFIG_CONFLICT` before any effect. Adapter exceptions, malformed
commits, invalid JSON/UTF-8, non-finite floats, duplicate object keys, exhausted
budgets, cancellation, and deadlines fail closed.

`ProductionRuntimeScriptExtensions::make_host_contributions` is the production
injection seam: an application extension constructs a fresh ConfigHost from the
exact pinned config publication, contributes its metadata/bindings/owner, and is
already checked against the task's `config_snapshot_id`. No repository path or
ambient current-config lookup crosses that seam. Tauri may remain responsible
for publishing and updating user configuration while the C++ WebUI path supplies
an equivalent port.

## Current boundary

The Host foundation, Windows Debug/Release tests, Android compile gate, catalog,
and composition seam are implemented. Concrete Tauri/WebUI persistence and
Python-versus-C++ behavior parity are intentionally later integration work.
