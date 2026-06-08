from pathlib import Path

from conan import ConanFile
from conan.errors import ConanException, ConanInvalidConfiguration
from conan.tools.files import copy, download, get, unzip


class BAASOnnxRuntimeConan(ConanFile):
    name = "baas-onnxruntime"
    version = "1.22.0"
    license = "MIT"
    package_type = "shared-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"provider": ["cpu", "cuda"]}
    default_options = {"provider": "cpu"}

    def validate(self):
        platform_key = self._platform_key()
        if platform_key not in self.conan_data["sources"][str(self.version)]:
            raise ConanInvalidConfiguration(
                f"baas-onnxruntime {self.version} does not have locked metadata for "
                f"{self.settings.os}/{self.settings.arch} provider={self.options.provider}"
            )

    def source(self):
        pass

    def _platform_key(self):
        provider = str(self.options.provider)
        os_name = str(self.settings.os)
        arch = str(self.settings.arch)
        if provider == "cuda":
            if os_name == "Windows" and arch == "x86_64":
                return "windows-x86_64-cuda"
            return f"{os_name.lower()}-{arch}-cuda"
        if os_name == "Windows" and arch == "x86_64":
            return "windows-x86_64"
        if os_name == "Linux" and arch == "x86_64":
            return "linux-x86_64"
        if os_name == "Macos" and arch == "armv8":
            return "macos-armv8"
        if os_name == "Android" and arch in {"armv8", "x86_64"}:
            return "android"
        return f"{os_name.lower()}-{arch}"

    def _android_abi(self):
        arch = str(self.settings.arch)
        if arch == "armv8":
            return "arm64-v8a"
        if arch == "x86_64":
            return "x86_64"
        raise ConanInvalidConfiguration(f"Unsupported Android arch for ONNX Runtime: {arch}")

    def _prebuilt_root(self):
        source = self.conan_data["sources"][str(self.version)][self._platform_key()]
        root = Path(self.build_folder) / "prebuilt" / self._platform_key()
        if source.get("archive_type") == "aar":
            archive = Path(self.build_folder) / "onnxruntime-android.aar"
            download(self, source["url"], str(archive), sha256=source["sha256"])
            unzip(self, str(archive), destination=str(root))
            return root
        get(
            self,
            url=source["url"],
            sha256=source["sha256"],
            strip_root=source.get("strip_root", True),
            destination=str(root),
        )
        return root

    def package(self):
        root = self._prebuilt_root()
        package = Path(self.package_folder)
        if self.settings.os == "Android":
            copy(self, "*", str(root / "headers"), str(package / "include"))
            copy(self, "*", str(root / "headers"), str(package / "include" / "onnxruntime"))
            copy(self, "libonnxruntime.so", str(root / "jni" / self._android_abi()), str(package / "lib"))
            copy(self, "libonnxruntime.so", str(root / "jni" / self._android_abi()), str(package / "bin"))
        else:
            copy(self, "*", str(root / "include"), str(package / "include"))
            copy(self, "*", str(root / "include"), str(package / "include" / "onnxruntime"))
            copy(self, "*.lib", str(root / "lib"), str(package / "lib"))
            copy(self, "*.dll", str(root / "lib"), str(package / "bin"))
            copy(self, "*.so*", str(root / "lib"), str(package / "lib"))
            copy(self, "*.dylib*", str(root / "lib"), str(package / "lib"))
        self._check_minimum_package()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "baas-onnxruntime")
        self.cpp_info.set_property("cmake_target_name", "BAAS::ONNXRuntime")
        self.cpp_info.libs = ["onnxruntime"]
        self.cpp_info.bindirs = ["bin"] if self.settings.os == "Windows" else ["lib", "bin"]
        if str(self.options.provider) == "cuda":
            provider = self.cpp_info.components["cuda_provider"]
            provider.set_property("cmake_target_name", "BAAS::ONNXRuntimeCUDAProvider")
            provider.libs = ["onnxruntime", "onnxruntime_providers_cuda", "onnxruntime_providers_shared"]
            provider.libdirs = ["lib"]
            provider.bindirs = ["bin"]

    def _check_minimum_package(self):
        package = Path(self.package_folder)
        if self.settings.os == "Windows":
            library = package / "lib" / "onnxruntime.lib"
            runtime = package / "bin" / "onnxruntime.dll"
        elif self.settings.os == "Macos":
            library = package / "lib" / "libonnxruntime.dylib"
            runtime = package / "lib" / "libonnxruntime.1.22.0.dylib"
        else:
            library = package / "lib" / "libonnxruntime.so"
            runtime = library
        required = [
            package / "include" / "onnxruntime" / "onnxruntime_cxx_api.h",
            library,
            runtime,
        ]
        if str(self.options.provider) == "cuda":
            required.extend(
                [
                    package / "lib" / "onnxruntime_providers_cuda.lib",
                    package / "lib" / "onnxruntime_providers_shared.lib",
                ]
            )
        missing = [str(path.relative_to(package)) for path in required if not path.exists()]
        if missing:
            raise ConanException("ONNX Runtime package is missing required files: " + ", ".join(missing))
