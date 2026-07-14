# BAAS Script Runtime Migration Roadmap

Status legend: `[ ]` pending, `[~]` in progress, `[x]` verified.

This roadmap tracks the full migration from the Python automation runtime in
`baas-dev` to a C++ runtime with an updateable, Turing-complete scripting
language. A phase is complete only when its listed evidence exists and passes;
an isolated prototype is not considered project completion.

## Non-negotiable outcomes

- Preserve observable Python behavior during the initial migration period.
- Keep frequently changing automation logic and resources independently
  updateable without rebuilding the C++ core.
- Move stable image, OCR, device, configuration, logging, scheduling, and
  service capabilities into reusable C++ host APIs.
- Provide a versioned, Turing-complete language with diagnostics, modules,
  cancellation, timeouts, and testability.
- Provide a concurrent `cpp-httplib` service compatible with `baas-tauri`.
- Support reproducible Windows x64 builds and Android targets, followed by the
  remaining supported desktop platforms.
- Maintain behavior, performance, concurrency, smoke, migration, and protocol
  tests in CI/CD.

## Phase 0 — Baseline and authoritative inventories

- [x] Create a dedicated foundation branch.
- [x] Inventory Python modules, automation operations, dynamic language use,
  service endpoints, tests, and resources in `baas-dev`.
- [x] Inventory reusable C++ capabilities, platform guards, tests, and build
  targets in `baas-cpp-dev`.
- [x] Inventory the current `baas-tauri` process and service contract without
  modifying its existing dirty worktree.
- [~] Record every discovered Python operation in `MIGRATION_MATRIX.md` with an
  owner, C++ host binding, parity test, and migration status.
  Taxonomy v3 preserves all 15,469 sites as 4,319 operations and 4,908
  operation/source-scope decisions. It separates script Host requirements,
  script/module rewrites, C++ service internals, Tauri UI replacement,
  tooling/tests, dependencies, and unresolved calls; bounded AST/type
  resolution reduced unresolved decisions from 1,842 to 1,279 without guessing
  dynamic owners. 1,279 disposition decisions and 11 Host contracts remain
  strict gaps. See
  `OPERATION_INDEX_AUDIT.md`.
- [x] Implement an opt-in, deterministic, bounded Python parity trace foundation
  on `feat/cpp-parity-trace` without changing the default execution path.
- [x] Add a deterministic static migration validator for grid actions, image
  mappings, and OCR calls, with fixture tests and real-repository reporting.
- [x] Capture baseline Python golden traces for representative workflows.
  `PYTHON_GOLDEN_TRACES.md` records four deterministic offline workflows across
  configuration, image matching, scheduling, and operation orchestration with
  pinned source commits, bounded/redacted evidence, and a rebuilding `--check`.
  Device, OCR, service, Tauri, and Python-to-C++ replay parity remain pending.
- [~] Capture baseline latency, memory, startup, throughput, and package-size
  measurements on Windows x64. Host process startup, import readiness, RSS,
  legacy and production service-injected `cafe_reward.match`, and logical tree
  sizes are recorded in `evidence/python-performance-baseline.json`. A safe
  direct in-process ASGI `/health` baseline adds readiness, RSS, request
  throughput, and request-scope teardown in
  `evidence/python-service-performance-baseline.json`. The audited full service lifespan
  initializes OCR/core state, watchers, update work, and a background data
  thread, so it and real service shutdown remain explicitly skipped. Localhost
  server transport, device/emulator, OCR, Tauri end-to-end, and packaged
  installer measurements remain pending.
- [x] Record platform and emulator smoke-test prerequisites.
  `PLATFORM_SMOKE_PREREQUISITES.md` and its deterministic read-only checker
  record the profiles and exact future smoke evidence. JDK 17, both Android 36
  images, and dedicated arm64/x86_64 AVD configurations are now available, but
  no platform runtime smoke is claimed.

Exit evidence: inventories cover all source roots, the migration matrix has no
unresolved disposition or Host-contract gaps, and baseline commands/results
are recorded.

## Phase 1 — Language and compatibility specification

- [x] Write the Draft 0.1 lexical/syntactic grammar and source encoding rules,
  including UTF-8/BOM recovery, exact literals/operators, precedence, and
  contextual restrictions in `LANGUAGE_GRAMMAR.md`.
- [x] Specify value types, equality, conversions, collections, mutability, and
  JSON interoperation.
- [x] Specify lexical scope, functions, closures, recursion, loops, branching,
  non-local control flow, and module loading sufficient for Turing completeness.
