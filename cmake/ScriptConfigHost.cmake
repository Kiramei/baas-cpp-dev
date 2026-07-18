include_guard(GLOBAL)

if(NOT TARGET BAAS_script_runtime)
    message(FATAL_ERROR "BAAS script Config Host requires script runtime")
endif()

add_library(
        BAAS_script_config_host
        STATIC
        "${BAAS_PROJECT_PATH}/src/script/host/ConfigHost.cpp"
)
target_compile_features(BAAS_script_config_host PUBLIC cxx_std_20)
target_include_directories(
        BAAS_script_config_host
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_script_config_host
        PUBLIC BAAS_script_runtime
)
if(MSVC)
    target_compile_options(
            BAAS_script_config_host
            PUBLIC /utf-8 PRIVATE /W4 /permissive-
    )
else()
    target_compile_options(
            BAAS_script_config_host
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SCRIPT_CONFIG_HOST_TESTS)
    include(CTest)
    add_executable(
            BAAS_script_config_host_tests
            "${BAAS_PROJECT_PATH}/tests/script/ConfigHostTests.cpp"
    )
    target_compile_features(BAAS_script_config_host_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_script_config_host_tests
            PRIVATE BAAS_script_config_host BAAS_script_host_runtime_composition
    )
    if(MSVC)
        target_compile_options(
                BAAS_script_config_host_tests PRIVATE /utf-8 /W4 /permissive-
        )
    else()
        target_compile_options(
                BAAS_script_config_host_tests PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(NAME BAAS_script_config_host_tests COMMAND BAAS_script_config_host_tests)
    set_tests_properties(BAAS_script_config_host_tests PROPERTIES TIMEOUT 60)
endif()
