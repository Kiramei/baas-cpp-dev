from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import collect_libs, get


class BAASSpdlogConan(ConanFile):
    name = "baas-spdlog"
    version = "1.15.3"
    license = "MIT"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False]}
    default_options = {"shared": True}

    def layout(self):
        cmake_layout(self, src_folder="src")

    def source(self):
        source = self.conan_data["sources"][str(self.version)]
        get(self, url=source["url"], sha256=source["sha256"], strip_root=True, destination=self.source_folder)

    def generate(self):
        tc = CMakeToolchain(self)
        options = dict(self.conan_data["build"]["cmake_options"])
        options["BUILD_SHARED_LIBS"] = "ON" if self.options.shared else "OFF"
        options["SPDLOG_BUILD_SHARED"] = "ON" if self.options.shared else "OFF"
        for key, value in options.items():
            tc.variables[key] = str(value)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-spdlog")
        self.cpp_info.set_property("cmake_target_name", "BAAS::spdlog")
        self.cpp_info.libs = collect_libs(self)
        self.cpp_info.bindirs = ["bin"] if self.options.shared else []
