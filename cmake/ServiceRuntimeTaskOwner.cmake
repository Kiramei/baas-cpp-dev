include_guard(GLOBAL)

find_package(Threads REQUIRED)

add_library(
        BAAS_service_runtime_task_owner
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/runtime/RuntimeTaskOwner.cpp"
)
target_compile_features(BAAS_service_runtime_task_owner PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_runtime_task_owner
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_runtime_task_owner
        PUBLIC Threads::Threads
)

if(MSVC)
    target_compile_options(
            BAAS_service_runtime_task_owner
            PUBLIC /utf-8
            PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_service_runtime_task_owner PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SERVICE_RUNTIME_TASK_OWNER_TESTS)
    include(CTest)
    target_compile_definitions(
            BAAS_service_runtime_task_owner
            PRIVATE BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS=1
    )
    add_executable(
            BAAS_service_runtime_task_owner_tests
            "${BAAS_PROJECT_PATH}/tests/service/RuntimeTaskOwnerTests.cpp"
            "${BAAS_PROJECT_PATH}/tests/service/RuntimeTaskOwnerOdrConsumer.cpp"
    )
    target_compile_features(
            BAAS_service_runtime_task_owner_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_runtime_task_owner_tests
            PRIVATE BAAS_service_runtime_task_owner
    )
    set_source_files_properties(
            "${BAAS_PROJECT_PATH}/tests/service/RuntimeTaskOwnerTests.cpp"
            PROPERTIES COMPILE_DEFINITIONS
            BAAS_SERVICE_RUNTIME_TASK_OWNER_TEST_HOOKS=1
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_runtime_task_owner_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_runtime_task_owner_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_service_runtime_task_owner_tests
            COMMAND BAAS_service_runtime_task_owner_tests
    )
    set_tests_properties(
            BAAS_service_runtime_task_owner_tests
            PROPERTIES
            TIMEOUT 30
            LABELS "service;runtime-task-reservations"
    )
endif()
