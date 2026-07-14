include_guard(GLOBAL)

find_package(Threads REQUIRED)

add_library(
        BAAS_script_runtime
        STATIC
        "${BAAS_PROJECT_PATH}/src/script/Lexer.cpp"
        "${BAAS_PROJECT_PATH}/src/script/Parser.cpp"
        "${BAAS_PROJECT_PATH}/src/script/SemanticAnalyzer.cpp"
        "${BAAS_PROJECT_PATH}/src/script/SyntaxCheck.cpp"
        "${BAAS_PROJECT_PATH}/src/script/Token.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/BoundedExecutor.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/JsonBridge.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/ValueHeap.cpp"
)

target_compile_features(BAAS_script_runtime PUBLIC cxx_std_20)
target_include_directories(
        BAAS_script_runtime
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(BAAS_script_runtime PUBLIC Threads::Threads)

if(MSVC)
    # Script source is UTF-8 by definition. Propagate the input character set to
    # consumers so UTF-8 test fixtures and embedders behave consistently on MSVC.
    target_compile_options(BAAS_script_runtime PUBLIC /utf-8 PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_script_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SCRIPT_TOOLS)
    add_executable(
            BAAS_script_check
            "${BAAS_PROJECT_PATH}/apps/script_check/main.cpp"
    )
    target_compile_features(BAAS_script_check PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_check PRIVATE BAAS_script_runtime)
    if(MSVC)
        target_compile_options(BAAS_script_check PRIVATE /W4 /permissive-)
    else()
        target_compile_options(BAAS_script_check PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(BUILD_SCRIPT_TESTS)
    include(CTest)
    add_executable(
            BAAS_script_lexer_tests
            "${BAAS_PROJECT_PATH}/tests/script/LexerTests.cpp"
    )
    target_compile_features(BAAS_script_lexer_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_lexer_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_lexer_tests COMMAND BAAS_script_lexer_tests)

    add_executable(
            BAAS_script_parser_tests
            "${BAAS_PROJECT_PATH}/tests/script/ParserTests.cpp"
    )
    target_compile_features(BAAS_script_parser_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_parser_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_parser_tests COMMAND BAAS_script_parser_tests)

    add_executable(
            BAAS_script_semantic_tests
            "${BAAS_PROJECT_PATH}/tests/script/SemanticAnalyzerTests.cpp"
    )
    target_compile_features(BAAS_script_semantic_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_semantic_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_semantic_tests COMMAND BAAS_script_semantic_tests)

    add_executable(
            BAAS_script_executor_tests
            "${BAAS_PROJECT_PATH}/tests/script/BoundedExecutorTests.cpp"
    )
    target_compile_features(BAAS_script_executor_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_executor_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_executor_tests COMMAND BAAS_script_executor_tests)
    set_tests_properties(BAAS_script_executor_tests PROPERTIES TIMEOUT 60)

    add_executable(
            BAAS_script_value_heap_tests
            "${BAAS_PROJECT_PATH}/tests/script/ValueHeapTests.cpp"
    )
    target_compile_features(BAAS_script_value_heap_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_value_heap_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_value_heap_tests COMMAND BAAS_script_value_heap_tests)
    set_tests_properties(BAAS_script_value_heap_tests PROPERTIES TIMEOUT 60)

    add_executable(
            BAAS_script_syntax_check_tests
            "${BAAS_PROJECT_PATH}/tests/script/SyntaxCheckTests.cpp"
    )
    target_compile_features(BAAS_script_syntax_check_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_syntax_check_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_syntax_check_tests COMMAND BAAS_script_syntax_check_tests)

    if(BUILD_SCRIPT_TOOLS)
        add_test(
                NAME BAAS_script_check_valid_cli
                COMMAND BAAS_script_check --json "${BAAS_PROJECT_PATH}/tests/script/fixtures/valid.baas"
        )
        add_test(
                NAME BAAS_script_check_invalid_cli
                COMMAND BAAS_script_check --json "${BAAS_PROJECT_PATH}/tests/script/fixtures/invalid.baas"
        )
        set_tests_properties(BAAS_script_check_invalid_cli PROPERTIES WILL_FAIL TRUE)
        add_test(
                NAME BAAS_script_check_control_modules_valid_cli
                COMMAND BAAS_script_check --json
                        "${BAAS_PROJECT_PATH}/tests/script/fixtures/control_modules_valid.baas"
        )
    endif()

    add_executable(
            BAAS_script_json_bridge_tests
            "${BAAS_PROJECT_PATH}/tests/script/JsonBridgeTests.cpp"
    )
    target_compile_features(BAAS_script_json_bridge_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_json_bridge_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_json_bridge_tests COMMAND BAAS_script_json_bridge_tests)
    set_tests_properties(BAAS_script_json_bridge_tests PROPERTIES TIMEOUT 60)
endif()
