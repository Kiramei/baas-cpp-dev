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
        self.assertIn("baas_request_dependencies(libgit2 miniz)", root_cmake)
        self.assertIn("find_package(baas-libgit2 CONFIG QUIET)", dependency_cmake)
        self.assertIn('PUBLIC BAAS_runtime_repository_updater', target_cmake)
        self.assertIn('PRIVATE BAAS::libgit2', target_cmake)
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


if __name__ == "__main__":
    unittest.main()
