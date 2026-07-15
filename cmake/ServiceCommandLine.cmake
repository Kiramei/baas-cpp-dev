include_guard(GLOBAL)

add_library(
        BAAS_service_command_line
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/ServiceCommandLine.cpp"
)
target_compile_features(BAAS_service_command_line PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_command_line
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)

if(MSVC)
    target_compile_options(BAAS_service_command_line PUBLIC /utf-8)
    target_compile_options(BAAS_service_command_line PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_command_line PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_COMMAND_LINE_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_command_line_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceCommandLineTests.cpp"
    )
    target_compile_features(BAAS_service_command_line_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_service_command_line_tests
            PRIVATE BAAS_service_command_line
    )
    add_test(
            NAME BAAS_service_command_line_tests
            COMMAND BAAS_service_command_line_tests
    )
    set_tests_properties(BAAS_service_command_line_tests PROPERTIES TIMEOUT 30)
endif()
