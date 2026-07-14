include_guard(GLOBAL)

add_library(
        BAAS_service_protocol
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/protocol/PipeFraming.cpp"
)
target_compile_features(BAAS_service_protocol PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_protocol
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)

if(MSVC)
    target_compile_options(BAAS_service_protocol PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_protocol PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_PROTOCOL_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_pipe_framing_tests
            "${BAAS_PROJECT_PATH}/tests/service/PipeFramingTests.cpp"
    )
    target_compile_features(BAAS_service_pipe_framing_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_service_pipe_framing_tests PRIVATE BAAS_service_protocol)
    add_test(NAME BAAS_service_pipe_framing_tests COMMAND BAAS_service_pipe_framing_tests)
endif()
