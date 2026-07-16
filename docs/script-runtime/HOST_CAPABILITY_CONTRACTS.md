# Capability-scoped Host API Contract (Draft 0.1)

Status: Host contracts specified; the bounded LogHost production foundation is
implemented, while live activation and the remaining real adapters are not.
This document fixes the Phase 1 contract surface. A metadata-only registry,
synchronous `baas/log.emit` bridge, queued LogHost, and BAASLogger sink exist;
no package activation or parity completion is claimed. Most catalog Host APIs
therefore remain specified, not implemented.

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
Privileged Python ownership and the conservative static proof boundary are
fixed by
[`ADR-0003-privileged-operation-boundaries.md`](ADR-0003-privileged-operation-boundaries.md).

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

The metadata registry MUST accept only canonical `baas/<name>` descriptors with
unique `{module, major, minor}` versions, exports, and binding ownership. For a
manifest `{major, min_minor}` requirement it MUST consider only that exact major
and deterministically select the greatest registered minor satisfying
`minor >= min_minor`, independent of descriptor or request order. A higher minor
MUST retain every earlier export with the same binding and capability.

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

Registry resolution MUST reject an imported module absent from manifest
`host_modules`, then reject an imported export capability absent from manifest
`capabilities`, before evaluating grants. For a declared capability, the fixed
narrowing order is service/user policy, platform availability, then per-task
narrowing. Successful results contain only their four-way intersection; no
denial may be converted into a weaker export or adapter fallback.

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
| `baas/notify` | `context_strand` | `execution_context_id` | `notification_events` |
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

The checked synchronous ABI fixes v1 type ids to `Resource=1`, `Image=2`,
`OcrModel=3`, and `Device=4`; earlier scalar `HostValueType` ordinals are not
renumbered, and the owning `Bytes` ABI value is appended after all v1 handle
types. Adapter id, exact type, generation, context, immutable snapshot,
external-byte charge, and authentication token are validated before callback
entry. Producer grants and callback borrows are distinct transfer roles: a
borrow cannot be returned as a result, copied borrows are revoked together at
callback return, and a grant is consumed exactly once by publication.

Native creation reserves external memory and a generation slot before adapter
allocation. Open wrappers, queued releases, native-released/awaiting-ACK
tombstones, and detached teardown records retain that charge until reliable
ACK. Failed or unknown records rotate without starving later releases and are
never silently ACKed. Collection only enqueues; adapter I/O runs on the owning
context strand. Evaluator `close()` rejects further execution and transfers
remaining records plus their shared accounting ledger to the dispatcher so
retry does not depend on Heap lifetime. If `close()` returns false, the
embedder MUST retain the `HostReleaseDispatcher` and retry on its owner strand
until `destruction_safe()` is true. Destroying the final dispatcher owner while
`destruction_safe()` is false MUST fail-fast; it MUST NOT silently discard
queued, native-released/awaiting-ACK, or detached native ownership.

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

### HST-010 — Configuration, logging, and notification contracts

`baas/config.snapshot` MUST return an immutable revisioned snapshot;
`baas/config.get` reads only that snapshot; `baas/config.transact` MUST use an
expected revision, preserve unknown fields, validate atomically, and return
`HOST009_CONFIG_CONFLICT` without a partial write. `baas/log.emit` MUST attach
task/session/config metadata supplied by the host, accept bounded structured
fields, redact configured secrets, and enqueue to a bounded thread-safe sink.
Scripts MUST NOT choose filesystem log destinations or forge host identity.
`baas/notify.show` MUST emit at most one policy-approved, bounded notification
and returns no interaction result. `baas/notify.prompt` preserves the legacy
action-response contract: it MUST suspend cooperatively, present only the
ordered declared actions, and return either the selected declared action as a
`NotificationAction` map or `null` when dismissed. Platform notification UI,
callbacks, and thread affinity belong to `NotifyHost`; scripts MUST NOT receive
native window, callback, or notification handles. The two exports require
distinct `notification.show` and `notification.interact` capabilities, so a
one-way notification grant cannot manufacture an interactive prompt.

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
binding, parity ID, and owning module. Catalog mappings without a current Host
decision are reserved script APIs and MUST NOT be presented as legacy migration
requirements. Every decision ID MUST appear in the generated matrix. The eleven
taxonomy-v3 gaps listed below retain their operation IDs and reserved capability
contracts, but taxonomy v5 classifies their observed `core/*` occurrences as
`CPP_RUNTIME_INTERNAL`; they do not require a script Host binding.
`INVENTORIED` means specified and assigned only; it MUST NOT mean implemented,
linked, parity-complete, or safe for production.

