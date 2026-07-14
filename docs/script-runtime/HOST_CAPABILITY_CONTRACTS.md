# Capability-scoped Host API Contract (Draft 0.1)

Status: specified, not implemented. This document fixes the Phase 1 contract
surface; it does not claim that any named C++ interface, adapter, binding, or
parity test exists or passes.

The normative machine catalog is
[`host-capabilities.v1.json`](host-capabilities.v1.json). Taxonomy coverage is
checked against [`evidence/operation-index.json`](evidence/operation-index.json)
and every resulting decision row in
[`MIGRATION_MATRIX.md`](MIGRATION_MATRIX.md). Package capability negotiation is
defined by [`PACKAGE_VERSIONING.md`](PACKAGE_VERSIONING.md), values and handles
by [`VALUE_SEMANTICS.md`](VALUE_SEMANTICS.md), and execution ownership by
[`ADR-0001-runtime-architecture.md`](ADR-0001-runtime-architecture.md). Host
status translation and asynchronous suspension additionally compose with
[`ERRORS_AND_CLEANUP.md`](ERRORS_AND_CLEANUP.md) ERR-016 and
[`ASYNC_TASKS.md`](ASYNC_TASKS.md) ASY-015.

`MUST`, `MUST NOT`, `SHOULD`, and `MAY` are normative. API version 1.0 remains a
draft until implementation and parity evidence accept it; incompatible changes
before that point replace this draft and its machine catalog together.

## Normative clauses

### HST-001 — Stable module, capability, and binding identity

Every host module MUST use a canonical `baas/<name>` module ID, an independent
`{major, minor}` API version, dotted lowercase capability IDs, and a binding ID
of the form `host.<domain>.<operation>.v<major>`. Module exports may gain
additive behavior only under the same major. A binding ID MUST NOT be reused
for a different parameter, result, permission, side-effect, or error contract.
The canonical schema-1 inventory is `host-capabilities.v1.json`; Markdown names
that disagree with that catalog are invalid.

### HST-002 — Checked invocation and result boundary

A binding MUST accept only the ordered parameters declared in the catalog.
Required, optional, duplicate, unknown, type-invalid, and limit-invalid
arguments are checked before side effects. Values cross the ABI as the scalar,
JSON-safe, bytes, and generational `host<T>` forms defined here and in
`VALUE_SEMANTICS.md`; a raw pointer, native descriptor, environment view, or
singleton MUST NOT cross it. A synchronous binding returns `HostResult<T>` and
an asynchronous binding completes an equivalent task result. C++ exceptions
MUST NOT cross the language ABI.

### HST-003 — Stable HostError envelope

Every failure MUST be a structured `HostError` with `code`, safe `message`,
`retryable`, `effect_state`, and optional JSON-safe `details`. `effect_state`
is exactly `not_started`, `committed`, or `unknown`; cancellation MUST NOT claim
rollback when an external effect may have happened. The stable codes
`HOST001_CAPABILITY_DENIED` through `HOST016_BACKPRESSURE` are enumerated in the
machine catalog. `HostError` is the adapter ABI status, not the script-visible
ERR-003 Error or an ERR-004 language code. The outer native guard MUST translate
it under ERR-016, attach the safe host binding identity, and preserve
`effect_state` in allowlisted details before control returns to the VM. Native
diagnostics, secrets, absolute policy roots, command lines, credentials, and
remote response bodies MUST NOT be copied into the public message or details.

The catalog `language_mapping` is the declarative ERR-016 translation table and
is normative. A `default` maps unconditionally. A mapping with `discriminator`
MUST carry that exact allowlisted detail and match exactly one listed `cases`
key; a missing or unknown discriminator is an undeclared adapter status and
translates to `HostInternal`. The complete v1 mapping is:

