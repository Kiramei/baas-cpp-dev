include_guard(GLOBAL)

if(NOT TARGET BAAS_runtime_repository)
    message(FATAL_ERROR "BUILD_RUNTIME_REPOSITORY_UPDATER requires BUILD_RUNTIME_REPOSITORY")
endif()

add_library(
        BAAS_runtime_repository_updater
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/repository/RuntimeRepositoryUpdater.cpp"
)
target_compile_features(BAAS_runtime_repository_updater PUBLIC cxx_std_20)
target_include_directories(
        BAAS_runtime_repository_updater
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_runtime_repository_updater
        PUBLIC BAAS_runtime_repository
)
if(MSVC)
    target_compile_options(
            BAAS_runtime_repository_updater
            PUBLIC /utf-8
            PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_runtime_repository_updater
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_RUNTIME_REPOSITORY_UPDATER_TESTS)
    include(CTest)
    target_compile_definitions(
            BAAS_runtime_repository_updater
            PRIVATE BAAS_RUNTIME_REPOSITORY_UPDATER_TESTING=1
    )
    add_executable(
            BAAS_runtime_repository_updater_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/RuntimeRepositoryUpdaterTests.cpp"
    )
    target_compile_features(BAAS_runtime_repository_updater_tests PRIVATE cxx_std_20)
    target_compile_definitions(
            BAAS_runtime_repository_updater_tests
            PRIVATE BAAS_RUNTIME_REPOSITORY_UPDATER_TESTING=1
    )
    target_link_libraries(
            BAAS_runtime_repository_updater_tests
            PRIVATE BAAS_runtime_repository_updater
    )
    add_test(
            NAME BAAS_runtime_repository_updater_tests
            COMMAND BAAS_runtime_repository_updater_tests
    )
    set_tests_properties(
            BAAS_runtime_repository_updater_tests
            PROPERTIES TIMEOUT 120
    )
endif()
