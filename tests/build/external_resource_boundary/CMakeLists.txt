cmake_minimum_required(VERSION 3.22)

project(BAASExternalResourceBoundaryHarness LANGUAGES NONE)

foreach(_required_variable
        BAAS_REPOSITORY_ROOT
        BAAS_RESOURCE_PROFILE
        BAAS_RESOURCE_HARNESS_MODE
        BAAS_RESOURCE_RESULT_FILE)
    if(NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${_required_variable} is required")
    endif()
endforeach()

get_filename_component(BAAS_REPOSITORY_ROOT "${BAAS_REPOSITORY_ROOT}" ABSOLUTE)
set(BAAS_APP_BAAS_CPP_DIR "${BAAS_REPOSITORY_ROOT}/apps/BAAS")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime")

if(BAAS_RESOURCE_PROFILE STREQUAL "baas")
    set(_declaration_module
            "${BAAS_REPOSITORY_ROOT}/apps/BAAS/cmake/RuntimeResources.cmake")
elseif(BAAS_RESOURCE_PROFILE STREQUAL "isa")
    set(_declaration_module
            "${BAAS_REPOSITORY_ROOT}/apps/ISA/cmake/RuntimeResources.cmake")
elseif(BAAS_RESOURCE_PROFILE STREQUAL "ocr")
    set(_declaration_module
            "${BAAS_REPOSITORY_ROOT}/apps/ocr_server/cmake/RuntimeResources.cmake")
elseif(BAAS_RESOURCE_PROFILE STREQUAL "afwc")
    set(_declaration_module
            "${BAAS_REPOSITORY_ROOT}/apps/BAAS_auto_fight_workflow_checker/cmake/RuntimeResources.cmake")
else()
    message(FATAL_ERROR "Unknown BAAS_RESOURCE_PROFILE: ${BAAS_RESOURCE_PROFILE}")
endif()

if(BAAS_RESOURCE_HARNESS_MODE STREQUAL "record")
    set(BAAS_FETCHED_RESOURCE_ROOT "${CMAKE_BINARY_DIR}/recorded-fetched-root")
    set_property(GLOBAL PROPERTY BAAS_RECORDED_COPY_COUNT 0)

    function(baas_require_runtime_resources)
        get_property(_recorded GLOBAL PROPERTY BAAS_RECORDED_REQUIRED_RESOURCES)
        list(APPEND _recorded ${ARGN})
        set_property(GLOBAL PROPERTY BAAS_RECORDED_REQUIRED_RESOURCES "${_recorded}")
    endfunction()

    function(baas_fetch_resources)
        get_property(_recorded GLOBAL PROPERTY BAAS_RECORDED_FETCHED_RESOURCES)
        list(APPEND _recorded ${ARGN})
        set_property(GLOBAL PROPERTY BAAS_RECORDED_FETCHED_RESOURCES "${_recorded}")
    endfunction()

    function(baas_copy_local_runtime_resources)
        cmake_parse_arguments(
                BAAS_RECORDED_COPY
                ""
                "BASE_DIR;DESTINATION"
                "ITEMS"
                ${ARGN}
        )
        if(BAAS_RECORDED_COPY_UNPARSED_ARGUMENTS)
            message(FATAL_ERROR
                    "Unexpected recorded copy arguments: "
                    "${BAAS_RECORDED_COPY_UNPARSED_ARGUMENTS}")
        endif()
        get_property(_copy_count GLOBAL PROPERTY BAAS_RECORDED_COPY_COUNT)
        if(_copy_count STREQUAL "")
            set(_copy_count 0)
        endif()
        set_property(GLOBAL PROPERTY "BAAS_RECORDED_COPY_BASE_${_copy_count}"
                "${BAAS_RECORDED_COPY_BASE_DIR}")
        set_property(GLOBAL PROPERTY "BAAS_RECORDED_COPY_DESTINATION_${_copy_count}"
                "${BAAS_RECORDED_COPY_DESTINATION}")
        set_property(GLOBAL PROPERTY "BAAS_RECORDED_COPY_ITEMS_${_copy_count}"
                "${BAAS_RECORDED_COPY_ITEMS}")
        math(EXPR _copy_count "${_copy_count} + 1")
        set_property(GLOBAL PROPERTY BAAS_RECORDED_COPY_COUNT "${_copy_count}")
    endfunction()

    include("${_declaration_module}")

    get_property(_required GLOBAL PROPERTY BAAS_RECORDED_REQUIRED_RESOURCES)
    get_property(_fetched GLOBAL PROPERTY BAAS_RECORDED_FETCHED_RESOURCES)
    get_property(_copy_count GLOBAL PROPERTY BAAS_RECORDED_COPY_COUNT)
    if(_copy_count STREQUAL "")
        set(_copy_count 0)
    endif()
    string(REPLACE ";" "," _required "${_required}")
    string(REPLACE ";" "," _fetched "${_fetched}")
    file(WRITE "${BAAS_RESOURCE_RESULT_FILE}"
            "required=${_required}\n"
            "fetched=${_fetched}\n"
            "copy_count=${_copy_count}\n")
    if(_copy_count GREATER 0)
        math(EXPR _copy_last "${_copy_count} - 1")
        foreach(_copy_index RANGE 0 ${_copy_last})
            get_property(_copy_base GLOBAL PROPERTY
                    "BAAS_RECORDED_COPY_BASE_${_copy_index}")
            get_property(_copy_destination GLOBAL PROPERTY
                    "BAAS_RECORDED_COPY_DESTINATION_${_copy_index}")
            get_property(_copy_items GLOBAL PROPERTY
                    "BAAS_RECORDED_COPY_ITEMS_${_copy_index}")
            string(REPLACE ";" "," _copy_items "${_copy_items}")
            file(APPEND "${BAAS_RESOURCE_RESULT_FILE}"
                    "copy_${_copy_index}_base=${_copy_base}\n"
                    "copy_${_copy_index}_destination=${_copy_destination}\n"
                    "copy_${_copy_index}_items=${_copy_items}\n")
        endforeach()
    endif()
elseif(BAAS_RESOURCE_HARNESS_MODE STREQUAL "execute")
    if(WIN32)
        set(TARGET_OS_NAME Windows)
        set(_adb_files adb.exe AdbWinApi.dll AdbWinUsbApi.dll)
    elseif(APPLE)
        set(TARGET_OS_NAME MacOS)
        set(_adb_files adb)
    else()
        set(TARGET_OS_NAME Linux)
        set(_adb_files adb)
    endif()

    set(BAAS_PROJECT_PATH "${CMAKE_BINARY_DIR}/fixture-project")
    file(MAKE_DIRECTORY "${BAAS_PROJECT_PATH}/resource")
    file(WRITE "${BAAS_PROJECT_PATH}/resource/global_setting.json" "{}\n")
    file(WRITE "${BAAS_PROJECT_PATH}/resource/static.json" "{}\n")

    set(BAAS_FETCH_RESOURCES OFF)
    set(BAAS_RESOURCE_OUTPUT_ROOT "${CMAKE_BINARY_DIR}/preseed"
            CACHE PATH "" FORCE)
    set(BAAS_RESOURCE_DOWNLOAD_ROOT "${CMAKE_BINARY_DIR}/fetch-must-not-run"
            CACHE PATH "" FORCE)
    file(WRITE "${BAAS_RESOURCE_DOWNLOAD_ROOT}" "offline fetch sentinel\n")
    include("${BAAS_REPOSITORY_ROOT}/cmake/BAASResources.cmake")

    set(_platform_tools
            "${BAAS_FETCHED_RESOURCE_ROOT}/bin/${TARGET_OS_NAME}/platform-tools")
    file(MAKE_DIRECTORY
            "${_platform_tools}"
            "${BAAS_FETCHED_RESOURCE_ROOT}/bin/scrcpy"
            "${BAAS_FETCHED_RESOURCE_ROOT}/ocr_models"
            "${BAAS_FETCHED_RESOURCE_ROOT}/yolo_models")
    foreach(_adb_file IN LISTS _adb_files)
        file(WRITE "${_platform_tools}/${_adb_file}" "fixture ${_adb_file}\n")
    endforeach()
    file(WRITE "${BAAS_FETCHED_RESOURCE_ROOT}/bin/scrcpy/server.bin"
            "fixture scrcpy\n")
    file(WRITE "${BAAS_FETCHED_RESOURCE_ROOT}/ocr_models/model.bin"
            "fixture ocr\n")
    file(WRITE "${BAAS_FETCHED_RESOURCE_ROOT}/yolo_models/model.bin"
            "fixture yolo\n")
    file(WRITE "${BAAS_FETCHED_RESOURCE_ROOT}/yolo_models/data.yaml"
            "names: []\n")

    include("${_declaration_module}")
    baas_install_required_runtime_resources()

    file(READ "${BAAS_RESOURCE_DOWNLOAD_ROOT}" _fetch_sentinel)
    if(NOT _fetch_sentinel STREQUAL "offline fetch sentinel\n")
        message(FATAL_ERROR "Offline fetch sentinel was modified")
    endif()
    file(GLOB_RECURSE _runtime_files
            LIST_DIRECTORIES false
            RELATIVE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/*")
    list(SORT _runtime_files)
    string(REPLACE ";" "," _runtime_files "${_runtime_files}")
    file(WRITE "${BAAS_RESOURCE_RESULT_FILE}"
            "files=${_runtime_files}\n"
            "fetch_sentinel=unchanged\n")
else()
    message(FATAL_ERROR
            "Unknown BAAS_RESOURCE_HARNESS_MODE: ${BAAS_RESOURCE_HARNESS_MODE}")
endif()
