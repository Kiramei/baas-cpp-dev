BAAS_sub_title_LOG("BAAS_APP MacOS Configure")

if(BAAS_APP_USE_CUDA)
    message(FATAL_ERROR "CUDA is not available in MacOS.")
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
        INSTALL_RPATH "@executable_path"
        BUILD_RPATH "@executable_path"
)

target_link_libraries(
        BAAS_APP
        PRIVATE
        ${LIB_RAW}
)

add_custom_command(
        TARGET BAAS_APP
        POST_BUILD
        COMMAND /bin/sh -c "install_name_tool -add_rpath @executable_path \"$<TARGET_FILE:BAAS_APP>\" 2>/dev/null || true"
        COMMENT "Updating rpath for BAAS_APP"
        VERBATIM
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
