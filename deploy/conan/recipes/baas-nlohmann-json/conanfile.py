from pathlib import Path

from conan import ConanFile
from conan.tools.files import copy, get


class BAASNlohmannJsonConan(ConanFile):
    name = "baas-nlohmann-json"
    version = "3.11.3"
    license = "MIT"
    package_type = "header-library"

    def package_id(self):
        self.info.clear()

    def source(self):
        source = self.conan_data["sources"][str(self.version)]
        get(self, url=source["url"], sha256=source["sha256"], strip_root=True, destination=self.source_folder)

    def package(self):
        copy(self, "*", str(Path(self.source_folder) / "include" / "nlohmann"), str(Path(self.package_folder) / "include" / "nlohmann"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-nlohmann-json")
        self.cpp_info.set_property("cmake_target_name", "BAAS::nlohmann_json")
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
