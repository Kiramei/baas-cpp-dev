# Runtime repository activation contract

Status: activation and transport-independent update publication are implemented.
Git transport and application policy/provider integration remain separate.

## Build and ownership boundary

The service runtime and optional libgit2 backend consume application-owned
repository state. Their CMake configurations do not download, copy, generate,
or package a runtime resource tree, and neither `resources.lock.json` nor the
legacy `apps/*/resource` trees are build inputs for those targets. A launcher or
updater owns acquisition and publication of the external state before the
service activates it.

The configure-time resource helper in `cmake/BAASResources.cmake` is a legacy
application exception. It is loaded only for `BUILD_APP_BAAS`, `BUILD_APP_ISA`,
`BUILD_BAAS_OCR`, or `BUILD_BAAS_AW_CHECKER`; downloads are opt-in through
`-DBAAS_FETCH_RESOURCES=ON`. New service, foundation, and runtime-repository
targets must not use that helper.

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

Repository payloads are exposed only through `RuntimeRepositoryReadBundle`.
`RuntimeRepositorySnapshot::open_read_bundle()` opens resources and scripts
from the same snapshot generation and returns pathless read capabilities. A
view exposes logical manifest entries, never a native root path. It opens every
root and entry component relative to retained native directory handles with
link and reparse traversal disabled.

Opening a view reads the manifest from one validated handle, bounds it, verifies
its SHA-256 against the snapshot descriptor, and parses the canonical tree
manifest rules below. Before publishing the view it exactly enumerates and
stream-hashes every listed payload through fixed-size buffers; extra files,
empty directories, aliases, linked files, or any hash mismatch fail the whole
bundle. Opening and hashing accept a cancellation token and enforce entry,
file, and total-byte budgets.

`read(logical_path, max_bytes, stop_token)` accepts only an exact listed path
and returns owned bytes after checking that the same anchored file handle is
regular, non-reparse, has link count one, has the declared size, and hashes to
the declared SHA-256. Validation never reopens a pathname for content use.

## Service resource contract

Before composing mutable configuration state or starting transports, the service
reads these exact logical entries from the admitted `resources` view:

```text
service/configuration/defaults/user.json
service/configuration/defaults/event.json
service/configuration/defaults/switch.json
service/configuration/defaults/static.json
```

They are external, generation-bound runtime data. They are not CMake inputs,
embedded resources, or executable string literals. Startup bounds their byte,
JSON-depth, and JSON-node counts and validates the initializer shapes; any missing,
oversized, malformed, or incompatible document rejects the generation before the
service creates user configuration directories or listeners. `FileResourceStore`
receives only immutable owned bytes, never repository paths or an update capability.

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

## External cancellation commit claim

Update callers that expose publication through a cancellable request may pass a
`RuntimeRepositoryCommitClaim`. The updater invokes it exactly once for a
changed generation, after candidate objects and the immutable snapshot are
installed and after the final stop-token check, but before writing the recovery
journal or changing `previous.json` or `current.json`. The callback receives
the exact target generation.

Returning `false` leaves no durable publication intent that crash recovery
could replay. Returning `true` closes the caller's cancellation window;
cancellation is not observed again during journal preparation and pointer
replacement. The caller must then report
either committed success or an irrevocable error if the replacement or its
durability cleanup fails. A same-generation no-op does not invoke the claim.

Crash recovery is a separate startup phase. `recover(validator)` completes any
durable decision made by an earlier process before interactive requests are
accepted. Interactive handlers then call `update` with
`RuntimeRepositoryRecoveryPolicy::RequireClean`; a leftover journal fails that
request without replaying it, so a newly cancelled request cannot publish an
older request's generation and then report cancellation. Non-interactive
startup orchestration retains the backward-compatible `RecoverPending` policy.
The updater rejects a commit claim combined with `RecoverPending`, preventing
interactive callers from accidentally bypassing this startup boundary.

## Trusted update plan

