if(BAAS_OCR_SERVER_USE_CUDA)
    message(FATAL_ERROR "CUDA is not available in MacOS.")
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
        INSTALL_RPATH "@executable_path"
        BUILD_RPATH "@executable_path"
)

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

add_custom_command(
        TARGET BAAS_ocr_server
        POST_BUILD
        COMMAND /bin/sh -c "install_name_tool -add_rpath @executable_path \"$<TARGET_FILE:BAAS_ocr_server>\" 2>/dev/null || true"
        COMMENT "Updating rpath for BAAS_ocr_server"
        VERBATIM
)
