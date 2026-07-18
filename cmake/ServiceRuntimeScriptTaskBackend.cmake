include_guard(GLOBAL)

find_package(Threads REQUIRED)

add_library(
        BAAS_service_runtime_script_task_backend
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/runtime/RuntimeScriptTaskBackend.cpp"
)
target_compile_features(
        BAAS_service_runtime_script_task_backend PUBLIC cxx_std_20
)
target_include_directories(
        BAAS_service_runtime_script_task_backend
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_runtime_script_task_backend
        PUBLIC BAAS_service_runtime_task_owner Threads::Threads
)
if(MSVC)
    target_compile_options(
            BAAS_service_runtime_script_task_backend
            PUBLIC /utf-8
            PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_service_runtime_script_task_backend
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SERVICE_RUNTIME_SCRIPT_TASK_BACKEND_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_runtime_script_task_backend_tests
            "${BAAS_PROJECT_PATH}/tests/service/RuntimeScriptTaskBackendTests.cpp"
    )
    target_compile_features(
            BAAS_service_runtime_script_task_backend_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_runtime_script_task_backend_tests
            PRIVATE BAAS_service_runtime_script_task_backend
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_runtime_script_task_backend_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_runtime_script_task_backend_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_service_runtime_script_task_backend_tests
            COMMAND BAAS_service_runtime_script_task_backend_tests
    )
    set_tests_properties(
            BAAS_service_runtime_script_task_backend_tests
            PROPERTIES TIMEOUT 30 LABELS "service;runtime-script-backend"
    )
endif()
