include_guard(GLOBAL)

add_library(
        BAAS_service_remote_backend
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/ProductionRemoteBackend.cpp"
)
target_compile_features(BAAS_service_remote_backend PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_remote_backend
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_remote_backend
        PUBLIC
            BAAS_service_remote_handler
            BAAS_service_adb_sync
        PRIVATE
            BAAS::httplib
            BAAS::nlohmann_json
            BAAS::sodium
)

if(MSVC)
    target_compile_options(BAAS_service_remote_backend PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(BAAS_service_remote_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_REMOTE_BACKEND_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_remote_backend_tests
            "${BAAS_PROJECT_PATH}/tests/service/ProductionRemoteBackendTests.cpp"
    )
    target_compile_features(BAAS_service_remote_backend_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_remote_backend_tests
            PRIVATE BAAS_service_remote_backend
    )
    if(MSVC)
        target_compile_options(BAAS_service_remote_backend_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(BAAS_service_remote_backend_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(
            NAME BAAS_service_remote_backend_tests
            COMMAND BAAS_service_remote_backend_tests
    )
    set_tests_properties(BAAS_service_remote_backend_tests PROPERTIES TIMEOUT 60)
endif()

if(BUILD_SERVICE_REMOTE_BACKEND_LIVE_SMOKE)
    add_executable(
            BAAS_service_remote_backend_live_smoke
            "${BAAS_PROJECT_PATH}/tests/service/ProductionRemoteBackendLiveSmoke.cpp"
    )
    target_compile_features(BAAS_service_remote_backend_live_smoke PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_remote_backend_live_smoke
            PRIVATE
                BAAS_service_remote_backend
                BAAS_service_file_resource_store
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_remote_backend_live_smoke
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_remote_backend_live_smoke
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
endif()
