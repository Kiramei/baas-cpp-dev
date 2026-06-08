set(
        BAAS_OCR_LIBRARY_OUTPUT_DIRECTORY
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib/${ANDROID_ABI}
)

set_target_properties(
        BAAS_ocr_server
        PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY
        ${BAAS_OCR_LIBRARY_OUTPUT_DIRECTORY}
)

copy_android_libcxx_shared(${BAAS_OCR_LIBRARY_OUTPUT_DIRECTORY})

set(
        LIB_RAW
        log
        android
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

target_link_libraries(
        BAAS_ocr_server
        PRIVATE
        ${LIB_RAW}
)

baas_copy_conan_runtime_dependencies(
        BAAS_ocr_server
        DESTINATION "${BAAS_OCR_LIBRARY_OUTPUT_DIRECTORY}"
        PACKAGES
        baas-opencv
        baas-onnxruntime
        baas-spdlog
        baas-simdutf
)
