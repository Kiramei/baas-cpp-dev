from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.migration.operation_index import (
    BEGIN_MARKER,
    END_MARKER,
    OperationIndexer,
    RuleSet,
    normalize_path_text,
    render_generated_matrix,
    stable_json,
    update_matrix,
)


HERE = Path(__file__).resolve().parent
FIXTURE_REPO = HERE / "fixtures" / "operation_repo"
INDEXER = HERE.parent / "operation_index.py"
RULES = HERE.parent / "operation_rules.v4.json"


class OperationIndexTests(unittest.TestCase):
    def generate(self, root: Path = FIXTURE_REPO) -> dict:
        return OperationIndexer(root, RuleSet(RULES)).generate()

    @staticmethod
    def scope_decision(operation: dict, source_scope: str) -> dict:
        return next(
            decision
            for decision in operation["scope_decisions"]
            if decision["source_scope"] == source_scope
        )

    def test_fixture_covers_alias_member_self_super_chained_and_dynamic(self) -> None:
        report = self.generate()
        by_symbol = {operation["symbol"]: operation for operation in report["operations"]}

        self.assertEqual(by_symbol["core.picture.co_detect"]["call_form"], "alias-member")
        self.assertEqual(by_symbol["self.click"]["call_form"], "self")
        self.assertEqual(by_symbol["self.click"]["occurrences"], 3)
        self.assertEqual(by_symbol["self.ocr.recognize"]["family"], "ocr.inference")
        self.assertEqual(by_symbol["obj.member"]["call_form"], "member")
        self.assertEqual(by_symbol["super().run"]["call_form"], "super")
        self.assertEqual(
            by_symbol["dynamic:call-result(factory)().send"]["call_form"],
            "chained",
        )
        dynamic = next(
            operation
            for operation in report["operations"]
            if operation["symbol"].startswith("dynamic:getattr(self,?)")
        )
        self.assertEqual(dynamic["migration_status"], "UNRESOLVED")
        self.assertEqual(dynamic["disposition"], "UNRESOLVED")
        self.assertEqual(dynamic["resolution"], "dynamic")

        alias = by_symbol["core.device.Control.Control"]
        self.assertEqual(alias["call_form"], "alias")
        self.assertEqual(alias["family"], "device.input-capture")
        self.assertEqual(alias["disposition"], "HOST_BINDING_REQUIRED")

    def test_relative_imports_package_init_shadowing_and_rebinding(self) -> None:
        report = self.generate()
        operations = report["operations"]
        symbols = {operation["symbol"] for operation in operations}

        self.assertIn("module.pkg.helpers.execute", symbols)
        self.assertIn("module.pkg.helpers.run", symbols)
        self.assertIn("module.pkg.sub.local.initialize", symbols)
        self.assertIn("module.pkg.sub.local.go", symbols)
        self.assertIn("core.image.detect", symbols)
        self.assertIn("image.detect", symbols)
        self.assertIn("builtins.object.co_detect", symbols)
        self.assertIn("conditional_alias.co_detect", symbols)

        resolved_image = next(
            operation for operation in operations if operation["symbol"] == "core.image.detect"
        )
        unresolved_image = next(
            operation for operation in operations if operation["symbol"] == "image.detect"
        )
        self.assertEqual(resolved_image["occurrences"], 2)
        self.assertEqual(resolved_image["resolution"], "resolved")
        self.assertEqual(unresolved_image["occurrences"], 3)
        self.assertEqual(unresolved_image["resolution"], "unresolved")

        resolved_picture = next(
            operation
            for operation in operations
            if operation["symbol"] == "core.picture.co_detect"
            and operation["call_form"] == "alias-member"
        )
        self.assertGreaterEqual(resolved_picture["occurrences"], 3)
        identical_branch = next(
            operation
            for operation in operations
            if operation["symbol"] == "core.picture.co_detect"
            and operation["call_form"] == "alias-member"
        )
        ambiguous_branch = next(
            operation
            for operation in operations
            if operation["symbol"] == "conditional_alias.co_detect"
        )
        self.assertEqual(identical_branch["resolution"], "resolved")
        self.assertEqual(ambiguous_branch["resolution"], "unresolved")

    def test_provable_value_types_constructors_and_star_exports_are_resolved(self) -> None:
        report = self.generate()
        operations = report["operations"]
        by_symbol = {operation["symbol"]: operation for operation in operations}

        inferred_symbols = {
            "argparse.ArgumentParser.add_argument",
            "builtins.dict.get",
            "builtins.list.append",
            "builtins.str.strip",
            "io.IOBase.write",
            "module.pkg.types.LocalWorker.run",
            "pathlib.Path.exists",
            "pathlib.Path.resolve",
        }
        self.assertTrue(inferred_symbols.issubset(by_symbol))
        for symbol in inferred_symbols:
            self.assertIn(by_symbol[symbol]["resolution"], {"inferred", "resolved"})

        dynamic = next(
            operation
            for operation in operations
            if operation["symbol"] == "dynamic:call-result(factory)().send"
        )
        self.assertEqual(dynamic["resolution"], "dynamic")
        unknown_attribute = by_symbol[
            "dynamic:attribute-result(module.pkg.types.LocalWorker.client).send"
        ]
        self.assertEqual(unknown_attribute["resolution"], "dynamic")
        unresolved = next(
            operation for operation in operations if operation["symbol"] == "candidate.get"
        )
        self.assertEqual(unresolved["resolution"], "unresolved")
        self.assertEqual(by_symbol["any_value.get"]["resolution"], "unresolved")
        self.assertEqual(by_symbol["builtins.float.as_integer_ratio"]["resolution"], "inferred")
        self.assertEqual(by_symbol["picture.co_detect"]["resolution"], "unresolved")
        self.assertEqual(by_symbol["Hidden"]["resolution"], "unresolved")
        self.assertEqual(by_symbol["PrivateWorker"]["resolution"], "unresolved")

    def test_scope_dispositions_and_host_gaps_are_separate(self) -> None:
        report = self.generate()
        by_symbol = {operation["symbol"]: operation for operation in report["operations"]}

        cases = {
            "PyQt5.QtWidgets.QApplication": ("LEGACY_GUI", "TAURI_UI_REPLACED"),
            "json.loads": ("MIGRATION_TOOLING", "MIGRATION_TOOLING_ONLY"),
            "shutil.copy": ("DEPLOYMENT_TOOLING", "MIGRATION_TOOLING_ONLY"),
            "module.pkg.helpers.run": ("TEST", "TEST_ONLY"),
            "thirdparty.run": ("SCRIPT_RUNTIME", "EXTERNAL_DEPENDENCY"),
        }
        for symbol, (source_scope, disposition) in cases.items():
            decision = self.scope_decision(by_symbol[symbol], source_scope)
            self.assertEqual(decision["disposition"], disposition, symbol)
            self.assertEqual(decision["host_binding_gap_fields"], [], symbol)

        process = self.scope_decision(by_symbol["subprocess.run"], "SCRIPT_RUNTIME")
        self.assertEqual(process["disposition"], "HOST_BINDING_REQUIRED")
        self.assertEqual(process["host_binding_gap_fields"], [])
        self.assertEqual(process["cpp_host_binding"], "baas::script::host::ProcessHost")
        self.assertEqual(process["owner"], "Runtime Privileged I/O")
        self.assertEqual(process["parity_test_id"], "PARITY-PROCESS-HOST")
        self.assertGreater(report["summary"]["unresolved_disposition_scope_decisions"], 0)
        self.assertEqual(report["summary"]["host_binding_gaps"], 0)

    def test_frozen_source_boundaries_do_not_require_receiver_inference(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory) / "repo"
            sources = {
                "service/api.py": "CPP_SERVICE",
                "gui/view.py": "LEGACY_GUI",
                "tests/test_unknown.py": "TEST",
                "deploy/tool.py": "DEPLOYMENT_TOOLING",
                "develop_tools/tool.py": "MIGRATION_TOOLING",
                "module/task.py": "SCRIPT_RUNTIME",
            }
            for relative in sources:
                path = repository / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    "def run(value):\n"
                    "    value.unknown()\n"
                    "    getattr(value, 'dynamic_unknown')()\n",
                    encoding="utf-8",
                )

            report = self.generate(repository)
            expected = {
                "CPP_SERVICE": "CPP_SERVICE_INTERNAL",
                "LEGACY_GUI": "TAURI_UI_REPLACED",
                "TEST": "TEST_ONLY",
                "DEPLOYMENT_TOOLING": "MIGRATION_TOOLING_ONLY",
                "MIGRATION_TOOLING": "MIGRATION_TOOLING_ONLY",
                "SCRIPT_RUNTIME": "UNRESOLVED",
            }
            covered_scopes: set[str] = set()
            for operation in report["operations"]:
                if operation["resolution"] not in {"dynamic", "unresolved"}:
                    continue
                for decision in operation["scope_decisions"]:
                    covered_scopes.add(decision["source_scope"])
                    self.assertEqual(
                        decision["disposition"],
                        expected[decision["source_scope"]],
                        (operation["symbol"], decision["source_scope"]),
                    )
                    if decision["source_scope"] != "SCRIPT_RUNTIME":
                        self.assertTrue(
                            decision["classification_rule"].endswith("-boundary-v4")
                        )
            self.assertEqual(covered_scopes, set(sources.values()))

    def test_privileged_script_boundaries_precede_dynamic_and_stdlib_defaults(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory) / "repo"
            sources = {
                "core/Baas_thread.py": (
                    "import win32com.client\n"
                    "def run(shell, path):\n"
                    "    win32com.client.Dispatch('WScript.Shell')\n"
                    "    shell.CreateShortCut(path)\n"
                ),
                "core/device/emulator_manager/probe.py": (
                    "import winreg\n"
                    "def run():\n"
                    "    winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, 'key')\n"
                    "    bst_read_registry_key('')\n"
                ),
                "core/device/nemu_client.py": (
                    "import ctypes\n"
                    "def run(file_out, length):\n"
                    "    ctypes.pointer((ctypes.c_ubyte * length)())\n"
                    "    file_out.fileno()\n"
                ),
                "core/device/window_capture/windows/window_info.py": (
                    "def run(handle):\n"
                    "    monitor_from_window(handle, 2)\n"
                    "    get_monitor_info(handle, None)\n"
                ),
                "core/device/control/pyautogui.py": (
                    "def run(user32):\n"
                    "    user32.SystemParametersInfoA(1, 2, 3, 4)\n"
                ),
                "core/notification.py": "def run():\n    _notify(title='done')\n    _toast(title='done')\n",
                "core/exception.py": "def run(context):\n    context.send('stop')\n",
                "core/ipc_manager.py": (
                    "class SharedMemory:\n"
                    "    shm_map = {}\n"
                    "def run(name):\n"
                    "    SharedMemory.shm_map[name]._release()\n"
                ),
                "core/ocr/baas_ocr_client/server_installer.py": (
                    "import pygit2, zipfile\n"
                    "def run(repo, archive):\n"
                    "    pygit2.init_repository('repo')\n"
                    "    repo.reset(None, 0)\n"
                    "    archive.extractall('dst')\n"
                ),
                "core/ocr/ocr.py": "def run(s):\n    s.bind(('127.0.0.1', 0))\n",
                "core/device/scrcpy/core.py": (
                    "import av\n"
                    "def run(codec, frame):\n"
                    "    av.CodecContext.create('h264', 'r')\n"
                    "    codec.parse(b'data')\n"
                    "    frame.to_ndarray(format='bgr24')\n"
                ),
                "module/ordinary.py": (
                    "def run(obj, left, right):\n"
                    "    obj.bind()\n"
                    "    obj.fileno()\n"
                    "    obj.extractall('dst')\n"
                    "    (left * right)()\n"
                ),
                "core/device/other.py": "def run(left, right):\n    (left * right)()\n",
                "core/ocr/other.py": "def run(repo):\n    repo.fetch()\n",
            }
            for relative, source in sources.items():
                path = repository / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source, encoding="utf-8")

            report = self.generate(repository)
            decisions_by_file: dict[str, list[dict]] = {}
            for operation in report["operations"]:
                for decision in operation["scope_decisions"]:
                    for location in decision["representative_locations"]:
                        decisions_by_file.setdefault(location["file"], []).append(decision)

            expected_rules = {
                "core/Baas_thread.py": {"windows-shortcut-tooling-boundary-v4"},
                "core/device/emulator_manager/probe.py": {"emulator-registry-device-boundary-v4"},
                "core/device/nemu_client.py": {
                    "device-descriptor-boundary-v4",
                    "nemu-native-device-boundary-v4",
                },
                "core/device/window_capture/windows/window_info.py": {"window-input-device-boundary-v4"},
                "core/device/control/pyautogui.py": {"window-input-device-boundary-v4"},
                "core/notification.py": {"notification-host-boundary-v4"},
                "core/exception.py": {"raw-ipc-service-boundary-v4"},
                "core/ocr/baas_ocr_client/server_installer.py": {"ocr-updater-tooling-boundary-v4"},
                "core/ocr/ocr.py": {"socket-listener-service-boundary-v4"},
                "core/device/scrcpy/core.py": {"vision-host-v2"},
            }
            for filename, rules in expected_rules.items():
                with self.subTest(filename=filename):
                    self.assertTrue(decisions_by_file[filename])
                    self.assertEqual(
                        {item["classification_rule"] for item in decisions_by_file[filename]},
                        rules,
                        decisions_by_file[filename],
                    )
            self.assertEqual(
                {
                    item["classification_rule"]
                    for item in decisions_by_file["core/ipc_manager.py"]
                },
                {"raw-ipc-service-boundary-v4"},
            )
            for filename in ("module/ordinary.py", "core/device/other.py", "core/ocr/other.py"):
                self.assertTrue(
                    all(item["disposition"] == "UNRESOLVED" for item in decisions_by_file[filename]),
                    filename,
                )

    def test_audited_module_receivers_are_exact_and_preserve_unknown_core_calls(self) -> None:
        cases = {
            "module/cafe_reward.py": (
                "def run(name, ret_queue, self):\n"
                "    name.replace('a', 'b')\n"
                "    ret_queue.put(1)\n"
                "    self.u2().pinch_in(percent=50)\n",
                {
                    "name.replace": ("audited-module-intrinsics-v4", "SCRIPT_LANGUAGE_OR_MODULE"),
                    "ret_queue.put": ("scheduler-host-v2", "HOST_BINDING_REQUIRED"),
                    "dynamic:call-result(self.u2)().pinch_in": (
                        "device-host-v2",
                        "HOST_BINDING_REQUIRED",
                    ),
                },
            ),
            "module/total_assault.py": (
                "def run(unable_to_fight_formation):\n"
                "    return unable_to_fight_formation.all()\n",
                {
                    "unable_to_fight_formation.all": (
                        "vision-host-v2",
                        "HOST_BINDING_REQUIRED",
                    )
                },
            ),
        }
        for relative, (source, expected) in cases.items():
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as directory:
                repository = Path(directory) / "repo"
                path = repository / relative
                path.parent.mkdir(parents=True)
                path.write_text(source, encoding="utf-8")
                report = self.generate(repository)
                by_symbol = {item["symbol"]: item for item in report["operations"]}
                for symbol, (rule, disposition) in expected.items():
                    decision = self.scope_decision(by_symbol[symbol], "SCRIPT_RUNTIME")
                    self.assertEqual(decision["classification_rule"], rule, symbol)
                    self.assertEqual(decision["disposition"], disposition, symbol)

        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory) / "repo"
            path = repository / "core" / "other.py"
            path.parent.mkdir(parents=True)
            path.write_text(
                "def run(name):\n    return name.replace('a', 'b')\n",
                encoding="utf-8",
            )
            report = self.generate(repository)
            operation = next(item for item in report["operations"] if item["symbol"] == "name.replace")
            decision = self.scope_decision(operation, "SCRIPT_RUNTIME")
            self.assertEqual(decision["disposition"], "UNRESOLVED")

    def test_exact_type_guards_and_proven_container_elements_propagate_conservatively(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory) / "repo"
            module = repository / "module" / "only.py"
            module.parent.mkdir(parents=True)
            module.write_text(
                "def guarded(item):\n"
                "    if type(item) is str:\n"
                "        parts = item.split(',')\n"
                "        parts[0].isdigit()\n"
                "    item.split(',')\n"
                "def annotated(values: list[str], mapping: dict[str, int]):\n"
                "    values[0].strip()\n"
                "    mapping['answer'].bit_length()\n"
                "def literal():\n"
                "    ['text'][0].strip()\n"
                "    {'answer': 42}['answer'].bit_length()\n",
                encoding="utf-8",
            )

            report = self.generate(repository)
            by_symbol = {item["symbol"]: item for item in report["operations"]}
            for symbol in (
                "builtins.int.bit_length",
                "builtins.str.isdigit",
                "builtins.str.split",
                "builtins.str.strip",
            ):
                with self.subTest(symbol=symbol):
                    decision = self.scope_decision(by_symbol[symbol], "SCRIPT_RUNTIME")
                    self.assertEqual(decision["disposition"], "SCRIPT_LANGUAGE_OR_MODULE")
                    self.assertEqual(decision["classification_rule"], "language-builtins-v2")
            outside_guard = self.scope_decision(by_symbol["item.split"], "SCRIPT_RUNTIME")
            self.assertEqual(outside_guard["disposition"], "UNRESOLVED")

    def test_proven_factory_iteration_and_nested_definition_types_are_conservative(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory) / "repo"
            module = repository / "module" / "only.py"
            module.parent.mkdir(parents=True)
            module.write_text(
                "import adbutils\n"
                "import logging\n"
                "import psutil\n"
                "import re\n"
                "import socket\n"
                "import subprocess\n"
                "import threading\n"
                "def run():\n"
                "    class Process:\n"
                "        def wait(self):\n"
                "            return None\n"
                "    def nested():\n"
                "        return 1\n"
                "    nested()\n"
                "    Process().wait()\n"
                "    with open('input.bin', 'rb') as stream:\n"
                "        stream.read()\n"
                "    adbutils.adb.device().shell('id')\n"
                "    logging.StreamHandler().setFormatter(None)\n"
                "    psutil.Process(1).wait()\n"
                "    socket.socket().close()\n"
                "    subprocess.Popen(['tool']).communicate()\n"
                "    threading.Timer(1, nested).start()\n"
                "    re.match('x', 'x').group(0)\n"
                "    for match in re.finditer('x', 'x'):\n"
                "        match.groups()\n"
                "    for process in psutil.process_iter():\n"
                "        process.cmdline()\n",
                encoding="utf-8",
            )
            report = self.generate(repository)
            by_symbol = {operation["symbol"]: operation for operation in report["operations"]}

            expected = {
                "module.only.run.nested": "SCRIPT_LANGUAGE_OR_MODULE",
                "module.only.run.Process.wait": "SCRIPT_LANGUAGE_OR_MODULE",
                "io.IOBase.read": "SCRIPT_LANGUAGE_OR_MODULE",
                "adbutils.AdbDevice.shell": "HOST_BINDING_REQUIRED",
                "logging.Handler.setFormatter": "HOST_BINDING_REQUIRED",
                "psutil.Process.wait": "HOST_BINDING_REQUIRED",
                "socket.socket.close": "HOST_BINDING_REQUIRED",
                "subprocess.Popen.communicate": "HOST_BINDING_REQUIRED",
                "threading.Timer.start": "HOST_BINDING_REQUIRED",
                "re.Match.group": "SCRIPT_LANGUAGE_OR_MODULE",
                "re.Match.groups": "SCRIPT_LANGUAGE_OR_MODULE",
                "psutil.Process.cmdline": "HOST_BINDING_REQUIRED",
            }
            for symbol, disposition in expected.items():
                with self.subTest(symbol=symbol):
                    self.assertIn(symbol, by_symbol)
                    self.assertEqual(by_symbol[symbol]["disposition"], disposition)
                    self.assertNotIn(by_symbol[symbol]["resolution"], {"dynamic", "unresolved"})

    def test_fixture_discovers_registries_routes_dispatch_and_parse_errors(self) -> None:
        report = self.generate()
        symbols = {operation["symbol"] for operation in report["operations"]}

        self.assertIn("registry:core.Baas_thread.func_dict:sample_task", symbols)
        self.assertIn("dispatch:operation:click*", symbols)
        self.assertIn("dispatch:operation:end-turn", symbols)
        self.assertIn("route:POST:/command", symbols)
        self.assertIn("dispatch:command:status", symbols)
        self.assertIn("dispatch:command:start", symbols)
        self.assertIn("dispatch:command:stop", symbols)
        self.assertEqual(report["summary"]["parse_errors"], 1)
        self.assertEqual(report["unresolved_sources"][0]["file"], "broken.py")

    def test_shapes_locations_and_paths_are_deterministic(self) -> None:
        first = stable_json(self.generate())
        second = stable_json(self.generate())
        self.assertEqual(first, second)
        self.assertNotIn(str(FIXTURE_REPO), first)
        self.assertNotIn("\\", first)
        self.assertEqual(normalize_path_text(r"core\device\Control.py"), "core/device/Control.py")

        report = json.loads(first)
        click = next(operation for operation in report["operations"] if operation["symbol"] == "self.click")
        self.assertEqual(
            [shape["shape"] for shape in click["call_shapes"]],
            [
                "pos=2;kw=-;*args=0;**kwargs=0;await=false",
                "pos=2;kw=wait_over;*args=0;**kwargs=0;await=false",
            ],
        )
        self.assertLessEqual(len(click["representative_locations"]), 3)
        self.assertEqual(click["id"], "op-8446093f5f2315e0")
        self.assertEqual(report["schema_version"], 2)
        self.assertEqual(report["identity_version"], 1)
        self.assertEqual(report["generator_version"], "4.1.0")
        self.assertEqual(report["rules"]["rules_version"], 4)

    def test_strict_fails_for_unresolved_disposition_and_parse_error(self) -> None:
        process = subprocess.run(
            [
                sys.executable,
                str(INDEXER),
                "--python-repo",
                str(FIXTURE_REPO),
                "--rules",
                str(RULES),
                "--strict",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(process.returncode, 1, process.stderr)
        report = json.loads(process.stdout)
        self.assertGreater(report["summary"]["unresolved_disposition_scope_decisions"], 0)
        self.assertEqual(report["summary"]["host_binding_gaps"], 0)
        self.assertEqual(report["summary"]["parse_errors"], 1)

    def test_strict_succeeds_for_fully_classified_fixture(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory) / "repo"
            repository.mkdir()
            (repository / "module").mkdir()
            (repository / "module" / "only.py").write_text(
                "def run(self):\n    self.click(1, 2, wait_over=True)\n",
                encoding="utf-8",
            )
            process = subprocess.run(
                [
                    sys.executable,
                    str(INDEXER),
                    "--python-repo",
                    str(repository),
                    "--rules",
                    str(RULES),
                    "--strict",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(process.returncode, 0, process.stderr)
            report = json.loads(process.stdout)
            self.assertEqual(report["summary"]["operation_count"], 1)
            self.assertEqual(report["summary"]["unresolved_disposition_scope_decisions"], 0)
            self.assertEqual(report["summary"]["host_binding_gaps"], 0)

    def test_strict_reports_unresolved_and_host_binding_gaps_independently(self) -> None:
        cases = (
            (
                "import aiohttp\naiohttp.get('https://example.invalid')\n",
                0,
                1,
            ),
            (
                "def run(obj):\n    obj.unknown()\n",
                1,
                0,
            ),
        )
        for source, unresolved, host_gaps in cases:
            with self.subTest(unresolved=unresolved, host_gaps=host_gaps):
                with tempfile.TemporaryDirectory() as directory:
                    repository = Path(directory) / "repo"
                    module = repository / "module"
                    module.mkdir(parents=True)
                    (module / "only.py").write_text(source, encoding="utf-8")
                    process = subprocess.run(
                        [
                            sys.executable,
                            str(INDEXER),
                            "--python-repo",
                            str(repository),
                            "--rules",
                            str(RULES),
                            "--strict",
                        ],
                        check=False,
                        capture_output=True,
                        text=True,
                    )
                    report = json.loads(process.stdout)

                self.assertEqual(process.returncode, 1, process.stderr)
                self.assertEqual(
                    report["summary"]["unresolved_disposition_scope_decisions"],
                    unresolved,
                )
                self.assertEqual(report["summary"]["host_binding_gaps"], host_gaps)

    def test_matrix_marker_update_preserves_manual_sections(self) -> None:
        report = self.generate()
        generated = render_generated_matrix(report, "evidence/operation-index.json")
        with tempfile.TemporaryDirectory() as directory:
            matrix = Path(directory) / "MIGRATION_MATRIX.md"
            matrix.write_text("# Manual\n\nKeep this text.\n", encoding="utf-8")
            update_matrix(matrix, generated)
            first = matrix.read_text(encoding="utf-8")
            update_matrix(matrix, generated)
            second = matrix.read_text(encoding="utf-8")

        self.assertEqual(first, second)
        self.assertIn("Keep this text.", first)
        self.assertEqual(first.count(BEGIN_MARKER), 1)
        self.assertEqual(first.count(END_MARKER), 1)
        for operation in report["operations"]:
            self.assertIn(operation["id"], first)

    def test_output_is_independent_of_repository_copy_location(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            first_root = Path(directory) / "one"
            second_root = Path(directory) / "two"
            shutil.copytree(FIXTURE_REPO, first_root)
            shutil.copytree(FIXTURE_REPO, second_root)
            first = self.generate(first_root)
            second = self.generate(second_root)
        self.assertEqual(first, second)

    def test_rules_digest_is_independent_of_line_endings(self) -> None:
        content = RULES.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            lf = Path(directory) / "lf.json"
            crlf = Path(directory) / "crlf.json"
            lf.write_bytes(content.replace("\r\n", "\n").encode("utf-8"))
            crlf.write_bytes(content.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
            self.assertEqual(RuleSet(lf).sha256, RuleSet(crlf).sha256)


if __name__ == "__main__":
    unittest.main()
