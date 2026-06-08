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

LOG_LINE()
message(STATUS "Conan LIB RAW :")
foreach (LIB ${LIB_RAW})
    message(STATUS "${LIB}")
endforeach ()

set_target_properties(
        BAAS_ocr_server
        PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_RPATH "@executable_path"
)

target_link_libraries(
        BAAS_ocr_server
        PRIVATE
        ${LIB_RAW}
)

add_custom_command(
        TARGET BAAS_ocr_server
        POST_BUILD
        COMMAND /bin/sh -c "install_name_tool -add_rpath @executable_path \"$<TARGET_FILE:BAAS_ocr_server>\" 2>/dev/null || true"
        COMMENT "Updating rpath for BAAS_ocr_server"
        VERBATIM
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
