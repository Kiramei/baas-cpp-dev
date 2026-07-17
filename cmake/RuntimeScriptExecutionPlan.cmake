include_guard(GLOBAL)

if(NOT TARGET BAAS_runtime_script_catalog
   OR NOT TARGET BAAS_runtime_script_package_loader)
    message(FATAL_ERROR
        "RuntimeScriptExecutionPlan requires the runtime script catalog and package loader")
endif()

add_library(
        BAAS_runtime_script_execution_plan
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/script/RuntimeScriptExecutionPlan.cpp"
)
add_library(BAAS::runtime_script_execution_plan ALIAS BAAS_runtime_script_execution_plan)
target_compile_features(BAAS_runtime_script_execution_plan PUBLIC cxx_std_20)
target_include_directories(
        BAAS_runtime_script_execution_plan
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_runtime_script_execution_plan
        PUBLIC BAAS_runtime_script_catalog BAAS_runtime_script_package_loader
)

if(MSVC)
    target_compile_options(
            BAAS_runtime_script_execution_plan
            PUBLIC /utf-8
            PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_runtime_script_execution_plan
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_RUNTIME_SCRIPT_EXECUTION_PLAN_TESTS)
    if(NOT TARGET BAAS_resource_core)
        message(FATAL_ERROR "RuntimeScriptExecutionPlan tests require BAAS_resource_core")
    endif()
    include(CTest)
    add_executable(
            BAAS_runtime_script_execution_plan_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/RuntimeScriptExecutionPlanTests.cpp"
    )
    target_compile_features(BAAS_runtime_script_execution_plan_tests PRIVATE cxx_std_20)
    target_compile_definitions(
            BAAS_runtime_script_execution_plan_tests
            PRIVATE BAAS_RUNTIME_REPOSITORY_TESTING
    )
    target_link_libraries(
            BAAS_runtime_script_execution_plan_tests
            PRIVATE BAAS_runtime_script_execution_plan BAAS_resource_core
    )
    if(MSVC)
        target_compile_options(
                BAAS_runtime_script_execution_plan_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_runtime_script_execution_plan_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_runtime_script_execution_plan_tests
            COMMAND BAAS_runtime_script_execution_plan_tests
    )
    set_tests_properties(BAAS_runtime_script_execution_plan_tests PROPERTIES TIMEOUT 60)
endif()
