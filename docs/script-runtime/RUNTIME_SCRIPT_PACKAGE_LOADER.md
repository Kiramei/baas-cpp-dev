# Runtime BAAS Script package loader

`BAAS_runtime_script_package_loader` is the composition boundary between an
activated external scripts repository and the BAAS Script parser/runtime. It
discovers source; it does not execute source.

## Capability boundary

The loader accepts only a `RuntimeRepositoryReadView` for repository id
`scripts`. It never accepts, stores, or returns a native filesystem path. The
entry point is a canonical, extensionless package module id. Every package
module is translated by `ModuleSpecifier::manifest_source_path()` to its exact
`.baas` logical path and read through the immutable view. Consequently:

- only entries in the verified repository manifest can be read;
- spelling and case are byte-exact, with no case folding or path probing;
- the pinned repository generation remains authoritative throughout the load;
- files created outside that manifest are invisible even if they exist below
  the repository root;
- no source or default runtime repository is compiled into the executable.

The repository layer remains independent of BAAS Script. This target lives in
`runtime/script` and links the repository capability and script runtime in one
direction; neither lower layer depends on this loader.

## Discovery and validation

The loader parses and semantically analyzes the entry source, reads semantic
imports in source order, and recursively repeats the process for package
imports. Package sources become owned `SourceModule` values. `baas/...` host
imports are retained as logical requirements but are never converted to file
names or read from the repository.

After discovery, the complete definitions are passed to
`validate_module_graph`. Missing definitions and cycles fail closed. Successful
`SourceModule` output follows the validated dependency-before-importer
initialization order. The result is only a package snapshot; constructing an
evaluator and executing an entry function are separate production steps.

## Bounded and cancellable operation

Independent limits cover module count, source bytes per file, aggregate source
bytes, import edges, longest package-import depth, semantic AST nodes and
nesting, canonical specifier size/depth, and aggregate loader/graph work. All
limits are checked before returning a package. Overflow and invalid zero limits
fail closed.

A supplied `std::stop_token` is checked before discovery, around every verified
repository read, between parse/semantic stages, during import traversal, and
before/after graph validation. Repository hashing also consumes the same token.
Parsing and graph validation are bounded synchronous calls, so cancellation is
observed at the next stage boundary rather than by terminating a native thread.

Failures use stable `RSPnnn_*` codes and may identify only the canonical logical
module id. Parse and semantic failures retain structured language diagnostics;
repository exceptions never expose a native path.

## Build and CI

The standalone target is opt-in:

```text
-DBUILD_RUNTIME_SCRIPT_PACKAGE_LOADER=ON
-DBUILD_RUNTIME_SCRIPT_PACKAGE_LOADER_TESTS=ON
```

Tests construct a real temporary two-repository snapshot, open its verified
`RuntimeRepositoryReadBundle`, and use only `bundle.scripts()` when invoking the
loader. Foundation CI builds and runs the tests on Windows, Linux, and macOS in
Debug and Release. Its Android arm64-v8a and x86_64 jobs compile the production
loader against the NDK without embedding test repositories or source files.
