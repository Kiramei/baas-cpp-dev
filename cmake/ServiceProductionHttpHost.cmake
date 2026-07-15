include_guard(GLOBAL)

add_library(
        BAAS_service_production_http_host
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/http/ProductionHttpHost.cpp"
)
target_compile_features(BAAS_service_production_http_host PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_production_http_host
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_production_http_host
        PUBLIC
        BAAS_service_auth_http
        BAAS_service_provider_handler
        BAAS_service_sync_handler
        BAAS_service_http
)

if(MSVC)
    target_compile_options(BAAS_service_production_http_host PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_production_http_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_PRODUCTION_HTTP_HOST_TESTS)
    include(CTest)
    add_library(
            BAAS_service_production_http_host_android_policy_contract
            OBJECT
            "${BAAS_PROJECT_PATH}/tests/service/ProductionHttpHostAndroidPolicyCompile.cpp"
    )
    target_compile_features(
            BAAS_service_production_http_host_android_policy_contract
            PRIVATE cxx_std_20
    )
    target_compile_definitions(
            BAAS_service_production_http_host_android_policy_contract
            PRIVATE __ANDROID__=1
    )
    target_link_libraries(
            BAAS_service_production_http_host_android_policy_contract
            PRIVATE BAAS_service_production_http_host
    )
    add_executable(
            BAAS_service_production_http_host_tests
            "${BAAS_PROJECT_PATH}/tests/service/ProductionHttpHostTests.cpp"
    )
    target_compile_features(BAAS_service_production_http_host_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_production_http_host_tests
            PRIVATE BAAS_service_production_http_host
    )
    add_dependencies(
            BAAS_service_production_http_host_tests
            BAAS_service_production_http_host_android_policy_contract
    )
    add_test(
            NAME BAAS_service_production_http_host_tests
            COMMAND BAAS_service_production_http_host_tests
    )
    set_tests_properties(
            BAAS_service_production_http_host_tests
            PROPERTIES TIMEOUT 60
    )
endif()
