if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "SOURCE_DIR and BUILD_DIR are required")
endif()

file(STRINGS "${BUILD_DIR}/CMakeCache.txt" _supervisor_cache
    REGEX "^BUILD_WEBUI_SERVICE_SUPERVISOR(_TESTS)?:BOOL=")
list(SORT _supervisor_cache)
set(_expected_cache
    "BUILD_WEBUI_SERVICE_SUPERVISOR:BOOL=OFF"
    "BUILD_WEBUI_SERVICE_SUPERVISOR_TESTS:BOOL=OFF")
list(SORT _expected_cache)
if(NOT _supervisor_cache STREQUAL _expected_cache)
    message(FATAL_ERROR
        "WebUI service supervisor must default fully OFF: ${_supervisor_cache}")
endif()

file(GLOB_RECURSE _unexpected_targets
    "${BUILD_DIR}/CMakeFiles/BAAS_webui_service_process_owner*.dir")
if(_unexpected_targets)
    message(FATAL_ERROR
        "Default-OFF configure unexpectedly created supervisor targets")
endif()

set(_android_build "${BUILD_DIR}-android-rejection")
file(REMOVE_RECURSE "${_android_build}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${SOURCE_DIR}"
        -B "${_android_build}"
        -DCMAKE_SYSTEM_NAME=Android
        -DBUILD_WEBUI_SERVICE_SUPERVISOR=ON
    RESULT_VARIABLE _android_result
    OUTPUT_VARIABLE _android_output
    ERROR_VARIABLE _android_error
)
set(_android_log "${_android_output}\n${_android_error}")
if(_android_result EQUAL 0
   OR NOT _android_log MATCHES
       "pure WebUI BAAS_service process supervisor is desktop-only")
    message(FATAL_ERROR
        "Standard Android configure must fail with the desktop-only boundary:\n${_android_log}")
endif()

message(STATUS "WebUI supervisor default-OFF and Android boundaries passed")
