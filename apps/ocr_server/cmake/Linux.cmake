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

LOG_LINE()
message(STATUS "Conan LIB RAW :")
foreach (LIB ${LIB_RAW})
    message(STATUS "${LIB}")
endforeach ()

set_target_properties(
        BAAS_ocr_server
        PROPERTIES
        INSTALL_RPATH "\$ORIGIN"
        BUILD_RPATH "\$ORIGIN"
)

set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

target_link_libraries(
        BAAS_ocr_server
        PRIVATE
        ${LIB_RAW}
)

baas_copy_conan_runtime_dependencies(
        BAAS_ocr_server
        DESTINATION "${CMAKE_BINARY_DIR}/bin"
        PACKAGES
        baas-opencv
        baas-onnxruntime
        baas-spdlog
        baas-simdutf
)
