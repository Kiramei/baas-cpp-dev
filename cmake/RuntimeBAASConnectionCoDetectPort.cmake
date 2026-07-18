include_guard(GLOBAL)

foreach(required_target
        BAAS_runtime_co_detect_production_adapter
        BAAS::OpenCV
        BAAS::spdlog
        BAAS::simdutf)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR "RuntimeBAASConnectionCoDetectPort requires ${required_target}")
    endif()
endforeach()

set(_baas_connection_co_detect_backend_source
    "${BAAS_PROJECT_PATH}/src/runtime/procedure/BAASApplicationCoDetectBackend.cpp")
add_library(BAAS_runtime_baas_connection_co_detect_port STATIC
    "${BAAS_PROJECT_PATH}/src/runtime/procedure/BAASConnectionCoDetectPort.cpp"
    "${_baas_connection_co_detect_backend_source}")
add_library(BAAS::runtime_baas_connection_co_detect_port
    ALIAS BAAS_runtime_baas_connection_co_detect_port)
target_compile_features(BAAS_runtime_baas_connection_co_detect_port PUBLIC cxx_std_20)
target_include_directories(BAAS_runtime_baas_connection_co_detect_port
    PUBLIC "${BAAS_PROJECT_PATH}/include")
target_link_libraries(BAAS_runtime_baas_connection_co_detect_port
    PUBLIC BAAS_runtime_co_detect_production_adapter
    PRIVATE BAAS::OpenCV BAAS::spdlog BAAS::simdutf)
if(MSVC)
    # This one translation unit consumes the existing legacy public headers.
    # Keep /WX for new code while suppressing only warnings originating in those
    # already-shipped inline/template headers.
    set_source_files_properties("${_baas_connection_co_detect_backend_source}"
        PROPERTIES COMPILE_OPTIONS "/wd4101;/wd4189;/wd4267;/wd4458")
    target_compile_options(BAAS_runtime_baas_connection_co_detect_port
        PUBLIC /utf-8 PRIVATE /W4 /WX /permissive- /EHsc)
else()
    set_source_files_properties("${_baas_connection_co_detect_backend_source}"
        PROPERTIES COMPILE_OPTIONS
            "-Wno-unused-variable;-Wno-unused-parameter;-Wno-ignored-qualifiers;-Wno-shadow")
    target_compile_options(BAAS_runtime_baas_connection_co_detect_port
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_RUNTIME_BAAS_CONNECTION_CO_DETECT_LINK_CLOSURE)
    foreach(required_target
            BAAS::OpenCV
            BAAS::ONNXRuntime
            BAAS::FFmpeg
            BAAS::nlohmann_json)
        if(NOT TARGET ${required_target})
            message(FATAL_ERROR
                "BAAS connection co-detect link closure requires ${required_target}")
        endif()
    endforeach()
    add_executable(BAAS_runtime_baas_connection_co_detect_link_closure
        "${BAAS_PROJECT_PATH}/tests/runtime/BAASApplicationCoDetectBackendLinkClosure.cpp"
        ${BAAS_CORE_SOURCES})
    target_compile_features(
        BAAS_runtime_baas_connection_co_detect_link_closure PRIVATE cxx_std_20)
    target_include_directories(
        BAAS_runtime_baas_connection_co_detect_link_closure
        PRIVATE "${BAAS_PROJECT_PATH}/include" "${CMAKE_BINARY_DIR}")
    target_compile_definitions(
        BAAS_runtime_baas_connection_co_detect_link_closure
        PRIVATE BAAS_BUILD_DLL BENCHMARK_STATIC_DEFINE)
    target_link_libraries(
        BAAS_runtime_baas_connection_co_detect_link_closure PRIVATE
        BAAS_runtime_baas_connection_co_detect_port
        BAAS::OpenCV BAAS::ONNXRuntime BAAS::FFmpeg BAAS::nlohmann_json
        BAAS::spdlog BAAS::simdutf)
    if(MSVC)
        target_compile_options(
            BAAS_runtime_baas_connection_co_detect_link_closure
            PRIVATE /utf-8 /W0 /permissive- /EHsc)
        target_link_libraries(
            BAAS_runtime_baas_connection_co_detect_link_closure
            PRIVATE ws2_32 shlwapi)
        set_source_files_properties(
            "${BAAS_PROJECT_PATH}/tests/runtime/BAASApplicationCoDetectBackendLinkClosure.cpp"
            PROPERTIES COMPILE_OPTIONS "/W4;/WX")
    else()
        target_compile_options(
            BAAS_runtime_baas_connection_co_detect_link_closure
            PRIVATE -w)
        set_source_files_properties(
            "${BAAS_PROJECT_PATH}/tests/runtime/BAASApplicationCoDetectBackendLinkClosure.cpp"
            PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
    endif()
endif()

if(BUILD_RUNTIME_BAAS_CONNECTION_CO_DETECT_PORT_TESTS)
    include(CTest)
    find_package(Threads REQUIRED)
    add_executable(BAAS_runtime_baas_connection_co_detect_port_tests
        "${BAAS_PROJECT_PATH}/tests/runtime/BAASConnectionCoDetectPortTests.cpp")
    target_compile_features(BAAS_runtime_baas_connection_co_detect_port_tests
        PRIVATE cxx_std_20)
    target_link_libraries(BAAS_runtime_baas_connection_co_detect_port_tests
        PRIVATE BAAS_runtime_baas_connection_co_detect_port Threads::Threads)
    if(MSVC)
        target_compile_options(BAAS_runtime_baas_connection_co_detect_port_tests
            PRIVATE /utf-8 /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(BAAS_runtime_baas_connection_co_detect_port_tests
            PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME BAAS_runtime_baas_connection_co_detect_port_tests
        COMMAND BAAS_runtime_baas_connection_co_detect_port_tests)
    set_tests_properties(BAAS_runtime_baas_connection_co_detect_port_tests
        PROPERTIES TIMEOUT 90)
endif()