| Host status | ERR-004 language code |
| --- | --- |
| `HOST001_CAPABILITY_DENIED` | `CapabilityDenied` |
| `HOST002_INVALID_ARGUMENT` | `HostValidationFailed` |
| `HOST003_CANCELLED` | `Cancelled` |
| `HOST004_DEADLINE_EXCEEDED` | `details.deadline_scope=context` → `DeadlineExceeded`; `call` → `Timeout` |
| `HOST005_BUDGET_EXCEEDED` | `details.budget_scope=external_memory` → `MemoryLimitExceeded`; `host_operation` → `TaskLimitExceeded` |
| `HOST006_UNAVAILABLE` | `HostUnavailable` |
| `HOST007_IO_ERROR` | `HostUnavailable` |
| `HOST008_DEVICE_DISCONNECTED` | `DeviceDisconnected` |
| `HOST009_CONFIG_CONFLICT` | `HostValidationFailed` |
| `HOST010_RESOURCE_NOT_FOUND` | `ResourceMissing` |
| `HOST011_MODEL_UNAVAILABLE` | `OcrModelUnavailable` |
| `HOST012_POLICY_DENIED` | `CapabilityDenied` |
| `HOST013_PROTOCOL_ERROR` | `HostUnavailable` |
| `HOST014_INTERNAL` | `HostInternal` |
| `HOST015_HANDLE_CLOSED` | `HostValidationFailed` |
| `HOST016_BACKPRESSURE` | `HostUnavailable` |

### HST-004 — Capability evaluation and least privilege

Before argument-dependent work, a binding MUST require its catalog capability
in the effective set computed as package declaration intersected with service
policy, platform availability, and per-task narrowing. Denial returns
`HOST001_CAPABILITY_DENIED`; a binding MUST NOT fall back to raw filesystem,
process, network, device, or service access. Capability arguments are narrowed
again by injected policy objects such as device IDs, resource snapshots,
filesystem roots, executable IDs, endpoint allowlists, and service operation
IDs. Possession of one handle or capability MUST NOT imply another.

### HST-005 — Deadlines, cancellation, and effect visibility

Every binding MUST use the earlier of the execution-context deadline and an
optional call deadline. `cooperative` calls register the context stop token at
each blocking wait and bounded work chunk; `preflight` calls check cancellation
before their single bounded commit. Cancellation returns
`HOST003_CANCELLED`, deadline expiry returns `HOST004_DEADLINE_EXCEEDED`, and
neither may be swallowed or converted to success. A blocking native API that
cannot be interrupted MUST run on a bounded adapter pool and report
`effect_state: unknown` when completion cannot be proven. At language
translation, an execution-context deadline becomes terminal `DeadlineExceeded`;
a narrower call deadline becomes catchable `Timeout`, as required by ERR-004.
The adapter MUST set `details.deadline_scope` to exactly `context` or `call`
before returning `HOST004_DEADLINE_EXCEEDED`.

### HST-006 — Reservation and incremental budgets

Each invocation MUST charge the named catalog budget. Fixed work is reserved
transactionally before side effects; bytes, pixels, OCR tokens, process rows,
queue entries, and elapsed blocking work are charged incrementally. Exhaustion
returns `HOST005_BUDGET_EXCEEDED`, releases unused reservation, and MUST NOT
borrow from another execution context. Per-call size limits only narrow the
context budget. Queue saturation returns `HOST016_BACKPRESSURE` rather than
creating an unbounded worker or buffer. Every named binding budget MUST appear
once in catalog `budget_scopes`; `HOST005_BUDGET_EXCEEDED` MUST copy that scope
to `details.budget_scope`. Byte/model/buffer reservations use
`external_memory`; count, queue, lookup, and bounded work reservations use
`host_operation`.

### HST-007 — Thread safety, strands, and reentrancy

The module execution table is normative. `thread_safe` modules may run
concurrently; `bounded_cpu_pool` and `bounded_io_pool` modules use separately
bounded pools; every `*_strand` or `handle_strand` module serializes by its
declared `strand_key`. A per-device operation MUST run on the same `device_id`
strand across capture and input, and a socket handle MUST serialize state
changes, send, receive, and close. Host completion callbacks MUST resume through
the owning execution context and MUST NOT re-enter the VM while an adapter lock
or strand callback is active. Asynchronous calls MUST use the bounded suspension
token, exactly-once immutable completion, late-result release, and context-strand
materialization rules in ASY-015.

