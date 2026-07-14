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
