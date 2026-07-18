include_guard(GLOBAL)

foreach(required_target BAAS_runtime_repository BAAS_resource_core BAAS::nlohmann_json)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR
            "RuntimeResourceSnapshotLoader requires ${required_target}")
    endif()
endforeach()

add_library(
        BAAS_runtime_resource_snapshot_loader
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/resources/RuntimeResourceSnapshotLoader.cpp"
)
add_library(
        BAAS::runtime_resource_snapshot_loader
        ALIAS BAAS_runtime_resource_snapshot_loader
)
target_compile_features(BAAS_runtime_resource_snapshot_loader PUBLIC cxx_std_20)
target_include_directories(
        BAAS_runtime_resource_snapshot_loader
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_runtime_resource_snapshot_loader
        PUBLIC BAAS_runtime_repository BAAS_resource_core
        PRIVATE BAAS::nlohmann_json
)
if(MSVC)
    target_compile_options(
            BAAS_runtime_resource_snapshot_loader
            PUBLIC /utf-8 PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_runtime_resource_snapshot_loader
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_RUNTIME_RESOURCE_SNAPSHOT_LOADER_TESTS)
    include(CTest)
    target_compile_definitions(
            BAAS_runtime_resource_snapshot_loader
            PRIVATE BAAS_RUNTIME_RESOURCE_SNAPSHOT_LOADER_TESTING
    )
    add_executable(
            BAAS_runtime_resource_snapshot_loader_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/RuntimeResourceSnapshotLoaderTests.cpp"
    )
    target_compile_features(
            BAAS_runtime_resource_snapshot_loader_tests PRIVATE cxx_std_20
    )
    target_compile_definitions(
            BAAS_runtime_resource_snapshot_loader_tests
            PRIVATE BAAS_RUNTIME_RESOURCE_SNAPSHOT_LOADER_TESTING
                    BAAS_RUNTIME_REPOSITORY_TESTING
    )
    target_link_libraries(
            BAAS_runtime_resource_snapshot_loader_tests
            PRIVATE BAAS_runtime_resource_snapshot_loader
    )
    if(MSVC)
        target_compile_options(
                BAAS_runtime_resource_snapshot_loader_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_runtime_resource_snapshot_loader_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_runtime_resource_snapshot_loader_tests
            COMMAND BAAS_runtime_resource_snapshot_loader_tests
    )
    set_tests_properties(
            BAAS_runtime_resource_snapshot_loader_tests PROPERTIES TIMEOUT 60
    )
endif()
