import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.apple import fix_apple_shared_install_name
from conan.tools.env import VirtualBuildEnv
from conan.tools.files import copy, get, replace_in_file, rm, rmdir
from conan.tools.gnu import Autotools, AutotoolsToolchain
from conan.tools.layout import basic_layout
from conan.tools.microsoft import (
    MSBuild,
    MSBuildToolchain,
    is_msvc,
    is_msvc_static_runtime,
)


required_conan_version = ">=2.0"


class BAASLibsodiumConan(ConanFile):
    name = "baas-libsodium"
    version = "1.0.22"
    description = "BAAS private source build of libsodium"
    license = "ISC"
    homepage = "https://libsodium.org/"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "use_soname": [True, False],
        "PIE": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "use_soname": True,
        "PIE": False,
    }

    @property
    def _is_mingw(self):
        return self.settings.os == "Windows" and self.settings.compiler == "gcc"

    @property
    def _settings_build(self):
        return getattr(self, "settings_build", self.settings)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC
        if self.settings.os == "Android":
            # Upstream requires versioned sonames to be disabled on Android.
            self.options.use_soname = False

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
        self.settings.rm_safe("compiler.cppstd")
        self.settings.rm_safe("compiler.libcxx")

    def layout(self):
        basic_layout(self, src_folder="src")

    def validate(self):
        if str(self.settings.os) not in ("Windows", "Linux", "Macos", "Android"):
            raise ConanInvalidConfiguration(
                "baas-libsodium supports Windows, Linux, macOS, and Android"
            )
        if self.settings.os == "Android" and self.options.use_soname:
            raise ConanInvalidConfiguration(
                "libsodium requires use_soname=False on Android"
            )
        if self.options.shared and is_msvc(self) and is_msvc_static_runtime(self):
            raise ConanInvalidConfiguration(
                "A shared libsodium build cannot use the static MSVC runtime"
            )

    def build_requirements(self):
        # The private-recipe workflow is intentionally --no-remote. Cross builds
        # hosted on Windows therefore use a locally configured bash instead of
        # adding Conan Center's msys2 package as an implicit remote dependency.
        if not is_msvc(self) and self._settings_build.os == "Windows":
            self.win_bash = True
            bash_path = self.conf.get("tools.microsoft.bash:path", check_type=str)
            bash_subsystem = self.conf.get(
                "tools.microsoft.bash:subsystem", check_type=str
            )
            if not bash_path or not bash_subsystem:
                raise ConanInvalidConfiguration(
                    "Non-MSVC libsodium builds hosted on Windows require "
                    "tools.microsoft.bash:path, tools.microsoft.bash:subsystem, "
                    "and a POSIX make executable on PATH"
                )

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
        if is_msvc(self):
            toolchain = MSBuildToolchain(self)
            toolchain.configuration = "{}{}".format(
                "Debug" if self.settings.build_type == "Debug" else "Release",
                "DLL" if self.options.shared else "LIB",
            )
            toolchain.generate()
            return

        VirtualBuildEnv(self).generate()
        toolchain = AutotoolsToolchain(self)
        yes_no = lambda value: "yes" if value else "no"
        toolchain.configure_args.append(
            "--enable-soname-versions={}".format(yes_no(self.options.use_soname))
        )
        toolchain.configure_args.append(
            "--enable-pie={}".format(yes_no(self.options.PIE))
        )
        if self._is_mingw:
            toolchain.extra_ldflags.append("-lssp")
        toolchain.generate()

    @property
    def _msvc_solution_folder(self):
        folders = {
            "170": "vs2012",
            "180": "vs2013",
            "190": "vs2015",
            "191": "vs2017",
            "192": "vs2019",
            "193": "vs2022",
            "194": "vs2022",
            "195": "vs2026",
        }
        return folders.get(str(self.settings.compiler.version), "vs2026")

    def _build_msvc(self):
        solution_folder = os.path.join(
            self.source_folder, "builds", "msvc", self._msvc_solution_folder
        )
        project = os.path.join(
            solution_folder, "libsodium", "libsodium.vcxproj"
        )
        toolchain = os.path.join(
            self.generators_folder, MSBuildToolchain.filename
        )
        replace_in_file(
            self,
            project,
            '<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />',
            '<Import Project="{}" /><Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />'.format(
                toolchain
            ),
        )

        msbuild = MSBuild(self)
        msbuild.build_type = "{}{}".format(
            "Dyn" if self.options.shared else "Static",
            "Debug" if self.settings.build_type == "Debug" else "Release",
        )
        if self.settings.arch == "x86":
            msbuild.platform = "Win32"
        msbuild.build(os.path.join(solution_folder, "libsodium.sln"))

    def build(self):
        if is_msvc(self):
            self._build_msvc()
            return

        autotools = Autotools(self)
        if self._is_mingw:
            autotools.autoreconf()
        autotools.configure()
        autotools.make()

    def package(self):
        copy(
            self,
            "*LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )
        if is_msvc(self):
            output = os.path.join(self.source_folder, "bin")
            copy(
                self,
                "*.lib",
                src=output,
                dst=os.path.join(self.package_folder, "lib"),
                keep_path=False,
            )
            copy(
                self,
                "*.dll",
                src=output,
                dst=os.path.join(self.package_folder, "bin"),
                keep_path=False,
            )
            headers = os.path.join(
                self.source_folder, "src", "libsodium", "include"
            )
            copy(
                self,
                "*.h",
                src=headers,
                dst=os.path.join(self.package_folder, "include"),
                excludes=("*/private/*",),
            )
            return

        autotools = Autotools(self)
        autotools.install()
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))
        rm(self, "*.la", os.path.join(self.package_folder, "lib"))
        fix_apple_shared_install_name(self)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-libsodium")
        self.cpp_info.set_property("cmake_target_name", "BAAS::sodium")
        self.cpp_info.set_property("pkg_config_name", "libsodium")
        self.cpp_info.libs = ["libsodium" if is_msvc(self) else "sodium"]
        if not self.options.shared:
            self.cpp_info.defines = ["SODIUM_STATIC"]
        if self.settings.os in ("FreeBSD", "Linux"):
            self.cpp_info.system_libs.append("pthread")
        if self._is_mingw:
            self.cpp_info.system_libs.append("ssp")
