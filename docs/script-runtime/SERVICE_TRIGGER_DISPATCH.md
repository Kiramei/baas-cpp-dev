# Trigger dispatch and response bridge

`BAAS_service_trigger_dispatch` is a transport-independent foundation with no
networking or runtime dependency. It bridges completed `TriggerIngressItem`
values and `TriggerSession`, selects a registered callback, and publishes
bounded protocol responses. It does not implement BAAS runtime commands,
networking, authentication, scheduling,
devices, or application lifecycle.

## Transaction and identity boundary

`TriggerDispatcher::submit()` move-consumes one ingress item. It first resolves
the item's immutable catalog descriptor to a registered handler and only then
calls the single centralized session-admission helper. An unknown registration,
unregistered descriptor, or duplicate registry entry therefore cannot reserve
a timestamp. Once a host is integrated, the dispatcher is its required
submission path; callers must not separately call the low-level
`TriggerIngressItem::admit_to()` and then attempt to dispatch the same item.

Every successful `TriggerSession::admit()` mints an opaque `AdmissionReceipt`
containing a process-unique session instance ID and a monotonic per-session
generation. `publish()` requires that receipt and checks owner, timestamp, and
generation before accepting output. A stale handler cannot inject into a later
command that reuses the same timestamp, and a receipt cannot cross sessions or
be revived by reconstructing a session at the same address. `rollback()` is
allowed only before any response has entered the queue; visible progress or a
terminal batch makes rollback return `response_already_queued` without changing
the queue or correlation.

After admission, handlers receive only a const `AdmittedTriggerRequest` and a
scoped `TriggerResponseSink`. The request owns the exact ingress envelope,
optional binary bytes, actual command, catalog descriptor, timestamp, and
catalog-derived response mode. It exposes no raw session, receipt, mutable
identity, or caller-built `CommandResponse`. Prefix registrations use the
descriptor name such as `start_*`, while the handler and every response retain
the actual command such as `start_custom_task`.

## Registry and handler model

`TriggerDispatcher::create()` validates all registrations and then seals an
immutable vector of descriptor-to-handler bindings. Descriptor names must
exactly equal catalog `canonical_name` values. Stable registry errors distinguish
`invalid_limits`, `unknown_descriptor`, `duplicate_registration`, and
`empty_handler`; submit distinguishes `unregistered_command` before admission.

`submit()` invokes a handler synchronously. The immutable dispatcher itself may
be used concurrently for independent sessions. A registered callback may be
called concurrently and must synchronize any mutable state it captures. The
request and response sink are callback-scoped and must not be retained or used
by detached work.

## Controlled response publication

Handlers can publish stream `progress`, or stage one terminal `success`,
`error`, or `cancelled` result. They supply only data, error text, and optional
binary bytes. The sink copies sealed identity and mode into
`encode_command_response()` and then uses receipt-checked session publication.
Single-response commands reject progress. A second terminal or progress after a
staged terminal is rejected. The codec remains the sole owner of stream
`done:true`, binary size metadata, and the atomic JSON-plus-optional-binary
`OutboundBatch`.

Terminal output is staged until the handler returns normally. Consequently a
handler cannot stage success, continue side effects, then throw while exposing
that success to the client. On exception the staged terminal is discarded and
replaced with a bounded error terminal. Exception diagnostics are capped and
converted to printable ASCII, which is valid UTF-8. A second allocation or
encoding failure is caught by an outer boundary and returns
`internal_failure/close_session`; no handler exception crosses `submit()`.

`TriggerSession::publish(receipt, OutboundBatch&&)` validates before moving the
batch. Queue backpressure leaves the caller-owned batch unchanged. If a staged
terminal meets `queue_full` or `queued_bytes_exceeded`, submit returns
`retry_response` plus one owning `PendingTriggerResponse`. Its `retry()` moves
the same batch only after capacity exists, so a large binary is not copied on
each retry. Only terminal batches become pending; rejected progress followed by
handler return or exception is converted into a retryable terminal error or an
explicit `close_session` result, never an orphaned progress continuation.

Other publish failures, including closed session, invalid receipt, or
`cancellation_response_required`, are preserved in `TriggerResponseResult` and
produce `close_session`. The caller must close that session and propagate the
returned active-command cancellation list. A response that fails encoding can
be corrected by the handler while it is still running; if no terminal can be
formed after the handler exits, submit also returns `close_session`. Failed
publication never releases the correlation.

## Verification and remaining boundary

`BAAS_service_trigger_dispatch_tests` covers registry failures, pre-admission
unregistered rejection, prefix identity, single/stream rules, unique terminals,
binary batching, success-then-throw replacement, hostile exception text,
large-binary backpressure retry, ignored progress backpressure on return and
throw, cancellation/closed propagation, receipt owner and generation ABA,
same-address session reconstruction, rollback visibility, and concurrent
independent sessions. Debug and Release CI build it as an independent target.

Still required before task execution is complete:

- real catalog handler implementations and runtime/executor ownership;
- executor cancellation/deadline propagation and a live backpressure waiter;
- authenticated WebSocket and local Pipe hosts using submit and egress leases;
- shared Python/C++/Tauri fixtures, load/fault tests, and end-to-end execution.
