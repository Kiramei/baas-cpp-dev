include_guard(GLOBAL)

add_library(
        BAAS_service_provider_backend
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/ProductionProviderBackend.cpp"
)
target_compile_features(BAAS_service_provider_backend PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_provider_backend
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_provider_backend
        PUBLIC
        BAAS_service_provider_handler
        BAAS_service_auth_crypto
)

if(MSVC)
    target_compile_options(BAAS_service_provider_backend PUBLIC /utf-8)
    target_compile_options(BAAS_service_provider_backend PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_provider_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_PROVIDER_BACKEND_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_provider_backend_tests
            "${BAAS_PROJECT_PATH}/tests/service/ProductionProviderBackendTests.cpp"
    )
    target_compile_features(BAAS_service_provider_backend_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_provider_backend_tests
            PRIVATE BAAS_service_provider_backend
    )
    add_test(
            NAME BAAS_service_provider_backend_tests
            COMMAND BAAS_service_provider_backend_tests
    )
    set_tests_properties(
            BAAS_service_provider_backend_tests
            PROPERTIES TIMEOUT 60
    )
endif()
