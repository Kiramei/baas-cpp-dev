include_guard(GLOBAL)

add_library(
        BAAS_legacy_procedure_execution
        STATIC
        "${BAAS_PROJECT_PATH}/src/procedure/LegacyProcedureExecution.cpp"
)
add_library(BAAS::legacy_procedure_execution ALIAS BAAS_legacy_procedure_execution)
target_compile_features(BAAS_legacy_procedure_execution PUBLIC cxx_std_20)
target_include_directories(
        BAAS_legacy_procedure_execution
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
if(MSVC)
    target_compile_options(
            BAAS_legacy_procedure_execution
            PUBLIC /utf-8 PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_legacy_procedure_execution PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_LEGACY_PROCEDURE_EXECUTION_TESTS)
    include(CTest)
    add_executable(
            BAAS_legacy_procedure_execution_tests
            "${BAAS_PROJECT_PATH}/tests/procedure/LegacyProcedureExecutionTests.cpp"
    )
    target_compile_features(BAAS_legacy_procedure_execution_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_legacy_procedure_execution_tests
            PRIVATE BAAS_legacy_procedure_execution
    )
    if(MSVC)
        target_compile_options(
                BAAS_legacy_procedure_execution_tests
                PRIVATE /utf-8 /W4 /permissive- /EHsc
        )
    else()
        target_compile_options(
                BAAS_legacy_procedure_execution_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_legacy_procedure_execution_tests
            COMMAND BAAS_legacy_procedure_execution_tests
    )
    set_tests_properties(
            BAAS_legacy_procedure_execution_tests PROPERTIES TIMEOUT 30
    )
endif()
