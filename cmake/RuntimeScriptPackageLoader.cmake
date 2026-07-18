include_guard(GLOBAL)

if(NOT TARGET BAAS_runtime_repository OR NOT TARGET BAAS_script_runtime)
    message(FATAL_ERROR
        "RuntimeScriptPackageLoader requires BAAS_runtime_repository and BAAS_script_runtime")
endif()

add_library(
        BAAS_runtime_script_package_loader
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/script/RuntimeScriptPackageLoader.cpp"
)
add_library(
        BAAS::runtime_script_package_loader
        ALIAS BAAS_runtime_script_package_loader
)
target_compile_features(BAAS_runtime_script_package_loader PUBLIC cxx_std_20)
target_include_directories(
        BAAS_runtime_script_package_loader
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_runtime_script_package_loader
        PUBLIC BAAS_runtime_repository BAAS_script_runtime
)

if(MSVC)
    target_compile_options(
            BAAS_runtime_script_package_loader
            PUBLIC /utf-8
            PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_runtime_script_package_loader
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_RUNTIME_SCRIPT_PACKAGE_LOADER_TESTS)
    if(NOT TARGET BAAS_resource_core)
        message(FATAL_ERROR
            "RuntimeScriptPackageLoader tests require BAAS_resource_core")
    endif()
    include(CTest)
    add_executable(
            BAAS_runtime_script_package_loader_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/RuntimeScriptPackageLoaderTests.cpp"
    )
    target_compile_features(
            BAAS_runtime_script_package_loader_tests PRIVATE cxx_std_20
    )
    target_compile_definitions(
            BAAS_runtime_script_package_loader_tests
            PRIVATE BAAS_RUNTIME_REPOSITORY_TESTING
    )
    target_link_libraries(
            BAAS_runtime_script_package_loader_tests
            PRIVATE BAAS_runtime_script_package_loader BAAS_resource_core
    )
    if(MSVC)
        target_compile_options(
                BAAS_runtime_script_package_loader_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_runtime_script_package_loader_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_runtime_script_package_loader_tests
            COMMAND BAAS_runtime_script_package_loader_tests
    )
    set_tests_properties(
            BAAS_runtime_script_package_loader_tests PROPERTIES TIMEOUT 60
    )
endif()
