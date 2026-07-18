# Group resource publication compiler

Status: the deterministic compiler and its host CLI are implemented. The real
reviewed production lock and generated bundles remain external dynamic data and
are deliberately not part of `baas-cpp-dev`.

## Boundary

`baas-runtime-publisher` turns a reviewed lock plus one exact Git commit into
the two existing `BAASCDSB` v1 support-bundle variants used by
`navigation.to_main_page` and `group.open`. It does not inspect the mutable
checkout, discover resources, repair names, or invent a locale fallback. The
production inventory/extractor and human review that create the real lock live
in the external resource publication workflow.

The compiler library is cross-platform and static. The CLI is a host tool and
is not built for Android. Android builds still compile the library for both
supported ABIs so its lock and archive code cannot silently become
desktop-only.

No generated `.bundle`, copied PNG, `baas.resources.json`, user configuration,
or BAAS Script package belongs in the C++ executable or this repository.

## Lock v1

The strict root has exactly:

- `schema`: `baas.group-publication-lock/v1`;
- `compiler`: exactly `schema=baas.runtime-publisher/v1` and unsigned
  `version=1`;
- `source`: exactly one lowercase 40-character SHA-1 `commit`; and
- `bundles`: a nonempty list sorted bytewise by `(bundle_id, profile)`.

Every bundle has exactly `bundle_id`, `locale`, `profile`, `output_path`,
`member_count`, and `members`. `locale` must equal one of the exact profiles
`CN`, `JP`, `Global_en-us`, `Global_zh-tw`, or `Global_ko-kr`; it must also equal
`profile`. The first member is the matching `feature-graph`. It is followed by
bytewise-sorted `rgb-range-set` members and then bytewise-sorted
`png-template` members. The declared count must equal the complete closure.

Every source-backed member records this exact descriptor:

```text
path, oid, size, sha256
```

`path` is a safe Git-relative path, `oid` is the exact lowercase SHA-1 blob ID
found in the pinned commit tree, `size` is the exact nonzero blob size, and
`sha256` is the lowercase SHA-256 of the exact blob bytes. An image member binds
two such descriptors: `source` for the PNG and `crop_source` for the generated
Python crop-metadata module. It also binds exact `feature`, four crop
coordinates, `threshold_milli` (exactly the Python default `800`), and
`mean_rgb_tolerance` (exactly `20`). Per-call overrides remain in the procedure
definition and cannot enter a support bundle. RGB members bind the exact legacy
`source_key` as well as the profile RGB blob.

Unknown fields, duplicate JSON names, unsafe paths, unsorted members, duplicate
IDs/features, a neutral locale, a profile mismatch, or an incomplete graph/RGB/
PNG closure are rejected. There are no optional alias, fallback, placeholder,
or discovery fields.

## Exact source rules

The source repository is opened with libgit2 without parent-directory search.
The compiler looks up the exact commit, its exact tree entry, and then reads the
blob through the libgit2 object database. It verifies tree/blob type, OID, byte
size, and SHA-256 before parsing. Working-tree files, Git filters, line-ending
conversion, untracked replacements, environment variables, and ambient resource
directories are never consulted.

For an image `src/images/<profile>/<group>/<name>.png`, the Python feature must
be exactly `<group>_<name>`, including case and spelling. Its crop source must be
exactly `src/images/<profile>/x_y_range/<group>.py`, whose active `prefix`,
`path`, key, and coordinates must match. A commented declaration is absent.
This preserves the Python baseline defects: missing crop, missing PNG, prefix,
case, or spelling disagreements remain false instead of being repaired.

PNG bytes are validated in place (signature, bounded canonical chunks, CRC,
IHDR, zlib stream, filter bytes, and crop dimensions) and copied directly from
the ODB blob into the archive. They are never decoded and re-encoded. RGB
samples are strictly extracted from the exact profile JSON and converted to the
frozen RGB-range schema. The feature graph is derived only from reviewed lock
members; click/reaction priority belongs in the procedure definition, not the
graph.

## Deterministic output

Each support bundle is a deterministic ZIP32 STORE stream:

- local and central entry order is `bundle.magic`, `manifest.json`, then
  `m00000000` onward;
- version-needed, flags, method, DOS time/date, attributes, disk numbers, and
  creator fields have fixed values;
- there are no extra fields, comments, directories, descriptors, encryption,
  ZIP64 records, prefixes, or suffixes; and
- JSON is compact UTF-8 with a frozen field order and no trailing newline.

The publisher emits all declared bundle paths followed by canonical
`baas.resources.json`. Individual files use exclusive sibling temporary files,
durable flush, and atomic replacement. The manifest is replaced last and is the
publication commit point. `--check` performs no writes and rejects missing,
different, symlinked, or undeclared files.

## CLI

```text
baas-runtime-publisher verify-source --repository DIR --lock FILE
baas-runtime-publisher compile-group --repository DIR --lock FILE --output DIR
baas-runtime-publisher compile-group --repository DIR --lock FILE --output DIR --check true
baas-runtime-publisher verify-publication --repository DIR --lock FILE --output DIR
baas-runtime-publisher check-reproducible --repository DIR --lock FILE
```

`verify-source` performs the complete source and semantic preflight without
writing. `compile-group` compiles once and writes or checks. `verify-publication`
recompiles from the ODB, compares every byte, and rejects undeclared output.
`check-reproducible` performs two independent ODB compilations and compares all
paths and bytes.

## Reviewed production inventory boundary

Commit `b8cc64705feb0067aba349892031a450d1bf8083` is the target Python baseline.
The reviewed inventory counts are per profile and include the feature graph:

| Profile | navigation members | group members |
| --- | ---: | ---: |
| `CN` | 64 | 16 |
| `JP` | 56 | 12 |
| `Global_en-us` | 61 | 17 |
| `Global_zh-tw` | 58 | 14 |
| `Global_ko-kr` | 57 | 13 |

The often quoted navigation `77` and group `25` counts are union-closure upper
bounds across profiles (one graph plus all audited RGB/image identities), not a
valid member count for any single locale. A production lock must be emitted by
the external inventory workflow, independently reviewed against these counts,
and then consumed by this compiler. The small in-repository fixture tests the
schema and algorithms; it is explicitly not a production inventory or bundle.

## Verification

`BAAS_runtime_group_publication_compiler_tests` creates a pinned temporary Git
repository, records real blob identities, dirties its checkout, compiles twice,
checks a fixed archive SHA-256, exercises source and schema negatives, checks
atomic/`--check` behavior, and activates the generated archive through the
existing `RuntimeResourceSnapshotLoader` and `CoDetectSupportBundle` loader.
The existing strict bundle suite remains the authoritative ZIP/manifest/PNG
negative corpus and runs in the same CI job.
