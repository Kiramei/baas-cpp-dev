# Conan migration gaps

- OCR Server is the current cross-platform Conan migration target: Windows x64,
  Linux x64, macOS arm64, Android arm64-v8a, and Android x86_64.
- CMake no longer has repository-local `dll/` or `lib/` dependency fallback
  logic. Targets that request dependencies must be configured with Conan
  generators.
- OCR Linux/macOS/Android currently support ONNX Runtime CPU only.
- BAAS App Linux/macOS CMake is Conan-only, but macOS BAAS App still needs
  locked `baas-ffmpeg` macOS metadata before it can be built end to end.
- ISA remains Windows-only.
- `baas-nlohmann-json` remains header-only. A true shared-library conversion
  would require adding a BAAS wrapper library, because upstream nlohmann_json
  does not build a meaningful shared binary.
