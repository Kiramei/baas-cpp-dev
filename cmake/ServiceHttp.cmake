include_guard(GLOBAL)

add_library(
        BAAS_service_http
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/http/HttplibAdapter.cpp"
)
target_compile_features(BAAS_service_http PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_http
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_http
        PUBLIC
        BAAS_service_router
        BAAS::httplib
)

if(MSVC)
    target_compile_options(BAAS_service_http PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_http PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_HTTP_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_httplib_adapter_tests
            "${BAAS_PROJECT_PATH}/tests/service/HttplibAdapterTests.cpp"
    )
    target_compile_features(BAAS_service_httplib_adapter_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_service_httplib_adapter_tests PRIVATE BAAS_service_http)
    add_test(
            NAME BAAS_service_httplib_adapter_tests
            COMMAND BAAS_service_httplib_adapter_tests
    )
    set_tests_properties(
            BAAS_service_httplib_adapter_tests
            PROPERTIES TIMEOUT 15
    )
endif()
