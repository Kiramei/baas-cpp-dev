import pathlib
import re
import unittest

import yaml


ROOT = pathlib.Path(__file__).resolve().parents[2]
RECIPE = ROOT / "deploy" / "conan" / "recipes" / "baas-miniz"
VERSION = "3.1.2"
SHA256 = "98468f8924934b723276680f85238b6c78bf1f8b49b4459cc9b7214a20e2e9fb"


class MinizDependencyContractTests(unittest.TestCase):
    def test_source_and_default_version_are_pinned(self) -> None:
        conanfile = (RECIPE / "conanfile.py").read_text(encoding="utf-8")
        profile = (
            ROOT / "deploy" / "conan" / "profiles" / "dependency-versions-default"
        ).read_text(encoding="utf-8")
        self.assertRegex(
            conanfile,
            re.compile(r'^\s*version\s*=\s*"3\.1\.2"', re.MULTILINE),
        )
        self.assertIn("miniz_version=3.1.2", profile)

        for path in (RECIPE / "conandata.yml", RECIPE / "versions" / "3.1.2.yml"):
            metadata = yaml.safe_load(path.read_text(encoding="utf-8"))
            source = metadata["sources"][VERSION]
            self.assertEqual(
                source["url"],
                "https://github.com/richgel999/miniz/archive/refs/tags/3.1.2.tar.gz",
            )
            self.assertEqual(source["sha256"], SHA256)

    def test_recipe_is_managed_and_exports_stable_target(self) -> None:
        manager = (
            ROOT / "deploy" / "conan" / "scripts" / "manage_recipes.py"
        ).read_text(encoding="utf-8")
        aggregate = (ROOT / "deploy" / "conan" / "conanfile.py").read_text(
            encoding="utf-8"
        )
        dependency_cmake = (ROOT / "cmake" / "BAASDependency.cmake").read_text(
            encoding="utf-8"
        )
        recipe = (RECIPE / "conanfile.py").read_text(encoding="utf-8")

        self.assertIn('"baas-miniz",', manager)
        self.assertIn('"baas-miniz",', aggregate)
        self.assertIn('dependency STREQUAL "miniz"', dependency_cmake)
        self.assertIn("find_package(baas-miniz CONFIG REQUIRED)", dependency_cmake)
        self.assertIn('"cmake_target_name", "BAAS::miniz"', recipe)
        self.assertIn('license = "MIT"', recipe)
        self.assertIn('copy(self, "LICENSE"', recipe)
        self.assertIn('self.cpp_info.includedirs = ["include", "include/miniz"]', recipe)
        self.assertIn('self.cpp_info.defines = ["MINIZ_STATIC_DEFINE"]', recipe)

    def test_test_package_consumes_only_the_baas_target(self) -> None:
        cmake = (RECIPE / "test_package" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        source = (RECIPE / "test_package" / "test.cpp").read_text(encoding="utf-8")
        self.assertIn("target_link_libraries(baas_miniz_test PRIVATE BAAS::miniz)", cmake)
        self.assertIn("mz_compress2", source)
        self.assertIn("mz_uncompress", source)
        self.assertIn("mz_zip_writer_finalize_heap_archive", source)
        self.assertIn("mz_zip_reader_extract_file_to_heap", source)


if __name__ == "__main__":
    unittest.main()
