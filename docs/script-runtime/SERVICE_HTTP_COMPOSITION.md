# Production HTTP host composition

`ProductionHttpHost` is the production composition boundary for the C++ HTTP
service. It constructs one ownership graph in this order:

1. `AuthOwner` from explicitly supplied `AuthDependencies`;
2. provider and sync factories from the injected production backends;
3. `BusinessHandlerFactories`, including injected trigger and remote factories;
4. `ProductionSessionFactory` for control and business WebSocket channels;
5. `AuthHttpAdapter`; and
6. `HttpHost`, which owns `Router`, `HttplibAdapter`, `WebSocketOwner`, the
   cpp-httplib server, and its listener/workers.

The composition root does not silently create an in-memory authentication
store, fake clock/random source, fake password deriver, or placeholder business
handler. A production caller must explicitly provide the normal file storage,
system clock/random source, sodium password deriver, provider backend, resource
store, and trigger handler. Trigger and remote handler implementations are
integration dependencies; this layer does not duplicate their channel logic.

## Construction and platform policy

`open_production_http_host()` returns a structured missing-dependency,
authentication, invalid-configuration, or construction error. Partial
construction is owned by local RAII values, so failure releases the `AuthOwner`,
factories, adapters, and backend references without publishing a usable host.
Converting AuthOwner's unique result to shared ownership also remains inside
that rollback boundary.

On desktop, `RemoteChannelPolicy::desktop_only` requires an explicit remote
factory. `disabled` does not. Android never installs `/ws/remote`, so the same
desktop policy does not force an impossible dependency there. The decision is
available through `production_remote_handler_required()` for application
configuration validation.

The optional health provider and shutdown intent are shared into `HttpHost`.
`HttpHost` retains the shutdown intent that backs Router's non-owning call
pointer, and Router retains the health provider. Neither can disappear while a
request is being served.

## Lifecycle and rollback

`start()` delegates to the bounded loopback `HttpHost`. A new-listener failure
is immediately followed by `HttpHost::stop()`, leaving the composition stopped
before the error is returned. `already_active` preserves the running host.
`stop()` is idempotent. Destruction calls it
before member teardown; `HttpHost` and its `WebSocketOwner` drain before session
factories, trigger/remote factories, provider/resource backends, or `AuthOwner`
are released.

Configure `BUILD_SERVICE_PRODUCTION_HTTP_HOST_TESTS=ON`. The dedicated Debug and
Release test covers valid start/HTTP routing/stop, Auth HTTP projection, every
required dependency class, desktop/Android remote policy, listener exception
rollback, invalid-construction rollback, and ownership release. Concrete
application startup and end-to-end trigger/remote behavior remain downstream
integration gates once their production factories are supplied.
