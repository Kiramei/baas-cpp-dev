import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ConformanceRunnerContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cmake = (ROOT / "cmake/ScriptRuntime.cmake").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github/workflows/foundation-runtime.yml").read_text(
            encoding="utf-8"
        )
        cls.guide = (ROOT / "docs/script-runtime/CONFORMANCE_RUNNER.md").read_text(
            encoding="utf-8"
        )
        cls.roadmap = (ROOT / "docs/script-runtime/ROADMAP.md").read_text(encoding="utf-8")
        cls.corpus_root = ROOT / "tests/script/conformance/v1"

    def test_tool_and_cross_platform_ci_are_explicit(self):
        self.assertIn("add_executable(\n            BAAS_script_run", self.cmake)
        self.assertIn("apps/script_run/**", self.workflow)
        self.assertIn("--target BAAS_script_check BAAS_script_run", self.workflow)
        for runner in ("windows-latest", "ubuntu-latest", "macos-latest"):
            self.assertIn(runner, self.workflow)

    def test_v1_corpus_is_nonempty_and_every_directory_is_registered(self):
        expected = {
            "bounds",
            "cycle",
            "diagnostic",
            "escape",
            "happy",
            "host",
            "missing",
            "nested",
            "runtime_error",
            "structured",
        }
        actual = {path.name for path in self.corpus_root.iterdir() if path.is_dir()}
        self.assertEqual(expected, actual)
        self.assertTrue((self.corpus_root / "CORPUS.md").is_file())
        for name in expected:
            self.assertIn(name, self.cmake)

    def test_documented_boundary_is_honest(self):
        for anchor in (
            "**not** the\nproduction bytecode VM",
            "Host modules remain",
            "not provide handle-relative, race-free production package",
            "Exit code `0`",
            "Exit code `0` means",
            "byte-identical stdout",
        ):
            self.assertIn(anchor, self.guide)
        self.assertIn("production bytecode\n  VM execution CLI remains pending", self.roadmap)
        self.assertIn("structured Error\nidentity and cleanup", self.guide)
        self.assertIn("BAAS_script_run_python_cleanup_parity", self.cmake)


if __name__ == "__main__":
    unittest.main()
