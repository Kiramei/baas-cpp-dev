import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class BAASCppHttplibTestPackage(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        # Keep MSBuild FileTracker paths below the legacy Windows path ceiling
        # when this explicit test package runs from a deep worktree.
        cmake_layout(self, generator="Ninja", build_folder="build/b")

    def generate(self):
        CMakeDeps(self).generate()
        CMakeToolchain(self, generator="Ninja").generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            executable = os.path.join(self.cpp.build.bindir, "baas_cpp_httplib_test")
            self.run(executable, env="conanrun")
