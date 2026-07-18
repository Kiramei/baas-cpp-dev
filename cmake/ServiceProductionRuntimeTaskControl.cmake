include_guard(GLOBAL)

foreach(required_target
        BAAS_service_runtime_task_owner
        BAAS_service_runtime_task_triggers
        BAAS_service_runtime_script_task_backend
        BAAS_service_production_runtime_script_task_factory
        BAAS_runtime_repository)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR
            "ServiceProductionRuntimeTaskControl requires ${required_target}")
    endif()
endforeach()

add_library(
        BAAS_service_production_runtime_task_control
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/ProductionRuntimeTaskControl.cpp"
)
target_compile_features(
        BAAS_service_production_runtime_task_control PUBLIC cxx_std_20
)
target_include_directories(
        BAAS_service_production_runtime_task_control
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_production_runtime_task_control
        PUBLIC
        BAAS_service_runtime_task_owner
        BAAS_service_runtime_task_triggers
        BAAS_service_runtime_script_task_backend
        BAAS_service_production_runtime_script_task_factory
        BAAS_runtime_repository
        PRIVATE BAAS::nlohmann_json
)
if(MSVC)
    target_compile_options(
            BAAS_service_production_runtime_task_control
            PUBLIC /utf-8 PRIVATE /W4 /WX /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_service_production_runtime_task_control
            PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()