| Operation evidence | Python symbol | Reserved capability | Reserved binding ID | Historical Host rule |
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

## Metadata registry foundation

`HostModuleRegistry` is a standard-library-only, immutable metadata registry.
`HostModuleDescriptor` contains a canonical module ID, `{major, minor}`, and
per-export `{export_name, binding_id, capability}` strings; it deliberately has
no callback, adapter object, native pointer, native file descriptor, or device
handle.
Construction validates descriptor identity, additive minor compatibility,
explicit count/string/work budgets, and UTF-8 before publishing the registry.

`resolve` accepts manifest module requirements and declared capabilities plus
requested exports and policy/platform/task capability sets. `HostResolution`
returns bytewise-sorted modules, selected exact versions, bytewise-sorted
bindings, the sorted effective capability intersection, and validation work.
After safe publication, concurrent `const resolve` calls are permitted because
all registry state is immutable and every call owns its scratch state.

## Bounded synchronous bridge evidence

`SynchronousNativeBindingSet` is an immutable callback/contract registry keyed
by binding ID. It is intentionally separate from `HostModuleRegistry`, so the
metadata registry still contains no callback, adapter object, pointer, native
descriptor, or device handle. Published bindings are bounded and accept only
`thread_safe` execution with `preflight` or `cooperative` cancellation.
Cooperative bindings receive a context-owned cancellation/deadline probe and
MUST poll it between bounded work chunks. Parameters are ordered,
required parameters cannot follow optional ones, and callbacks receive owning
null/boolean/integer/finite-float/string/bytes/JSON-safe values only. Bytes are
not JSON-safe and are charged as one kind byte plus their payload against the
aggregate Host conversion byte limit.

`SynchronousEvaluator` deduplicates Host imports from validated source, resolves
their exact versions with empty export sets, then calls `resolve` lazily for one
accessed export at a time. Successful and stable failed authorizations are
cached; transient allocation/aggregate-work failures roll back the cache entry.
The manifest/policy/platform/task gate, adapter availability, owner thread,
reentry, call shape, and named budget checks occur before argument evaluation.
Each parameter conversion is narrowed by the remaining aggregate node/byte/work
allowance before it copies data. Native callback entry is the first Host side
effect boundary.

The guarded callback returns `HostResult<HostValue>` or a structured `HostError`.
All HOST001-HOST016 mappings and deadline/budget discriminators are total;
`std::bad_alloc` uses an allocation-free boundary state that maps to
`MemoryLimitExceeded`, while other C++ exceptions are redacted to `HostInternal`.
The synchronous conformance evaluator materializes translated Host failures as
ERR-003 heap Errors with `origin = host`, one allowlisted Host frame, stable
`host_code`, `retryable`, `effect_state`, and allowlisted discriminator details.
The remaining real/asynchronous adapters and production bytecode VM are pending.

The executable catalog vertical slice is `host.log.emit.v1(level:string,
message:string, fields?:ordered-map<string,json>) -> null`, capability
`log.emit`, budget `log_events`. `InMemoryLogHost` remains deterministic test
evidence. `QueuedLogHost` adds the bounded ordered queue, immutable host identity,
recursive secret redaction, stable backpressure, and sink-failure containment;
`BAASLoggerLogSink` is the primary application's real logger adapter. Live
package activation and execution-context identity injection remain pending; see
`LOG_HOST.md`.

Registry failures use the following stable foundation codes. They are package
validation results, not adapter `HOSTnnn` statuses and not proof that a Host
operation can execute:

