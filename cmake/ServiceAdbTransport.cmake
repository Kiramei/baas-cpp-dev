include_guard(GLOBAL)

find_package(Threads REQUIRED)

add_library(
        BAAS_service_adb_transport
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/adb/ServiceAdbTransport.cpp"
)
target_compile_features(BAAS_service_adb_transport PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_adb_transport
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(BAAS_service_adb_transport PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(BAAS_service_adb_transport PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(BAAS_service_adb_transport PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(BAAS_service_adb_transport PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_ADB_TRANSPORT_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_adb_transport_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceAdbTransportTests.cpp"
    )
    target_compile_features(BAAS_service_adb_transport_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_adb_transport_tests
            PRIVATE BAAS_service_adb_transport Threads::Threads
    )
    if(MSVC)
        target_compile_options(BAAS_service_adb_transport_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(BAAS_service_adb_transport_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(
            NAME BAAS_service_adb_transport_tests
            COMMAND BAAS_service_adb_transport_tests
    )
    set_tests_properties(BAAS_service_adb_transport_tests PROPERTIES TIMEOUT 30)
endif()
