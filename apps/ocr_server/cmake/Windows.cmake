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
    baas_append_onnxruntime_cuda_provider(
            LIB_RAW
            "BAAS_OCR_SERVER_USE_CUDA requires conan install with -o \"&:onnxruntime_use_cuda=True\""
    )
endif()

baas_link_runtime_target(
        BAAS_ocr_server
        SCOPE PRIVATE
        LIBRARIES ${LIB_RAW}
        PACKAGES
        baas-opencv
        baas-onnxruntime
        baas-spdlog
        baas-simdutf
)
