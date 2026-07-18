include_guard(GLOBAL)

if(NOT TARGET BAAS::nlohmann_json)
    message(FATAL_ERROR "Legacy procedure definition validation requires BAAS::nlohmann_json")
endif()

add_library(
        BAAS_legacy_procedure_definition_validation
        STATIC
        "${BAAS_PROJECT_PATH}/src/procedure/LegacyProcedureDefinitionValidation.cpp"
)
target_compile_features(BAAS_legacy_procedure_definition_validation PUBLIC cxx_std_20)
target_include_directories(
        BAAS_legacy_procedure_definition_validation
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_legacy_procedure_definition_validation
        PUBLIC BAAS::nlohmann_json
)
if(MSVC)
    target_compile_options(
            BAAS_legacy_procedure_definition_validation
            PUBLIC /utf-8 PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_legacy_procedure_definition_validation PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_LEGACY_PROCEDURE_DEFINITION_TESTS)
    include(CTest)
    add_executable(
            BAAS_legacy_procedure_definition_tests
            "${BAAS_PROJECT_PATH}/tests/procedure/LegacyProcedureDefinitionValidationTests.cpp"
    )
    target_compile_features(BAAS_legacy_procedure_definition_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_legacy_procedure_definition_tests
            PRIVATE BAAS_legacy_procedure_definition_validation
    )
    add_test(
            NAME BAAS_legacy_procedure_definition_tests
            COMMAND BAAS_legacy_procedure_definition_tests
    )
    set_tests_properties(
            BAAS_legacy_procedure_definition_tests PROPERTIES TIMEOUT 30
    )
endif()
