include_guard(GLOBAL)

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

function(baas_require_runtime_resources)
    get_property(_resources GLOBAL PROPERTY BAAS_REQUIRED_RUNTIME_RESOURCES)
    list(APPEND _resources ${ARGN})
    if(_resources)
        list(REMOVE_DUPLICATES _resources)
    endif()
    set_property(GLOBAL PROPERTY BAAS_REQUIRED_RUNTIME_RESOURCES "${_resources}")
endfunction()

function(baas_copy_local_runtime_resources)
    cmake_parse_arguments(
            BAAS_LOCAL_RUNTIME_RESOURCE
            ""
            "BASE_DIR;DESTINATION"
            "ITEMS"
            ${ARGN}
    )

    if(BAAS_LOCAL_RUNTIME_RESOURCE_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
                "Unknown baas_copy_local_runtime_resources arguments: "
                "${BAAS_LOCAL_RUNTIME_RESOURCE_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT BAAS_LOCAL_RUNTIME_RESOURCE_BASE_DIR)
        message(FATAL_ERROR "baas_copy_local_runtime_resources requires BASE_DIR")
    endif()
    if(NOT BAAS_LOCAL_RUNTIME_RESOURCE_ITEMS)
        message(FATAL_ERROR "baas_copy_local_runtime_resources requires ITEMS")
    endif()

    if(BAAS_LOCAL_RUNTIME_RESOURCE_DESTINATION)
        set(_destination "${BAAS_LOCAL_RUNTIME_RESOURCE_DESTINATION}")
    else()
        set(_destination "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resource")
    endif()

    foreach(_item IN LISTS BAAS_LOCAL_RUNTIME_RESOURCE_ITEMS)
        if(_item STREQUAL "config")
            file(
                    GLOB
                    _config_files
                    "${BAAS_LOCAL_RUNTIME_RESOURCE_BASE_DIR}/config/*.json"
            )
            if(NOT _config_files)
                message(FATAL_ERROR "No config json files found in ${BAAS_LOCAL_RUNTIME_RESOURCE_BASE_DIR}/config")
            endif()
            file(COPY ${_config_files} DESTINATION "${_destination}")
            continue()
        endif()

        set(_item_path "${BAAS_LOCAL_RUNTIME_RESOURCE_BASE_DIR}/${_item}")
        if(NOT EXISTS "${_item_path}")
            message(FATAL_ERROR "Local runtime resource not found: ${_item_path}")
        endif()
        file(COPY "${_item_path}" DESTINATION "${_destination}")
    endforeach()
endfunction()

function(_baas_platform_tools_resource out_var)
    if(TARGET_OS_NAME STREQUAL "Windows")
        set(${out_var} "platform_tools_windows" PARENT_SCOPE)
    elseif(TARGET_OS_NAME STREQUAL "MacOS")
        set(${out_var} "platform_tools_macos" PARENT_SCOPE)
    elseif(TARGET_OS_NAME STREQUAL "Linux")
        set(${out_var} "platform_tools_linux" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "ADB runtime resource is not supported on ${TARGET_OS_NAME}")
    endif()
endfunction()

function(_baas_adb_files out_var)
    if(TARGET_OS_NAME STREQUAL "Windows")
        set(${out_var} adb.exe AdbWinApi.dll AdbWinUsbApi.dll PARENT_SCOPE)
    elseif(TARGET_OS_NAME STREQUAL "MacOS" OR TARGET_OS_NAME STREQUAL "Linux")
        set(${out_var} adb PARENT_SCOPE)
    else()
        message(FATAL_ERROR "ADB runtime resource is not supported on ${TARGET_OS_NAME}")
    endif()
endfunction()

function(_baas_install_adb_resource)
    _baas_platform_tools_resource(_platform_tools_resource)
    baas_fetch_resources("${_platform_tools_resource}")

    set(_platform_tools_dir "${BAAS_FETCHED_RESOURCE_ROOT}/bin/${TARGET_OS_NAME}/platform-tools")
    if(NOT EXISTS "${_platform_tools_dir}")
        message(FATAL_ERROR "ADB platform-tools directory not found: ${_platform_tools_dir}")
    endif()

    _baas_adb_files(_adb_files)
    foreach(_adb_file IN LISTS _adb_files)
        set(_adb_path "${_platform_tools_dir}/${_adb_file}")
        if(NOT EXISTS "${_adb_path}")
            message(FATAL_ERROR "ADB runtime file not found: ${_adb_path}")
        endif()
        message(STATUS "Found ADB runtime file: ${_adb_path}")
        file(COPY "${_adb_path}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endforeach()

    file(
            COPY
            "${_platform_tools_dir}"
            DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resource/bin/${TARGET_OS_NAME}"
    )
endfunction()

function(baas_install_required_runtime_resources)
    get_property(_resources GLOBAL PROPERTY BAAS_REQUIRED_RUNTIME_RESOURCES)
    if(NOT _resources)
        return()
    endif()

    list(REMOVE_DUPLICATES _resources)
    set(_runtime_resource_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resource")

    foreach(_resource IN LISTS _resources)
        if(_resource STREQUAL "global_config")
            file(
                    COPY
                    "${BAAS_PROJECT_PATH}/resource/global_setting.json"
                    DESTINATION "${_runtime_resource_dir}"
            )
        elseif(_resource STREQUAL "static_config")
            file(
                    COPY
                    "${BAAS_PROJECT_PATH}/resource/static.json"
                    DESTINATION "${_runtime_resource_dir}"
            )
        elseif(_resource STREQUAL "adb")
            _baas_install_adb_resource()
        elseif(_resource STREQUAL "scrcpy_server")
            baas_fetch_resources(scrcpy_server)
            file(
                    COPY
                    "${BAAS_FETCHED_RESOURCE_ROOT}/bin/scrcpy"
                    DESTINATION "${_runtime_resource_dir}/bin"
            )
        elseif(_resource STREQUAL "ocr_models")
            baas_fetch_resources(ocr_models)
            file(
                    COPY
                    "${BAAS_FETCHED_RESOURCE_ROOT}/ocr_models"
                    DESTINATION "${_runtime_resource_dir}"
            )
        elseif(_resource STREQUAL "yolo_models")
            baas_fetch_resources(yolo_models)
            file(
                    COPY
                    "${BAAS_FETCHED_RESOURCE_ROOT}/yolo_models"
                    DESTINATION "${_runtime_resource_dir}"
            )
        else()
            message(FATAL_ERROR "Unknown BAAS runtime resource: ${_resource}")
        endif()
    endforeach()
endfunction()