- [x] Specify structured errors, stack traces, source spans, cleanup/defer, and
  host-error translation.
- [ ] Specify futures/tasks, cancellation, deadlines, thread-safety boundaries,
  and deterministic testing hooks.
- [ ] Specify capability-scoped host APIs for image, OCR, device, configuration,
  logging, scheduler, resources, filesystem, and service operations.
- [x] Specify language/runtime/API version negotiation and deprecation policy.
- [x] Specify script/resource manifests, integrity checks, atomic updates,
  rollback, and cache layout.
- [ ] Publish conformance examples and a normative test corpus.
- [ ] Resolve implementation ADRs (AST versus bytecode, ownership/GC strategy,
  module cache, sandbox boundary, and threading model).

Exit evidence: reviewed specification, accepted ADRs, parser fixtures, and a
versioned compatibility contract linked from the migration matrix.

Draft contract evidence: `PACKAGE_VERSIONING.md` defines independent language,
host API, manifest, and package versions plus detached signatures, capability
resolution, immutable cache snapshots, atomic activation, and rollback gates.
`VALUE_SEMANTICS.md` defines the normative value, numeric, equality,
collection, mutability, heap-isolation, JSON, budget, and RT001-RT023 contract;
its required clauses and implementation anchors are checked in Foundation CI.
`CONTROL_FLOW_AND_MODULES.md` defines source-order lexical bindings, closure
capture, recursion/control transfer, deterministic module loading/isolation,
and a constructive two-counter-machine argument; its static anchors and
pending VM/loader boundary are checked in Foundation CI.
`ERRORS_AND_CLEANUP.md` defines compile/runtime error separation, stable Error
envelopes and codes, source/stack/cause contracts, deterministic defer unwind,
and redacted C++ host-failure translation; its static anchors and pending
VM/host-translator boundary are checked in Foundation CI.

## Phase 2 — C++ runtime and developer tooling

- [~] Add standalone `BAAS_script_runtime` and CLI/test targets. The library,
  focused CTest targets, and non-executing `BAAS_script_check` CLI exist; the
  script execution CLI remains pending.
- [x] Implement UTF-8 lexer with stable byte/line/column source diagnostics and
  malformed-input recovery.
- [x] Implement the complete parser and source-spanned immutable AST.
- [x] Implement lexical semantic analysis, declaration/reference resolution,
  closure-capture metadata, and AST node/depth limits.
- [~] Implement values, environments, closures, recursion, collections, and
  control flow. Inline values, every heap cell kind, ordered collections,
  generational references, roots, tracing GC, budgets, equality/truthiness,
  JSON-safety checks, host release queues, and rooted lexical environments with
  parent lookup, shadowing, mutability, and assignment are implemented; closure
  execution, evaluator/VM, recursion, and control flow remain pending.
- [x] Implement the dependency-free ordered JSON value bridge and checked
  JSON-safe cross-context deep-copy foundation.
- [ ] Implement modules, imports, and native-function registration.
- [~] Implement structured exceptions, stack traces, cancellation, and limits.
  The total allocation-free RT001-RT023 language-code/catchability mapping is
  implemented; Error envelopes, stack capture, VM unwinding, executor/host
  translation, cancellation propagation, and execution limits remain pending.
- [x] Implement the bounded cooperative executor, queue backpressure, task
  handles, cancellation requests, and drain/cancel-pending shutdown.
- [ ] Integrate language-level task/future primitives with the VM and executor.
- [~] Provide formatter/linter, syntax checker, language-server path, and script
  package validator. The bounded multi-file/stdin syntax and lexical-semantic
  checker is implemented; formatter, linter policy, LSP, and package validator
  remain pending.
- [ ] Add unit, conformance, property, fuzz, sanitizer, and leak tests.
- [ ] Add benchmarks for parse, load, dispatch, and hot-loop execution.

Exit evidence: conformance corpus passes, memory/error tests pass, and the CLI
can execute multi-module recursive and concurrent programs on Windows x64.