The standalone C++/WebUI product uses the in-process libgit2 backend, but a
browser request is never a repository policy source. The update entry point
accepts only a publisher-signed plan verified against a product trust key.
The browser may request “check” or “apply”; it cannot supply a remote URL,
advertised ref, exact commit, manifest hash, trust key, or replacement plan.
Before opening any untrusted listener, the product composition root constructs
one long-lived verifier that owns the trust key, reads the system clock itself,
and reads the last accepted state from a trusted state provider. A request
handler can pass only the signed envelope bytes; it cannot inject a key, time,
current generation, accepted sequence, or payload identity.

The outer envelope has schema
`baas.runtime-repositories.signed-plan-envelope/v1` and exactly `schema`,
`payload`, and `signature`. `payload` is canonical padded base64url of canonical
UTF-8 JSON. `signature` is an Ed25519 signature over the ASCII domain
`baas.runtime-repositories.update-plan.signature/v1\n` followed by those exact
decoded payload bytes. Signature verification happens before payload parsing.
Both envelope and payload use a no-DOM SAX preparse that stops at their depth
and node limits before the bounded DOM and duplicate-key parse is attempted.

The signed payload has schema `baas.runtime-repositories.update-plan/v1` and
exactly `schema`, `sequence`, `not_before_unix`, `expires_unix`,
`allow_bootstrap`, `previous_generations`, and `repositories`. Decimal values
are canonical unsigned decimal strings. The two repository entries are ordered
`resources`, then `scripts`, and bind the remote URL, advertised ref, full
lowercase commit object ID, manifest filename, and manifest SHA-256. Validity
is bounded to 31 days. Bootstrap must be explicit; otherwise the currently
pinned generation must be the target (a no-op) or occur in the signed
predecessor set. Unknown fields, duplicates, noncanonical encodings, expired
plans, and unapproved generation transitions fail closed.

The trusted state consists of the published generation, highest accepted
sequence, and SHA-256 of the exact signed payload. It is persisted atomically
with a successful generation publication. A changed generation requires a
strictly higher sequence. An equal sequence is accepted only when both the
current generation and payload SHA-256 match, making a retry idempotent without
allowing an older signed rollback plan to cycle between authorized predecessors.

`RuntimeRepositoryTrustedPlanUpdateOwner` is the only standalone composition
that may join verification to publication. Startup first calls updater recovery,
then reconciles `.trusted-plan-journal.json` against the updater's actual pinned
generation. The commit claim durably writes that policy journal before the
updater writes its own publication journal. After a successful current-pointer
replacement, the owner atomically replaces `.trusted-plan-state.json` and removes
the policy journal. A restart completes the next trusted state when current is
the journal target, discards the intent only when current and the previous
trusted state both still match, and otherwise fails closed. No update request is
accepted until this startup recovery succeeds.

The owner holds `.trusted-plan-writer.lock` across pointer refresh, policy
reconciliation, verification, updater execution, and policy-state completion,
so multiple C++ publisher processes cannot interleave those phases. Its durable
`.trusted-plan-owner` marker makes the store single-publisher: the Tauri/Rust
publisher rejects publish and rollback while preserving read-only exact-generation
activation. Starting standalone owner recovery is the explicit, irreversible
writer-ownership handoff and creates the marker before updater recovery. An
existing generation without trusted policy state can then be adopted only by a
signed plan whose target is that same generation; a bootstrap plan cannot use
missing policy state to move an existing installation to another generation.

Interactive callers may supply their terminal `RuntimeRepositoryCommitClaim`.
For a changed generation the owner first prepares the policy journal, then asks
the terminal claim to close cancellation, and only then permits the updater
journal/current transaction. A same-generation policy-sequence advance invokes
the same terminal claim before changing trusted state. Rejection leaves the
repository generation and trusted sequence unchanged.

Verification produces an immutable `RuntimeRepositoryUpdatePlanProvider`.
Only that provider crosses into the updater/libgit2 layer. The existing Tauri
exact-generation launch path remains a reader-only path and performs no fetch;
the standalone updater publishes a complete generation first and then starts
or restarts the service pinned to that exact generation.

The concrete desktop process boundary is documented in
`RUNTIME_REPOSITORY_UPDATE_APPLICATION.md`. It links the libgit2 backend only
into the separate `BAAS_runtime_repository_update` publisher, accepts the
signed envelope over bounded stdin, and returns the exact generation required
for the service restart handoff.
