import re
from pathlib import Path

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import cmake_layout


CONF_NAMESPACE = "user.baas.dependencies"
DEPENDENCY_ORDER = [
    "baas-opencv",
    "baas-onnxruntime",
    "baas-lz4",
    "baas-nlohmann-json",
    "baas-cpp-httplib",
    "baas-spdlog",
    "baas-simdutf",
]


def _dependency_conf_key(dependency):
    return f"{dependency.removeprefix('baas-').replace('-', '_')}_version"


def _version_sort_key(version):
    tokens = re.findall(r"\d+|[A-Za-z]+", version)
    return tuple((0, int(token)) if token.isdigit() else (1, token) for token in tokens)


class BAASDepsConan(ConanFile):
    name = "baas-deps"
    version = "0.1"

    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeToolchain", "CMakeDeps"

    options = {
        "onnxruntime_use_cuda": [True, False],
        "use_ffmpeg": [True, False],
        "use_benchmark": [True, False],
        "use_opencv_dnn": [True, False],
    }

    default_options = {
        "onnxruntime_use_cuda": False,
        "use_ffmpeg": True,
        "use_benchmark": True,
        "use_opencv_dnn": False,
        "baas-onnxruntime/*:provider": "cpu",
        "baas-opencv/*:with_dnn": False,
    }

    def layout(self):
        cmake_layout(self)
        self.folders.generators = "generators"

    def configure(self):
        self.options["baas-onnxruntime"].provider = "cuda" if self.options.onnxruntime_use_cuda else "cpu"
        self.options["baas-opencv"].with_dnn = bool(self.options.use_opencv_dnn)

    def requirements(self):
        for dependency in DEPENDENCY_ORDER:
            self.requires(f"{dependency}/{self._selected_dependency_version(dependency)}")

        if self.options.use_ffmpeg:
            self.requires(f"baas-ffmpeg/{self._selected_dependency_version('baas-ffmpeg')}")

        if self.options.use_benchmark:
            self.requires(f"baas-benchmark/{self._selected_dependency_version('baas-benchmark')}")

    def _recipe_version(self, dependency):
        conanfile = Path(self.recipe_folder) / "recipes" / dependency / "conanfile.py"
        match = re.search(
            r'^\s*version\s*=\s*["\']([^"\']+)["\']',
            conanfile.read_text(encoding="utf-8"),
            re.MULTILINE,
        )
        if not match:
            raise ConanInvalidConfiguration(f"Cannot determine recommended version from {conanfile}")
        return match.group(1)

    def _allowed_versions(self, dependency):
        versions_dir = Path(self.recipe_folder) / "recipes" / dependency / "versions"
        versions = sorted((path.stem for path in versions_dir.glob("*.yml")), key=_version_sort_key)
        if not versions:
            raise ConanInvalidConfiguration(f"No fixed source metadata found in {versions_dir}")
        return versions

    def _selected_dependency_version(self, dependency):
        default = self._recipe_version(dependency)
        conf_name = f"{CONF_NAMESPACE}:{_dependency_conf_key(dependency)}"
        version = str(self.conf.get(conf_name, default=default))
        allowed_versions = self._allowed_versions(dependency)
        allowed = set(allowed_versions)
        if version not in allowed:
            allowed_text = ", ".join(allowed_versions)
            raise ConanInvalidConfiguration(
                f"{conf_name}={version!r} is not an allowed BAAS dependency version. "
                f"Allowed versions for {dependency}: {allowed_text}. "
                "Add fixed source metadata under recipes/<dependency>/versions before enabling another version."
            )
        return version
