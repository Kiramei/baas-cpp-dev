include_guard(GLOBAL)

find_package(Threads REQUIRED)

add_library(
        BAAS_service_shutdown_coordinator
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/ServiceShutdown.cpp"
)
target_compile_features(BAAS_service_shutdown_coordinator PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_shutdown_coordinator
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_shutdown_coordinator
        PUBLIC BAAS_service_router Threads::Threads
)

if(MSVC)
    target_compile_options(BAAS_service_shutdown_coordinator PUBLIC /utf-8)
    target_compile_options(BAAS_service_shutdown_coordinator PRIVATE /W4 /permissive-)
else()
    target_compile_options(
            BAAS_service_shutdown_coordinator
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SERVICE_SHUTDOWN_COORDINATOR_TESTS)
    include(CTest)
    target_compile_definitions(
            BAAS_service_shutdown_coordinator
            PRIVATE BAAS_SERVICE_SHUTDOWN_TEST_HOOKS=1
    )
    add_executable(
            BAAS_service_shutdown_coordinator_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceShutdownTests.cpp"
    )
    target_compile_features(
            BAAS_service_shutdown_coordinator_tests
            PRIVATE cxx_std_20
    )
    target_compile_definitions(
            BAAS_service_shutdown_coordinator_tests
            PRIVATE BAAS_SERVICE_SHUTDOWN_TEST_HOOKS=1
    )
    target_link_libraries(
            BAAS_service_shutdown_coordinator_tests
            PRIVATE BAAS_service_shutdown_coordinator
    )
    add_test(
            NAME BAAS_service_shutdown_coordinator_tests
            COMMAND BAAS_service_shutdown_coordinator_tests
    )
    set_tests_properties(
            BAAS_service_shutdown_coordinator_tests
            PROPERTIES TIMEOUT 30
    )
endif()
