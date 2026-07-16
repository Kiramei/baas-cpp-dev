if (NOT ${TARGET_OS_NAME} STREQUAL "Windows")
    message(STATUS "ISA only support Windows")
    return()
endif ()

BAAS_hr_LOG("APP ISA")

project(ISA VERSION 1.0.0)

configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/version.h.in
        ${CMAKE_BINARY_DIR}/ISA_version.h
)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/RuntimeResources.cmake")

if (MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/MP>")
    add_compile_options(/W0)
endif()

option(USE_CUDA "Use CUDA" OFF)
message(STATUS "USE_CUDA: ${USE_CUDA}")

file(
        GLOB_RECURSE
        BAAS_MODULE_RESOURCE
        ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp
)

add_executable(
        ISA
        main.cpp
        ${BAAS_CORE_SOURCES}
        ${BAAS_MODULE_RESOURCE}
)

target_include_directories(
        ISA
        PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${BAAS_PROJECT_PATH}/include
        ${CMAKE_BINARY_DIR}
)

target_compile_definitions(
        ISA PRIVATE
        __CUDA__
        BAAS_BUILD_DLL
        BENCHMARK_STATIC_DEFINE
)

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

baas_append_onnxruntime_cuda_provider(
        LIB_RAW
        "ISA requires conan install with -o \"&:onnxruntime_use_cuda=True\""
)

baas_append_benchmark_libraries(LIB_RAW)

baas_link_runtime_target(
        ISA
        SCOPE PRIVATE
        LIBRARIES ${LIB_RAW}
        PACKAGES
        baas-opencv
        baas-onnxruntime
        baas-ffmpeg
        baas-lz4
        baas-spdlog
        baas-simdutf
        baas-benchmark
)
