include_guard(GLOBAL)

if(NOT TARGET BAAS_script_runtime OR NOT TARGET BAAS_resource_core)
    message(FATAL_ERROR "BAAS script Resource Host requires script runtime and resource core")
endif()

add_library(
        BAAS_script_resource_host
        STATIC
        "${BAAS_PROJECT_PATH}/src/script/host/ResourceHost.cpp"
)
target_compile_features(BAAS_script_resource_host PUBLIC cxx_std_20)
target_include_directories(
        BAAS_script_resource_host
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_script_resource_host
        PUBLIC BAAS_script_runtime BAAS_resource_core
)
if(MSVC)
    target_compile_options(BAAS_script_resource_host PUBLIC /utf-8 PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_script_resource_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SCRIPT_RESOURCE_HOST_TESTS)
    include(CTest)
    add_executable(
            BAAS_script_resource_host_tests
            "${BAAS_PROJECT_PATH}/tests/script/ResourceHostTests.cpp"
    )
    target_compile_features(BAAS_script_resource_host_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_script_resource_host_tests
            PRIVATE BAAS_script_resource_host
    )
    add_test(NAME BAAS_script_resource_host_tests COMMAND BAAS_script_resource_host_tests)
    set_tests_properties(BAAS_script_resource_host_tests PROPERTIES TIMEOUT 60)
endif()
