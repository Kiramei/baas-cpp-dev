include_guard(GLOBAL)

add_library(
        BAAS_resource_core
        STATIC
        "${BAAS_PROJECT_PATH}/src/resources/ResourceSnapshot.cpp"
)
target_compile_features(BAAS_resource_core PUBLIC cxx_std_20)
target_include_directories(
        BAAS_resource_core
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
if(MSVC)
    target_compile_options(BAAS_resource_core PUBLIC /utf-8 PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_resource_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_RESOURCE_CORE_TESTS)
    include(CTest)
    add_executable(
            BAAS_resource_snapshot_tests
            "${BAAS_PROJECT_PATH}/tests/resources/ResourceSnapshotTests.cpp"
    )
    target_compile_features(BAAS_resource_snapshot_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_resource_snapshot_tests PRIVATE BAAS_resource_core)
    add_test(NAME BAAS_resource_snapshot_tests COMMAND BAAS_resource_snapshot_tests)
    set_tests_properties(BAAS_resource_snapshot_tests PROPERTIES TIMEOUT 60)
endif()
