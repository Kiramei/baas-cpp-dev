from pathlib import Path

from conan import ConanFile
from conan.tools.files import copy, download


class BAASCppHttplibConan(ConanFile):
    name = "baas-cpp-httplib"
    version = "0.50.1"
    license = "MIT"
    package_type = "header-library"

    def package_id(self):
        self.info.clear()

    def source(self):
        source = self.conan_data["sources"][str(self.version)]
        download(self, source["url"], "httplib.h", sha256=source["sha256"])

    def package(self):
        copy(self, "httplib.h", self.source_folder, str(Path(self.package_folder) / "include"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-cpp-httplib")
        self.cpp_info.set_property("cmake_target_name", "BAAS::httplib")
        # cpp-httplib is header-only. Every translation unit in a process must
        # therefore observe the same configuration macros; publish the BAAS
        # protocol limit from the package target instead of redefining it on
        # individual consumers.
        self.cpp_info.defines = ["CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH=67108864"]
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
