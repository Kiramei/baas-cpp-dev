include_guard(GLOBAL)

add_library(
        BAAS_service_adb_discovery_trigger
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/AdbDiscoveryTriggerRegistration.cpp"
)
target_compile_features(BAAS_service_adb_discovery_trigger PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_adb_discovery_trigger
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_adb_discovery_trigger
        PUBLIC BAAS_service_trigger_dispatch
)

if(WIN32)
    target_link_libraries(
            BAAS_service_adb_discovery_trigger
            PRIVATE advapi32
    )
endif()

if(MSVC)
    target_compile_options(BAAS_service_adb_discovery_trigger PUBLIC /utf-8)
    target_compile_options(
            BAAS_service_adb_discovery_trigger PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_service_adb_discovery_trigger PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SERVICE_ADB_DISCOVERY_TRIGGER_TESTS)
    include(CTest)
    target_compile_definitions(
            BAAS_service_adb_discovery_trigger
            PRIVATE BAAS_ADB_DISCOVERY_TEST_HOOKS
    )
    add_executable(
            BAAS_service_adb_discovery_trigger_tests
            "${BAAS_PROJECT_PATH}/tests/service/AdbDiscoveryTriggerRegistrationTests.cpp"
    )
    target_compile_features(
            BAAS_service_adb_discovery_trigger_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_adb_discovery_trigger_tests
            PRIVATE
            BAAS_service_adb_discovery_trigger
            BAAS_service_trigger_executor
    )
    target_compile_definitions(
            BAAS_service_adb_discovery_trigger_tests
            PRIVATE BAAS_ADB_DISCOVERY_TEST_HOOKS
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_adb_discovery_trigger_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_adb_discovery_trigger_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_service_adb_discovery_trigger_tests
            COMMAND BAAS_service_adb_discovery_trigger_tests
    )
    set_tests_properties(
            BAAS_service_adb_discovery_trigger_tests
            PROPERTIES TIMEOUT 45
    )
endif()
