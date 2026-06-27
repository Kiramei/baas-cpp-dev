if(BAAS_OCR_SERVER_USE_CUDA)
    message(FATAL_ERROR "Linux OCR Conan build currently supports ONNX Runtime CPU only")
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
)

set_target_properties(
        BAAS_ocr_server
        PROPERTIES
        INSTALL_RPATH "\$ORIGIN"
        BUILD_RPATH "\$ORIGIN"
)

set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

baas_link_runtime_target(
        BAAS_ocr_server
        SCOPE PRIVATE
        DESTINATION "${CMAKE_BINARY_DIR}/bin"
        LIBRARIES ${LIB_RAW}
        PACKAGES
        baas-opencv
        baas-onnxruntime
        baas-spdlog
        baas-simdutf
)
