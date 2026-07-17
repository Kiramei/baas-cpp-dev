include_guard(GLOBAL)

foreach(required_target
        BAAS_runtime_repository
        BAAS_runtime_resource_snapshot_loader
        BAAS_runtime_script_execution_plan
        BAAS_script_procedure_snapshot
        BAAS::nlohmann_json)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR "RuntimeProcedureActivation requires ${required_target}")
    endif()
endforeach()

add_library(
        BAAS_runtime_procedure_activation
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/procedure/RuntimeProcedureActivation.cpp"
)
add_library(BAAS::runtime_procedure_activation ALIAS BAAS_runtime_procedure_activation)
target_compile_features(BAAS_runtime_procedure_activation PUBLIC cxx_std_20)
target_include_directories(
        BAAS_runtime_procedure_activation
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_runtime_procedure_activation
        PUBLIC
            BAAS_runtime_repository
            BAAS_runtime_resource_snapshot_loader
            BAAS_runtime_script_execution_plan
            BAAS_script_procedure_snapshot
        PRIVATE BAAS::nlohmann_json
)
if(MSVC)
    target_compile_options(
            BAAS_runtime_procedure_activation
            PUBLIC /utf-8 PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_runtime_procedure_activation PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_RUNTIME_PROCEDURE_ACTIVATION_TESTS)
    include(CTest)
    target_compile_definitions(
            BAAS_runtime_procedure_activation
            PRIVATE BAAS_RUNTIME_PROCEDURE_ACTIVATION_TESTING
    )
    add_executable(
            BAAS_runtime_procedure_activation_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/RuntimeProcedureActivationTests.cpp"
    )
    target_compile_features(BAAS_runtime_procedure_activation_tests PRIVATE cxx_std_20)
    target_compile_definitions(
            BAAS_runtime_procedure_activation_tests
            PRIVATE
                BAAS_RUNTIME_PROCEDURE_ACTIVATION_TESTING
                BAAS_RUNTIME_REPOSITORY_TESTING
    )
    target_link_libraries(
            BAAS_runtime_procedure_activation_tests
            PRIVATE BAAS_runtime_procedure_activation
    )
    if(MSVC)
        target_compile_options(
                BAAS_runtime_procedure_activation_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_runtime_procedure_activation_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_runtime_procedure_activation_tests
            COMMAND BAAS_runtime_procedure_activation_tests
    )
    set_tests_properties(
            BAAS_runtime_procedure_activation_tests PROPERTIES TIMEOUT 90
    )
endif()
