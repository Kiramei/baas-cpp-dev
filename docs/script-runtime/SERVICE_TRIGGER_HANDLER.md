# Production trigger business handler

`BAAS_service_trigger_handler` is the authenticated business-channel adapter
for `/ws/trigger`. It deliberately contains no command implementation. A host
constructs one shared immutable `TriggerDispatcher`, owns it through a shared
`TriggerExecutor`, and installs `service::channels::TriggerHandlerFactory` in
`BusinessHandlerFactories::trigger`.

## Connection path

Each accepted trigger connection owns exactly one bounded `TriggerIngress`, one
shared `TriggerSession`, and one exclusive `TriggerConnectionOwner` obtained
from the supplied executor. Decrypted plaintext is interpreted as JSON except
while ingress is waiting for the immediately-adjacent binary payload declared
by `import_config`. Completed input is move-submitted to the existing executor;
the adapter never invokes or copies a command handler.

Catalog and admission failures that still have a bounded correlation identity
produce one `command_response` error and leave ingress open. Stream-command
rejections retain `ResponseMode::stream` and terminal `data.done:true`. Invalid
JSON, illegal frame order, and uncorrelated limit failures are connection-fatal.
Executor transaction failure is internal-fatal; ordinary bounded reservation
failure is a correlated command error.

## Completion-confirmed egress

The connection registers a weak `OutputReadyObserver`. On an empty-to-nonempty
edge it obtains one `TriggerSession::SendLease`, converts that immutable batch
to one plaintext JSON message plus an optional immediately-following binary
message, and calls the observed `BusinessPlaintextSink::emit_batch` overload.
No handler/session mutex is held while calling the external sink.

Only `BusinessBatchWriteResult::written` calls
`TriggerConnectionOwner::complete_send`; this both releases the correlation and
asks the executor to retry any terminal response retained by session
backpressure. Admission rejection or an asynchronous write failure calls
`fail_send`, permanently closes trigger egress, and cancels remaining work. A
synchronous completion cannot recursively acquire another lease: the pump
coalesces wakeups and keeps at most one observed batch in flight.

## Closure and cancellation

Peer final, the connection stop token, driver cleanup, and destruction converge
on one idempotent shutdown path. It closes ingress, cancels the output observer,
and calls `TriggerConnectionOwner::close`, which propagates stop to queued and
running command owners. Shutdown is linearized with an external sink call: a
non-reentrant closer waits for a call admitted before closure to return, and no
call can be admitted after closure. A late completion owns only a weak adapter
reference and cannot publish or acknowledge into a destroyed connection.

The v1 wire protocol does not define a generic cancel envelope. Legacy
commands such as `stop_scheduler` and `stop_all_tasks` remain ordinary catalog
commands implemented by the injected dispatcher; transport disconnect and stop
token cancellation are owned by this adapter/executor boundary.

## Limits and verification

`TriggerHandlerLimits` composes the existing ingress and session limits and an
optional per-connection executor task limit. Global worker, connection, input,
and retained-response budgets remain owned by `TriggerExecutorLimits`.

`BAAS_service_trigger_handler_tests` covers single and streaming responses,
declared inbound binary and adjacent outbound binary, one-lease FIFO ordering,
session backpressure retry after confirmed write, correlated stream rejection,
synchronous observed failure, output-callback/close concurrency, peer final,
stop-token shutdown, disconnect cancellation, and create-time capacity versus
stopped-executor classification. The target is exercised in Debug and Release.

Still outside this adapter are the concrete BAAS runtime command registrations,
the production HTTP/WebSocket composition root, shared Python/Tauri golden
fixtures, and live host/device smoke tests.
