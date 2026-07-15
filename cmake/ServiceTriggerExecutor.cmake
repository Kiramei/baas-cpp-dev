include_guard(GLOBAL)

find_package(Threads REQUIRED)

add_library(
        BAAS_service_trigger_executor
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/trigger/TriggerExecutor.cpp"
)
target_compile_features(BAAS_service_trigger_executor PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_trigger_executor
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_trigger_executor
        PUBLIC BAAS_service_trigger_dispatch Threads::Threads
)

if(MSVC)
    target_compile_options(BAAS_service_trigger_executor PUBLIC /utf-8)
    target_compile_options(BAAS_service_trigger_executor PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_trigger_executor PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_TRIGGER_EXECUTOR_TESTS)
    include(CTest)
    target_compile_definitions(
            BAAS_service_trigger_executor
            PRIVATE BAAS_SERVICE_TRIGGER_EXECUTOR_TEST_HOOKS=1
    )
    add_executable(
            BAAS_service_trigger_executor_tests
            "${BAAS_PROJECT_PATH}/tests/service/TriggerExecutorTests.cpp"
            "${BAAS_PROJECT_PATH}/tests/service/TriggerExecutorOdrConsumer.cpp"
    )
    target_compile_features(BAAS_service_trigger_executor_tests PRIVATE cxx_std_20)
    set_source_files_properties(
            "${BAAS_PROJECT_PATH}/tests/service/TriggerExecutorTests.cpp"
            PROPERTIES COMPILE_DEFINITIONS
            BAAS_SERVICE_TRIGGER_EXECUTOR_TEST_HOOKS=1
    )
    target_link_libraries(
            BAAS_service_trigger_executor_tests
            PRIVATE BAAS_service_trigger_executor
    )
    add_test(
            NAME BAAS_service_trigger_executor_tests
            COMMAND BAAS_service_trigger_executor_tests
    )
    set_tests_properties(
            BAAS_service_trigger_executor_tests
            PROPERTIES TIMEOUT 30
    )
endif()
