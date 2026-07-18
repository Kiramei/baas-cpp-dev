include_guard(GLOBAL)

if(NOT TARGET BAAS_runtime_repository OR NOT TARGET BAAS_script_runtime)
    message(FATAL_ERROR
        "RuntimeScriptCatalog requires BAAS_runtime_repository and BAAS_script_runtime")
endif()

add_library(
        BAAS_runtime_script_catalog
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/script/RuntimeScriptCatalog.cpp"
)
add_library(BAAS::runtime_script_catalog ALIAS BAAS_runtime_script_catalog)
target_compile_features(BAAS_runtime_script_catalog PUBLIC cxx_std_20)
target_include_directories(
        BAAS_runtime_script_catalog
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_runtime_script_catalog
        PUBLIC BAAS_runtime_repository BAAS_script_runtime
)

if(MSVC)
    target_compile_options(
            BAAS_runtime_script_catalog
            PUBLIC /utf-8
            PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_runtime_script_catalog
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_RUNTIME_SCRIPT_CATALOG_TESTS)
    if(NOT TARGET BAAS_resource_core)
        message(FATAL_ERROR "RuntimeScriptCatalog tests require BAAS_resource_core")
    endif()
    include(CTest)
    add_executable(
            BAAS_runtime_script_catalog_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/RuntimeScriptCatalogTests.cpp"
    )
    target_compile_features(BAAS_runtime_script_catalog_tests PRIVATE cxx_std_20)
    target_compile_definitions(
            BAAS_runtime_script_catalog_tests
            PRIVATE BAAS_RUNTIME_REPOSITORY_TESTING
    )
    target_link_libraries(
            BAAS_runtime_script_catalog_tests
            PRIVATE BAAS_runtime_script_catalog BAAS_resource_core
    )
    if(MSVC)
        target_compile_options(
                BAAS_runtime_script_catalog_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_runtime_script_catalog_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_runtime_script_catalog_tests
            COMMAND BAAS_runtime_script_catalog_tests
    )
    set_tests_properties(BAAS_runtime_script_catalog_tests PROPERTIES TIMEOUT 60)
endif()
