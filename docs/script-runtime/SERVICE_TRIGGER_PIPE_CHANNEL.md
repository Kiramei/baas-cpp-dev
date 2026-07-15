# Trigger Pipe business channel

`TriggerPipeChannelFactory` is the production BPIP adapter between `PipeHost`
and the transport-independent trigger runtime. It is intentionally not a
listener or a `ServiceApplication` composition root.

## Channel policy

The factory accepts only `PipeChannel::trigger`. `provider`, `sync`, and
`remote` return no handler and therefore produce PipeHost's fail-closed
`channel_unavailable` terminal result. An accepted open still uses the common
BPIP `open_ok` response before any business output.

## Ingress and execution

BPIP `JSON` maps to `TriggerIngress::receive_json_frame`; BPIP `BYTES` maps to
`receive_binary_frame`. This preserves the `import_config` declaration and its
immediately following binary payload as one owned `TriggerIngressItem`.
Catalog response modes, executor global limits, the configured per-connection
task limit, ingress/session byte limits, command rejection, and transaction
rollback all remain enforced by the shared Trigger runtime.

Peer `CLOSE`, host stop, handler destruction, and connection failure close the
ingress and `TriggerConnectionOwner`. Closing requests stop for every admitted
task and waits for external executor work to drain.

## Egress ownership

Each `TriggerSession::SendLease` is written through one
`PipeConnectionWriter::write_batch` call. JSON and an optional BYTES frame are
encoded into the same owning transport write, so they cannot be interleaved.
A full successful write calls `TriggerConnectionOwner::complete_send`, which
also retries terminal output retained by session backpressure. Any partial,
timed-out, throwing, budget-rejected, or otherwise failed write calls
`fail_send`, closes the session, cancels remaining work, and interrupts the
Pipe stream. The failed lease is never retried.

`on_close` has a strong output-callback barrier: it first interrupts blocking
I/O, then waits until every admitted pump call has exited. Thus the writer and
stream cannot be destroyed while an executor output callback still uses them.

## Build and verification

Configure `BUILD_SERVICE_TRIGGER_PIPE_CHANNEL=ON` for the library, or
`BUILD_SERVICE_TRIGGER_PIPE_CHANNEL_TESTS=ON` for the fake-PipeHost end-to-end
suite. The suite covers trigger-only selection, binary pairing, atomic
JSON+BYTES output, single and stream leases, queue backpressure, close
cancellation, write failure, strict ingress limits, and per-connection task
capacity in both Debug and Release CI builds.
