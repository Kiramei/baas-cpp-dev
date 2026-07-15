include_guard(GLOBAL)

find_package(Threads REQUIRED)

add_library(
        BAAS_service_pipe_host
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/pipe/PipeHost.cpp"
        "${BAAS_PROJECT_PATH}/src/service/pipe/NativePipeListener.cpp"
)
target_compile_features(BAAS_service_pipe_host PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_pipe_host
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_pipe_host
        PUBLIC BAAS_service_protocol Threads::Threads
)

if(WIN32)
    target_link_libraries(BAAS_service_pipe_host PRIVATE Advapi32)
endif()

if(MSVC)
    target_compile_options(BAAS_service_pipe_host PUBLIC /utf-8)
    target_compile_options(BAAS_service_pipe_host PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_pipe_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_PIPE_HOST_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_pipe_host_tests
            "${BAAS_PROJECT_PATH}/tests/service/PipeHostTests.cpp"
    )
    target_compile_features(BAAS_service_pipe_host_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_pipe_host_tests
            PRIVATE BAAS_service_pipe_host
    )
    add_test(NAME BAAS_service_pipe_host_tests COMMAND BAAS_service_pipe_host_tests)
    set_tests_properties(BAAS_service_pipe_host_tests PROPERTIES TIMEOUT 30)
endif()
