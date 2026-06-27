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

baas_link_runtime_target(
        BAAS_ocr_server
        SCOPE PRIVATE
        DESTINATION "${BAAS_OCR_LIBRARY_OUTPUT_DIRECTORY}"
        LIBRARIES ${LIB_RAW}
        PACKAGES
        baas-opencv
        baas-onnxruntime
        baas-spdlog
        baas-simdutf
)
