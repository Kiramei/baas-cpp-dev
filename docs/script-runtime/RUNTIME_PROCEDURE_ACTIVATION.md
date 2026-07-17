# Runtime procedure activation

`RuntimeProcedureActivation` is the production boundary between a trusted BAAS Script
execution plan and legacy procedure execution. It never discovers a mutable checkout,
accepts a native path, or compiles procedure/resource data into the executable. The caller
supplies three immutable capabilities: the scripts `RuntimeRepositoryReadView`, an
unforgeable `RuntimeScriptExecutionPlan` built from that exact scripts generation and
commit, and a `RuntimeResourceSnapshotActivation` from the same repository generation.

The loader compares all provenance before reading procedure metadata. A bare
`ResourceSnapshot` is deliberately insufficient. The resulting activation retains the
generation, scripts commit, resources commit, selected resource snapshot, procedure
snapshot, and every verified definition byte after all repository handles are gone.

## Script repository contracts

The scripts root contains `baas.procedures.json`. Version 1 is strict JSON (UTF-8, no
comments, no duplicate keys) and has exactly these fields:

```json
{
  "schema": "baas.procedures/v1",
  "entries": [
    {
      "id": "group/menu",
      "definition": {
        "path": "procedures/group/menu.json",
        "size": 123,
        "sha256": "lowercase-sha256"
      },
      "terminals": [
        { "source": "joined", "id": "joined" },
        { "source": "exists", "id": "already_joined" }
      ],
      "effects": ["capture", "vision", "input", "wait"],
      "resources": ["image/group/menu"]
    }
  ]
}
```

Entry and nested object field sets are exact. Procedure IDs, terminal IDs, resource IDs,
definition paths, lowercase digests, cardinalities, strings, total definition bytes, JSON
depth/nodes, and validation work are bounded. Procedure/path duplicates and ASCII case
collisions fail closed. Definition paths are canonical lowercase logical repository paths
under `procedures/`; they are matched against the pinned repository tree entry by exact
path, size, and digest before reading.

Only the sorted `RuntimeScriptExecutionPlan::procedure_ids()` closure is read and retained.
The global manifest can describe other procedures, but an unrequested definition is never
opened. Every resource declared by a requested procedure must resolve in the supplied
same-generation resource activation.

Each requested definition is another strict JSON wrapper with exactly three fields:

```json
{
  "schema": "baas.procedure-definition/v1",
  "engine": "legacy.appear_then_click/v1",
  "payload": {}
}
```

Version 1 admits only `legacy.appear_then_click/v1`, and `payload` must be an object. The
activation owns the exact verified wrapper bytes so a later engine adapter does not reopen
the repository or rely on a native path.

## Identity and publication

`ProcedureDescriptorInput::implementation_sha256` binds the engine ID, complete definition
file SHA-256, and ordered source-to-logical-terminal mapping. The descriptor canonical
domain is `baas.procedure.descriptor/v2`; the snapshot domain is
`baas.procedure.snapshot/v2`. Therefore changing only definition bytes or only terminal
mapping order changes both `ProcedureSnapshot::snapshot_id()` and the activation identity.

The activation identity additionally binds its generation, scripts commit, resources
commit, and procedure snapshot identity. `RuntimeProcedureActivation` and individual
`RuntimeProcedureDefinition` objects cannot be publicly constructed, copied, or moved.
All reads, parsing, validation, snapshot construction, and identity construction finish
before publication. Cancellation, malformed input, repository mismatch, allocation
failure, or any limit violation returns a stable `RPA...` error with no partial activation.

## Build and platform gate

`BAAS_script_procedure_snapshot` owns the repository-independent descriptor snapshot.
`BAAS_script_procedure_host` links that target instead of compiling the snapshot source
itself. `BAAS_runtime_procedure_activation` composes repository, execution-plan, resource
activation, and snapshot targets. Foundation CI runs the native activation tests on Windows,
Linux, and macOS in Debug and Release, and cross-builds the production target for Android
`arm64-v8a` and `x86_64`.
