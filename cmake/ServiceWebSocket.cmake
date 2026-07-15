include_guard(GLOBAL)

find_package(Threads REQUIRED)

add_library(
        BAAS_service_websocket
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/websocket/WebSocketHandshake.cpp"
        "${BAAS_PROJECT_PATH}/src/service/websocket/WebSocketOwner.cpp"
)
target_compile_features(BAAS_service_websocket PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_websocket
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_websocket
        PUBLIC
        BAAS_service_origin_policy
        BAAS::httplib
        Threads::Threads
)

if(MSVC)
    target_compile_options(BAAS_service_websocket PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_websocket PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_WEBSOCKET_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_websocket_handshake_tests
            "${BAAS_PROJECT_PATH}/tests/service/WebSocketHandshakeTests.cpp"
    )
    target_compile_features(BAAS_service_websocket_handshake_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_websocket_handshake_tests
            PRIVATE BAAS_service_websocket
    )
    add_test(
            NAME BAAS_service_websocket_handshake_tests
            COMMAND BAAS_service_websocket_handshake_tests
    )
    set_tests_properties(
            BAAS_service_websocket_handshake_tests
            PROPERTIES TIMEOUT 15
    )

    add_executable(
            BAAS_service_websocket_owner_tests
            "${BAAS_PROJECT_PATH}/tests/service/WebSocketOwnerTests.cpp"
    )
    target_compile_features(BAAS_service_websocket_owner_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_websocket_owner_tests
            PRIVATE BAAS_service_websocket
    )
    add_test(
            NAME BAAS_service_websocket_owner_tests
            COMMAND BAAS_service_websocket_owner_tests
    )
    set_tests_properties(
            BAAS_service_websocket_owner_tests
            PROPERTIES TIMEOUT 30
    )
endif()
