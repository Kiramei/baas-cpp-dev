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

if(TARGET BAAS::benchmark AND TARGET BAAS::benchmark_main)
    list(APPEND LIB_RAW BAAS::benchmark BAAS::benchmark_main)
else()
    message(FATAL_ERROR "BAAS::benchmark package was not found. Run conan install with -o \"&:use_benchmark=True\"")
endif()

LOG_LINE()
message(STATUS "Conan LIB RAW :")
foreach (LIB ${LIB_RAW})
    message(STATUS "${LIB}")
endforeach ()

set_target_properties(
        BAAS_APP
        PROPERTIES
        INSTALL_RPATH "\$ORIGIN"
        BUILD_RPATH "\$ORIGIN"
)

set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

target_link_libraries(
        BAAS_APP
        PRIVATE
        ${LIB_RAW}
)

baas_copy_conan_runtime_dependencies(
        BAAS_APP
        DESTINATION "${CMAKE_BINARY_DIR}/bin"
        PACKAGES
        baas-opencv
        baas-onnxruntime
        baas-ffmpeg
        baas-lz4
        baas-spdlog
        baas-simdutf
        baas-benchmark
)
