# Runtime repository activation contract

Status: activation and transport-independent update publication are implemented.
Git transport and application policy/provider integration remain separate.

## State layout

The application-owned state directory contains one `runtime-repositories`
directory. Activation reads only these two regular, non-symlink files:

```text
runtime-repositories/
  current.json
  snapshots/<generation>.json
```

`current.json` has exactly the fields below. Unknown and duplicate fields are
invalid.

```json
{
  "schema": "baas.runtime-repositories.current/v1",
  "generation": "<64 lowercase hexadecimal characters>",
  "snapshot": "snapshots/<generation>.json"
}
```

The named snapshot has exactly `schema`, `generation`, and `repositories`.
`repositories` contains exactly two records, ordered first by `resources` and
then by `scripts`. Each record has exactly `id`, `commit`, `root`, `manifest`,
and `manifest_sha256`:

- `id` is `resources` or `scripts` in the required array position;
- `commit` is 40 or 64 lowercase hexadecimal characters;
- `root` is exactly `objects/<id>/<commit>`;
- `manifest` is one lowercase filename segment using only `[a-z0-9_.-]`, and
  is neither `.` nor `..`;
- `manifest_sha256` is 64 lowercase hexadecimal characters.

Activation bounds both files and the JSON node, depth, and string counts. It
rejects malformed JSON, non-ASCII schema data, traversal, symlinked state
components, non-regular files, unknown fields, identity mismatches, and paths
outside the state root. The activated `RuntimeRepositorySnapshot` owns copied
descriptors and remains unchanged if `current.json` later advances. Activation
does not probe `objects`, open manifests, or turn external repository data into
build inputs.

Path validation and file reading operate on the same opened object. Windows
opens every state component with reparse-point inspection, pins directory
handles, validates the final handle path within the root, bounds size from that
handle, and reads that handle. Unix walks from an opened root directory with
`openat` plus `O_NOFOLLOW`, bounds with `fstat`, and reads the same final file
descriptor. No validated pathname is reopened for content reads.

## Repository tree manifest

Each immutable repository root contains the snapshot-selected manifest. Its
exact bytes are bound by `manifest_sha256`; the manifest then binds every other
file in the tree. The document has schema
`baas.runtime-repository.tree-manifest/v1` and exactly `schema` and `entries`.
Every entry has exactly:

- `path`: the canonical relative UTF-8 path;
- `size`: the canonical unsigned decimal byte length encoded as a string;
- `sha256`: the lowercase SHA-256 of the exact file bytes;
- `mode`: `file`, corresponding only to Git mode `100644`.

The manifest does not list itself. Entries must list every other file exactly
once; extra files, missing files, empty directories, executable Git entries,
links, reparse points, and special files are invalid. Paths use `/`, are at
most 1,024 bytes and 32 components, reject Windows reserved names and
characters, and are compared with ASCII case folding to reject common
cross-platform aliases. Valid precomposed CJK, Hiragana, Katakana, and Hangul
UTF-8 names are supported. Decomposed combining-mark forms and Hangul Jamo are
rejected so a checkout cannot silently acquire a different normalized name.

Protocol limits are 16,384 files, 32,768 total file/directory entries, 256 MiB
per file, 2 GiB total file bytes, and 16 MiB manifest bytes. A transport may
enforce stricter source-specific limits but may not publish a tree that this
validator rejects. C++ and Rust implementations must apply the same manifest
schema and acceptance rules before publishing or rolling back a generation.

## Generation identity

Generation is lowercase SHA-256. The hash input is the following ordered field
sequence:

1. `baas.runtime-repositories.snapshot/v1`;
2. for `resources`, then `scripts`: `id`, `commit`, `root`, `manifest`, and
   `manifest_sha256`.

Every field, including the domain, is encoded as an unsigned 8-byte big-endian
byte length followed by its ASCII bytes. The digest must equal both the current
generation, the snapshot generation, and the snapshot filename.

Writers must first publish the immutable snapshot file and then atomically
replace `current.json`. A reader racing that replacement may pin either the old
or the new complete generation; it never follows repository data through the
mutable pointer after activation.
