import pathlib
import unittest

import yaml


ROOT = pathlib.Path(__file__).resolve().parents[2]


class GroupResourcePublicationContractTests(unittest.TestCase):
    def test_cmake_is_opt_in_and_android_keeps_only_the_library(self) -> None:
        root = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        target = (ROOT / "cmake" / "RuntimeGroupPublicationCompiler.cmake").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'option(BUILD_RUNTIME_GROUP_PUBLICATION_COMPILER "Build deterministic group resource publication compiler" OFF)',
            root,
        )
        self.assertIn("BAAS_runtime_group_publication_compiler", target)
        self.assertIn("BAAS::libgit2", target)
        self.assertIn("BAAS::miniz", target)
        self.assertIn('CMAKE_SYSTEM_NAME STREQUAL "Android"', target)
        self.assertIn("if(NOT (ANDROID", target)
        self.assertIn("baas-runtime-publisher", target)
        self.assertIn("BAAS_runtime_group_publication_compiler_tests", target)

    def test_cli_exposes_the_four_frozen_commands(self) -> None:
        source = (
            ROOT / "apps" / "baas-runtime-publisher" / "main.cpp"
        ).read_text(encoding="utf-8")
        for command in (
            "verify-source",
            "compile-group",
            "verify-publication",
            "check-reproducible",
        ):
            self.assertIn(command, source)
        self.assertIn("--check", source)

    def test_workflow_push_and_pull_request_paths_are_equal_and_complete(self) -> None:
        workflow_path = ROOT / ".github" / "workflows" / "foundation-runtime.yml"
        workflow = yaml.safe_load(workflow_path.read_text(encoding="utf-8"))
        trigger = workflow[True]  # PyYAML 1.1 parses the key `on` as boolean true.
        push_paths = set(trigger["push"]["paths"])
        pull_paths = set(trigger["pull_request"]["paths"])
        self.assertEqual(push_paths, pull_paths)
        for required in (
            ".github/workflows/foundation-runtime.yml",
            "CMakeLists.txt",
            "cmake/**",
            "include/runtime/**",
            "src/runtime/**",
            "tests/runtime/**",
            "tests/docs/**",
            "apps/baas-runtime-publisher/**",
            "deploy/conan/recipes/baas-libgit2/**",
            "deploy/conan/recipes/baas-miniz/**",
            "deploy/conan/recipes/baas-nlohmann-json/**",
            "deploy/conan/recipes/baas-opencv/**",
            "docs/script-runtime/GROUP_RESOURCE_PUBLICATION.md",
            "docs/script-runtime/CO_DETECT_PYTHON_COMPAT_ENGINE.md",
        ):
            self.assertIn(required, push_paths)
        text = workflow_path.read_text(encoding="utf-8")
        self.assertIn("BUILD_RUNTIME_GROUP_PUBLICATION_COMPILER_TESTS=ON", text)
        self.assertIn("BAAS_runtime_group_publication_compiler_tests", text)
        self.assertIn("BUILD_RUNTIME_GROUP_PUBLICATION_COMPILER=ON", text)
        self.assertIn("BAAS_runtime_group_publication_compiler", text)
        self.assertIn("BAAS_runtime_group_publication_compiler_link_probe", text)
        self.assertIn("android-clang-arm64-v8a-release", text)
        self.assertIn("android-clang-x86_64-release", text)
        self.assertIn("Kiramei/baas-dev", text)
        self.assertIn("generate_group_publication_b8cc_lock.py", text)

    def test_cpp_repository_contains_no_generated_dynamic_publication(self) -> None:
        tracked_candidates = [
            path
            for path in ROOT.rglob("*")
            if path.is_file()
            and (
                path.suffix == ".bundle"
                or path.name == "baas.group-publication.lock.json"
            )
            and ".git" not in path.parts
            and "build" not in path.parts
        ]
        self.assertEqual(tracked_candidates, [])

    def test_documentation_freezes_exact_counts_and_union_upper_bounds(self) -> None:
        publication = (
            ROOT / "docs" / "script-runtime" / "GROUP_RESOURCE_PUBLICATION.md"
        ).read_text(encoding="utf-8")
        co_detect = (
            ROOT / "docs" / "script-runtime" / "CO_DETECT_PYTHON_COMPAT_ENGINE.md"
        ).read_text(encoding="utf-8")
        for text in (publication, co_detect):
            self.assertIn("union", text)
            for count in ("63", "56", "60", "57", "16", "12", "17", "14", "13"):
                self.assertIn(count, text)
            self.assertIn("alias", text)
        self.assertIn("b8cc64705feb0067aba349892031a450d1bf8083", publication)

    def test_production_gate_is_public_and_generator_is_integration_only(self) -> None:
        header = (
            ROOT / "include" / "runtime" / "publisher" / "GroupPublicationCompiler.h"
        ).read_text(encoding="utf-8")
        generator = (
            ROOT / "tests" / "runtime" / "generate_group_publication_b8cc_lock.py"
        ).read_text(encoding="utf-8")
        self.assertIn("validate_group_production_lock", header)
        self.assertIn("b8cc64705feb0067aba349892031a450d1bf8083", header)
        self.assertIn("integration-test inventory tool", generator.lower())

    def test_publication_io_is_anchored_and_png_budgets_match_consumer(self) -> None:
        source = (
            ROOT / "src" / "runtime" / "publisher" / "GroupPublicationCompiler.cpp"
        ).read_text(encoding="utf-8")
        tests = (
            ROOT / "tests" / "runtime" / "GroupPublicationCompilerTests.cpp"
        ).read_text(encoding="utf-8")
        for token in (
            "max_png_source_bytes = 4U * 1024U * 1024U",
            "max_decoded_png_bytes = 4U * 1024U * 1024U",
            "max_total_decoded_png_bytes = 128U * 1024U * 1024U",
            "openat(",
            "renameat(",
            "fstatat(",
            "NtCreateFile",
            "FILE_RENAME_INFO",
            "FileIdBothDirectoryInfo",
            "before-read-open",
            "before-write-create",
        ):
            self.assertIn(token, source)
        self.assertIn("cumulative decoded PNG work over 128 MiB", tests)
        self.assertIn("deterministic final-component replacement race", tests)


if __name__ == "__main__":
    unittest.main()
