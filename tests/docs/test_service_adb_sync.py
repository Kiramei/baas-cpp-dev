import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceAdbSyncDocumentationTests(unittest.TestCase):
    def test_protocol_and_side_effect_boundary_are_explicit(self) -> None:
        spec = (ROOT / "docs/script-runtime/SERVICE_ADB_SYNC.md").read_text()
        for required in (
            "host:transport:<serial>",
            "sync:",
            "STAT",
            "SEND",
            "DATA",
            "DONE",
            "OKAY",
            "FAIL",
            "local_io_error",
            "emulator-5556",
            "no shell, forward,",
        ):
            self.assertIn(required, spec)

    def test_three_platform_debug_release_gate_builds_sync(self) -> None:
        workflow = (ROOT / ".github/workflows/foundation-runtime.yml").read_text()
        self.assertIn("BUILD_SERVICE_ADB_SYNC_TESTS=ON", workflow)
        self.assertIn("BAAS_service_adb_sync_tests", workflow)
        self.assertEqual(
            workflow.count("docs/script-runtime/SERVICE_ADB_SYNC.md"), 2
        )


if __name__ == "__main__":
    unittest.main()
