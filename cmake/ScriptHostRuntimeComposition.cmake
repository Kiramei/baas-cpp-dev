include_guard(GLOBAL)

if(NOT TARGET BAAS_script_runtime)
    message(FATAL_ERROR "BAAS script Host runtime composition requires script runtime")
endif()

add_library(
        BAAS_script_host_runtime_composition
        STATIC
        "${BAAS_PROJECT_PATH}/src/script/host/HostRuntimeComposition.cpp"
)
target_compile_features(BAAS_script_host_runtime_composition PUBLIC cxx_std_20)
target_include_directories(
        BAAS_script_host_runtime_composition
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_script_host_runtime_composition
        PUBLIC BAAS_script_runtime
)
if(MSVC)
    target_compile_options(
            BAAS_script_host_runtime_composition
            PUBLIC /utf-8 PRIVATE /W4 /permissive-
    )
else()
    target_compile_options(
            BAAS_script_host_runtime_composition
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SCRIPT_HOST_RUNTIME_COMPOSITION_TESTS)
    include(CTest)
    add_executable(
            BAAS_script_host_runtime_composition_tests
            "${BAAS_PROJECT_PATH}/tests/script/HostRuntimeCompositionTests.cpp"
    )
    target_compile_features(
            BAAS_script_host_runtime_composition_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_script_host_runtime_composition_tests
            PRIVATE BAAS_script_host_runtime_composition
    )
    add_test(
            NAME BAAS_script_host_runtime_composition_tests
            COMMAND BAAS_script_host_runtime_composition_tests
    )
    set_tests_properties(
            BAAS_script_host_runtime_composition_tests PROPERTIES TIMEOUT 60
    )
endif()
