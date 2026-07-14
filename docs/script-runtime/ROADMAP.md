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
  The generated operation index tracks every discovered item, but 4,022 remain
  explicitly `UNCLASSIFIED`; see `OPERATION_INDEX_AUDIT.md`.
- [x] Implement an opt-in, deterministic, bounded Python parity trace foundation
  on `feat/cpp-parity-trace` without changing the default execution path.
- [x] Add a deterministic static migration validator for grid actions, image
  mappings, and OCR calls, with fixture tests and real-repository reporting.
- [ ] Capture baseline Python golden traces for representative workflows.
- [ ] Capture baseline latency, memory, startup, throughput, and package-size
  measurements on Windows x64.
- [ ] Record platform and emulator smoke-test prerequisites.

Exit evidence: inventories cover all source roots, the migration matrix has no
unclassified operation families, and baseline commands/results are recorded.

## Phase 1 — Language and compatibility specification

- [ ] Write the lexical grammar and source encoding rules.
- [ ] Specify value types, equality, conversions, collections, mutability, and
  JSON interoperation.
- [ ] Specify lexical scope, functions, closures, recursion, loops, branching,
  non-local control flow, and module loading sufficient for Turing completeness.
- [ ] Specify structured errors, stack traces, source spans, cleanup/defer, and
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

## Phase 2 — C++ runtime and developer tooling

- [ ] Add standalone `BAAS_script_runtime` and CLI/test targets.
- [x] Implement UTF-8 lexer with stable byte/line/column source diagnostics and
  malformed-input recovery.
- [x] Implement the complete parser and source-spanned immutable AST.
- [x] Implement lexical semantic analysis, declaration/reference resolution,
  closure-capture metadata, and AST node/depth limits.
- [ ] Implement values, environments, closures, recursion, collections, and
  control flow.
- [ ] Implement modules, imports, native-function registration, and JSON bridge.
- [ ] Implement structured exceptions, stack traces, cancellation, and limits.
- [x] Implement the bounded cooperative executor, queue backpressure, task
  handles, cancellation requests, and drain/cancel-pending shutdown.
- [ ] Integrate language-level task/future primitives with the VM and executor.
- [ ] Provide formatter/linter, syntax checker, language-server path, and script
  package validator.
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
- [ ] Define a versioned service protocol from the observed Tauri contract.
- [ ] Implement `cpp-httplib` routing, validation, JSON errors, health/version,
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

Verified foundation evidence: commit `fec6db0` adds production-anchored service
vectors (14/14 Python tests), and commit `8b1ff52` adds the standalone C++ BPIP
framing library. Coordinator-run MSVC Debug and Release reviews each passed 30
repeated framing runs; the integrated four-target Debug suite passed 20 repeats.

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

- [ ] Windows x64 build, unit, parity, service, performance, and smoke pipelines.
- [ ] Android arm64-v8a/x86_64 build and emulator smoke pipelines.
- [ ] Linux x64 and macOS arm64 build/test pipelines for supported components.
- [ ] Cache Conan dependencies and allocate build parallelism without
  oversubscribing concurrent agent jobs.
- [ ] Add race, sanitizer, fuzz, malformed-script, and resource-corruption jobs.
- [ ] Add startup, memory, binary size, throughput, and tail-latency budgets with
  regression thresholds against the Python baseline.
- [ ] Exercise concurrent sessions, OCR, cancellation, service backpressure, and
  long-running stability.

Exit evidence: required platform matrix is green and performance budgets pass.

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
