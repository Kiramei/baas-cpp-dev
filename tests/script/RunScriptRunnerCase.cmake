if(NOT DEFINED RUNNER OR NOT DEFINED PACKAGE_ROOT OR NOT DEFINED ENTRY
   OR NOT DEFINED PYTHON_EXECUTABLE)
    message(FATAL_ERROR "runner, package root, entry, and Python are required")
endif()
if(NOT DEFINED REPEAT)
    set(REPEAT 1)
endif()

set(previous_output "")
string(MD5 output_id "${ENTRY};${EXTRA_ARGS};${EXPECTED_FILE}")
set(output_path "${CMAKE_CURRENT_BINARY_DIR}/script-run-${output_id}.json")
foreach(iteration RANGE 1 ${REPEAT})
    execute_process(
            COMMAND "${RUNNER}" --package-root "${PACKAGE_ROOT}" --entry "${ENTRY}" ${EXTRA_ARGS}
            RESULT_VARIABLE exit_code
            OUTPUT_FILE "${output_path}"
            ERROR_VARIABLE stderr
    )
    file(READ "${output_path}" stdout)
    string(REPLACE "\r\n" "\n" stdout "${stdout}")
    if(NOT exit_code EQUAL EXPECTED_EXIT)
        message(FATAL_ERROR "unexpected exit ${exit_code}; expected ${EXPECTED_EXIT}; stdout=${stdout}; stderr=${stderr}")
    endif()
    if(NOT stderr STREQUAL "")
        message(FATAL_ERROR "runner wrote to stderr: ${stderr}")
    endif()
    execute_process(
            COMMAND "${PYTHON_EXECUTABLE}" -c
                    "import json,pathlib,sys; json.loads(pathlib.Path(sys.argv[1]).read_bytes().decode('utf-8', errors='strict'))"
                    "${output_path}"
            RESULT_VARIABLE json_exit
            ERROR_VARIABLE json_error
    )
    if(NOT json_exit EQUAL 0)
        message(FATAL_ERROR "runner stdout is not strict UTF-8 JSON: ${json_error}")
    endif()
    string(FIND "${stdout}" "${EXPECTED_FRAGMENT}" fragment_position)
    if(fragment_position EQUAL -1)
        message(FATAL_ERROR "expected fragment not found: ${EXPECTED_FRAGMENT}; stdout=${stdout}")
    endif()
    if(DEFINED EXPECTED_FILE)
        file(READ "${EXPECTED_FILE}" expected_output)
        string(REPLACE "\r\n" "\n" expected_output "${expected_output}")
        if(NOT stdout STREQUAL expected_output)
            message(FATAL_ERROR "runner output does not match ${EXPECTED_FILE}: ${stdout}")
        endif()
    endif()
    if(iteration GREATER 1 AND NOT stdout STREQUAL previous_output)
        message(FATAL_ERROR "runner output is not deterministic: ${previous_output} != ${stdout}")
    endif()
    set(previous_output "${stdout}")
endforeach()
file(REMOVE "${output_path}")
