include_guard(GLOBAL)

if(NOT TARGET BAAS_script_runtime OR NOT TARGET BAAS_resource_core)
    message(FATAL_ERROR "BAAS script Procedure Host requires script runtime and resource core")
endif()

find_package(Threads REQUIRED)

add_library(
        BAAS_script_procedure_host
        STATIC
        "${BAAS_PROJECT_PATH}/src/script/host/ProcedureSnapshot.cpp"
        "${BAAS_PROJECT_PATH}/src/script/host/ProcedureHost.cpp"
)
target_compile_features(BAAS_script_procedure_host PUBLIC cxx_std_20)
target_include_directories(
        BAAS_script_procedure_host
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_script_procedure_host
        PUBLIC BAAS_script_runtime BAAS_resource_core Threads::Threads
)
if(MSVC)
    target_compile_options(BAAS_script_procedure_host PUBLIC /utf-8 PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_script_procedure_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SCRIPT_PROCEDURE_HOST_TESTS)
    include(CTest)
    target_compile_definitions(
            BAAS_script_procedure_host
            PRIVATE BAAS_SCRIPT_PROCEDURE_HOST_TEST_HOOKS=1
    )
    add_executable(
            BAAS_script_procedure_host_tests
            "${BAAS_PROJECT_PATH}/tests/script/ProcedureHostTests.cpp"
    )
    target_compile_features(BAAS_script_procedure_host_tests PRIVATE cxx_std_20)
    target_compile_definitions(
            BAAS_script_procedure_host_tests
            PRIVATE BAAS_SCRIPT_PROCEDURE_HOST_TEST_HOOKS=1
    )
    target_link_libraries(
            BAAS_script_procedure_host_tests
            PRIVATE BAAS_script_procedure_host Threads::Threads
    )
    add_test(NAME BAAS_script_procedure_host_tests COMMAND BAAS_script_procedure_host_tests)
    set_tests_properties(BAAS_script_procedure_host_tests PROPERTIES TIMEOUT 90)
endif()
