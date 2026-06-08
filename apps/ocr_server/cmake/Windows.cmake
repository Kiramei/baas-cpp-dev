if(BAAS_OCR_SERVER_USE_CUDA)
    target_compile_definitions(
            BAAS_ocr_server
            PRIVATE
            __CUDA__
    )
endif()

set(
        LIB_RAW
        BAAS_ipc
        BAAS::OpenCV
        BAAS::ONNXRuntime
        BAAS::nlohmann_json
        BAAS::httplib
        BAAS::spdlog
        BAAS::simdutf
        ws2_32
        shlwapi
)

if(BAAS_OCR_SERVER_USE_CUDA)
    if(NOT TARGET BAAS::ONNXRuntimeCUDAProvider)
        message(FATAL_ERROR "BAAS_OCR_SERVER_USE_CUDA requires conan install with -o \"&:onnxruntime_use_cuda=True\"")
    endif()
    list(APPEND LIB_RAW BAAS::CUDA)
    list(APPEND LIB_RAW BAAS::ONNXRuntimeCUDAProvider)
endif()

message(STATUS "LIB RAW :")
foreach (LIB ${LIB_RAW})
    message(STATUS "${LIB}")
endforeach ()
LOG_LINE()

target_link_libraries(
        BAAS_ocr_server
        ${LIB_RAW}
)

baas_copy_conan_runtime_dependencies(
        BAAS_ocr_server
        PACKAGES
        baas-opencv
        baas-onnxruntime
        baas-spdlog
        baas-simdutf
)
