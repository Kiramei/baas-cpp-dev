include_guard(GLOBAL)

add_library(
        BAAS_service_auth_http
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/http/AuthHttpAdapter.cpp"
)
target_compile_features(BAAS_service_auth_http PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_auth_http
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_auth_http
        PUBLIC
        BAAS_service_auth_owner
        BAAS_service_router
)

if(MSVC)
    target_compile_options(BAAS_service_auth_http PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_auth_http PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_AUTH_HTTP_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_auth_http_tests
            "${BAAS_PROJECT_PATH}/tests/service/AuthHttpAdapterTests.cpp"
    )
    target_compile_features(BAAS_service_auth_http_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_auth_http_tests
            PRIVATE BAAS_service_auth_http
    )
    add_test(NAME BAAS_service_auth_http_tests COMMAND BAAS_service_auth_http_tests)
    set_tests_properties(BAAS_service_auth_http_tests PROPERTIES TIMEOUT 60)
endif()
