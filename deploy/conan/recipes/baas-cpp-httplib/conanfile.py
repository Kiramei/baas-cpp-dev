from pathlib import Path

from conan import ConanFile
from conan.tools.files import copy, download, patch


class BAASCppHttplibConan(ConanFile):
    name = "baas-cpp-httplib"
    version = "0.50.1"
    license = "MIT"
    package_type = "header-library"
    settings = "os"
    exports_sources = "patches/*", "LICENSE"
    default_options = {
        "openssl/*:shared": False,
        "openssl/*:no_apps": True,
        "openssl/*:no_zlib": True,
    }

    def requirements(self):
        # One process-wide header configuration avoids ODR/ABI mismatches
        # between HTTPS clients and the existing HTTP/WebSocket consumers.
        self.requires("openssl/3.5.7")

    def package_id(self):
        self.info.clear()

    def source(self):
        source = self.conan_data["sources"][str(self.version)]
        download(self, source["url"], "httplib.h", sha256=source["sha256"])
        if str(self.version) == "0.50.1":
            patch(
                self,
                base_path=self.source_folder,
                patch_file="patches/websocket-interrupt.patch",
            )

    def package(self):
        copy(self, "httplib.h", self.source_folder, str(Path(self.package_folder) / "include"))
        copy(self, "LICENSE", self.source_folder, str(Path(self.package_folder) / "licenses"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-cpp-httplib")
        self.cpp_info.set_property("cmake_target_name", "BAAS::httplib")
        self.cpp_info.requires = ["openssl::openssl"]
        # cpp-httplib is header-only. Every translation unit in a process must
        # therefore observe the same configuration macros; publish the BAAS
        # protocol limit from the package target instead of redefining it on
        # individual consumers.
        self.cpp_info.defines = [
            "CPPHTTPLIB_OPENSSL_SUPPORT=1",
            "CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH=67108864",
        ]
        if str(self.version) == "0.50.1":
            self.cpp_info.defines.append("CPPHTTPLIB_HEADER_MAX_TOTAL_LENGTH=32768")
            self.cpp_info.defines.append(
                "CPPHTTPLIB_WEBSOCKET_INTERRUPT_POLL_INTERVAL_MICROSECONDS=100000"
            )
            self.cpp_info.defines.append("BAAS_CPP_HTTPLIB_HAS_WEBSOCKET_INTERRUPT=1")
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        if self.settings.os == "Windows":
            self.cpp_info.system_libs.append("crypt32")
        elif self.settings.os == "Macos":
            self.cpp_info.defines.append("CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN=1")
            self.cpp_info.frameworks.extend(["Security", "CoreFoundation"])
