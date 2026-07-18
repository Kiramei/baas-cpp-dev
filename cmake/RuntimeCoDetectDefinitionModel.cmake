include_guard(GLOBAL)

foreach(required_target BAAS_resource_core BAAS_runtime_strict_json)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR "RuntimeCoDetectDefinitionModel requires ${required_target}")
    endif()
endforeach()

add_library(
        BAAS_runtime_co_detect_definition_model
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/procedure/CoDetectPythonCompatDefinition.cpp"
)
add_library(BAAS::runtime_co_detect_definition_model ALIAS BAAS_runtime_co_detect_definition_model)
target_compile_features(BAAS_runtime_co_detect_definition_model PUBLIC cxx_std_20)
target_include_directories(
        BAAS_runtime_co_detect_definition_model
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_runtime_co_detect_definition_model
        PRIVATE BAAS_resource_core BAAS_runtime_strict_json
)
if(MSVC)
    target_compile_options(
            BAAS_runtime_co_detect_definition_model
            PUBLIC /utf-8 PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_runtime_co_detect_definition_model PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_RUNTIME_CO_DETECT_DEFINITION_MODEL_TESTS)
    include(CTest)
    add_executable(
            BAAS_runtime_co_detect_definition_model_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/CoDetectPythonCompatDefinitionTests.cpp"
    )
    target_compile_features(
            BAAS_runtime_co_detect_definition_model_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_runtime_co_detect_definition_model_tests
            PRIVATE BAAS_runtime_co_detect_definition_model
    )
    if(MSVC)
        target_compile_options(
                BAAS_runtime_co_detect_definition_model_tests
                PRIVATE /utf-8 /W4 /permissive- /EHsc
        )
    else()
        target_compile_options(
                BAAS_runtime_co_detect_definition_model_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_runtime_co_detect_definition_model_tests
            COMMAND BAAS_runtime_co_detect_definition_model_tests
    )
    set_tests_properties(
            BAAS_runtime_co_detect_definition_model_tests PROPERTIES TIMEOUT 60
    )
endif()
