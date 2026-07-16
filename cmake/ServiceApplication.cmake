include_guard(GLOBAL)

add_library(
        BAAS_service_application
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/ServiceApplication.cpp"
)
target_compile_features(BAAS_service_application PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_application
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_compile_definitions(
        BAAS_service_application
        PUBLIC BAAS_SERVICE_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(
        BAAS_service_application
        PUBLIC
        BAAS_service_command_line
        BAAS_service_shutdown_coordinator
        BAAS_service_production_http_host
        BAAS_service_provider_backend
        BAAS_service_file_resource_store
        BAAS_service_runtime_provider_bridge
        BAAS_service_remote_backend
        BAAS_service_status_trigger
        BAAS_service_configuration_triggers
        BAAS_service_trigger_handler
)

add_executable(
        BAAS_service
        "${BAAS_PROJECT_PATH}/apps/BAAS_service/main.cpp"
)
target_compile_features(BAAS_service PRIVATE cxx_std_20)
target_link_libraries(BAAS_service PRIVATE BAAS_service_application)
set_target_properties(BAAS_service PROPERTIES OUTPUT_NAME "BAAS_service")

if(MSVC)
    target_compile_options(BAAS_service_application PUBLIC /utf-8)
    target_compile_options(BAAS_service_application PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(BAAS_service PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(BAAS_service_application PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(BAAS_service PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_APP_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_application_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceApplicationTests.cpp"
    )
    target_compile_features(BAAS_service_application_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_application_tests
            PRIVATE BAAS_service_application
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_application_tests PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_application_tests PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_service_application_tests
            COMMAND BAAS_service_application_tests
    )
    set_tests_properties(
            BAAS_service_application_tests
            PROPERTIES TIMEOUT 90
    )

    add_test(NAME BAAS_service_version_cli COMMAND BAAS_service --version)
    set_tests_properties(
            BAAS_service_version_cli
            PROPERTIES
            PASS_REGULAR_EXPRESSION "BAAS_service ${PROJECT_VERSION}"
            TIMEOUT 10
    )
    add_test(NAME BAAS_service_help_cli COMMAND BAAS_service --help)
    set_tests_properties(
            BAAS_service_help_cli
            PROPERTIES
            PASS_REGULAR_EXPRESSION "Usage: BAAS_service"
            TIMEOUT 10
    )
endif()
