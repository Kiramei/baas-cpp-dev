include_guard(GLOBAL)

find_package(Threads REQUIRED)

add_library(
        BAAS_service_file_resource_store
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/adapters/FileResourceStore.cpp"
)
target_compile_features(BAAS_service_file_resource_store PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_file_resource_store
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_file_resource_store
        PUBLIC
        BAAS::nlohmann_json
        Threads::Threads
)

if(MSVC)
    target_compile_options(
            BAAS_service_file_resource_store PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_service_file_resource_store PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_SERVICE_FILE_RESOURCE_STORE_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_file_resource_store_tests
            "${BAAS_PROJECT_PATH}/tests/service/FileResourceStoreTests.cpp"
    )
    target_compile_features(
            BAAS_service_file_resource_store_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_file_resource_store_tests
            PRIVATE BAAS_service_file_resource_store
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_file_resource_store_tests PRIVATE /EHsc
        )
    endif()
    add_test(
            NAME BAAS_service_file_resource_store_tests
            COMMAND BAAS_service_file_resource_store_tests
    )
    set_tests_properties(
            BAAS_service_file_resource_store_tests
            PROPERTIES TIMEOUT 90
    )
endif()
