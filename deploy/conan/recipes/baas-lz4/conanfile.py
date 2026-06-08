import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import collect_libs, get


class BAASLz4Conan(ConanFile):
    name = "baas-lz4"
    version = "1.10.0"
    license = "BSD-2-Clause"
    package_type = "shared-library"
    settings = "os", "arch", "compiler", "build_type"

    def layout(self):
        cmake_layout(self, src_folder="src")

    def source(self):
        source = self.conan_data["sources"][str(self.version)]
        get(self, url=source["url"], sha256=source["sha256"], strip_root=True, destination=self.source_folder)

    def generate(self):
        tc = CMakeToolchain(self)
        for key, value in self.conan_data["build"]["cmake_options"].items():
            tc.variables[key] = str(value)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure(build_script_folder=os.path.join(self.source_folder, self.conan_data["build"]["source_subdir"]))
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-lz4")
        self.cpp_info.set_property("cmake_target_name", "BAAS::LZ4")
        self.cpp_info.libs = collect_libs(self)
        self.cpp_info.bindirs = ["bin"]
        if self.settings.os == "Windows":
            self.cpp_info.defines = ["LZ4_DLL_IMPORT=1"]
