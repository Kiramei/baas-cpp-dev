BAAS_sub_title_LOG("BAAS_APP Windows Configure")

if(BAAS_APP_USE_CUDA)
    target_compile_definitions(
            BAAS_APP
            PRIVATE
            __CUDA__
    )
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
        ws2_32
        shlwapi
)

if(BAAS_APP_USE_CUDA)
    baas_append_onnxruntime_cuda_provider(
            LIB_RAW
            "BAAS_APP_USE_CUDA requires conan install with -o \"&:onnxruntime_use_cuda=True\""
    )
endif()

baas_append_benchmark_libraries(LIB_RAW)

baas_link_runtime_target(
        BAAS_APP
        SCOPE PRIVATE
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

set(ICON_RESOURCE "${BAAS_APP_BAAS_CPP_DIR}/src/rc/logo.ico")
set(RES_FILE "${BAAS_APP_BAAS_CPP_DIR}/src/rc/app.rc")
target_sources(BAAS_APP PRIVATE ${RES_FILE})
