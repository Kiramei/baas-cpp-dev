# Runtime resource snapshot loader

`BAAS_runtime_resource_snapshot_loader` materializes one immutable
`RuntimeResourceSnapshotActivation` from the `resources()` capability of a
pinned `RuntimeRepositoryReadBundle`. The loader has no API for native paths,
`BAAS_RESOURCE_DIR`, process-global resource state, URLs, Git references, or
browser-supplied repository metadata. Resource bytes remain external and are
read only at runtime. The activation owns the `baas::resources::ResourceSnapshot`
and the exact resources repository generation and commit copied from that
pinned capability.

## External manifest v1

The repository-root logical path is exactly `baas.resources.json`. Its UTF-8
JSON document has this exact top-level field set:

```json
{
  "schema": "baas.resources/v1",
  "entries": []
}
```

Each entry is an object with exactly these required fields:

- `id`: canonical lowercase logical resource id accepted by
  `ResourceSnapshot`;
- `path`: exact, case-sensitive repository-root logical path;
- `media_type`: canonical lowercase MIME type;
- `size`: unsigned JSON integer containing the byte count;
- `sha256`: 64-character lowercase SHA-256 digest.

The only optional fields are `locale` and `activity`, both canonical selector
tokens. Unknown, missing, duplicate JSON members and duplicate resource
variants or paths are rejected. Paths are never joined against an ambient
directory: they must exactly match an entry in the already-validated read-view
tree manifest. That read view enforces portable canonical paths, rejects path
aliases and symlink/reparse substitution, and verifies bytes from anchored
native handles.

`locale` and `current_activity` for the active `ResourceSelector` are explicit
native caller inputs. They are not top-level manifest fields and must never be
accepted from a browser request or inferred from process-global state.

## Validation and publication

The loader verifies the package declaration against the read-view tree entry,
then calls `RuntimeRepositoryReadView::read`, which verifies the opened file's
identity, size, and digest. Finally `ResourceSnapshot::build` independently
checks declared size and digest and defensively owns every byte before the
snapshot is published. Removing repository files after a successful load does
not invalidate the snapshot.

The activation provenance is deliberately not caller-constructible. A later
procedure or task activation must compare its pinned scripts generation with
`RuntimeResourceSnapshotActivation::generation()` before composing executable
code and resource bytes. A bare `ResourceSnapshot` is insufficient evidence of
same-generation repository selection.

`RuntimeResourceSnapshotLoaderLimits` independently bounds manifest bytes,
entries, total bytes, bytes per file, string bytes, and cumulative work.
JSON depth and node counts are separately bounded during parser callbacks,
before a hostile nested document can be materialized.
Cancellation is checked before and between reads, parsing, validation, and
publication. The `noexcept` result exposes only a stable typed error and either
a complete snapshot or no snapshot; it never exposes a path or partial output.
Allocation failure maps to `RRL018_RESOURCE_EXHAUSTED`.

Test hooks are free functions enabled privately only on test targets. They add
no field, virtual member, or conditional layout to `RuntimeRepositoryReadView`,
`ResourceSnapshot`, or another production class.

## Building the isolated boundary

```powershell
cmake -S . -B build/runtime-resource-loader `
  -DBUILD_RUNTIME_RESOURCE_SNAPSHOT_LOADER_TESTS=ON `
  -DBAAS_FETCH_RESOURCES=OFF
cmake --build build/runtime-resource-loader --config Debug `
  --target BAAS_runtime_resource_snapshot_loader_tests
ctest --test-dir build/runtime-resource-loader -C Debug `
  -R '^BAAS_runtime_resource_snapshot_loader_tests$' --output-on-failure
```

The target has no configured, generated, or installed fixture payload and does
not invoke legacy resource download/copy tooling.