| Code | Condition |
| --- | --- |
| `HREG001_INVALID_LIMITS` | a configured limit is zero |
| `HREG002_MODULE_VERSION_LIMIT_EXCEEDED` | registered/declared module versions exceed the bound |
| `HREG003_EXPORT_LIMIT_EXCEEDED` | descriptor/request export count exceeds the bound |
| `HREG004_CAPABILITY_LIMIT_EXCEEDED` | a capability layer exceeds the bound |
| `HREG005_IMPORT_LIMIT_EXCEEDED` | imported Host modules exceed the bound |
| `HREG006_STRING_BUDGET_EXCEEDED` | one or total identifier bytes exceed bounds |
| `HREG007_VALIDATION_WORK_LIMIT_EXCEEDED` | deterministic validation work is exhausted |
| `HREG008_INVALID_UTF8` | identity metadata is not valid UTF-8 |
| `HREG009_INVALID_MODULE_ID` | module ID is not canonical `baas/<name>` |
| `HREG010_INVALID_EXPORT_NAME` | export is not lowercase ASCII identity |
| `HREG011_INVALID_CAPABILITY_ID` | capability is not dotted lowercase identity |
| `HREG012_INVALID_BINDING_ID` | binding does not match module-major binding shape |
| `HREG013_DUPLICATE_MODULE_VERSION` | `{module, major, minor}` repeats |
| `HREG014_DUPLICATE_EXPORT` | one descriptor/import repeats an export |
| `HREG015_DUPLICATE_BINDING` | binding ownership conflicts |
| `HREG016_INCOMPATIBLE_MINOR_CONTRACT` | a higher minor removes or changes an export |
| `HREG017_DUPLICATE_MANIFEST_MODULE` | manifest repeats a Host requirement |
| `HREG018_DUPLICATE_CAPABILITY` | one capability layer repeats an ID |
| `HREG019_DUPLICATE_IMPORT` | request repeats an imported Host module |
| `HREG020_UNDECLARED_MODULE` | import is absent from manifest `host_modules` |
| `HREG021_MODULE_UNAVAILABLE` | declared module has no registered descriptor |
| `HREG022_VERSION_INCOMPATIBLE` | exact major/sufficient minor cannot be selected |
| `HREG023_UNKNOWN_EXPORT` | selected version lacks the requested export |
| `HREG024_UNDECLARED_CAPABILITY` | export capability is absent from the manifest |
| `HREG025_CAPABILITY_DENIED` | policy, platform, or task narrowing denies it |

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
| `baas/notify` | `host.notify.show.v1(title, body, options) -> null`; `host.notify.prompt.v1(title, body, actions, options) -> NotificationAction?` |
| `baas/scheduler` | `host.scheduler.register.v1`; `host.scheduler.dispatch.v1`; `host.scheduler.sleep.v1`; `host.scheduler.schedule.v1`; `host.scheduler.cancel.v1` |
| `baas/resource` | `host.resource.resolve.v1`; `host.resource.read.v1` |
| `baas/fs` | `host.fs.open_root.v1`; `host.fs.read.v1`; `host.fs.list.v1`; `host.fs.write_atomic.v1`; `host.fs.mutate.v1` |
| `baas/service` | `host.service.publish.v1`; `host.service.request.v1` |
| `baas/process` | `host.process.list.v1`; `host.process.inspect.v1`; `host.process.run.v1`; `host.process.spawn.v1`; `host.process.wait.v1`; `host.process.terminate.v1` |
| `baas/http` | `host.http.request.v1(method, url, headers, body, options) -> HttpResponse` |
| `baas/socket` | `host.socket.open.v1`; `host.socket.recv.v1`; `host.socket.send.v1`; `host.socket.set_blocking.v1`; `host.socket.close.v1` |

## Explicitly pending implementation evidence

The production adapter set and metadata registry do not define or invoke
`ProcessHost`, `HttpHost`, `SocketHost`, `ServiceHost`, or the other real named
adapters. `QueuedLogHost` and `BAASLoggerLogSink` are implemented foundations,
but live package/task composition is not. Real adapter integrations for
the checked `host<T>` foundation, async completion, bounded pools, keyed
strands, production VM registration, full manifest
activation, ERR-003 unwinding, and Python-versus-C++ parity remain pending until
their production code and tests exist.
