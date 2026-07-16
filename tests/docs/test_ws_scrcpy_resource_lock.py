import copy
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
LOCK = ROOT / "resources.lock.json"
JAR = ROOT / "resource" / "ws-scrcpy" / "scrcpy-server.jar"
FETCH = ROOT / "scripts" / "fetch_resources.py"


class WsScrcpyResourceLockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        lock = json.loads(LOCK.read_text(encoding="utf-8-sig"))
        cls.entry = copy.deepcopy(lock["resources"]["ws_scrcpy_server"])
        cls.jar = JAR.read_bytes()

    def run_fetch(self, jar: bytes) -> tuple[subprocess.CompletedProcess[str], pathlib.Path]:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = pathlib.Path(temporary.name)
        source = root / "resource" / "ws-scrcpy" / "scrcpy-server.jar"
        source.parent.mkdir(parents=True)
        source.write_bytes(jar)
        lock = root / "resources.lock.json"
        lock.write_text(
            json.dumps({"resources": {"ws_scrcpy_server": self.entry}}),
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                sys.executable,
                "-B",
                str(FETCH),
                "--lock",
                str(lock),
                "--output-root",
                str(root / "output"),
                "--download-root",
                str(root / "download"),
                "--resource",
                "ws_scrcpy_server",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        return result, root

    def test_vendored_jar_is_fetched_without_network(self) -> None:
        result, root = self.run_fetch(self.jar)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = root / "output" / "bin" / "ws-scrcpy" / "scrcpy-server.jar"
        self.assertEqual(output.read_bytes(), self.jar)
        self.assertEqual(list((root / "download").iterdir()), [])

    def test_same_size_jar_drift_fails_sha_validation(self) -> None:
        drifted = bytearray(self.jar)
        drifted[len(drifted) // 2] ^= 0x01
        result, _ = self.run_fetch(bytes(drifted))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Vendored resource SHA256 mismatch", result.stdout + result.stderr)

    def test_truncated_jar_drift_fails_size_validation(self) -> None:
        result, _ = self.run_fetch(self.jar[:-1])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Vendored resource size mismatch", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
