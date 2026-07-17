# BAAS Script Package and Versioning Contract (Draft 0.1)

This document defines the package boundary used by the script runtime. It is a
pre-v1 contract: field names may change until the language is frozen, but an
implementation must reject unknown contract versions instead of guessing.

## Version domains

Four versions are independent and must never be inferred from one another:

- `manifest_schema` describes the JSON document shape;
- `language` selects parser and language semantics;
- each imported `baas/*` host module has its own API version;
- `package.version` identifies immutable automation content.

Language and host API versions use `{major, minor}` integer pairs. A runtime is
compatible when the major is equal and its supported minor is greater than or
equal to the requested minimum minor. Major changes are breaking. Minor changes
are additive: a name or behavior available in an earlier minor cannot be
removed or silently changed within the same major.

Package versions are three non-negative integers (`major.minor.patch`) used for
selection and rollback metadata only. They do not override language or host API
compatibility checks. Build labels are allowed for diagnostics but do not
participate in ordering.

The runtime exposes its supported language pair and the version of every
registered host module through the CLI/service version endpoint. Package
activation resolves and stores the exact selected versions; a running task uses
that immutable resolution even if a newer runtime or package becomes active.

## Files and detached signature

The package root contains:

- `baas.package.json`: UTF-8 JSON manifest, without a byte-order mark;
- `baas.package.sig`: detached signature over the exact manifest file bytes;
- the modules and resources named by the manifest.

When a package is carried inside the signed runtime scripts repository, the
runtime catalog provides an explicit canonical `package_root` and requires the
manifest entry to be exactly `<package_root>/baas.package.json`. All manifest
module/resource paths remain relative to that root. Repository-bundled
activation may use native trust evidence from the signed repository update plan
instead of the standalone detached package signature only when that evidence
binds the exact generation and scripts commit and the read capability verifies
the signed tree manifest plus every payload digest. A bare read view or browser
request is not trust evidence. Standalone artifacts still require
`baas.package.sig`; an implementation that supports only repository-bundled
mode must reject, not ignore, detached signatures.

The signature is detached deliberately. No JSON canonicalization is required,
and reformatting the manifest invalidates the signature. The signature file is
bounded UTF-8 JSON with exactly these schema-1 fields:

```json
{
  "signature_schema": 1,
  "algorithm": "Ed25519",
  "key_id": "baas-release-example",
  "signature": "base64url-with-rfc4648-padding=="
}
```

Unknown or duplicate fields are rejected, the decoded Ed25519 signature must
be exactly 64 bytes, and the configured trust store resolves `key_id` before
verification. Key distribution and rotation are release-policy inputs.
Unsigned packages may be allowed only by an explicit developer-mode policy and
are never accepted by a normal update path.

## Manifest schema 1

Schema 1 has this logical shape. Hashes are lowercase hexadecimal SHA-256.
Paths use `/`, are relative to the package root, and are case-sensitive logical
identifiers on every platform.

```json
{
  "manifest_schema": 1,
  "package": {
    "id": "bluearchive.automation.core",
    "version": "1.2.3",
    "build": "optional-diagnostic-label"
  },
  "language": { "major": 1, "min_minor": 0 },
  "entrypoint": "main.baas",
  "host_modules": {
    "baas/config": { "major": 1, "min_minor": 0 },
    "baas/device": { "major": 1, "min_minor": 1 }
  },
  "capabilities": [
    "config.read",
    "device.capture",
    "device.input"
  ],
  "profiles": ["desktop", "android"],
  "modules": [
    {
      "path": "main.baas",
      "size": 1234,
      "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    }
  ],
  "resources": [
    {
      "id": "common.button.confirm",
      "path": "resources/common/button_confirm.png",
      "media_type": "image/png",
      "size": 5678,
      "sha256": "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
    }
  ],
  "limits": {
    "source_bytes": 8388608,
    "resource_bytes": 536870912,
    "module_count": 4096,
    "resource_count": 65536
  }
}
```

Required top-level fields are `manifest_schema`, `package`, `language`,
`entrypoint`, `host_modules`, `capabilities`, `profiles`, `modules`, and
`resources`. `limits` may only lower runtime policy limits; it cannot grant
additional capacity. Unknown fields are rejected in schema 1 so misspelled
security or compatibility fields cannot be ignored. A future schema may add an
explicit extensions object.

