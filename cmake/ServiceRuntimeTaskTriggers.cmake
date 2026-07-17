include_guard(GLOBAL)

add_library(
        BAAS_service_runtime_task_triggers
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/RuntimeTaskTriggerRegistration.cpp"
)
target_compile_features(BAAS_service_runtime_task_triggers PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_runtime_task_triggers
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_runtime_task_triggers
        PUBLIC BAAS_service_trigger_dispatch
        PRIVATE BAAS::nlohmann_json
)
if(MSVC)
    target_compile_options(
            BAAS_service_runtime_task_triggers PRIVATE /W4 /permissive- /EHsc /utf-8
    )
else()
    target_compile_options(
            BAAS_service_runtime_task_triggers PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SERVICE_RUNTIME_TASK_TRIGGER_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_runtime_task_trigger_tests
            "${BAAS_PROJECT_PATH}/tests/service/RuntimeTaskTriggerRegistrationTests.cpp"
    )
    target_compile_features(
            BAAS_service_runtime_task_trigger_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_runtime_task_trigger_tests
            PRIVATE
            BAAS_service_runtime_task_triggers
            BAAS_service_trigger_executor
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_runtime_task_trigger_tests PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_runtime_task_trigger_tests PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_service_runtime_task_trigger_tests
            COMMAND BAAS_service_runtime_task_trigger_tests
    )
    set_tests_properties(
            BAAS_service_runtime_task_trigger_tests PROPERTIES TIMEOUT 45
    )
endif()
