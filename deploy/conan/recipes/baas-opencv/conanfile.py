from pathlib import Path

from conan import ConanFile
from conan.errors import ConanException
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import collect_libs, get


class BAASOpenCVConan(ConanFile):
    name = "baas-opencv"
    version = "4.13.0"
    license = "Apache-2.0"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "with_dnn": [True, False]}
    default_options = {"shared": True, "with_dnn": False}

    def layout(self):
        cmake_layout(self, src_folder="src")

    def source(self):
        source = self.conan_data["sources"][str(self.version)]
        get(self, url=source["url"], sha256=source["sha256"], strip_root=True, destination=self.source_folder)

    def generate(self):
        tc = CMakeToolchain(self)
        for key, value in self._cmake_options().items():
            tc.variables[key] = str(value)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        self._check_minimum_package()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-opencv")
        self.cpp_info.set_property("cmake_target_name", "BAAS::OpenCV")
        self.cpp_info.libs = collect_libs(self)
        self.cpp_info.bindirs = ["bin"] if self.options.shared else []
        if self.options.with_dnn:
            dnn = self.cpp_info.components["dnn"]
            dnn.set_property("cmake_target_name", "BAAS::OpenCVDnn")
            dnn.libs = self.cpp_info.libs
            dnn.includedirs = ["include"]

    def _cmake_options(self):
        build_key = "opencv_dnn" if self.options.with_dnn else "opencv"
        build = self.conan_data["build"][build_key]
        options = dict(build["cmake_options"])
        options["BUILD_SHARED_LIBS"] = "ON" if self.options.shared else "OFF"
        if not self.options.with_dnn:
            selected = build.get("cmake_options_by_config", {}).get(str(self.settings.build_type).lower(), {})
            options.update(selected)
        return options

    def _check_minimum_package(self):
        package = Path(self.package_folder)
        if not (package / "include" / "opencv2" / "core.hpp").exists():
            raise ConanException("OpenCV package is missing include/opencv2/core.hpp")
        if self.options.with_dnn and not (package / "include" / "opencv2" / "dnn.hpp").exists():
            raise ConanException("OpenCV DNN package is missing include/opencv2/dnn.hpp")
