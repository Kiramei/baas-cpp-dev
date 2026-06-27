if (NOT ${TARGET_OS_NAME} STREQUAL "Windows")
    message(STATUS "ISA only support Windows")
    return()
endif ()

BAAS_hr_LOG("APP ISA")

project(ISA VERSION 1.0.0)

configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/version.h.in
        ${CMAKE_BINARY_DIR}/ISA_version.h
)

set(BIN_RESOURCE_REQUIRED               ON PARENT_SCOPE)
set(ADB_BINARY_RESOURCE_REQUIRED        ON PARENT_SCOPE)
set(OCR_MODEL_RESOURCE_REQUIRED         ON PARENT_SCOPE)
set(STATIC_CONFIG_RESOURCE_REQUIRED     ON PARENT_SCOPE)
set(GLOBAL_CONFIG_RESOURCE_REQUIRED     ON PARENT_SCOPE)

file(
        GLOB
        USER_CONFIG_FILES
        ${CMAKE_CURRENT_LIST_DIR}/resource/config/*.json
)
file(
        COPY
        ${USER_CONFIG_FILES}
        DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resource
)
file(
        COPY
        ${CMAKE_CURRENT_LIST_DIR}/resource/image
        DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resource
)
file(
        COPY
        ${CMAKE_CURRENT_LIST_DIR}/resource/feature
        DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resource
)
file(
        COPY
        ${CMAKE_CURRENT_LIST_DIR}/resource/procedure
        DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resource
)

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

if(NOT TARGET BAAS::ONNXRuntimeCUDAProvider)
    message(FATAL_ERROR "ISA requires conan install with -o \"&:onnxruntime_use_cuda=True\"")
endif()

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

if(TARGET BAAS::ONNXRuntimeCUDAProvider)
    list(APPEND LIB_RAW BAAS::ONNXRuntimeCUDAProvider)
endif()

if(TARGET BAAS::benchmark AND TARGET BAAS::benchmark_main)
    list(APPEND LIB_RAW BAAS::benchmark BAAS::benchmark_main)
else()
    message(FATAL_ERROR "BAAS::benchmark package was not found. Run conan install with -o \"&:use_benchmark=True\"")
endif()

message(STATUS "LIB RAW :")
foreach (LIB ${LIB_RAW})
    message(STATUS "${LIB}")
endforeach ()

target_link_libraries(
        ISA
        ${LIB_RAW}
)

baas_copy_conan_runtime_dependencies(
        ISA
        PACKAGES
        baas-opencv
        baas-onnxruntime
        baas-ffmpeg
        baas-lz4
        baas-spdlog
        baas-simdutf
        baas-benchmark
)
