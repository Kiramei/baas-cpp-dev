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
route decorators, and string dispatch operations. A versioned rule file adds
an owner, proposed C++ host binding, parity-test ID, and migration status only
when a conservative rule matches.

```powershell
python scripts/migration/operation_index.py `
  --python-repo ..\baas-dev `
  --output docs\script-runtime\evidence\operation-index.json `
  --matrix docs\script-runtime\MIGRATION_MATRIX.md `
  --evidence-link evidence/operation-index.json `
  --strict
```

A normal run returns zero after producing evidence even if gaps remain, while
`--strict` returns 1 when any operation is `UNCLASSIFIED` or any source cannot
be parsed. Invalid paths, rules, or matrix markers return 2. Dynamic calls are
always preserved as unclassified evidence. The generated JSON contains no
timestamps or absolute paths; the matrix updater replaces only its generated
marker block.

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
