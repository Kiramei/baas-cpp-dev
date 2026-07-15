include_guard(GLOBAL)

add_library(
        BAAS_service_trigger_catalog
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/trigger/TriggerCommandCatalog.cpp"
)
target_compile_features(BAAS_service_trigger_catalog PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_trigger_catalog
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)

if(MSVC)
    target_compile_options(BAAS_service_trigger_catalog PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_trigger_catalog PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_TRIGGER_CATALOG_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_trigger_catalog_tests
            "${BAAS_PROJECT_PATH}/tests/service/TriggerCommandCatalogTests.cpp"
    )
    target_compile_features(BAAS_service_trigger_catalog_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_trigger_catalog_tests
            PRIVATE
            BAAS_service_trigger_catalog
    )
    add_test(
            NAME BAAS_service_trigger_catalog_tests
            COMMAND BAAS_service_trigger_catalog_tests
    )
    set_tests_properties(
            BAAS_service_trigger_catalog_tests
            PROPERTIES TIMEOUT 30
    )
endif()
