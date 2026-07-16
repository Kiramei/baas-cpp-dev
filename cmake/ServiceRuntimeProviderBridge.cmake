include_guard(GLOBAL)

find_package(Threads REQUIRED)

add_library(
        BAAS_service_runtime_provider_bridge
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/adapters/FileResourceWatcher.cpp"
        "${BAAS_PROJECT_PATH}/src/service/app/ServiceRuntimeProviderBridge.cpp"
)
target_compile_features(BAAS_service_runtime_provider_bridge PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_runtime_provider_bridge
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_runtime_provider_bridge
        PUBLIC
        BAAS_service_file_resource_store
        BAAS_service_provider_backend
        BAAS::nlohmann_json
        Threads::Threads
)

if(MSVC)
    target_compile_options(
            BAAS_service_runtime_provider_bridge
            PUBLIC /utf-8
            PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_service_runtime_provider_bridge PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_runtime_provider_bridge_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceRuntimeProviderBridgeTests.cpp"
    )
    target_compile_features(
            BAAS_service_runtime_provider_bridge_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_runtime_provider_bridge_tests
            PRIVATE BAAS_service_runtime_provider_bridge
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_runtime_provider_bridge_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_runtime_provider_bridge_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_service_runtime_provider_bridge_tests
            COMMAND BAAS_service_runtime_provider_bridge_tests
    )
    set_tests_properties(
            BAAS_service_runtime_provider_bridge_tests
            PROPERTIES TIMEOUT 90
    )
endif()