| Module | Execution mode | Strand key | Default budget |
| --- | --- | --- | --- |
| `baas/vision` | `bounded_cpu_pool` | none | `vision_work_units` |
| `baas/ocr` | `bounded_cpu_pool` | `model_id` | `ocr_work_units` |
| `baas/device` | `device_strand` | `device_id` | `device_operations` |
| `baas/config` | `config_strand` | `config_id` | `config_read_operations` |
| `baas/log` | `thread_safe` | none | `log_events` |
| `baas/scheduler` | `context_strand` | `execution_context_id` | `scheduler_operations` |
| `baas/resource` | `thread_safe` | `snapshot_id` | `resource_bytes` |
| `baas/fs` | `root_strand` | `root_handle` | `filesystem_operations` |
| `baas/service` | `session_strand` | `service_session_id` | `service_messages` |
| `baas/process` | `bounded_io_pool` | process handle when present | `process_operations` |
| `baas/http` | `bounded_io_pool` | `request_id` | `network_bytes` |
| `baas/socket` | `handle_strand` | `socket_handle` | `socket_operations` |

### HST-008 — Host handles and lifetime

Every `host<T>` value MUST be an unforgeable, generational, execution-context
owned handle with an explicit close operation or context teardown release.
Closing is idempotent; later use returns `HOST015_HANDLE_CLOSED`. Handles MUST
NOT cross execution contexts, outlive their immutable package/config/resource
snapshot, expose native addresses, or rely on script garbage-collection timing
for security or external cleanup. Release records follow ADR-0002 bounds.

### HST-009 — Vision, OCR, and device contracts

`baas/vision` MUST expose deterministic `match`, `detect`, and `color` over
explicit image/resource handles, ordered options, and bounded numeric work.
`baas/ocr` MUST expose explicit model loading and `recognize`; locale, region,
candidate, filtering, and pass-method choices belong in the options map and
MUST NOT use process-global defaults. `baas/device` MUST expose `capture`,
`click`, `swipe`, and `app_control`; coordinates identify the captured frame or
declared logical resolution, input is serialized on `device_id`, and
`HOST008_DEVICE_DISCONNECTED` is distinct from cancellation. Vision/OCR CPU
work MUST NOT hold the device strand. Click options carry any long-click
duration; swipe options cover scroll semantics without introducing an ambient
gesture mode.

### HST-010 — Configuration and logging contracts

`baas/config.snapshot` MUST return an immutable revisioned snapshot;
`baas/config.get` reads only that snapshot; `baas/config.transact` MUST use an
expected revision, preserve unknown fields, validate atomically, and return
`HOST009_CONFIG_CONFLICT` without a partial write. `baas/log.emit` MUST attach
task/session/config metadata supplied by the host, accept bounded structured
fields, redact configured secrets, and enqueue to a bounded thread-safe sink.
Scripts MUST NOT choose filesystem log destinations or forge host identity.

### HST-011 — Host scheduler contract and language-task separation

`baas/task` is exclusively the capability-free structured-concurrency standard
module specified by ASY-008; its `cancel(Task) -> bool` is a language Task
operation and is not a Host binding. The capability-scoped automation surface
MUST instead use canonical module `baas/scheduler`, capabilities under
`scheduler.*`, bindings under `host.scheduler.*`, `SchedulerHost`, and opaque
`host<ScheduledTask>` handles. `baas/scheduler.register` MUST execute during
package validation/activation and produce a deterministic duplicate-task error
before task execution. `dispatch`, `sleep`, `schedule`, and
`cancel(host<ScheduledTask>) -> null` MUST use ASY-015, propagate
cancellation/deadlines, and use bounded queues. These two `cancel` operations
MUST NOT be overloaded, aliased, or treated as ABI-compatible. Detached native
threads and script-visible OS thread handles are forbidden. The
execution-context strand orders scheduler state transitions but MUST NOT
serialize independent device strands or CPU pools.

