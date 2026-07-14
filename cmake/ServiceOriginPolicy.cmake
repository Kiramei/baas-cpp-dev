include_guard(GLOBAL)

add_library(
        BAAS_service_origin_policy
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/http/OriginPolicy.cpp"
)
target_compile_features(BAAS_service_origin_policy PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_origin_policy
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)

if(MSVC)
    target_compile_options(BAAS_service_origin_policy PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_origin_policy PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_ORIGIN_POLICY_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_origin_policy_tests
            "${BAAS_PROJECT_PATH}/tests/service/OriginPolicyTests.cpp"
    )
    target_compile_features(BAAS_service_origin_policy_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_origin_policy_tests
            PRIVATE
            BAAS_service_origin_policy
    )
    add_test(
            NAME BAAS_service_origin_policy_tests
            COMMAND BAAS_service_origin_policy_tests
    )
    set_tests_properties(
            BAAS_service_origin_policy_tests
            PROPERTIES TIMEOUT 15
    )
endif()
