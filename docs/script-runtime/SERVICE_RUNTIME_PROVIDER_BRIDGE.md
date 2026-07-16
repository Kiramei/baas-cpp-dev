# Service runtime/provider bridge

`ServiceRuntimeProviderBridge` is the production lifecycle boundary between a
project-root `FileResourceStore` and the shared `ProductionProviderBackend`.
It owns a portable `FileResourceWatcher` worker and never publishes synthesized
static data or an optimistic initialized flag.

## Initialization contract

A complete scan must successfully load and validate:

- the sorted config list;
- `config.json` and `event.json` for every listed configuration;
- `config/static.json`;
- the bounded projection of `setup.toml`.

`config/gui.json` is refreshed and validated when present but remains optional
for headless projects. Only after the complete scan does the bridge commit the
real static snapshot with `replace_static` and set `all_data_initialized=true`.
Missing, malformed, or over-capacity required data leaves the Provider false.
The watcher continues after an incomplete startup, so a later complete scan can
recover without restarting the service.

## Watching and failure behavior

The watcher polls through anchored `FileResourceStore` APIs. The interval is
bounded to 10 ms through one minute (250 ms by default), and each pass is
bounded by `ResourceStoreLimits`. Removed identifiers publish root removes for
both cached keys before any current id is admitted, so an exact-capacity A-to-B
replacement progresses even when one retired sibling remains on disk. Current
config/event pairs and global GUI, static, and setup resources are then rescanned.

Any runtime scan failure immediately resets Provider initialization to false.
A valid later scan replaces static data and restores true. Changed valid files
publish the existing root-replacement Sync update. Invalid or removed files
invalidate stale cache entries but are never published as valid data.

`stop()` requests worker cancellation, wakes its wait, joins it, closes runtime
log admission, and finally resets initialization. No watcher callback can enter
after a normal external stop returns. Provider callbacks are synchronous and
may re-enter stop or destroy the last bridge owner from the watcher thread; that
self-stop detaches only the thread handle while a shared implementation keeps
the worker state alive through return. A later external stop still waits on the
explicit worker-completed barrier, including while such a callback is blocked.
All public bridge and watcher entries first retain a local shared implementation,
so a synchronous startup callback may destroy the last public owner without
leaving the active entry on freed state.

## Logs

The bridge accepts structured runtime events with `scope`, `level`, and
`message`, attaches a numeric millisecond time, serializes strict JSON, and
publishes through the shared Provider backend. Scope, level, and message bytes
are independently bounded. Lifecycle logs are emitted only on readiness state
transitions, preventing a persistent failure from filling bounded history on
every poll.

## Build and verification

Enable `BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE`, or enable
`BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE_TESTS` to also build
`BAAS_service_runtime_provider_bridge_tests`. Debug and Release CI use the
service-application dependency closure. The native suite covers real initial
load, static replacement, delete/failure/recovery, config-list removal and stale
cache invalidation, root-remove publication, incomplete startup recovery,
bounded logs, and concurrent
filesystem churn during stop.
