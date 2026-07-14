# Service health and readiness foundation

`HealthReadinessOwner` is the C++ ownership point for the public runtime/auth
projection consumed by `GET /health`. It is a production-quality concurrency
and lifecycle foundation; the real runtime and persistent auth subsystems that
will publish into it are not implemented yet.

## State and publication contract

The owner begins in `starting` and stores one complete `HealthSnapshot`:

- `statuses`: the runtime's JSON-safe public status object;
- `auth.initialized`;
- `auth.pwd_epoch`;
- `auth.server_sign_public_key`.

`begin_startup(snapshot)` starts or restarts a lifecycle. `publish_ready(snapshot)`
succeeds only from `starting`; it cannot silently recover a `failed` owner.
`publish_failed(snapshot)` is valid from any state. Recovery therefore requires
an explicit `begin_startup()` followed by `publish_ready()`.

Every publication replaces state, runtime statuses, and public auth fields while
holding one mutex. `readiness_snapshot()` copies that complete value under the
same mutex. Concurrent HTTP workers cannot observe auth from one publication
and statuses from another. The snapshot deliberately has no sequence counter:
readiness correctness does not depend on a counter with an overflow policy.

## HTTP projection

The Router calls the provider exactly once for each valid `/health` request.

| Provider result | HTTP result |
| --- | --- |
| `starting` | `503 health_starting` |
| `failed` | `503 health_failed` |
| `ready` with a valid bounded snapshot | existing `200` `{ok,statuses,auth}` shape |
| provider throws | `503 health_provider_failed` |
| `ready` with invalid JSON/UTF-8 | `500 invalid_health_snapshot` |
| `ready` with oversized output | `500 response_too_large` |
| no state/provider | `503 health_unavailable` |

`with_health_snapshot()` remains the static compatibility path and is always a
ready `200` projection after construction-time validation. It should be used
for tests or genuinely static embedding, not to claim runtime readiness.

`Router::with_health_provider()` accepts and retains a non-null `shared_ptr`.
`HttpHostRouterConfig.health_provider` transfers that owner into its Router;
the Router, adapter, server, and active request ownership chain therefore cannot
outlive the provider. Listening and readiness remain separate: `HttpHost` may
be running on a non-zero port while `/health` returns `503`.

Stopping the HTTP host clears its published port but does not mutate the
readiness owner. An embedding that restarts the service must call
`begin_startup()` before starting or admitting the new lifecycle. Tests cover
starting, ready, failed, stop, restart, current-port publication, concurrent
readers/writers, provider lifetime, and the fixed static snapshot path.

## Deferred integration

This slice does not fabricate a runtime or auth subsystem. Wiring persistent
runtime/config loading and the real auth owner into these publication methods is
still required. The Tauri readiness probe, pipe-mode dynamic HTTP address
propagation, browser/WebView cookie behavior, and authenticated session owner
are separate coordinated work. No cookie/auth placeholder or Android reset
route is exposed here.

No cross-repository fixture was added: until Python or Tauri consumes the same
artifact, a C++-only duplicate would not prove shared compatibility. The frozen
successful body remains covered by the existing Router contract test.