Verified foundation evidence: commit `f0efa76` adds the owning immutable AST,
recursive-descent/Pratt parser, recovery diagnostics, and independent lexer and
parser CTest targets. Coordinator-run MSVC Debug and Release builds both passed
2/2 tests before integration; the integrated Debug build also passed 2/2 tests.
Commit `f0e1985` adds the standard-library-only bounded executor. Independent
MSVC Debug and Release reviews each passed 30 repeated runs; after integration,
all lexer, parser, and executor tests passed 20 repeated Debug runs (3/3).
Commit `9cca7ba` adds lexical semantic analysis. Independent MSVC Debug and
Release reviews each passed 20 repeated lexer/parser/semantic runs; after
integration, all five script/service targets passed 20 repeated Debug runs and
a complete Release run.
Commit `bf7c180` implements the per-context non-moving tracing value heap from
ADR-0002 with stable RT001-RT017 errors, transactional budget accounting,
cycle-aware collections/equality, and bounded host-release records. Independent
Debug and Release reviews passed its five script CTest targets; the integrated
six-target script/service gate is recorded separately after CI review.
The JSON bridge foundation adds an insertion-ordered dependency-free value
model, iterative budgeted heap conversion, and isolated cross-context deep
copy. JSON text parsing/serialization and complete modules/imports remain
pending.

## Phase 3 — Host bindings and Python behavior parity

- [ ] Bind logging and structured events.
- [ ] Bind configuration, schema validation, and persistence.
- [ ] Bind resource lookup, image loading, and versioned manifests.
- [ ] Bind screenshot acquisition and image processing.
- [ ] Bind OCR lifecycle and inference operations.
- [ ] Bind device discovery, ADB/scrcpy/Nemu capture, input, and lifecycle.
- [ ] Bind feature matching and procedures.
- [ ] Bind scheduling, workflow state, retries, deadlines, and cancellation.
- [ ] Add host capability permissions and thread-safety declarations.
- [ ] Add Python-versus-C++ golden parity tests for every matrix entry.

Exit evidence: every required operation has a binding and parity test; no
Python fallback is needed for migrated representative workflows.

## Phase 4 — Service backend and Tauri integration

- [x] Freeze deterministic v1 canonical JSON, crypto-derivation, control
  envelope, and BPIP golden vectors; keep the nondeterministic secret-stream
  header/ciphertext gate explicit.
- [x] Implement the byte-exact BPIP v1 encoder and incremental decoder with
  fragmentation, coalescing, sticky errors, and the 64 MiB preallocation gate.
- [~] Define a versioned service protocol from the observed Tauri contract.
  `SERVICE_PROTOCOL_V1.md` now defines the normative v1 wire/state/security
  contract and explicit missing gates; shared implementations and E2E evidence
  remain pending.
- [~] Implement `cpp-httplib` routing, validation, JSON errors, health/version,
  graceful shutdown, and bounded concurrency.
- [ ] Implement task submission, progress/events, cancellation, and result APIs.
- [ ] Implement configuration and resource update APIs with atomic persistence.
- [ ] Implement authentication/origin/listen-address policy appropriate to local
  desktop and Android deployment.
- [ ] Add protocol contract tests shared with `baas-tauri`.
- [ ] Add lifecycle tests for crash, restart, port conflicts, timeout, and stale
  task cleanup.
- [ ] Run Windows desktop end-to-end integration with `baas-tauri`.

Exit evidence: contract suite and Tauri end-to-end workflows pass under load.

Transport-independent routing foundation: `BAAS_service_router` now provides
dependency-free, in-memory routing on the frozen unversioned `/health` path,
exact method/path matching, bounded request/response bodies, uniform JSON-safe
errors, and foundation-only `/version`/`/shutdown` extensions. The production
`/health` status/auth payload and remaining required v1 routes are still
missing. It opens no listener and performs no shutdown.
`SERVICE_ROUTER_CORE.md` records the boundary and tests; the `cpp-httplib`
adapter, authentication, bounded concurrency, real graceful shutdown, and
Tauri integration remain required, so the broader checklist item stays open.

Optional HTTP adapter foundation: `BAAS_service_http` now requests
`BAAS::httplib` only when explicitly enabled, maps cpp-httplib method/path/body
and Router status/headers/body exactly, and applies method/path/body transport
budgets before Router effects. Its local test binds loopback on an ephemeral
port with bounded readiness/I/O/CTest timeouts and explicit stop/join. It is not
a production listener: auth/origin/listen policy, production lifecycle,
graceful draining, bounded concurrency/load evidence, and Tauri E2E remain
open as recorded in `SERVICE_HTTPLIB_ADAPTER.md`.

Verified foundation evidence: commit `fec6db0` adds production-anchored service
vectors (14/14 Python tests), and commit `8b1ff52` adds the standalone C++ BPIP
framing library. Coordinator-run MSVC Debug and Release reviews each passed 30
repeated framing runs; the integrated four-target Debug suite passed 20 repeats.
Commit `82c6326` adds the normative draft v1 protocol and nine machine checks
for its tagged examples, route/channel inventory, golden BPIP/control bytes,
and explicit missing gates. The combined service-contract suite passes 23/23;
secretstream bytes, live transports, lifecycle, C++ crypto, and Tauri E2E remain
open exactly as listed in the protocol compliance table.

