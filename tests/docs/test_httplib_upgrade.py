import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
RECIPE = ROOT / "deploy" / "conan" / "recipes" / "baas-cpp-httplib"
EXPECTED_SHA256 = "6aabb9750df0a779c7470f3a22753cee3dfeec580c44201aff1bf057aa91fcbc"


class HttplibUpgradeContractTests(unittest.TestCase):
    def test_default_pin_and_fixed_source_metadata_are_consistent(self) -> None:
        conanfile = (RECIPE / "conanfile.py").read_text(encoding="utf-8")
        conandata = (RECIPE / "conandata.yml").read_text(encoding="utf-8")
        fixed = (RECIPE / "versions" / "0.50.1.yml").read_text(encoding="utf-8")
        profile = (
            ROOT / "deploy" / "conan" / "profiles" / "dependency-versions-default"
        ).read_text(encoding="utf-8")

        self.assertRegex(
            conanfile,
            re.compile(r'^\s*version\s*=\s*"0\.50\.1"', re.MULTILINE),
        )
        self.assertIn("cpp_httplib_version=0.50.1", profile)
        for metadata in (conandata, fixed):
            self.assertIn('"0.50.1"', metadata)
            self.assertIn("/v0.50.1/httplib.h", metadata)
            self.assertIn(EXPECTED_SHA256, metadata)

    def test_legacy_source_metadata_remains_reproducible(self) -> None:
        for version in ("0.18.0", "0.20.1", "0.28.0"):
            self.assertTrue((RECIPE / "versions" / f"{version}.yml").is_file())
        test_package = (RECIPE / "test_package" / "test.cpp").read_text(encoding="utf-8")
        self.assertNotIn("CPPHTTPLIB_VERSION", test_package)
        self.assertNotIn("FormField", test_package)

    def test_header_only_configuration_is_published_by_package_target(self) -> None:
        conanfile = (RECIPE / "conanfile.py").read_text(encoding="utf-8")
        self.assertIn('self.requires("openssl/3.5.7")', conanfile)
        self.assertIn('"openssl/*:shared": False', conanfile)
        self.assertIn('"openssl/*:no_apps": True', conanfile)
        self.assertIn('"openssl/*:no_fips": True', conanfile)
        self.assertIn('"openssl/*:no_legacy": True', conanfile)
        self.assertIn('"openssl/*:no_zlib": True', conanfile)
        self.assertIn('self.cpp_info.requires = ["openssl::openssl"]', conanfile)
        self.assertIn('exports_sources = "patches/*", "LICENSE"', conanfile)
        self.assertIn('copy(self, "LICENSE", self.source_folder', conanfile)
        self.assertIn('Path(self.package_folder) / "licenses"', conanfile)
        self.assertTrue((RECIPE / "LICENSE").is_file())
        self.assertIn('"CPPHTTPLIB_OPENSSL_SUPPORT=1"', conanfile)
        self.assertIn('"CPPHTTPLIB_LISTEN_BACKLOG=65536"', conanfile)
        self.assertIn('"CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH=67108864"', conanfile)
        self.assertIn('self.cpp_info.system_libs.append("crypt32")', conanfile)
        self.assertIn(
            'self.cpp_info.frameworks.extend(["Security", "CoreFoundation"])',
            conanfile,
        )
        self.assertIn(
            'self.cpp_info.defines.append("CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN=1")',
            conanfile,
        )
        test_package = (RECIPE / "test_package" / "test.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("#if !defined(CPPHTTPLIB_OPENSSL_SUPPORT)", test_package)
        self.assertIn('httplib::SSLClient tls_client("localhost", 443)', test_package)
        self.assertIn("tls_client.is_valid()", test_package)
        self.assertIn(
            'self.cpp_info.defines.append("CPPHTTPLIB_HEADER_MAX_TOTAL_LENGTH=32768")',
            conanfile,
        )
        self.assertIn(
            '"CPPHTTPLIB_WEBSOCKET_INTERRUPT_POLL_INTERVAL_MICROSECONDS=100000"',
            conanfile,
        )
        patch = (RECIPE / "patches" / "websocket-interrupt.patch").read_text(
            encoding="utf-8"
        )
        self.assertIn("CPPHTTPLIB_HEADER_MAX_TOTAL_LENGTH", patch)
        self.assertIn("WebSocket::request_close", patch)
        self.assertIn("WebSocket::interrupt", patch)
        contract = (
            ROOT / "tests" / "service" / "HttplibUpgradeContractTests.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('CPPHTTPLIB_VERSION} == "0.50.1"', contract)
        self.assertIn("CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH == 67'108'864", contract)
        self.assertIn("CPPHTTPLIB_LISTEN_BACKLOG == 65'536", contract)
        self.assertIn("CPPHTTPLIB_HEADER_MAX_TOTAL_LENGTH == 32'768", contract)

    def test_thread_pool_and_ocr_multipart_api_match_v050(self) -> None:
        host = (ROOT / "src" / "service" / "http" / "HttpHost.cpp").read_text(
            encoding="utf-8"
        )
        server = (ROOT / "apps" / "ocr_server" / "src" / "server.cpp").read_text(
            encoding="utf-8"
        )
        helper = (
            ROOT / "apps" / "ocr_server" / "include" / "http_contract.h"
        ).read_text(encoding="utf-8")

        self.assertIn("pool_(workers, workers, queued_requests)", host)
        self.assertNotIn("req.has_file", server)
        self.assertNotIn("get_file_value", server)
        self.assertIn("request.form.get_field_count(\"data\")", helper)
        self.assertIn("request.form.get_file_count(\"image\")", helper)
        self.assertIn("std::string_view request_json_payload", helper)
        self.assertIn("const httplib::FormData* request_image_file", helper)
        self.assertIn("image_max_length = 64U * 1024U * 1024U", helper)
        self.assertIn("json_max_length = 1U * 1024U * 1024U", helper)
        self.assertIn("worker_count = 8U", helper)
        self.assertIn("max_queued_requests = 16U", helper)
        self.assertIn("server.new_task_queue = [workers, queued_requests]", helper)
        self.assertIn(
            "httplib::ThreadPool(workers, workers, queued_requests)", helper
        )
        self.assertIn('request.get_header_value_count("Content-Length") != 1', helper)
        self.assertIn("validate_multipart_shape_and_overhead(request)", helper)
        self.assertIn("http_contract::configure_server(svr)", server)

    def test_build_and_ci_gate_exact_upgrade_contract(self) -> None:
        cmake = (ROOT / "cmake" / "ServiceHttp.cmake").read_text(encoding="utf-8")
        workflow = (
            ROOT / ".github" / "workflows" / "httplib-http.yml"
        ).read_text(encoding="utf-8")
        documentation = (
            ROOT / "docs" / "script-runtime" / "SERVICE_HTTPLIB_ADAPTER.md"
        ).read_text(encoding="utf-8")
        websocket_documentation = (
            ROOT / "docs" / "script-runtime" / "SERVICE_WEBSOCKET_OWNER.md"
        ).read_text(encoding="utf-8")
        ocr_documentation = (ROOT / "apps" / "ocr_server" / "README.md").read_text(
            encoding="utf-8"
        )

        self.assertIn("find_package(Threads REQUIRED)", cmake)
        self.assertIn("BAAS_httplib_upgrade_contract_tests", cmake)
        self.assertIn("baas-cpp-httplib/0.50.1", workflow)
        self.assertIn("windows-latest", workflow)
        self.assertIn("ubuntu-latest", workflow)
        self.assertIn("macos-latest", workflow)
        self.assertIn("Debug", workflow)
        self.assertIn("Release", workflow)
        for path in (
            "tests/docs/test_httplib_upgrade.py",
            "apps/ocr_server/README.md",
            "deploy/conan/README.md",
            "cmake/ServiceWebSocket.cmake",
            "src/service/websocket/**",
            "tests/service/WebSocket*Tests.cpp",
            "docs/script-runtime/SERVICE_WEBSOCKET_OWNER.md",
            "docs/script-runtime/ROADMAP.md",
        ):
            self.assertEqual(workflow.count(f"- '{path}'"), 2)
        self.assertIn("-DBUILD_SERVICE_WEBSOCKET_TESTS=ON", workflow)
        self.assertIn("BAAS_service_websocket_handshake_tests", workflow)
        self.assertIn("BAAS_service_websocket_owner_tests", workflow)
        self.assertIn("BAAS_service_websocket_wire_tests", workflow)
        self.assertIn("service_websocket_.*", workflow)
        self.assertIn("cpp-httplib 0.50.1", documentation)
        self.assertIn("BAAS_(httplib_upgrade_contract|service_", documentation)
        self.assertIn("CPPHTTPLIB_HEADER_MAX_TOTAL_LENGTH=32768", documentation)
        self.assertIn("CPPHTTPLIB_LISTEN_BACKLOG=65536", documentation)
        self.assertIn("SERVICE_WEBSOCKET_OWNER.md", documentation)
        self.assertIn(
            "CPPHTTPLIB_WEBSOCKET_INTERRUPT_POLL_INTERVAL_MICROSECONDS=100000",
            websocket_documentation,
        )
        self.assertIn("8 fixed workers", documentation)
        self.assertRegex(documentation, r"16\s+waiting requests")
        self.assertIn("chunked multipart", documentation)
        self.assertIn("8 fixed workers", ocr_documentation)
        self.assertIn("16 waiting", ocr_documentation)
        self.assertIn("chunked multipart", ocr_documentation)
        self.assertNotIn("current cpp-httplib 0.18.0", documentation)
        self.assertNotIn("does not implement WebSocket routing", documentation)
        for route in ("control", "provider", "sync", "trigger", "remote"):
            self.assertIn(f"`/ws/{route}`", websocket_documentation)
        self.assertIn("32 KiB", websocket_documentation)
        self.assertIn("request_close()", websocket_documentation)
        self.assertIn("max_connections + http_worker_reserve", websocket_documentation)
        self.assertIn("BAAS_service_websocket_wire_tests", websocket_documentation)
        self.assertIn("partial masked frames", websocket_documentation)


if __name__ == "__main__":
    unittest.main()
