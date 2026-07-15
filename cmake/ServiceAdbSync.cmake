include_guard(GLOBAL)

add_library(
        BAAS_service_adb_sync
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/adb/ServiceAdbSync.cpp"
)
target_compile_features(BAAS_service_adb_sync PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_adb_sync
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(BAAS_service_adb_sync PUBLIC BAAS_service_adb_transport)

if(MSVC)
    target_compile_options(BAAS_service_adb_sync PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(BAAS_service_adb_sync PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_ADB_SYNC_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_adb_sync_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceAdbSyncTests.cpp"
    )
    target_compile_features(BAAS_service_adb_sync_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_service_adb_sync_tests PRIVATE BAAS_service_adb_sync)
    if(MSVC)
        target_compile_options(BAAS_service_adb_sync_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(BAAS_service_adb_sync_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME BAAS_service_adb_sync_tests COMMAND BAAS_service_adb_sync_tests)
    set_tests_properties(BAAS_service_adb_sync_tests PROPERTIES TIMEOUT 30)
endif()
