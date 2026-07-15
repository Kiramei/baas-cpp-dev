import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceTriggerHandlerDocsTests(unittest.TestCase):
    def test_adapter_is_built_tested_and_documented(self) -> None:
        header = (ROOT / "include/service/channels/TriggerHandler.h").read_text()
        source = (ROOT / "src/service/channels/TriggerHandler.cpp").read_text()
        cmake = (ROOT / "cmake/ServiceTriggerHandler.cmake").read_text()
        workflow = (ROOT / ".github/workflows/service-auth.yml").read_text()
        spec = (ROOT / "docs/script-runtime/SERVICE_TRIGGER_HANDLER.md").read_text()
        self.assertIn("BusinessChannelHandlerFactory", header)
        self.assertIn("TriggerIngress", source)
        self.assertIn("complete_send", source)
        self.assertIn("fail_send", source)
        self.assertIn("BAAS_service_trigger_handler_tests", cmake)
        self.assertIn("BUILD_SERVICE_TRIGGER_HANDLER_TESTS=ON", workflow)
        self.assertIn("BAAS_service_trigger_handler_tests", workflow)
        self.assertIn("Completion-confirmed egress", spec)
        self.assertIn("one observed batch in flight", spec)


if __name__ == "__main__":
    unittest.main()
