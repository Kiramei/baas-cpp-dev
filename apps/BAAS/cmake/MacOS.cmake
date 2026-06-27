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

baas_append_benchmark_libraries(LIB_RAW)

set_target_properties(
        BAAS_APP
        PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_RPATH "@executable_path"
)

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

add_custom_command(
        TARGET BAAS_APP
        POST_BUILD
        COMMAND /bin/sh -c "install_name_tool -add_rpath @executable_path \"$<TARGET_FILE:BAAS_APP>\" 2>/dev/null || true"
        COMMENT "Updating rpath for BAAS_APP"
        VERBATIM
)
