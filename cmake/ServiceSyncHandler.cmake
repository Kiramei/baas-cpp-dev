include_guard(GLOBAL)

add_library(
        BAAS_service_sync_handler
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/channels/SyncHandler.cpp"
)
target_compile_features(BAAS_service_sync_handler PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_sync_handler
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_sync_handler
        PUBLIC BAAS_service_business_session BAAS::nlohmann_json
)

if(MSVC)
    target_compile_options(BAAS_service_sync_handler PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_sync_handler PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_SYNC_HANDLER_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_sync_handler_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceSyncHandlerTests.cpp"
    )
    target_compile_features(BAAS_service_sync_handler_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_sync_handler_tests
            PRIVATE BAAS_service_sync_handler
    )
    add_test(
            NAME BAAS_service_sync_handler_tests
            COMMAND BAAS_service_sync_handler_tests
    )
    set_tests_properties(
            BAAS_service_sync_handler_tests
            PROPERTIES TIMEOUT 60
    )
endif()
