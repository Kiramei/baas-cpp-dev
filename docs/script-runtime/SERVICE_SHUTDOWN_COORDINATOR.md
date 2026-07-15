# Service shutdown coordinator

`BAAS_service_shutdown_coordinator` is the bounded lifecycle handoff between
HTTP `/shutdown`, operating-system termination events, and the future
`BAAS_service` main thread. It owns no HTTP host, resource store, trigger
runtime, application singleton, or persistence backend.

## Ownership and ordering

The application main thread first calls `block_service_shutdown_signals()`.
On POSIX this blocks `SIGINT` and `SIGTERM` on that thread; every application
thread created afterwards inherits the mask. Windows returns the same explicit
ownership token without changing a signal mask. The token must stay on, and be
destroyed or consumed on, the same main thread.

The main thread then passes the token and a shared `ServiceShutdownCoordinator`
to `open_service_signal_owner()`. Only one signal owner may be active in a
process. Its lifetime owns one platform waiter thread and the signal-block
token. `stop()` is idempotent, wakes that waiter, joins it, unregisters platform
delivery, and only then restores/releases the token. `stop()` and destruction
therefore run on the original main thread.

Application shutdown is intentionally linear:

1. the main thread blocks in `ServiceShutdownCoordinator::wait()`;
2. the first HTTP or platform request publishes an immutable reason and wakes
   every coordinator waiter;
3. the main thread marks readiness non-ready and stops admission;
4. the main thread stops and joins HTTP/WebSocket owners;
5. the main thread shuts down and joins trigger/runtime owners;
6. the main thread stops the signal owner and releases remaining dependencies.

No request callback or platform callback is permitted to perform steps 3-6.

## First-wins request contract

`ServiceShutdownCoordinator` implements `router::ShutdownIntent`. The HTTP
entry calls `request_shutdown()`, which attempts one compare/exchange from
`none` to `http_request`. Platform workers use the same operation with
`interrupt`, `terminate`, or `signal_failure`. Only the winner returns
`ShutdownDecision::accepted`; every later request returns `rejected`, and the
published reason never changes.

The winner briefly synchronizes on the wait mutex before `notify_all`. This
closes the condition-variable predicate-to-sleep lost-wakeup window while
keeping host stop, joins, allocation, and application work out of the request
path. `wait_for()` provides the same predicate discipline for bounded probes and
tests.

## Platform boundary

On POSIX the signal-owner thread inherits the blocked `SIGINT`/`SIGTERM` set and
uses `sigwait`. External signals are therefore converted to an ordinary method
call outside an asynchronous signal handler. `stop()` sets a normal atomic stop
flag, targets the waiter with blocked `SIGTERM`, and joins it; the stop wake is
not published as an application shutdown reason.

On Windows `SetConsoleCtrlHandler` maps `CTRL_C_EVENT`/`CTRL_BREAK_EVENT` to a
pre-created interrupt event and close/logoff/shutdown controls to a pre-created
terminate event. The handler's only external action is `SetEvent`: it does not
lock, allocate, log, touch the coordinator, or stop a host. A normal waiter
thread consumes those events and requests the corresponding reason. The two
console events have process lifetime so an in-flight console callback can never
touch a closed handle while an owner unregisters.

## Failure and capability boundary

Signal block, process-global ownership, event creation, handler registration,
and thread startup failures return a stable `ServiceSignalError`. A waiter API
failure publishes `signal_failure` so the main thread does not wait forever.
This module does not install a fake signal source and does not imply that HTTP,
Pipe, remote, sync, or trigger capabilities are available.

## Build and evidence

The library is opt-in through `BUILD_SERVICE_SHUTDOWN_COORDINATOR`; tests use
`BUILD_SERVICE_SHUTDOWN_COORDINATOR_TESTS` and force the library plus Router
dependency on. `BAAS_service_shutdown_coordinator_tests` covers HTTP first-win,
invalid reasons/owners, concurrent request races, 1,000 repeated
publication-versus-wait broadcast races, platform-event conversion, stop/join,
and process-owner reopening. Foundation CI builds and runs the target on
Windows, Linux, and macOS in Debug and Release.
