include_guard(GLOBAL)

add_library(
        BAAS_service_runtime_task_status_json
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/RuntimeTaskStatusJson.cpp"
)
target_compile_features(BAAS_service_runtime_task_status_json PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_runtime_task_status_json
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_runtime_task_status_json
        PUBLIC BAAS_service_runtime_task_owner
)
if(MSVC)
    target_compile_options(
            BAAS_service_runtime_task_status_json
            PUBLIC /utf-8
            PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_service_runtime_task_status_json
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SERVICE_RUNTIME_TASK_STATUS_JSON_TESTS)
    target_compile_definitions(
            BAAS_service_runtime_task_status_json
            PRIVATE BAAS_SERVICE_RUNTIME_TASK_STATUS_JSON_TEST_HOOKS=1
    )
    include(CTest)
    add_executable(
            BAAS_service_runtime_task_status_json_tests
            "${BAAS_PROJECT_PATH}/tests/service/RuntimeTaskStatusJsonTests.cpp"
    )
    target_compile_features(
            BAAS_service_runtime_task_status_json_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_runtime_task_status_json_tests
            PRIVATE BAAS_service_runtime_task_status_json
    )
    target_compile_definitions(
            BAAS_service_runtime_task_status_json_tests
            PRIVATE BAAS_SERVICE_RUNTIME_TASK_STATUS_JSON_TEST_HOOKS=1
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_runtime_task_status_json_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_runtime_task_status_json_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_service_runtime_task_status_json_tests
            COMMAND BAAS_service_runtime_task_status_json_tests
    )
    set_tests_properties(
            BAAS_service_runtime_task_status_json_tests PROPERTIES TIMEOUT 30
    )
endif()
