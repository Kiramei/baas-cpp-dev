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
    if(NOT TARGET BAAS::ONNXRuntimeCUDAProvider)
        message(FATAL_ERROR "BAAS_APP_USE_CUDA requires conan install with -o \"&:onnxruntime_use_cuda=True\"")
    endif()
    list(APPEND LIB_RAW BAAS::ONNXRuntimeCUDAProvider)
endif()

if(TARGET BAAS::benchmark AND TARGET BAAS::benchmark_main)
    list(APPEND LIB_RAW BAAS::benchmark BAAS::benchmark_main)
else()
    message(FATAL_ERROR "BAAS::benchmark package was not found. Run conan install with -o \"&:use_benchmark=True\"")
endif()

message(STATUS "LIB RAW :")
foreach (LIB ${LIB_RAW})
    message(STATUS "${LIB}")
endforeach ()

target_link_libraries(
        BAAS_APP
        ${LIB_RAW}
)

baas_copy_conan_runtime_dependencies(
        BAAS_APP
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
