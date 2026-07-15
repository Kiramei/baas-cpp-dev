include_guard(GLOBAL)

add_library(
        BAAS_service_status_trigger
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/StatusTriggerRegistration.cpp"
)
target_compile_features(BAAS_service_status_trigger PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_status_trigger
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_status_trigger
        PUBLIC BAAS_service_trigger_dispatch
)

if(MSVC)
    target_compile_options(BAAS_service_status_trigger PUBLIC /utf-8)
    target_compile_options(BAAS_service_status_trigger PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_status_trigger PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_STATUS_TRIGGER_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_status_trigger_tests
            "${BAAS_PROJECT_PATH}/tests/service/StatusTriggerRegistrationTests.cpp"
    )
    target_compile_features(BAAS_service_status_trigger_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_status_trigger_tests
            PRIVATE
            BAAS_service_status_trigger
            BAAS_service_trigger_executor
    )
    add_test(
            NAME BAAS_service_status_trigger_tests
            COMMAND BAAS_service_status_trigger_tests
    )
    set_tests_properties(
            BAAS_service_status_trigger_tests
            PROPERTIES TIMEOUT 45
    )
endif()
