include_guard(GLOBAL)

foreach(required_target
        BAAS_runtime_repository_updater
        BAAS_service_auth_crypto
        BAAS::nlohmann_json)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR
            "BAAS_service_runtime_repository_plan requires ${required_target}")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/ServiceRuntimeRepositoryTrustedPlanState.cmake")

add_library(
        BAAS_service_runtime_repository_plan
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/RuntimeRepositoryTrustedPlan.cpp"
        "${BAAS_PROJECT_PATH}/src/service/app/RuntimeRepositoryTrustedPlanUpdateOwner.cpp"
        "${BAAS_PROJECT_PATH}/include/service/app/RuntimeRepositoryTrustedPlan.h"
        "${BAAS_PROJECT_PATH}/include/service/app/RuntimeRepositoryTrustedPlanState.h"
        "${BAAS_PROJECT_PATH}/include/service/app/RuntimeRepositoryTrustedPlanUpdateOwner.h"
        "${BAAS_PROJECT_PATH}/include/service/adapters/BoundedJson.h"
)
target_compile_features(BAAS_service_runtime_repository_plan PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_runtime_repository_plan
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_runtime_repository_plan
        PUBLIC BAAS_runtime_repository_updater
        PRIVATE
        BAAS_service_auth_crypto
        BAAS_service_runtime_repository_trusted_plan_state
        BAAS::nlohmann_json
)

if(MSVC)
    target_compile_options(
            BAAS_service_runtime_repository_plan
            PRIVATE /W4 /WX /permissive- /EHsc /utf-8
    )
else()
    target_compile_options(
            BAAS_service_runtime_repository_plan
            PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(BUILD_SERVICE_RUNTIME_REPOSITORY_PLAN_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_runtime_repository_plan_tests
            "${BAAS_PROJECT_PATH}/tests/service/RuntimeRepositoryTrustedPlanTests.cpp"
    )
    target_compile_features(
            BAAS_service_runtime_repository_plan_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_runtime_repository_plan_tests
            PRIVATE
            BAAS_service_runtime_repository_plan
            BAAS_service_auth_crypto
            BAAS::nlohmann_json
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_runtime_repository_plan_tests
                PRIVATE /W4 /WX /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_runtime_repository_plan_tests
                PRIVATE -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(
            NAME BAAS_service_runtime_repository_plan_tests
            COMMAND BAAS_service_runtime_repository_plan_tests
    )
    set_tests_properties(
            BAAS_service_runtime_repository_plan_tests PROPERTIES TIMEOUT 30
    )
endif()
