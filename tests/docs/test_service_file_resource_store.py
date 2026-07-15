import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceFileResourceStoreDocsTests(unittest.TestCase):
    def test_adapter_is_built_tested_and_documented(self) -> None:
        header = (ROOT / "include/service/adapters/FileResourceStore.h").read_text()
        source = (ROOT / "src/service/adapters/FileResourceStore.cpp").read_text()
        module = (ROOT / "cmake/ServiceFileResourceStore.cmake").read_text()
        root_cmake = (ROOT / "CMakeLists.txt").read_text()
        workflow = (ROOT / ".github/workflows/foundation-runtime.yml").read_text()
        spec = (ROOT / "docs/script-runtime/SERVICE_FILE_RESOURCE_STORE.md").read_text()

        self.assertIn("committed_durability_uncertain", header)
        self.assertIn("FlushFileBuffers", source)
        self.assertIn("MOVEFILE_WRITE_THROUGH", source)
        self.assertIn("::fsync(file)", source)
        self.assertIn("::rename", source)
        self.assertIn("callback_slot", source)
        self.assertIn("BAAS_service_file_resource_store_tests", module)
        self.assertIn(
            'EXCLUDE REGEX "/src/service/adapters/.*\\\\.cpp$"',
            root_cmake,
        )
        self.assertIn("BUILD_SERVICE_FILE_RESOURCE_STORE_TESTS=ON", workflow)
        self.assertIn("BAAS_service_file_resource_store_tests", workflow)
        self.assertEqual(
            workflow.count("docs/script-runtime/SERVICE_FILE_RESOURCE_STORE.md"), 2
        )
        self.assertIn("setup_toml", spec)
        self.assertIn("dedicated TOML projection adapter", spec)
        self.assertIn("committed_durability_uncertain", spec)
        self.assertIn("entry barrier", spec)
        self.assertIn("refresh_and_publish", spec)


if __name__ == "__main__":
    unittest.main()
