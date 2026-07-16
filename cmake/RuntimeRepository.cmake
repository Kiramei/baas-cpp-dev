include_guard(GLOBAL)

add_library(
        BAAS_runtime_repository
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/repository/RuntimeRepositorySnapshot.cpp"
        "${BAAS_PROJECT_PATH}/src/runtime/repository/RuntimeRepositoryReadView.cpp"
        "${BAAS_PROJECT_PATH}/src/runtime/repository/RuntimeRepositoryTreeFormat.cpp"
)
target_compile_features(BAAS_runtime_repository PUBLIC cxx_std_20)
target_include_directories(
        BAAS_runtime_repository
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
if(MSVC)
    target_compile_options(BAAS_runtime_repository PUBLIC /utf-8 PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_runtime_repository PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_RUNTIME_REPOSITORY_TESTS)
    include(CTest)
    target_compile_definitions(
            BAAS_runtime_repository
            PRIVATE BAAS_RUNTIME_REPOSITORY_TESTING
    )
    add_executable(
            BAAS_runtime_repository_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/RuntimeRepositorySnapshotTests.cpp"
    )
    target_compile_features(BAAS_runtime_repository_tests PRIVATE cxx_std_20)
    target_compile_definitions(
            BAAS_runtime_repository_tests
            PRIVATE BAAS_RUNTIME_REPOSITORY_TESTING
    )
    target_link_libraries(BAAS_runtime_repository_tests PRIVATE BAAS_runtime_repository)
    add_test(NAME BAAS_runtime_repository_tests COMMAND BAAS_runtime_repository_tests)
    set_tests_properties(BAAS_runtime_repository_tests PROPERTIES TIMEOUT 60)
endif()
