import pathlib
import unittest

import yaml


ROOT = pathlib.Path(__file__).resolve().parents[2]
RECIPE = ROOT / "deploy" / "conan" / "recipes" / "baas-libgit2"


class LibGit2DependencyContractTests(unittest.TestCase):
    def test_source_metadata_is_fixed_and_versioned(self) -> None:
        current = yaml.safe_load((RECIPE / "conandata.yml").read_text(encoding="utf-8"))
        fixed = yaml.safe_load(
            (RECIPE / "versions" / "1.9.3.yml").read_text(encoding="utf-8")
        )
        self.assertEqual(current, fixed)
        source = current["sources"]["1.9.3"]
        self.assertEqual(
            source["url"],
            "https://github.com/libgit2/libgit2/archive/refs/tags/v1.9.3.tar.gz",
        )
        self.assertEqual(
            source["sha256"],
            "d532172d7ab24d2a25944e2434212d63ee85f3650e97b5f7579e7f201a78ad64",
        )
        self.assertEqual(current["dependencies"]["openssl"], "3.5.7")

    def test_recipe_freezes_static_cross_platform_policy(self) -> None:
        recipe = (RECIPE / "conanfile.py").read_text(encoding="utf-8")
        for declaration in (
            'name = "baas-libgit2"',
            'version = "1.9.3"',
            'package_type = "static-library"',
            '"Windows": "WinHTTP"',
            '"Macos": "SecureTransport"',
            '"Linux": "OpenSSL"',
            '"Android": "OpenSSL"',
            '"openssl/*:no_fips": True',
            '"openssl/*:no_legacy": True',
            'toolchain.variables["USE_THREADS"] = True',
            'toolchain.variables["USE_SSH"] = False',
            'toolchain.variables["USE_SHA1"] = "CollisionDetection"',
            'toolchain.variables["USE_NTLMCLIENT"] = False',
            'toolchain.variables["USE_ICONV"] = False',
            'toolchain.variables["USE_HTTP_PARSER"] = "builtin"',
            'toolchain.variables["USE_BUNDLED_ZLIB"] = True',
            'self.cpp_info.set_property("cmake_target_name", "BAAS::libgit2")',
        ):
            self.assertIn(declaration, recipe)
        self.assertNotIn("baas-miniz", recipe)

    def test_aggregate_and_cmake_switches_default_off(self) -> None:
        aggregate = (ROOT / "deploy" / "conan" / "conanfile.py").read_text(
            encoding="utf-8"
        )
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        dependency_cmake = (ROOT / "cmake" / "BAASDependency.cmake").read_text(
            encoding="utf-8"
        )
        target_cmake = (ROOT / "cmake" / "RuntimeRepositoryGit2.cmake").read_text(
            encoding="utf-8"
        )

        self.assertIn('"use_libgit2": False', aggregate)
        self.assertIn("if self.options.use_libgit2:", aggregate)
        self.assertIn(
            'option(BUILD_RUNTIME_REPOSITORY_GIT2 "Build the optional libgit2 repository dependency boundary" OFF)',
            root_cmake,
        )
        self.assertIn(
            'option(BUILD_RUNTIME_REPOSITORY_GIT2_TESTS "Build real optional libgit2 repository backend tests" OFF)',
            root_cmake,
        )
        self.assertIn("baas_request_dependencies(libgit2 miniz cpp_httplib)", root_cmake)
        self.assertIn("find_package(baas-libgit2 CONFIG QUIET)", dependency_cmake)
        self.assertIn('PUBLIC BAAS_runtime_repository_updater', target_cmake)
        self.assertIn('PRIVATE BAAS::libgit2', target_cmake)
        self.assertIn('BAAS::miniz BAAS::httplib', target_cmake)
        self.assertIn('BAAS_runtime_repository_git2_backend_tests', target_cmake)
        self.assertIn('use_libgit2=True', target_cmake)

    def test_recipe_is_exported_and_version_selected(self) -> None:
        manager = (ROOT / "deploy" / "conan" / "scripts" / "manage_recipes.py").read_text(
            encoding="utf-8"
        )
        profile = (
            ROOT / "deploy" / "conan" / "profiles" / "dependency-versions-default"
        ).read_text(encoding="utf-8")
        self.assertIn('"baas-libgit2",', manager)
        self.assertIn("user.baas.dependencies:libgit2_version=1.9.3", profile)

    def test_standalone_publisher_links_the_real_backend_without_changing_default(self) -> None:
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        publisher_cmake = (
            ROOT / "cmake" / "ServiceRuntimeRepositoryUpdateApplication.cmake"
        ).read_text(encoding="utf-8")
        publisher_header = (
            ROOT / "include" / "service" / "app" / "RuntimeRepositoryUpdateApplication.h"
        ).read_text(encoding="utf-8")
        publisher_source = (
            ROOT / "src" / "service" / "app" / "RuntimeRepositoryUpdateApplication.cpp"
        ).read_text(encoding="utf-8")
        publisher_main = (
            ROOT / "apps" / "BAAS_runtime_repository_update" / "main.cpp"
        ).read_text(encoding="utf-8")
        workflow = (
            ROOT / ".github" / "workflows" / "runtime-repository-git2.yml"
        ).read_text(encoding="utf-8")
        documentation = (
            ROOT / "docs" / "script-runtime" / "RUNTIME_REPOSITORY_UPDATE_APPLICATION.md"
        ).read_text(encoding="utf-8")

        self.assertIn(
            'option(BUILD_SERVICE_RUNTIME_REPOSITORY_UPDATE_APP "Build the standalone signed runtime repository update publisher" OFF)',
            root_cmake,
        )
        self.assertIn("set(BUILD_RUNTIME_REPOSITORY_GIT2 ON CACHE BOOL", root_cmake)
        self.assertIn("set(BUILD_SERVICE_RUNTIME_REPOSITORY_PLAN ON CACHE BOOL", root_cmake)
        self.assertIn("TARGET_OS_NAME STREQUAL \"Android\"", root_cmake)
        self.assertIn("BAAS_runtime_repository_git2", publisher_cmake)
        self.assertIn("BAAS_service_runtime_repository_plan", publisher_cmake)
        self.assertIn("add_executable(\n        BAAS_runtime_repository_update", publisher_cmake)
        self.assertIn("runtime_repository_update_input_max_bytes", publisher_header)
        self.assertIn("Libgit2RuntimeRepositoryFetchBackend backend", publisher_source)
        self.assertIn("owner.recover(validator)", publisher_source)
        self.assertIn("owner.apply(signed_envelope", publisher_source)
        self.assertIn("BAAS_RUNTIME_REPOSITORY_TRUSTED_PUBLIC_KEY_HEX", publisher_source)
        self.assertNotIn("product_public_key", publisher_header)
        self.assertNotIn("getenv", publisher_main)
        self.assertIn("BAAS_service_runtime_repository_update_application_tests", workflow)
        self.assertIn("--requires=baas-libgit2/1.9.3", workflow)
        self.assertIn("--requires=baas-libsodium/1.0.22", workflow)
        self.assertIn("'cmake/ServiceAuth.cmake'", workflow)
        self.assertIn("'include/service/adapters/BoundedJson.h'", workflow)
        self.assertIn("BAAS_service_runtime_repository_plan_tests", workflow)
        self.assertIn("Windows, Linux, and macOS", documentation)
        self.assertIn("browser never invokes libgit2 directly", documentation)


if __name__ == "__main__":
    unittest.main()
