import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import collect_libs, copy, get, rmdir


required_conan_version = ">=2.0"


class BAASLibGit2Conan(ConanFile):
    name = "baas-libgit2"
    version = "1.9.3"
    description = "BAAS private static source build of libgit2"
    license = "GPL-2.0-linking-exception"
    homepage = "https://libgit2.org/"
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "fPIC": [True, False],
    }
    default_options = {
        "fPIC": True,
        "openssl/*:shared": False,
        "openssl/*:no_apps": True,
        "openssl/*:no_fips": True,
        "openssl/*:no_zlib": True,
    }

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        self.settings.rm_safe("compiler.cppstd")
        self.settings.rm_safe("compiler.libcxx")

    def validate(self):
        if str(self.settings.os) not in ("Windows", "Linux", "Macos", "Android"):
            raise ConanInvalidConfiguration(
                "baas-libgit2 supports Windows, Linux, macOS, and Android"
            )

    def requirements(self):
        if self.settings.os in ("Linux", "Android"):
            openssl_version = self.conan_data["dependencies"]["openssl"]
            self.requires(f"openssl/{openssl_version}")

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

    @property
    def _https_backend(self):
        return {
            "Windows": "WinHTTP",
            "Macos": "SecureTransport",
            "Linux": "OpenSSL",
            "Android": "OpenSSL",
        }[str(self.settings.os)]

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_SHARED_LIBS"] = False
        toolchain.variables["BUILD_TESTS"] = False
        toolchain.variables["BUILD_CLI"] = False
        toolchain.variables["BUILD_EXAMPLES"] = False
        toolchain.variables["BUILD_FUZZERS"] = False
        toolchain.variables["USE_THREADS"] = True
        toolchain.variables["USE_SSH"] = False
        toolchain.variables["USE_HTTPS"] = self._https_backend
        toolchain.variables["USE_SHA1"] = "CollisionDetection"
        toolchain.variables["USE_SHA256"] = "HTTPS"
        toolchain.variables["USE_NTLMCLIENT"] = False
        toolchain.variables["USE_ICONV"] = False
        toolchain.variables["USE_HTTP_PARSER"] = "builtin"
        toolchain.variables["USE_BUNDLED_ZLIB"] = True
        toolchain.variables["REGEX_BACKEND"] = "builtin"
        toolchain.variables["LINK_WITH_STATIC_LIBRARIES"] = True
        toolchain.cache_variables["CMAKE_POLICY_DEFAULT_CMP0077"] = "NEW"
        toolchain.cache_variables["CMAKE_TRY_COMPILE_CONFIGURATION"] = str(
            self.settings.build_type
        )
        toolchain.generate()

        if self.settings.os in ("Linux", "Android"):
            CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(
            self,
            "COPYING",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )
        cmake = CMake(self)
        cmake.install()
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-libgit2")
        self.cpp_info.set_property("cmake_target_name", "BAAS::libgit2")
        self.cpp_info.set_property("pkg_config_name", "libgit2")
        self.cpp_info.libs = collect_libs(self)
        self.cpp_info.bindirs = []

        if self.settings.os == "Windows":
            self.cpp_info.system_libs.extend(
                ["winhttp", "rpcrt4", "crypt32", "ole32"]
            )
        elif self.settings.os == "Macos":
            self.cpp_info.frameworks.extend(["Security", "CoreFoundation"])
        elif self.settings.os == "Linux":
            self.cpp_info.system_libs.append("pthread")