## Phase 5 — Script and resource migration

- [ ] Rank Python modules by dependency fan-out and user value.
- [ ] Provide an AST-assisted inventory/conversion tool for Python operations.
- [ ] Migrate shared helpers and representative low-risk workflows first.
- [ ] Migrate every supported automation module and retain golden traces.
- [ ] Move mutable images/JSON/models into versioned resource packages.
- [ ] Implement update manifests, signature/integrity validation, atomic switch,
  rollback, and forward/backward compatibility checks.
- [ ] Remove Python runtime dependency only after the complete parity gate passes.

Exit evidence: migration matrix is complete, supported workflows pass on the
emulator, and installation no longer requires Python for normal operation.

## Phase 6 — Platforms, performance, and CI/CD

- [~] Windows x64 build, unit, parity, service, performance, and smoke pipelines.
- [ ] Android arm64-v8a/x86_64 build and emulator smoke pipelines.
- [~] Linux x64 and macOS arm64 build/test pipelines for supported components.
- [ ] Cache Conan dependencies and allocate build parallelism without
  oversubscribing concurrent agent jobs.
- [ ] Add race, sanitizer, fuzz, malformed-script, and resource-corruption jobs.
- [ ] Add startup, memory, binary size, throughput, and tail-latency budgets with
  regression thresholds against the Python baseline.
- [ ] Exercise concurrent sessions, OCR, cancellation, service backpressure, and
  long-running stability.

Exit evidence: required platform matrix is green and performance budgets pass.

Foundation evidence: `.github/workflows/foundation-runtime.yml` defines a
six-case Windows/Ubuntu/macOS Debug/Release matrix that builds the script
checker plus eight standalone script/service test executables and runs ten
CTest cases. Debug jobs also validate the checked-in
service vectors. The exact Windows commands passed locally for both build
types after the value-heap addition: all six CTest targets passed 20 repeated
Debug runs and one complete Release run; all 14 service-vector tests also
passed before the protocol-spec additions. The current Debug jobs also run the
service protocol/vector suite and the standard-library migration/baseline tool
suite. After adding the JSON bridge, the current Windows Debug suite passed all
ten CTest cases for 20 consecutive runs and Release passed 10/10 once. The
hosted Linux/macOS jobs, full application/parity/service smoke coverage,
Android, performance budgets, sanitizers, fuzzing, and caches remain pending.
The separate `android-ocr-foundation.yml` matrix now cross-builds and verifies
the current repository's OCR shared libraries for arm64-v8a and x86_64 with
NDK 29 and ABI-scoped Conan caches. Hosted execution and emulator load/OCR/APK
smoke evidence remain pending, so the combined Android checklist stays open.

## Phase 7 — Release, documentation, and completion audit

- [ ] Language reference, host API reference, migration guide, service protocol,
  build guide, debugging guide, and contributor architecture are complete.
- [ ] Branches are integrated through reviewable semantic commits.
- [ ] Release packaging contains runtime, scripts, resources, manifests, and
  notices with deterministic version metadata.
- [ ] Upgrade/rollback drills pass from supported prior versions.
- [ ] Audit every original requirement against authoritative files, test output,
  CI state, package artifacts, and Windows/Android runtime evidence.

Exit evidence: every checklist item is verified and the requirement-by-
requirement completion audit has no missing or indirect evidence.

## Branch, worktree, and commit policy

- Foundation branch: `feat/script-runtime-foundation`.
- Use focused branches such as `feat/script-language-spec`,
  `feat/script-runtime-core`, `feat/script-host-bindings`,
  `feat/script-service`, and `feat/script-migrations`.
- Each parallel writer uses a separate Git worktree. Read-only audits may share
  the main checkout.
- Commits must be semantic and reviewable: one coherent capability plus its
  tests/docs, without mixing unrelated refactors or splitting trivial edits.
- Preserve user changes in adjacent repositories; never reset or overwrite a
  dirty worktree.

## Build resource policy

- Detect logical CPU count before parallel builds.
- Reserve capacity for the coordinator and interactive emulator/service work.
- Give one full-core build exclusive access when link-time optimization or
  dependency compilation is active; otherwise divide cores across independent
  worktrees and set explicit `--parallel` limits.
- Record peak memory and reduce concurrency before paging or thermal throttling.