Every script module, resource, model, and data file consumed by package code
must appear exactly once in `modules` or `resources`. `entrypoint` must name a
listed module. Resource IDs are stable API identifiers and must be unique.
Duplicate JSON keys, duplicate logical paths, Windows case-fold collisions,
non-NFC path spellings, absolute paths, drive prefixes, `.`/`..` segments,
backslashes, NULs, symlinks, hard links, device files, and archive entries not
listed in the manifest are rejected.

## Capability and host-module resolution

An import of `baas/device` is valid only when:

1. the manifest declares a compatible `baas/device` requirement;
2. the runtime registered that exact major and a sufficient minor;
3. the package declares every capability required by the imported symbols;
4. execution policy grants a subset containing those declared capabilities.

When more than one registered descriptor has the requested major and satisfies
`minor >= min_minor`, resolution selects the greatest such minor. Selection is
bytewise/deterministic and independent of manifest, import, capability, or
registration input order. An unavailable exact major never falls forward or
back to another major. The activation contract requires the selected exact pair
to be stored in its immutable snapshot.

The effective capability set is the intersection of package declaration,
service/user policy, platform availability, and per-task narrowing. No layer
can broaden an earlier layer. A package cannot enumerate or import an
undeclared host module. Raw filesystem, process, network, native extension, and
remote-control capabilities are separate privileged grants and are absent by
default.

The metadata gate evaluates required export capabilities in fixed order:
manifest declaration, service/user policy, platform availability, then
per-task narrowing. Manifest absence is an undeclared-capability validation
failure; a later missing grant is `CapabilityDenied` attributed to that layer.
This pre-activation resolution neither locates nor calls a native adapter.

Deprecation metadata belongs to a host module version. Use of a deprecated name
emits a stable validation warning with its replacement and planned next-major
removal, but remains behavior-compatible for the current major. A security
policy may deny a capability immediately; denial produces `CapabilityDenied`
rather than falling back to a less secure operation.

## Validation and activation transaction

Activation is all-or-nothing and follows this order:

1. download into a newly created staging directory with archive, file-count,
   expanded-size, path, and compression-ratio limits;
2. read bounded manifest/signature bytes and verify the detached signature
   against an explicitly trusted key ID;
3. validate schema, versions, normalized paths, uniqueness, sizes, and hashes;
4. parse every module, resolve every package import, validate host-module and
   capability requirements, and compile using the target runtime;
5. run package-declared static checks and required compatibility smoke fixtures
   without device side effects;
6. move verified content into its immutable content-addressed cache location;
7. atomically replace the active-version record and fsync/flush according to
   platform guarantees;
8. retain the previous active record as a rollback candidate.

Any failure leaves the current active record unchanged. Recovery after a crash
must distinguish untrusted staging data, verified immutable content, and the
single atomic activation record. Staging data is never executable.

## Cache and snapshot layout

The logical layout is:

```text
packages/
  staging/<random-id>/...
  objects/<manifest-sha256>/...
  active/<package-id>.json
  rollback/<package-id>/<activation-sequence>.json
```

Object directories are immutable. Active and rollback records contain package
ID/version, manifest hash, selected language/host API versions, activation
sequence, and timestamp. Execution contexts retain a reference to their object
and resource snapshot. Garbage collection may delete an object only when it is
not active, not retained for rollback, and has no live execution reference.

Rollback performs the same compatibility check against the current runtime and
atomically switches the active record; it never edits cached content in place.
An incompatible rollback remains stored but cannot become active.

## Stable validation failures

Package tooling and the service report stable categories with the offending
manifest field/path and never execute partially validated content:

- `ManifestSchemaUnsupported`;
- `LanguageVersionMismatch`;
- `HostApiVersionMismatch`;
- `CapabilityDenied` or `CapabilityUndeclared`;
- `PackagePathInvalid` or `PackagePathCollision`;
- `PackageEntryMissing` or `PackageEntryUnexpected`;
- `PackageSizeLimit`;
- `PackageHashMismatch`;
- `PackageSignatureInvalid` or `PackageSignerUntrusted`;
- `PackageCompileFailed`;
- `PackageActivationFailed`.

## Required contract tests

- exact manifest bytes and detached-signature verification;
- same-major minor negotiation and major-version rejection;
- undeclared module/capability and policy narrowing;
- path traversal, separator, Unicode, case collision, link, duplicate-key, and
  archive-bomb rejection;
- missing/unexpected file, size, and hash mismatch;
- parse/import/compile failure before activation;
- crash injection at every activation step;
- concurrent activation/readers and immutable running-task snapshots;
- rollback compatibility and cache garbage collection with live references.
