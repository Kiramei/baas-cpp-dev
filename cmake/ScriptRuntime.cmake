include_guard(GLOBAL)

add_library(
        BAAS_script_runtime
        STATIC
        "${BAAS_PROJECT_PATH}/src/script/Lexer.cpp"
        "${BAAS_PROJECT_PATH}/src/script/Token.cpp"
)

target_compile_features(BAAS_script_runtime PUBLIC cxx_std_20)
target_include_directories(
        BAAS_script_runtime
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)

if(MSVC)
    # Script source is UTF-8 by definition. Propagate the input character set to
    # consumers so UTF-8 test fixtures and embedders behave consistently on MSVC.
    target_compile_options(BAAS_script_runtime PUBLIC /utf-8 PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_script_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SCRIPT_TESTS)
    include(CTest)
    add_executable(
            BAAS_script_runtime_tests
            "${BAAS_PROJECT_PATH}/tests/script/LexerTests.cpp"
    )
    target_compile_features(BAAS_script_runtime_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_runtime_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_runtime_tests COMMAND BAAS_script_runtime_tests)
endif()
