import pathlib
import tomllib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceFileResourceStoreDocsTests(unittest.TestCase):
    def test_adapter_is_built_tested_and_documented(self) -> None:
        header = (ROOT / "include/service/adapters/FileResourceStore.h").read_text()
        source = (ROOT / "src/service/adapters/FileResourceStore.cpp").read_text()
        module = (ROOT / "cmake/ServiceFileResourceStore.cmake").read_text()
        root_cmake = (ROOT / "CMakeLists.txt").read_text()
        workflow = (ROOT / ".github/workflows/service-application.yml").read_text()
        spec = (ROOT / "docs/script-runtime/SERVICE_FILE_RESOURCE_STORE.md").read_text()

        self.assertIn("committed_durability_uncertain", header)
        self.assertIn("NtFlushBuffersFileEx", source)
        self.assertIn("flush_windows_metadata", source)
        self.assertIn("NtSetInformationFile", source)
        self.assertIn("RootImportTransactionLock", source)
        self.assertIn("parse_import_journal_filename", source)
        self.assertIn("CommitPhase::claimed", source)
        self.assertIn("openat", source)
        self.assertIn("O_NOFOLLOW", source)
        self.assertIn("::fsync(file)", source)
        self.assertIn("::renameat", source)
        self.assertIn("callback_slot", source)
        self.assertIn("BAAS_service_file_resource_store_tests", module)
        self.assertIn(
            'EXCLUDE REGEX "/src/service/.*\\\\.cpp$"',
            root_cmake,
        )
        self.assertIn("BUILD_SERVICE_FILE_RESOURCE_STORE_TESTS=ON", workflow)
        self.assertIn("BAAS_service_file_resource_store_tests", workflow)
        self.assertEqual(
            workflow.count("docs/script-runtime/SERVICE_FILE_RESOURCE_STORE.md"), 2
        )
        self.assertIn("setup_toml", spec)
        self.assertIn("seven-field Python", spec)
        self.assertIn("unknown TOML", spec)
        self.assertIn("scalar/table redefinition", spec)
        self.assertIn("committed_durability_uncertain", spec)
        self.assertIn("entry barrier", spec)
        self.assertIn("refresh_and_publish", spec)
        self.assertIn("create_config", header)
        self.assertIn(".baas-create-", source)
        self.assertIn("BAAS_service_file_resource_store_tests", workflow)

    def test_preserved_setup_toml_shape_is_tomllib_readable(self) -> None:
        merged = '''
schema_version = 1
["general"]
"transport" = "pipe" # keep transport comment
channel = "dev"
unknown_future = """keep-me
[this.is.not.a.table]
# nor is this a comment
still-keep-me"""
get_remote_sha_method = "gitee"
mirrorc_cdk = "new-cdk"

[[plugins]]
name = "future-plugin"
'''
        parsed = tomllib.loads(merged)
        self.assertEqual(parsed["general"]["transport"], "pipe")
        self.assertIn("[this.is.not.a.table]", parsed["general"]["unknown_future"])
        self.assertEqual(parsed["plugins"][0]["name"], "future-plugin")


if __name__ == "__main__":
    unittest.main()
