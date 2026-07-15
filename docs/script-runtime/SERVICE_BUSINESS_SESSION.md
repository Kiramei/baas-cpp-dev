# Service business sessions

`BusinessSessionFactory` implements the authenticated WebSocket session
boundary for `/ws/provider`, `/ws/sync`, `/ws/trigger`, and desktop-only
`/ws/remote`. `ProductionSessionFactory` strictly routes `/ws/control` to the
existing control factory and the four exact business path/channel pairs to the
business factory. Unknown or confused path/channel pairs fail closed.

## Handshake and stream ownership

The driver consumes AuthOwner's move-only `BusinessHandshakeMaterial`; callers
cannot reconstruct ticket, transcript, key, or subscription authority from
duplicate public fields. The wire transition is:

1. exact bounded `client_hello` produces `server_hello`;
2. preauth sequence 0 carries `resume_proof` and the atomic AuthOwner resume;
3. encrypted `resume_ok` carries the server secretstream push header;
4. preauth sequence 1 `stream_ready` carries the client push header;
5. every subsequent application frame is binary secretstream ciphertext.

Resume installs revocation delivery before publication. Stream setup retains
the subscription, RX key, AAD, and server push state until the client header,
authorization recheck, pull state, inactive output sink, handler factory, sink
activation, and handler `ready` call all succeed. The authorization recheck
drains revocation and validates expiry before any handler construction or
`ready` side effect. Any failure tears down and unsubscribes.

## Handler and output boundary

Each channel injects a transport-independent `BusinessChannelHandlerFactory`.
Handlers receive authenticated plaintext in move-only `SecretBuffer` values
and share a thread-safe plaintext sink for immediate asynchronous output. A
single writer mutex covers secretstream sequence allocation and outbound queue
admission. Up to two plaintext messages are encrypted and admitted as one
batch, preserving atomic trigger JSON-plus-binary output without interleaving
from concurrent producers.

`BusinessPlaintextSink` now exposes an optional batch write-completion receipt.
An accepted emission is reported `written` only after every encrypted frame in
the admitted batch has reached the transport; a later write/discard failure and
a synchronous admission rejection are reported `failed`. The callback runs
without the plaintext writer mutex or outbound queue mutex held, so a trigger
host can safely translate it to `TriggerSession::complete_send()` or
`fail_send()`. Legacy sink implementations remain source-compatible; their
default observed overload rejects with `completion_unsupported` and completes
the receipt as failed rather than treating admission as delivery.

The sink and its write-completion observer retain only weak references across
the transport boundary. Synchronous queue rejection or later write failure
atomically closes output and requests an internal-error termination. Plaintext
output strings owned by the driver/sink are explicitly overwritten on success,
rejection, validation failure, and exception paths; string destruction is not
treated as secret erasure.

Every asynchronous emission rechecks the same session/revocation gate before
taking the secretstream writer mutex. AuthOwner is therefore never called while
the writer mutex is held. Cleanup and terminal selection share one atomic sink
latch, so a delayed batch callback cannot replace an already selected
authentication, protocol, timeout, or shutdown outcome.

Every streaming input and heartbeat validates the session and drains bounded
revocation delivery. Heartbeats invoke the business handler but emit no wire
heartbeat. Once server FINAL is admitted, handler heartbeats stop and MESSAGE
input is a protocol failure.

Secretstream failures retain their typed boundary: only ciphertext
authentication failure maps to authentication failure; allocation/runtime/key
initialization failures map to internal error; malformed input, invalid header,
unexpected tag, closed/poisoned stream, and exhausted sequence map to protocol
failure.

## FINAL and compatibility boundary

Clean completion requires two independent facts: an authenticated peer FINAL
and a server FINAL batch completion reported as `written`. Either side may send
FINAL first. The driver waits for the other latch, never emits a second server
FINAL, and bounds the wait with `final_close_timeout`. EOF, interruption, or
timeout before both latches is truncation.

The current Python and Tauri clients do not send secretstream FINAL, so their
ordinary transport EOF is intentionally reported as truncated rather than
clean FINAL completion. The remote handler may enable the legacy
`decrypt=false` server-to-client bypass exactly once after validating its first
configuration and before its first output. The production sink permits this
only for `remote`; inbound frames stay secretstream-authenticated, and every
other business channel remains encrypted.

## Build and evidence

Configure `BUILD_SERVICE_BUSINESS_SESSION_TESTS=ON`. The deterministic Debug
and Release gate reconstructs the client handshake, decrypts real output,
covers immediate/multi-producer and atomic two-frame output, replay/corruption,
observed accepted/written, synchronous rejection and asynchronous write failure,
callback re-entry, weak lifetimes, both FINAL
orders and timeout, revocation, default bounds, strict routing, and remote
policy plus encrypted/raw directionality. CI also cross-builds the library for Android, where `/ws/remote` is
unavailable.

This is session-boundary evidence, not a production E2E claim. The production
composition boundary is documented in `SERVICE_HTTP_COMPOSITION.md`. Concrete
trigger runtime registrations, the ADB/scrcpy device adapter, application
installation, Tauri/Python updates, and device/service smoke tests remain
outside this target.
