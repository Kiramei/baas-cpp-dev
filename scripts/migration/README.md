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
