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
Node, string-byte, logical-byte, and work limits apply once to the complete
ordered patch, not independently to each entry. Read/write aggregate bytes are
atomically reserved from the measured source value before deep copy; a copy
failure rolls back that reservation, and a failed reservation performs no copy.

## Production ownership

`ConfigHostPort::transact` owns application-schema validation, persistence, and
one atomic expected-revision compare-and-swap on the `config_id` strand. The
request owns its patch values, cancellation probe, and immutable identity; the
port must not retain references into the request. Conflicts return
`HOST009_CONFIG_CONFLICT` before any effect. Adapter exceptions, malformed
commits, invalid JSON/UTF-8, non-finite floats, duplicate object keys, exhausted
budgets, cancellation, and deadlines fail closed.

An adapter may report `Conflict` or `InvalidPatch` only with
`effect_state = not_started`. Any other effect state is an adapter contract
violation and becomes a redacted `HOST014_INTERNAL` while preserving the
committed/unknown effect state. A commit validates the combined
`config_id + snapshot_id` identity budget and is treated as committed if its
envelope is malformed.

`ProductionRuntimeScriptExtensions::make_host_contributions` is the production
injection seam: an application extension constructs a fresh ConfigHost from the
exact pinned config publication, contributes its metadata/bindings/owner, and is
already checked against the task's complete `config_id + config_snapshot_id`
identity. Reusing one snapshot label across config A/B cannot authorize an
extension for the other config. No repository path or
ambient current-config lookup crosses that seam. Tauri may remain responsible
for publishing and updating user configuration while the C++ WebUI path supplies
an equivalent port.

## Current boundary

The Host foundation, Windows Debug/Release tests, Android compile gate, catalog,
and composition seam are implemented. Concrete Tauri/WebUI persistence and
Python-versus-C++ behavior parity are intentionally later integration work.
