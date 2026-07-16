# BAAS synchronous conformance corpus

- Corpus version: `1`
- Language draft: `0.1`
- Engine contract: `synchronous_ast_conformance`, JSON schema `1`
- Registered positive cases: `happy/main`, `nested/valid`
- Registered negative cases: `missing/main`, `escape/main`, `cycle/a`,
  `diagnostic/main`, `runtime_error/main`, `host/main`, `bounds/loop`, and the
  nested valid/missing/Host/cycle/Unicode diagnostic cases under `nested/`

This corpus records only semantics implemented by `SynchronousEvaluator`.
Host modules, async/tasks, structured throw/catch/defer execution, the
production bytecode VM, and production package activation are intentionally
outside version 1. Adding a case requires registering it in
`cmake/ScriptRuntime.cmake`; an unregistered directory is not conformance
evidence.
