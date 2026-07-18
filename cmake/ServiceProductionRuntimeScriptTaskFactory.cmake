include_guard(GLOBAL)

foreach(required_target
        BAAS_service_runtime_script_task_backend
        BAAS_runtime_repository
        BAAS_runtime_resource_snapshot_loader
        BAAS_runtime_script_execution_plan
        BAAS_runtime_procedure_activation
        BAAS_runtime_co_detect_production_adapter
        BAAS_script_resource_host
        BAAS_script_procedure_host
        BAAS_script_host_runtime_composition
        BAAS_script_runtime)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR
            "ServiceProductionRuntimeScriptTaskFactory requires ${required_target}")
    endif()
endforeach()

add_library(
        BAAS_service_production_runtime_script_task_factory
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/runtime/ProductionRuntimeScriptTaskFactory.cpp"
)
add_library(
        BAAS::service_production_runtime_script_task_factory
        ALIAS BAAS_service_production_runtime_script_task_factory
)
target_compile_features(
        BAAS_service_production_runtime_script_task_factory PUBLIC cxx_std_20
)
target_include_directories(
        BAAS_service_production_runtime_script_task_factory
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_production_runtime_script_task_factory
        PUBLIC
        BAAS_service_runtime_script_task_backend
        BAAS_runtime_repository
        BAAS_runtime_resource_snapshot_loader
        BAAS_runtime_script_execution_plan
        BAAS_runtime_procedure_activation
        BAAS_runtime_co_detect_production_adapter
        BAAS_script_resource_host
        BAAS_script_procedure_host
        BAAS_script_host_runtime_composition
        BAAS_script_runtime
)
if(MSVC)
    target_compile_options(
            BAAS_service_production_runtime_script_task_factory
            PUBLIC /utf-8 PRIVATE /W4 /WX /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_service_production_runtime_script_task_factory
            PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(BUILD_SERVICE_PRODUCTION_RUNTIME_SCRIPT_TASK_FACTORY_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_production_runtime_script_task_factory_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/CoDetectProductionAdapterFixture.cpp"
            "${BAAS_PROJECT_PATH}/tests/service/ProductionRuntimeScriptTaskFactoryTests.cpp"
    )
    target_compile_features(
            BAAS_service_production_runtime_script_task_factory_tests
            PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_production_runtime_script_task_factory_tests
            PRIVATE BAAS_service_production_runtime_script_task_factory
                    BAAS::miniz BAAS::OpenCV
    )
    target_include_directories(
            BAAS_service_production_runtime_script_task_factory_tests
            PRIVATE "${BAAS_PROJECT_PATH}/tests/runtime"
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_production_runtime_script_task_factory_tests
                PRIVATE /utf-8 /W4 /WX /permissive- /EHsc
        )
    else()
        target_compile_options(
                BAAS_service_production_runtime_script_task_factory_tests
                PRIVATE -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(
            NAME BAAS_service_production_runtime_script_task_factory_tests
            COMMAND BAAS_service_production_runtime_script_task_factory_tests
    )
    set_tests_properties(
            BAAS_service_production_runtime_script_task_factory_tests
            PROPERTIES TIMEOUT 90 LABELS "service;runtime-script-production"
    )
endif()
