BAAS_sub_title_LOG("BAAS_APP Linux Configure")

if(BAAS_APP_USE_CUDA)
    message(FATAL_ERROR "Linux BAAS_APP Conan build currently supports ONNX Runtime CPU only")
endif()

set(
        LIB_RAW
        BAAS_ipc
        BAAS::OpenCV
        BAAS::ONNXRuntime
        BAAS::FFmpeg
        BAAS::LZ4
        BAAS::nlohmann_json
        BAAS::httplib
        BAAS::spdlog
        BAAS::simdutf
)

baas_append_benchmark_libraries(LIB_RAW)

set_target_properties(
        BAAS_APP
        PROPERTIES
        INSTALL_RPATH "\$ORIGIN"
        BUILD_RPATH "\$ORIGIN"
)

set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

baas_link_runtime_target(
        BAAS_APP
        SCOPE PRIVATE
        DESTINATION "${CMAKE_BINARY_DIR}/bin"
        LIBRARIES ${LIB_RAW}
        PACKAGES
        baas-opencv
        baas-onnxruntime
        baas-ffmpeg
        baas-lz4
        baas-spdlog
        baas-simdutf
        baas-benchmark
)
