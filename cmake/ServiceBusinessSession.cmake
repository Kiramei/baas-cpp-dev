include_guard(GLOBAL)

add_library(
        BAAS_service_business_session
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/websocket/BusinessSessionFactory.cpp"
)
target_compile_features(BAAS_service_business_session PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_business_session
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_business_session
        PUBLIC BAAS_service_control_session
)

if(MSVC)
    target_compile_options(BAAS_service_business_session PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_business_session PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_BUSINESS_SESSION_TESTS)
    include(CTest)
    target_compile_definitions(
            BAAS_service_business_session
            PRIVATE BAAS_BUSINESS_SESSION_TEST_HOOKS=1
    )
    add_executable(
            BAAS_service_business_session_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceBusinessSessionTests.cpp"
    )
    target_compile_features(BAAS_service_business_session_tests PRIVATE cxx_std_20)
    target_compile_definitions(
            BAAS_service_business_session_tests
            PRIVATE BAAS_BUSINESS_SESSION_TEST_HOOKS=1
    )
    target_link_libraries(
            BAAS_service_business_session_tests
            PRIVATE BAAS_service_business_session
    )
    add_test(
            NAME BAAS_service_business_session_tests
            COMMAND BAAS_service_business_session_tests
    )
    set_tests_properties(
            BAAS_service_business_session_tests
            PROPERTIES TIMEOUT 60
    )
endif()
