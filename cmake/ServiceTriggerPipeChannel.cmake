include_guard(GLOBAL)

add_library(
        BAAS_service_trigger_pipe_channel
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/pipe/TriggerPipeChannel.cpp"
)
target_compile_features(BAAS_service_trigger_pipe_channel PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_trigger_pipe_channel
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_trigger_pipe_channel
        PUBLIC BAAS_service_pipe_host BAAS_service_trigger_executor
)

if(MSVC)
    target_compile_options(BAAS_service_trigger_pipe_channel PUBLIC /utf-8)
    target_compile_options(BAAS_service_trigger_pipe_channel PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_trigger_pipe_channel PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_TRIGGER_PIPE_CHANNEL_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_trigger_pipe_channel_tests
            "${BAAS_PROJECT_PATH}/tests/service/TriggerPipeChannelTests.cpp"
    )
    target_compile_features(BAAS_service_trigger_pipe_channel_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_trigger_pipe_channel_tests
            PRIVATE BAAS_service_trigger_pipe_channel
    )
    add_test(
            NAME BAAS_service_trigger_pipe_channel_tests
            COMMAND BAAS_service_trigger_pipe_channel_tests
    )
    set_tests_properties(
            BAAS_service_trigger_pipe_channel_tests
            PROPERTIES TIMEOUT 30
    )
endif()
