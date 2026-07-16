from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import collect_libs, copy, get


class BAASMinizConan(ConanFile):
    name = "baas-miniz"
    version = "3.1.2"
    license = "MIT"
    homepage = "https://github.com/richgel999/miniz"
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"

    def layout(self):
        cmake_layout(self, src_folder="src")

    def source(self):
        source = self.conan_data["sources"][str(self.version)]
        get(
            self,
            url=source["url"],
            sha256=source["sha256"],
            strip_root=True,
            destination=self.source_folder,
        )

    def generate(self):
        tc = CMakeToolchain(self)
        for key, value in self.conan_data["build"]["cmake_options"].items():
            tc.variables[key] = str(value)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE", self.source_folder, self.package_folder + "/licenses")
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-miniz")
        self.cpp_info.set_property("cmake_target_name", "BAAS::miniz")
        self.cpp_info.libs = collect_libs(self)
        self.cpp_info.includedirs = ["include", "include/miniz"]
        self.cpp_info.defines = ["MINIZ_STATIC_DEFINE"]
        self.cpp_info.bindirs = []
