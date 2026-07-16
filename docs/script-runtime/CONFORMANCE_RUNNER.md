# Synchronous conformance runner

`BAAS_script_run` is an executable boundary around the existing bounded
`SynchronousEvaluator`. It is a deterministic development and CI oracle for
the currently implemented synchronous AST semantics. It is **not** the
production bytecode VM, a package activation loader, or a security sandbox.
Async/tasks and live Host modules remain unsupported by this tool. The
synchronous oracle executes structured `throw`, `try`/`catch`, and per-function
`defer` cleanup with bounded Error values.

## Invocation and package mapping

```text
BAAS_script_run --package-root DIR --entry happy/main [--export result]
```

The canonical package ID `happy/main` maps only to
`DIR/happy/main.baas`. Imports use the same package-root-relative canonical ID
form; source-level `.` and `..` relative specifiers are deliberately invalid.
The loader reads only the entry module's transitive package imports, rejects
Host imports, validates syntax while discovering imports, validates the final
snapshot with `ModuleGraph`, then constructs `SynchronousEvaluator`.

The local runner rejects noncanonical IDs, exact-case mismatches, filesystem
links/aliases, paths that resolve outside the package root, non-regular files,
and bounded read changes. Separate source/module, loader path/depth/work,
evaluator, heap/JSON bridge, and serialized-output limits constrain CI use.
These checks do not provide handle-relative, race-free production package
activation; that loader remains a separate ROADMAP item.

## Stable process contract

Except for the human-readable `--help` output, stdout contains one JSON object
and stderr remains empty. Schema 1 success has
the fixed fields `schema_version`, `engine`, `ok`, `entry`, `export`, `value`,
and deterministic evaluator `stats`. Errors have `schema_version`, `engine`,
`ok`, and an `error` object containing stable `phase`, `code`, and `message`,
plus `module`, `span`, or `steps` when applicable. Absolute host paths and raw
unexpected C++ exception text are not exposed.

Exit code `0` means successful execution, `1` means the tested package failed
loading, compilation, execution, or result conversion, and `2` means invalid
CLI usage or runner infrastructure failure.

## Corpus

The registered corpus is `tests/script/conformance/v1`, with metadata in its
`CORPUS.md`. CTest runs each process twice and requires byte-identical stdout.
The positive cases cover package imports and module caching, nested imports,
closures, recursion/defaults, loops and control transfer, structured Error
identity and cleanup, ordered collections, values, and JSON result conversion.
Negative cases cover missing modules, path escape, cycles, diagnostics, Host
rejection, runtime errors, and deterministic bounds.

`BAAS_script_run_python_cleanup_parity` compares deterministic Python
`try`/`except`/`finally` ordering with the equivalent synchronous script trace.
It also fixes the intentional ERR-014 difference: Python replaces the active
exception when `finally` raises, while BAAS keeps the existing primary Error and
records cleanup failures in `suppressed` unless terminal priority applies.
Nested function declarations, function expressions, and blocks are included so
the executable graph evidence covers the complete semantic import inventory.
Every captured stdout file is decoded as strict UTF-8, parsed by a standard JSON
parser, compared with its golden envelope, and repeated byte-for-byte.
