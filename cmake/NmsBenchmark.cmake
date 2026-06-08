include_guard(GLOBAL)

if(NOT BUILD_BAAS_NMS_BENCHMARK)
    return()
endif()

if(NOT TARGET BAAS::benchmark)
    message(FATAL_ERROR "BAAS::benchmark package was not found. Run conan install with -o \"&:use_benchmark=True\"")
endif()

if(NOT TARGET BAAS::OpenCVDnn)
    message(FATAL_ERROR "BAAS::OpenCVDnn package was not found. Run conan install with -o \"&:use_opencv_dnn=True\"")
endif()

add_executable(
        baas_nms_benchmark
        ${CMAKE_CURRENT_LIST_DIR}/../apps/benchmarks/nms/baas_nms_benchmark.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/utils/BAASImageNms.cpp
)

target_include_directories(
        baas_nms_benchmark
        PRIVATE
        ${BAAS_PROJECT_PATH}/include
)

target_link_libraries(
        baas_nms_benchmark
        PRIVATE
        BAAS::OpenCVDnn
        BAAS::benchmark
)
