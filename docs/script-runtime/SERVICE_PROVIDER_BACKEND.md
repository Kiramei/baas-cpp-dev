# Production provider state backend

`BAAS_service_provider_backend` is the application-side state owner injected as
`ProductionHttpHostDependencies::provider_backend`. It implements every
`ProviderBackend` snapshot and subscription operation without depending on BAAS
globals, a transport, or a test fixture. The later application composition layer
may bridge logger/runtime events into its publication API.

## State and validation

The owner starts with empty log arrays, status `{}`, unknown
`all_data_initialized`, and static snapshot `{timestamp:0,data:null}`. Callers
publish state through:

- `publish_log(entry_json)`, where the entry is a strict UTF-8 JSON object with
  one non-empty string `scope`;
- `publish_status(status_json)`, where status is a strict UTF-8 JSON object;
- `set_initialized(optional<bool>)`;
- `replace_static(timestamp_json, data_json)`, where timestamp is a JSON number
  and data may be any JSON value.

Validation rejects malformed UTF-8, malformed JSON, duplicate object keys,
invalid escapes/surrogates, excess depth/nodes, missing or non-string log scopes,
and the wrong top-level kind. Accepted JSON is retained byte-exactly, including
floating-point spellings and unknown fields. Malformed input returns
`internal_error`; a configured byte, scope, subscription, or snapshot bound
returns `capacity`; neither failure mutates state or invokes callbacks.

## Bounded history

Log history is bounded simultaneously by retained entry count, retained payload
bytes, distinct scopes, and exact serialized `scopes_json + entries_json` bytes.
After a valid publication, the oldest entries are evicted until every bound is
satisfied. A scope remains in the snapshot while any retained entry references
it and disappears with its final entry. One entry that cannot fit by itself is
rejected before mutation.

## Subscription and shutdown contract

Log and status subscribers have independent caps. A publisher snapshots
subscription slots while holding the owner lock, then invokes callbacks without
that lock. Each slot has its own admission gate and active-call counter.
Destroying a subscription first removes it from future snapshots, closes its
gate, and waits for an already-entered callback to finish. A callback may safely
call back into the provider. Self-unsubscribe closes admission without waiting
on itself; the publisher retains the slot until that callback returns.

Callback exceptions never escape a publication. The throwing subscription is
removed and closed, the publication reports `internal_error`, and later events
do not enter that callback. Owner destruction applies the same close barrier to
all remaining subscriptions. Snapshot methods reject an already-requested stop
token rather than returning stale work as success.

## Build and verification

Configure `BUILD_SERVICE_PROVIDER_BACKEND_TESTS=ON`. The option builds the
standalone backend, forces its provider/auth validation dependencies, and keeps
the source outside the legacy `BAAS_CORE_SOURCES` glob. Native tests cover exact
snapshots, bounded eviction, strict JSON/UTF-8 and capacity failures, stop-token
handling, subscription caps and callback exception isolation, a blocking
destructor barrier, and concurrent publication/subscription churn. CI runs the
suite on Windows, Linux, and macOS in Debug and Release.

The backend deliberately does not initialize BAAS globals, tail a concrete log
file, or create the service executable. Those are separate application
composition responsibilities.
