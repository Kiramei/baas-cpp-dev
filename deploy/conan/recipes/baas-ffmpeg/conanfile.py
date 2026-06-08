from pathlib import Path

from conan import ConanFile
from conan.errors import ConanException, ConanInvalidConfiguration
from conan.tools.files import copy, get


class BAASFfmpegConan(ConanFile):
    name = "baas-ffmpeg"
    version = "6.1-abi60"
    license = "LGPL-2.1-or-later"
    package_type = "shared-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"provider": ["prebuilt", "system"]}
    default_options = {"provider": "prebuilt"}

    def configure(self):
        if self.settings.os == "Linux":
            self.options.provider = "system"

    def validate(self):
        if self.settings.os == "Windows" and self.settings.arch != "x86_64":
            raise ConanInvalidConfiguration("baas-ffmpeg prebuilt provider is locked to Windows x64")
        if self.settings.os == "Macos":
            raise ConanInvalidConfiguration("baas-ffmpeg macOS prebuilt metadata is not locked yet")

    def source(self):
        pass

    def _prebuilt_root(self):
        source = self.conan_data["sources"][str(self.version)]["prebuilt"]
        root = Path(self.build_folder) / "prebuilt"
        get(self, url=source["url"], sha256=source["sha256"], strip_root=True, destination=root)
        return root

    def package(self):
        if str(self.options.provider) == "system":
            return
        root = self._prebuilt_root()
        package = Path(self.package_folder)
        copy(self, "*", str(root / "include"), str(package / "include"))
        copy(self, "*.lib", str(root / "lib"), str(package / "lib"))
        copy(self, "*.dll", str(root / "bin"), str(package / "bin"))
        self._check_minimum_package()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-ffmpeg")
        self.cpp_info.set_property("cmake_target_name", "BAAS::FFmpeg")
        self.cpp_info.libs = ["avcodec", "avformat", "avutil", "swresample", "swscale"]
        self.cpp_info.bindirs = ["bin"]
        if str(self.options.provider) == "system":
            self.cpp_info.includedirs = []
            self.cpp_info.libdirs = []
            self.cpp_info.bindirs = []
            self.cpp_info.system_libs = self.cpp_info.libs
            self.cpp_info.libs = []

    def _check_minimum_package(self):
        package = Path(self.package_folder)
        required = [
            package / "include" / "libavcodec" / "avcodec.h",
            package / "lib" / "avcodec.lib",
            package / "lib" / "avformat.lib",
            package / "lib" / "avutil.lib",
            package / "lib" / "swresample.lib",
            package / "lib" / "swscale.lib",
            package / "bin" / "avcodec-60.dll",
            package / "bin" / "avformat-60.dll",
            package / "bin" / "avutil-58.dll",
            package / "bin" / "swresample-4.dll",
            package / "bin" / "swscale-7.dll",
        ]
        missing = [str(path.relative_to(package)) for path in required if not path.exists()]
        if missing:
            raise ConanException("FFmpeg package is missing required files: " + ", ".join(missing))
