include_guard(GLOBAL)

foreach(required_target BAAS_runtime_repository BAAS::nlohmann_json)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR
            "Service runtime repository owner requires ${required_target}")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/ServiceRuntimeRepositoryTrustedPlanState.cmake")

add_library(
        BAAS_service_runtime_repository_owner
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/ServiceRuntimeRepositoryOwner.cpp"
)
target_compile_features(BAAS_service_runtime_repository_owner PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_runtime_repository_owner
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_runtime_repository_owner
        PUBLIC BAAS_runtime_repository
        PRIVATE BAAS_service_runtime_repository_trusted_plan_state
)

if(MSVC)
    target_compile_options(
            BAAS_service_runtime_repository_owner
            PUBLIC /utf-8
            PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_service_runtime_repository_owner PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SERVICE_RUNTIME_REPOSITORY_OWNER_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_runtime_repository_owner_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceRuntimeRepositoryOwnerTests.cpp"
    )
    target_compile_features(
            BAAS_service_runtime_repository_owner_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_runtime_repository_owner_tests
            PRIVATE BAAS_service_runtime_repository_owner
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_runtime_repository_owner_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_runtime_repository_owner_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_service_runtime_repository_owner_tests
            COMMAND BAAS_service_runtime_repository_owner_tests
    )
    set_tests_properties(
            BAAS_service_runtime_repository_owner_tests PROPERTIES TIMEOUT 60
    )
endif()