### HST-012 — Resource and filesystem contracts

`baas/resource` MUST resolve only manifest resource IDs inside the immutable
snapshot and return `HOST010_RESOURCE_NOT_FOUND` without probing ambient paths.
`baas/fs` is a separate privileged module: `open_root` resolves a policy ID,
and every `read`, `list`, `write_atomic`, or `mutate` path is relative,
normalized, and confined beneath that opaque root. Absolute paths, drive
prefixes, traversal,
separator aliases, symlink/reparse escape, and time-of-check/time-of-use root
replacement MUST be rejected with `HOST012_POLICY_DENIED`. Writes MUST be
bounded and atomic where the export says `write_atomic`; resource access never
grants filesystem access.

### HST-013 — Service contract

`baas/service.publish` and `baas/service.request` MUST address symbolic,
versioned event/operation IDs registered by the host and exchange bounded
JSON-safe values. This module is an in-process service boundary and MUST NOT
grant `network.http` or `network.socket`. Authentication/session identity comes
from the execution context, not script arguments. Adapter framing and routing
failures map to `HOST013_PROTOCOL_ERROR`; wire compatibility remains governed
by `SERVICE_PROTOCOL_V1.md` and its separate implementation gates.

### HST-014 — Process, HTTP, and raw-socket security

`baas/process` MUST be absent by default. `inspect`/`list` return only
policy-approved fields; `run`/`spawn` take an allowlisted `program_id` plus an
argument vector, never a shell command string, ambient environment, inherited
descriptor, or unrestricted current directory. Only a process handle created by
`spawn` may be passed to `wait` or policy-checked `terminate`.
`baas/http.request` MUST
revalidate scheme, host, resolved address, redirect target, proxy, credentials,
request bytes, response bytes, and timeout against policy at every hop.
`baas/socket` MUST use structured allowlisted endpoints and opaque handles;
`recv`, `send`, `set_blocking`, and `close` remain bounded and serialized on the
socket strand. Readiness waits MUST be asynchronous, cancellable, and
non-blocking to the executor even while their completions re-enter that strand.
DNS rebinding, loopback/private/link-local targets, listening,
raw sockets, and descriptor export require distinct future capabilities and
MUST NOT be implied by `network.http` or `network.socket`.

### HST-015 — Taxonomy and matrix traceability

Every current `HOST_BINDING_REQUIRED` scope decision MUST match exactly one
`taxonomy_mappings` rule in the machine catalog, including family, proposed C++
binding, parity ID, and owning module. Every decision ID MUST appear in the
generated matrix. The eleven taxonomy-v3 gaps listed below MUST retain their
operation IDs while receiving the stated capability and binding contract.
`INVENTORIED` means specified and assigned only; it MUST NOT mean implemented,
linked, parity-complete, or safe for production.

| Operation evidence | Python symbol | Capability | Stable binding ID | Taxonomy rule |
| --- | --- | --- | --- | --- |
| `op-aebb00093f62aa31` | `psutil.Process` | `process.inspect` | `host.process.inspect.v1` | `process-host-v3` |
| `op-4c8a2ce2dc27e423` | `psutil.process_iter` | `process.inspect` | `host.process.list.v1` | `process-host-v3` |
| `op-cdc75ef6e48bfd96` | `requests.get` | `network.http` | `host.http.request.v1` | `http-host-v3` |
| `op-a5958fbf211fd3cf` | `requests.post` | `network.http` | `host.http.request.v1` | `http-host-v3` |
| `op-49bb09af53004a29` | `socket.socket` | `network.socket` | `host.socket.open.v1` | `socket-host-v3` |
| `op-a99ebc4a865d175a` | `subprocess.Popen` | `process.spawn` | `host.process.spawn.v1` | `process-host-v3` |
| `op-289268bbe3e5c936` | `subprocess.check_output` | `process.spawn` | `host.process.run.v1` | `process-host-v3` |
| `op-f0d262f6e29c2c90` | `subprocess.run` | `process.spawn` | `host.process.run.v1` | `process-host-v3` |
| `op-96a7854dbf227e4e` | `socket.socket.recv` | `network.socket` | `host.socket.recv.v1` | `socket-host-v3` |
| `op-158dcb9d554cf9bf` | `socket.socket.send` | `network.socket` | `host.socket.send.v1` | `socket-host-v3` |
| `op-ee53939b067f3ccd` | `socket.socket.setblocking` | `network.socket` | `host.socket.set_blocking.v1` | `socket-host-v3` |

