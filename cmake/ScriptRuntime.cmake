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
        "${BAAS_PROJECT_PATH}/src/script/runtime/Environment.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/ErrorEnvelope.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/ErrorTranslation.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/HostModuleRegistry.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/JsonBridge.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/LogHost.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/ModuleGraph.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/ModuleSpecifier.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/SynchronousEvaluator.cpp"
        "${BAAS_PROJECT_PATH}/src/script/runtime/SynchronousHost.cpp"
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

    add_executable(
            BAAS_script_run
            "${BAAS_PROJECT_PATH}/apps/script_run/main.cpp"
    )
    target_compile_features(BAAS_script_run PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_run PRIVATE BAAS_script_runtime)
    if(MSVC)
        target_compile_options(BAAS_script_run PRIVATE /W4 /permissive-)
    else()
        target_compile_options(BAAS_script_run PRIVATE -Wall -Wextra -Wpedantic)
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
            BAAS_script_structured_error_heap_tests
            "${BAAS_PROJECT_PATH}/tests/script/StructuredErrorHeapTests.cpp"
    )
    target_compile_features(BAAS_script_structured_error_heap_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_structured_error_heap_tests PRIVATE BAAS_script_runtime)
    add_test(
            NAME BAAS_script_structured_error_heap_tests
            COMMAND BAAS_script_structured_error_heap_tests
    )
    set_tests_properties(BAAS_script_structured_error_heap_tests PROPERTIES TIMEOUT 60)

    add_executable(
            BAAS_script_error_envelope_tests
            "${BAAS_PROJECT_PATH}/tests/script/ErrorEnvelopeTests.cpp"
    )
    target_compile_features(BAAS_script_error_envelope_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_error_envelope_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_error_envelope_tests COMMAND BAAS_script_error_envelope_tests)
    set_tests_properties(BAAS_script_error_envelope_tests PROPERTIES TIMEOUT 60)

    add_executable(
            BAAS_script_environment_tests
            "${BAAS_PROJECT_PATH}/tests/script/EnvironmentTests.cpp"
    )
    target_compile_features(BAAS_script_environment_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_environment_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_environment_tests COMMAND BAAS_script_environment_tests)
    set_tests_properties(BAAS_script_environment_tests PROPERTIES TIMEOUT 60)

    add_executable(
            BAAS_script_error_translation_tests
            "${BAAS_PROJECT_PATH}/tests/script/ErrorTranslationTests.cpp"
    )
    target_compile_features(BAAS_script_error_translation_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_error_translation_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_error_translation_tests COMMAND BAAS_script_error_translation_tests)

    add_executable(
            BAAS_script_syntax_check_tests
            "${BAAS_PROJECT_PATH}/tests/script/SyntaxCheckTests.cpp"
    )
    target_compile_features(BAAS_script_syntax_check_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_syntax_check_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_syntax_check_tests COMMAND BAAS_script_syntax_check_tests)

    if(BUILD_SCRIPT_TOOLS)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
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
        add_test(
                NAME BAAS_script_check_errors_cleanup_valid_cli
                COMMAND BAAS_script_check --json
                        "${BAAS_PROJECT_PATH}/tests/script/fixtures/errors_cleanup_valid.baas"
        )
        add_test(
                NAME BAAS_script_check_errors_cleanup_invalid_cli
                COMMAND BAAS_script_check --json
                        "${BAAS_PROJECT_PATH}/tests/script/fixtures/errors_cleanup_invalid.baas"
        )
        set_tests_properties(BAAS_script_check_errors_cleanup_invalid_cli PROPERTIES WILL_FAIL TRUE)
        add_test(
                NAME BAAS_script_check_errors_cleanup_semantic_invalid_cli
                COMMAND BAAS_script_check --json
                        "${BAAS_PROJECT_PATH}/tests/script/fixtures/errors_cleanup_semantic_invalid.baas"
        )
        set_tests_properties(
                BAAS_script_check_errors_cleanup_semantic_invalid_cli
                PROPERTIES WILL_FAIL TRUE
        )
        add_test(
                NAME BAAS_script_check_async_tasks_valid_cli
                COMMAND BAAS_script_check --json
                        "${BAAS_PROJECT_PATH}/tests/script/fixtures/async_tasks_valid.baas"
        )
        add_test(
                NAME BAAS_script_check_async_tasks_invalid_cli
                COMMAND BAAS_script_check --json
                        "${BAAS_PROJECT_PATH}/tests/script/fixtures/async_tasks_invalid.baas"
        )
        set_tests_properties(BAAS_script_check_async_tasks_invalid_cli PROPERTIES WILL_FAIL TRUE)

        set(BAAS_SCRIPT_CONFORMANCE_ROOT "${BAAS_PROJECT_PATH}/tests/script/conformance/v1")
        set(BAAS_SCRIPT_CONFORMANCE_DIRECTORIES
                bounds cycle diagnostic escape happy host missing nested runtime_error)
        file(GLOB BAAS_SCRIPT_CONFORMANCE_ENTRIES
                RELATIVE "${BAAS_SCRIPT_CONFORMANCE_ROOT}"
                "${BAAS_SCRIPT_CONFORMANCE_ROOT}/*")
        set(BAAS_SCRIPT_CONFORMANCE_ACTUAL_DIRECTORIES "")
        foreach(entry IN LISTS BAAS_SCRIPT_CONFORMANCE_ENTRIES)
            if(IS_DIRECTORY "${BAAS_SCRIPT_CONFORMANCE_ROOT}/${entry}")
                list(APPEND BAAS_SCRIPT_CONFORMANCE_ACTUAL_DIRECTORIES "${entry}")
            endif()
        endforeach()
        list(SORT BAAS_SCRIPT_CONFORMANCE_DIRECTORIES)
        list(SORT BAAS_SCRIPT_CONFORMANCE_ACTUAL_DIRECTORIES)
        if(NOT BAAS_SCRIPT_CONFORMANCE_DIRECTORIES STREQUAL BAAS_SCRIPT_CONFORMANCE_ACTUAL_DIRECTORIES)
            message(FATAL_ERROR
                    "Every v1 conformance case directory must be explicitly registered")
        endif()
        set(BAAS_SCRIPT_RUN_EXPECTED_ROOT "${BAAS_PROJECT_PATH}/tests/script/expected/v1")
        function(baas_add_script_run_case name entry expected_exit expected_fragment expected_file)
            add_test(
                    NAME "${name}"
                    COMMAND "${CMAKE_COMMAND}"
                            "-DRUNNER=$<TARGET_FILE:BAAS_script_run>"
                            "-DPACKAGE_ROOT=${BAAS_SCRIPT_CONFORMANCE_ROOT}"
                            "-DENTRY=${entry}"
                            "-DEXPECTED_EXIT=${expected_exit}"
                            "-DEXPECTED_FRAGMENT=${expected_fragment}"
                            "-DEXPECTED_FILE=${BAAS_SCRIPT_RUN_EXPECTED_ROOT}/${expected_file}"
                            "-DREPEAT=2"
                            "-DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
                            -P "${BAAS_PROJECT_PATH}/tests/script/RunScriptRunnerCase.cmake"
            )
            set_tests_properties("${name}" PROPERTIES TIMEOUT 30)
        endfunction()

        baas_add_script_run_case(
                BAAS_script_run_conformance_v1
                happy/main 0
                "\"value\":{\"total\":762,\"same_module\":true,\"values\":[1,5,3],\"ordered\":{\"first\":3,\"second\":7,\"third\":11}}"
                happy.json
        )
        baas_add_script_run_case(
                BAAS_script_run_missing_module
                missing/main 1
                "\"code\":\"RUN003_MODULE_NOT_FOUND\""
                missing.json
        )
        baas_add_script_run_case(
                BAAS_script_run_path_escape
                escape/main 1
                "\"code\":\"MS012_DOT_SEGMENT\""
                escape.json
        )
        baas_add_script_run_case(
                BAAS_script_run_import_cycle
                cycle/a 1
                "\"code\":\"MG007_IMPORT_CYCLE\""
                cycle.json
        )
        baas_add_script_run_case(
                BAAS_script_run_compile_diagnostic
                diagnostic/main 1
                "\"phase\":\"compile\""
                diagnostic.json
        )
        baas_add_script_run_case(
                BAAS_script_run_runtime_error
                runtime_error/main 1
                "\"code\":\"DivisionByZero\""
                runtime_error.json
        )
        baas_add_script_run_case(
                BAAS_script_run_host_rejected
                host/main 1
                "\"code\":\"RUN006_HOST_IMPORT_UNAVAILABLE\""
                host.json
        )
        baas_add_script_run_case(
                BAAS_script_run_noncanonical_case
                HAPPY/main 1
                "\"code\":\"RUN003_MODULE_NOT_FOUND\""
                noncanonical_case.json
        )
        baas_add_script_run_case(
                BAAS_script_run_nested_valid
                nested/valid 0
                "\"value\":42"
                nested_valid.json
        )
        baas_add_script_run_case(
                BAAS_script_run_nested_missing
                nested/missing 1
                "\"module\":\"nested/not_found\""
                nested_missing.json
        )
        baas_add_script_run_case(
                BAAS_script_run_nested_host
                nested/host 1
                "\"code\":\"RUN006_HOST_IMPORT_UNAVAILABLE\""
                nested_host.json
        )
        baas_add_script_run_case(
                BAAS_script_run_nested_cycle
                nested/cycle_a 1
                "\"code\":\"MG007_IMPORT_CYCLE\""
                nested_cycle.json
        )
        baas_add_script_run_case(
                BAAS_script_run_unicode_diagnostic
                nested/unicode_diagnostic 1
                "界😀界😀"
                unicode_diagnostic.json
        )
        add_test(
                NAME BAAS_script_run_step_bound
                COMMAND "${CMAKE_COMMAND}"
                        "-DRUNNER=$<TARGET_FILE:BAAS_script_run>"
                        "-DPACKAGE_ROOT=${BAAS_SCRIPT_CONFORMANCE_ROOT}"
                        "-DENTRY=bounds/loop"
                        "-DEXPECTED_EXIT=1"
                        "-DEXPECTED_FRAGMENT=\"steps\":50"
                        "-DEXPECTED_FILE=${BAAS_SCRIPT_RUN_EXPECTED_ROOT}/step_bound.json"
                        "-DEXTRA_ARGS=--max-steps;50"
                        "-DREPEAT=2"
                        "-DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
                        -P "${BAAS_PROJECT_PATH}/tests/script/RunScriptRunnerCase.cmake"
        )
        set_tests_properties(BAAS_script_run_step_bound PROPERTIES TIMEOUT 30)
        add_test(
                NAME BAAS_script_run_output_bound
                COMMAND "${CMAKE_COMMAND}"
                        "-DRUNNER=$<TARGET_FILE:BAAS_script_run>"
                        "-DPACKAGE_ROOT=${BAAS_SCRIPT_CONFORMANCE_ROOT}"
                        "-DENTRY=happy/main"
                        "-DEXPECTED_EXIT=1"
                        "-DEXPECTED_FRAGMENT=\"code\":\"RUN021_OUTPUT_LIMIT_EXCEEDED\""
                        "-DEXPECTED_FILE=${BAAS_SCRIPT_RUN_EXPECTED_ROOT}/output_bound.json"
                        "-DEXTRA_ARGS=--max-json-output-bytes;64"
                        "-DREPEAT=2"
                        "-DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
                        -P "${BAAS_PROJECT_PATH}/tests/script/RunScriptRunnerCase.cmake"
        )
        set_tests_properties(BAAS_script_run_output_bound PROPERTIES TIMEOUT 30)
        add_test(
                NAME BAAS_script_run_source_bound
                COMMAND "${CMAKE_COMMAND}"
                        "-DRUNNER=$<TARGET_FILE:BAAS_script_run>"
                        "-DPACKAGE_ROOT=${BAAS_SCRIPT_CONFORMANCE_ROOT}"
                        "-DENTRY=happy/main"
                        "-DEXPECTED_EXIT=1"
                        "-DEXPECTED_FRAGMENT=\"code\":\"RUN005_SOURCE_LIMIT_EXCEEDED\""
                        "-DEXPECTED_FILE=${BAAS_SCRIPT_RUN_EXPECTED_ROOT}/source_bound.json"
                        "-DEXTRA_ARGS=--max-module-bytes;32"
                        "-DREPEAT=2"
                        "-DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
                        -P "${BAAS_PROJECT_PATH}/tests/script/RunScriptRunnerCase.cmake"
        )
        set_tests_properties(BAAS_script_run_source_bound PROPERTIES TIMEOUT 30)
        add_test(
                NAME BAAS_script_run_loader_work_bound
                COMMAND "${CMAKE_COMMAND}"
                        "-DRUNNER=$<TARGET_FILE:BAAS_script_run>"
                        "-DPACKAGE_ROOT=${BAAS_SCRIPT_CONFORMANCE_ROOT}"
                        "-DENTRY=happy/main"
                        "-DEXPECTED_EXIT=1"
                        "-DEXPECTED_FRAGMENT=\"code\":\"RUN008_LOADER_LIMIT_EXCEEDED\""
                        "-DEXPECTED_FILE=${BAAS_SCRIPT_RUN_EXPECTED_ROOT}/loader_work_bound.json"
                        "-DEXTRA_ARGS=--max-loader-work;1"
                        "-DREPEAT=2"
                        "-DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
                        -P "${BAAS_PROJECT_PATH}/tests/script/RunScriptRunnerCase.cmake"
        )
        set_tests_properties(BAAS_script_run_loader_work_bound PROPERTIES TIMEOUT 30)
    endif()

    add_executable(
            BAAS_script_json_bridge_tests
            "${BAAS_PROJECT_PATH}/tests/script/JsonBridgeTests.cpp"
    )
    target_compile_features(BAAS_script_json_bridge_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_json_bridge_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_json_bridge_tests COMMAND BAAS_script_json_bridge_tests)
    set_tests_properties(BAAS_script_json_bridge_tests PROPERTIES TIMEOUT 60)

    add_executable(
            BAAS_script_module_specifier_tests
            "${BAAS_PROJECT_PATH}/tests/script/ModuleSpecifierTests.cpp"
    )
    target_compile_features(BAAS_script_module_specifier_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_module_specifier_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_module_specifier_tests COMMAND BAAS_script_module_specifier_tests)

    add_executable(
            BAAS_script_module_graph_tests
            "${BAAS_PROJECT_PATH}/tests/script/ModuleGraphTests.cpp"
    )
    target_compile_features(BAAS_script_module_graph_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_module_graph_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_module_graph_tests COMMAND BAAS_script_module_graph_tests)
    set_tests_properties(BAAS_script_module_graph_tests PROPERTIES TIMEOUT 15)

    add_executable(
            BAAS_script_host_registry_tests
            "${BAAS_PROJECT_PATH}/tests/script/HostModuleRegistryTests.cpp"
    )
    target_compile_features(BAAS_script_host_registry_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_host_registry_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_host_registry_tests COMMAND BAAS_script_host_registry_tests)
    set_tests_properties(BAAS_script_host_registry_tests PROPERTIES TIMEOUT 30)

    add_executable(
            BAAS_script_sync_evaluator_tests
            "${BAAS_PROJECT_PATH}/tests/script/SynchronousEvaluatorTests.cpp"
    )
    target_compile_features(BAAS_script_sync_evaluator_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_sync_evaluator_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_sync_evaluator_tests COMMAND BAAS_script_sync_evaluator_tests)
    set_tests_properties(BAAS_script_sync_evaluator_tests PROPERTIES TIMEOUT 30)

    add_executable(
            BAAS_script_sync_host_tests
            "${BAAS_PROJECT_PATH}/tests/script/SynchronousHostTests.cpp"
    )
    target_compile_features(BAAS_script_sync_host_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_sync_host_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_sync_host_tests COMMAND BAAS_script_sync_host_tests)
    set_tests_properties(BAAS_script_sync_host_tests PROPERTIES TIMEOUT 30)

    add_executable(
            BAAS_script_sync_host_evaluator_tests
            "${BAAS_PROJECT_PATH}/tests/script/SynchronousHostEvaluatorTests.cpp"
    )
    target_compile_features(BAAS_script_sync_host_evaluator_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_sync_host_evaluator_tests PRIVATE BAAS_script_runtime)
    add_test(
            NAME BAAS_script_sync_host_evaluator_tests
            COMMAND BAAS_script_sync_host_evaluator_tests
    )
    set_tests_properties(BAAS_script_sync_host_evaluator_tests PROPERTIES TIMEOUT 30)

    add_executable(
            BAAS_script_log_host_tests
            "${BAAS_PROJECT_PATH}/tests/script/LogHostTests.cpp"
    )
    target_compile_features(BAAS_script_log_host_tests PRIVATE cxx_std_20)
    target_link_libraries(BAAS_script_log_host_tests PRIVATE BAAS_script_runtime)
    add_test(NAME BAAS_script_log_host_tests COMMAND BAAS_script_log_host_tests)
    set_tests_properties(BAAS_script_log_host_tests PROPERTIES TIMEOUT 30)
endif()

if(TARGET BAAS_APP)
    add_library(
            BAAS_script_baas_logger_adapter
            STATIC
            "${BAAS_PROJECT_PATH}/src/script/host/BAASLoggerLogSink.cpp"
    )
    target_compile_features(BAAS_script_baas_logger_adapter PUBLIC cxx_std_20)
    target_include_directories(
            BAAS_script_baas_logger_adapter
            PUBLIC
            "${BAAS_PROJECT_PATH}/include"
    )
    target_link_libraries(
            BAAS_script_baas_logger_adapter
            PUBLIC BAAS_script_runtime
            PRIVATE BAAS::spdlog BAAS::simdutf
    )
    target_link_libraries(
            BAAS_APP
            PRIVATE BAAS_script_baas_logger_adapter
    )
endif()
