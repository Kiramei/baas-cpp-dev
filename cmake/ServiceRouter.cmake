include_guard(GLOBAL)

add_library(
        BAAS_service_router
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/router/Router.cpp"
)
target_compile_features(BAAS_service_router PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_router
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)

if(MSVC)
    target_compile_options(BAAS_service_router PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_router PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_ROUTER_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_router_tests
            "${BAAS_PROJECT_PATH}/tests/service/RouterTests.cpp"
    )
    target_compile_features(BAAS_service_router_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_service_router_tests PRIVATE BAAS_service_router)
    add_test(NAME BAAS_service_router_tests COMMAND BAAS_service_router_tests)
endif()
