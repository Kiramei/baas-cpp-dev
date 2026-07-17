include_guard(GLOBAL)

add_library(
        BAAS_scheduler_policy
        STATIC
        "${BAAS_PROJECT_PATH}/src/scheduler/SchedulerPolicy.cpp"
        "${BAAS_PROJECT_PATH}/include/scheduler/SchedulerPolicy.h"
)
add_library(BAAS::scheduler_policy ALIAS BAAS_scheduler_policy)
target_compile_features(BAAS_scheduler_policy PUBLIC cxx_std_20)
target_include_directories(
        BAAS_scheduler_policy
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_scheduler_policy
        PRIVATE BAAS::nlohmann_json
)

if(MSVC)
    target_compile_options(
            BAAS_scheduler_policy
            PRIVATE /W4 /permissive- /EHsc /utf-8
    )
else()
    target_compile_options(
            BAAS_scheduler_policy
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SCHEDULER_POLICY_TESTS)
    include(CTest)
    add_executable(
            BAAS_scheduler_policy_tests
            "${BAAS_PROJECT_PATH}/tests/scheduler/SchedulerPolicyTests.cpp"
    )
    target_compile_features(BAAS_scheduler_policy_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_scheduler_policy_tests
            PRIVATE BAAS_scheduler_policy
    )
    if(MSVC)
        target_compile_options(
                BAAS_scheduler_policy_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_scheduler_policy_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_scheduler_policy_tests
            COMMAND BAAS_scheduler_policy_tests
    )
    set_tests_properties(
            BAAS_scheduler_policy_tests
            PROPERTIES TIMEOUT 30
    )
endif()
