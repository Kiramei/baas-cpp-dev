option(BAAS_FETCH_RESOURCES "Download BAAS runtime resources during configure" ON)
set(BAAS_RESOURCE_DOWNLOAD_ROOT "${CMAKE_BINARY_DIR}/baas-resource-downloads" CACHE PATH "BAAS resource download cache")
set(BAAS_RESOURCE_OUTPUT_ROOT "${CMAKE_BINARY_DIR}/baas-resources" CACHE PATH "BAAS fetched resource output root")
set(BAAS_FETCHED_RESOURCE_ROOT "${BAAS_RESOURCE_OUTPUT_ROOT}/resource")

function(baas_fetch_resources)
    set(_resources ${ARGN})
    if(NOT _resources)
        return()
    endif()

    list(REMOVE_DUPLICATES _resources)

    if(NOT BAAS_FETCH_RESOURCES)
        message(STATUS "BAAS resource download disabled; using ${BAAS_FETCHED_RESOURCE_ROOT}")
        return()
    endif()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    set(_cmd
            "${Python3_EXECUTABLE}"
            "${BAAS_PROJECT_PATH}/scripts/fetch_resources.py"
            "--lock"
            "${BAAS_PROJECT_PATH}/resources.lock.json"
            "--output-root"
            "${BAAS_FETCHED_RESOURCE_ROOT}"
            "--download-root"
            "${BAAS_RESOURCE_DOWNLOAD_ROOT}"
    )

    foreach(_resource IN LISTS _resources)
        list(APPEND _cmd "--resource" "${_resource}")
    endforeach()

    execute_process(
            COMMAND ${_cmd}
            WORKING_DIRECTORY "${BAAS_PROJECT_PATH}"
            RESULT_VARIABLE _baas_fetch_result
            OUTPUT_VARIABLE _baas_fetch_stdout
            ERROR_VARIABLE _baas_fetch_stderr
    )

    if(_baas_fetch_stdout)
        string(STRIP "${_baas_fetch_stdout}" _baas_fetch_stdout)
        message(STATUS "${_baas_fetch_stdout}")
    endif()

    if(NOT _baas_fetch_result EQUAL 0)
        if(_baas_fetch_stderr)
            string(STRIP "${_baas_fetch_stderr}" _baas_fetch_stderr)
        endif()
        message(FATAL_ERROR "Failed to fetch BAAS resources: ${_baas_fetch_stderr}")
    endif()
endfunction()
