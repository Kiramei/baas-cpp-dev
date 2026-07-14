# BAAS Python migration validator

This Python 3.11 standard-library tool statically inventories migration-sensitive
data in a `baas-dev` checkout. It never imports the inspected repository.

```powershell
python scripts/migration/validate.py `
  --python-repo ..\baas-dev `
  --output build\migration-report.json `
  --strict
```

Checks cover:

- grid-task JSON types and semantics, with supported `t` operations derived from
  `module/explore_tasks/task_utils.py::run_task_action`;
- literal `src/images/<locale>/x_y_range/**/*.py` mappings and their corresponding
  PNG resources, grouped by locale in the report;
- `self.ocr.<method>(...)` calls under `module/` against methods declared by
  `core/ocr/ocr.py::Baas_ocr`.

JSON reports contain no timestamps or machine-specific paths and are sorted for
stable diffs. Errors always return exit code 1. `--strict` additionally makes
warnings fail. Bad CLI input returns exit code 2.

## Operation-level migration index

`operation_index.py` is a separate Python 3.11 standard-library tool. It does
not change the validator above. It recursively parses every BAAS Python source
root without importing the checkout and records calls, static registries,
route decorators, and string dispatch operations. Rule schema v2 first assigns
each occurrence to a versioned source scope, then makes a conservative
disposition decision for every operation/scope pair. The decisions distinguish
required script Host APIs, script-language/module rewrites, C++ service
internals, legacy UI replaced by Tauri, migration/deployment tooling, tests,
external dependencies, and unresolved expressions.

```powershell
python scripts/migration/operation_index.py `
  --python-repo ..\baas-dev `
  --output docs\script-runtime\evidence\operation-index.json `
  --matrix docs\script-runtime\MIGRATION_MATRIX.md `
  --evidence-link evidence/operation-index.json `
  --strict
```

A normal run returns zero after producing evidence even if gaps remain.
`--strict` returns 1 when an operation/scope disposition is `UNRESOLVED`, a
`HOST_BINDING_REQUIRED` decision lacks its proposed binding/owner/parity
contract, or a source cannot be parsed. The JSON reports unresolved-disposition
and host-binding-gap counts separately. Dynamic calls stay unresolved even
when they occur below `tests/`, `gui/`, or tooling directories; source location
alone never marks an unknown call complete.

Schema v2 retains operation identity version 1. Unchanged
`(kind, call_form, symbol)` identities therefore keep their existing IDs;
corrected absolute/relative import resolution can intentionally retire an old
alias identity and create a corrected one. The generated JSON contains no
timestamps or absolute paths, and the matrix updater replaces only its
generated marker block. Invalid paths, rules, or matrix markers return 2.

## Host-side Python performance baseline

`measure_python_baseline.py` measures fresh-process startup/import cost, RSS,
the legacy and service-injected image-matching algorithms, and logical
repository/runtime sizes.
It launches one child process at a time and deliberately does not start the
FastAPI lifespan, materialize `ServiceContext`, import user configuration,
contact a device/emulator, or initialize OCR.

```powershell
python scripts/migration/measure_python_baseline.py `
  --python-repo ..\baas-dev `
  --output docs\script-runtime\evidence\python-performance-baseline.json `
  --timeout 120
```

The default uses five fresh processes per probe and three `cafe_reward.match`
calls per algorithm process. It reports the legacy module function separately
from the production service path installed by
`service.injection._patch_cafe_reward()`; the latter resets its template cache
once in every fresh process. `--quick` reduces those values to two and one;
explicit `--repetitions` and `--algorithm-iterations` values override either
mode. The benchmark Python defaults to `.venv/Scripts/python.exe` on Windows or
`.venv/bin/python` on POSIX and can be set with `--python-executable`.

The service extension is captured separately so the original timing samples
remain byte-for-byte unchanged. It measures the production `/health` route by
a direct in-process ASGI call, with a temporary service root and guards against
lifespan, OCR/core initialization, background data initialization, and outbound
`requests` HTTP:

```powershell
python scripts/migration/measure_python_service_baseline.py `
  --python-repo ..\baas-dev `
  --output docs\script-runtime\evidence\python-service-performance-baseline.json `
  --repetitions 5 `
  --request-iterations 50 `
  --timeout 60
```

The production lifespan and Uvicorn listener are structured `skipped` results,
because audited startup initializes OCR/core state, watchers, update work, and
a data-initialization thread. The service extension never treats ASGI
request-scope teardown as production shutdown. Validate only the committed
schema and safety boundary with:

```powershell
python scripts/migration/measure_python_service_baseline.py `
  --check-evidence docs\script-runtime\evidence\python-service-performance-baseline.json
```

CI runs mocked controller/schema tests and does not execute or threshold the
performance measurements.

The `process_wall_ms` metric includes interpreter creation and process exit.
The empty-startup probe is exactly `python -I -S -c pass`, so it has no in-child
timer or RSS field. For the other probes, `probe_elapsed_ms` begins inside the
child and import-only probes end as soon as the named imports are ready. The
two `cafe_reward.match` probes use the same GUI import stubs as tests. The
legacy probe leaves the original module function intact, while the production
service probe applies `_patch_cafe_reward()` and verifies the injection marker
before timing. Neither starts the service lifespan. JSON schema and ordering
are deterministic, but timing/RSS samples naturally vary. A missing dependency
or failed probe is recorded with a diagnostic and makes the command return 1;
invalid setup returns 2. Every repetition is a new interpreter process, but
the command does not flush the operating-system file cache.

## Representative Python golden traces

`capture_python_golden_traces.py` executes four pinned, offline workflows from
the clean `baas-dev` opt-in trace revision: configuration snapshot/patch,
service-injected image matching, scheduler queue/heartbeat, and grid-action
dispatch through the traced click facade. Device, OCR, network, service
lifespan, and Tauri paths are not executed.

```powershell
python scripts/migration/capture_python_golden_traces.py `
  --python-repo ..\.worktrees\baas-dev-parity-trace `
  --python-executable ..\baas-dev\.venv\Scripts\python.exe `
  --output docs\script-runtime\evidence\python-golden-traces.json `
  --check
```

The source must be clean at trace commit `3a8f58585b69bf7cf54fe66115352b41f4094aa3`,
whose parent is production commit `75bbacb545bc87e9510d85cbe8034f9180397004`.
The strict schema enforces workflow/category coverage, trace pairing/order,
redaction, hashes, and hard output bounds. `--check` is read-only and reports
the first differing JSON Pointer. See `docs/script-runtime/PYTHON_GOLDEN_TRACES.md`.