### HST-016 — Test ownership and implementation boundary

Each binding MUST have the catalog parity ID and four independent evidence
layers: contract tests using the production interface with fake hosts; policy
tests for declaration/grant/argument narrowing and ERR-016 status translation;
cancellation, budget,
backpressure, strand, and race tests; and Python-versus-C++ golden parity for
every mapped matrix family. Process/HTTP/socket additionally require adversarial
allowlist, redaction, redirect/DNS, handle-close, and partial-effect tests.
Service requires shared protocol vectors; vision/OCR/device require artifact or
emulator fixtures. Documentation tests validate the catalog, taxonomy evidence,
rules, matrix, roadmap, and CI linkage but MUST NOT count as C++ binding or
parity implementation.

## Version-1 binding signatures

The following compact view is informative; parameter requiredness, complete
error sets, cancellation modes, budgets, interfaces, and parity ownership are
normative in `host-capabilities.v1.json`.

| Module | Binding IDs and signatures |
| --- | --- |
| `baas/vision` | `host.vision.match.v1(image, template, options) -> MatchResult?`; `host.vision.detect.v1(image, rules, options) -> list<Detection>`; `host.vision.color.v1(image, feature, options) -> bool` |
| `baas/ocr` | `host.ocr.load_model.v1(resource_id, options) -> OcrModel`; `host.ocr.recognize.v1(model, image, region, options) -> list<OcrLine>` |
| `baas/device` | `host.device.capture.v1`; `host.device.click.v1`; `host.device.swipe.v1`; `host.device.app_control.v1` |
| `baas/config` | `host.config.snapshot.v1`; `host.config.get.v1`; `host.config.transact.v1` |
| `baas/log` | `host.log.emit.v1(level, message, fields) -> null` |
| `baas/scheduler` | `host.scheduler.register.v1`; `host.scheduler.dispatch.v1`; `host.scheduler.sleep.v1`; `host.scheduler.schedule.v1`; `host.scheduler.cancel.v1` |
| `baas/resource` | `host.resource.resolve.v1`; `host.resource.read.v1` |
| `baas/fs` | `host.fs.open_root.v1`; `host.fs.read.v1`; `host.fs.list.v1`; `host.fs.write_atomic.v1`; `host.fs.mutate.v1` |
| `baas/service` | `host.service.publish.v1`; `host.service.request.v1` |
| `baas/process` | `host.process.list.v1`; `host.process.inspect.v1`; `host.process.run.v1`; `host.process.spawn.v1`; `host.process.wait.v1`; `host.process.terminate.v1` |
| `baas/http` | `host.http.request.v1(method, url, headers, body, options) -> HttpResponse` |
| `baas/socket` | `host.socket.open.v1`; `host.socket.recv.v1`; `host.socket.send.v1`; `host.socket.set_blocking.v1`; `host.socket.close.v1` |

## Explicitly pending implementation evidence

No header or source file is introduced by this contract. `ProcessHost`,
`HttpHost`, `SocketHost`, `ServiceHost`, and the other named interfaces remain
proposed interface identities. Phase 3 binding, permission enforcement,
threading implementation, cancellation injection, and parity checkboxes remain
pending until their production code and tests exist.
