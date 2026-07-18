import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceAdbTransportDocumentationTests(unittest.TestCase):
    def test_boundary_and_safety_contract_are_explicit(self) -> None:
        spec = (ROOT / "docs/script-runtime/SERVICE_ADB_TRANSPORT.md").read_text()
        for required in (
            "host:transport:<serial>",
            "ADB SYNC",
            "Shell-v2",
            "127.0.0.1:5037",
            "AI_NUMERICHOST | AI_NUMERICSERV",
            "open_native_adb_stream",
            "forward-allocation `OKAY`",
            "native I/O lease",
            "stop()",
            "never pushes",
        ):
            self.assertIn(required, spec)

    def test_three_platform_foundation_gate_builds_tests(self) -> None:
        workflow = (ROOT / ".github/workflows/foundation-runtime.yml").read_text()
        self.assertIn("BUILD_SERVICE_ADB_TRANSPORT_TESTS=ON", workflow)
        self.assertIn("BAAS_service_adb_transport_tests", workflow)
        self.assertEqual(
            workflow.count("docs/script-runtime/SERVICE_ADB_TRANSPORT.md"), 2
        )

    def test_android_service_compile_closure_uses_the_posix_protocol_header(self) -> None:
        source = (ROOT / "src/service/adb/ServiceAdbTransport.cpp").read_text()
        workflow = (ROOT / ".github/workflows/service-application.yml").read_text()
        self.assertIn("#include <netinet/in.h>", source)
        self.assertIn("hints.ai_protocol = IPPROTO_TCP;", source)
        self.assertEqual(workflow.count("- 'src/service/**'"), 2)
        self.assertIn("android-clang-arm64-v8a-release", workflow)
        self.assertIn("android-clang-x86_64-release", workflow)
        self.assertIn("BAAS_service_application", workflow)


if __name__ == "__main__":
    unittest.main()
