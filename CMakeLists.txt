cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)

get_filename_component(BAAS_PROJECT_PATH "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/utils.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/platform.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/BAASResources.cmake")

BAAS_hr_LOG("PROJECT BAAS")

# Build type
if (CMAKE_BUILD_TYPE AND NOT (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
    message(FATAL_ERROR "Invalid build type: ${CMAKE_BUILD_TYPE}. Allowed : Debug, Release")
endif()

if(CMAKE_BUILD_TYPE)
    BAAS_sub_title_LOG("BUILD TYPE: ${CMAKE_BUILD_TYPE}")
else()
    BAAS_sub_title_LOG("BUILD TYPE: not set; dependency config defaults to Release")
endif()

# Target Platform
detect_target_platform(TARGET_OS_NAME)

if(NOT DEFINED TARGET_OS_NAME)
    message(FATAL_ERROR "Unknown Target OS: ${CMAKE_SYSTEM_NAME}")
endif()

BAAS_sub_title_LOG("Target OS : ${TARGET_OS_NAME}")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/BAASDependency.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/BAASTarget.cmake")

if(TARGET_OS_NAME STREQUAL "Android")
    baas_setup_android_ndk()
endif()


include("${CMAKE_CURRENT_LIST_DIR}/cmake/Version.cmake")
project(BAAS VERSION ${BAAS_VERSION})

# git configuration
get_git_info(GIT_BRANCH git rev-parse --abbrev-ref HEAD)
get_git_info(GIT_COMMIT_HASH  git rev-parse HEAD)
get_git_info(GIT_COMMIT_AUTHOR git log -1 --format=%an)
get_git_info(GIT_COMMIT_EMAIL  git log -1 --format=%ae)
get_git_info(GIT_COMMIT_TITLE  git log -1 --format=%s)
get_git_info(GIT_COMMIT_BODY   git log -1 --format=%b)
get_git_info(GIT_COMMIT_DATE   git log -1 --format=%cd --date=iso)

# build time
string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S (UTC)" UTC)

configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/version.h.in
        BAAS_version.h
)

configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/version.txt.in
        BAAS_version.txt
)

BAAS_sub_title_LOG("GIT INFO")
message(STATUS "Hash        : ${GIT_COMMIT_HASH}")
message(STATUS "Branch      : ${GIT_BRANCH}")
message(STATUS "Author      : ${GIT_COMMIT_AUTHOR} <${GIT_COMMIT_EMAIL}>")
message(STATUS "Commit      : ${GIT_COMMIT_TITLE}")
message(STATUS "Date        : ${GIT_COMMIT_DATE}")
message(STATUS "Build time  : ${BUILD_TIME}")


# app options
option(BUILD_APP_BAAS               "Build BAAS app          "  OFF)
option(BUILD_APP_ISA                "Build ISA  app          "  OFF)

# other
option(BUILD_BAAS_OCR           "build the ocr server app"               OFF)
option(BUILD_BAAS_AW_CHECKER    "build BAAS Auto Fight Workflow Checker" OFF)
option(BUILD_BAAS_NMS_BENCHMARK "Build BAAS vs OpenCV dnn NMS Google Benchmark executable" OFF)
option(BUILD_SCRIPT_RUNTIME     "Build the standalone BAAS script runtime" OFF)
option(BUILD_SCRIPT_TESTS       "Build native tests for the BAAS script runtime" OFF)
option(BUILD_SCRIPT_TOOLS       "Build BAAS script developer tools" OFF)
option(BUILD_SERVICE_PROTOCOL   "Build the standalone BAAS service protocol library" OFF)
option(BUILD_SERVICE_PROTOCOL_TESTS "Build native tests for the BAAS service protocol" OFF)
option(BUILD_SERVICE_TRIGGER_CATALOG "Build the transport-independent trigger command catalog" OFF)
option(BUILD_SERVICE_TRIGGER_CATALOG_TESTS "Build native tests for the trigger command catalog" OFF)
option(BUILD_SERVICE_TRIGGER_DISPATCH "Build the transport-independent trigger dispatcher" OFF)
option(BUILD_SERVICE_TRIGGER_DISPATCH_TESTS "Build native tests for trigger dispatch" OFF)
option(BUILD_SERVICE_PIPE_HOST "Build the bounded native Pipe host" OFF)
option(BUILD_SERVICE_PIPE_HOST_TESTS "Build deterministic tests for the Pipe host" OFF)
option(BUILD_SERVICE_TRIGGER_EXECUTOR "Build the bounded trigger execution owner" OFF)
option(BUILD_SERVICE_TRIGGER_EXECUTOR_TESTS "Build native tests for trigger execution ownership" OFF)
option(BUILD_SERVICE_ROUTER     "Build the transport-independent BAAS service router" OFF)
option(BUILD_SERVICE_ROUTER_TESTS "Build native tests for the BAAS service router" OFF)
option(BUILD_SERVICE_ORIGIN_POLICY "Build the standalone HTTP Origin/CORS policy" OFF)
option(BUILD_SERVICE_ORIGIN_POLICY_TESTS "Build native tests for HTTP Origin/CORS policy" OFF)
option(BUILD_SERVICE_HTTP       "Build the optional cpp-httplib service adapter" OFF)
option(BUILD_SERVICE_HTTP_TESTS "Build native tests for the cpp-httplib service adapter" OFF)

# BAAS_APP owns the production script Host adapters. Standalone runtime builds
# stay opt-in, while application builds always compile the runtime and its
# BAASLogger bridge together.
if(BUILD_APP_BAAS AND NOT BUILD_SCRIPT_RUNTIME)
    set(BUILD_SCRIPT_RUNTIME ON CACHE BOOL "Build the standalone BAAS script runtime" FORCE)
endif()

if(BUILD_SCRIPT_TESTS AND NOT BUILD_SCRIPT_RUNTIME)
    set(BUILD_SCRIPT_RUNTIME ON CACHE BOOL "Build the standalone BAAS script runtime" FORCE)
endif()

if(BUILD_SCRIPT_TOOLS AND NOT BUILD_SCRIPT_RUNTIME)
    set(BUILD_SCRIPT_RUNTIME ON CACHE BOOL "Build the standalone BAAS script runtime" FORCE)
endif()

if(BUILD_SERVICE_PROTOCOL_TESTS AND NOT BUILD_SERVICE_PROTOCOL)
    set(BUILD_SERVICE_PROTOCOL ON CACHE BOOL "Build the standalone BAAS service protocol library" FORCE)
endif()

# Trigger ingress consumes command policy from the catalog. Keep this a
# one-way dependency: the allocation-free catalog never depends on protocol
# framing, envelopes, or sessions.
if(BUILD_SERVICE_PROTOCOL AND NOT BUILD_SERVICE_TRIGGER_CATALOG)
    set(BUILD_SERVICE_TRIGGER_CATALOG ON CACHE BOOL "Build the transport-independent trigger command catalog" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_CATALOG_TESTS AND NOT BUILD_SERVICE_TRIGGER_CATALOG)
    set(BUILD_SERVICE_TRIGGER_CATALOG ON CACHE BOOL "Build the transport-independent trigger command catalog" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_DISPATCH_TESTS AND NOT BUILD_SERVICE_TRIGGER_DISPATCH)
    set(BUILD_SERVICE_TRIGGER_DISPATCH ON CACHE BOOL "Build the transport-independent trigger dispatcher" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_DISPATCH AND NOT BUILD_SERVICE_PROTOCOL)
    set(BUILD_SERVICE_PROTOCOL ON CACHE BOOL "Build the standalone BAAS service protocol library" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_DISPATCH AND NOT BUILD_SERVICE_TRIGGER_CATALOG)
    set(BUILD_SERVICE_TRIGGER_CATALOG ON CACHE BOOL "Build the transport-independent trigger command catalog" FORCE)
endif()

if(BUILD_SERVICE_PIPE_HOST_TESTS AND NOT BUILD_SERVICE_PIPE_HOST)
    set(BUILD_SERVICE_PIPE_HOST ON CACHE BOOL "Build the bounded native Pipe host" FORCE)
endif()

if(BUILD_SERVICE_PIPE_HOST AND NOT BUILD_SERVICE_PROTOCOL)
    set(BUILD_SERVICE_PROTOCOL ON CACHE BOOL "Build the standalone BAAS service protocol library" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_EXECUTOR_TESTS AND NOT BUILD_SERVICE_TRIGGER_EXECUTOR)
    set(BUILD_SERVICE_TRIGGER_EXECUTOR ON CACHE BOOL "Build the bounded trigger execution owner" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_EXECUTOR AND NOT BUILD_SERVICE_TRIGGER_DISPATCH)
    set(BUILD_SERVICE_TRIGGER_DISPATCH ON CACHE BOOL "Build the transport-independent trigger dispatcher" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_EXECUTOR AND NOT BUILD_SERVICE_PROTOCOL)
    set(BUILD_SERVICE_PROTOCOL ON CACHE BOOL "Build the standalone BAAS service protocol library" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_EXECUTOR AND NOT BUILD_SERVICE_TRIGGER_CATALOG)
    set(BUILD_SERVICE_TRIGGER_CATALOG ON CACHE BOOL "Build the transport-independent trigger command catalog" FORCE)
endif()

if(BUILD_SERVICE_ROUTER_TESTS AND NOT BUILD_SERVICE_ROUTER)
    set(BUILD_SERVICE_ROUTER ON CACHE BOOL "Build the transport-independent BAAS service router" FORCE)
endif()

if(BUILD_SERVICE_ORIGIN_POLICY_TESTS AND NOT BUILD_SERVICE_ORIGIN_POLICY)
    set(BUILD_SERVICE_ORIGIN_POLICY ON CACHE BOOL "Build the standalone HTTP Origin/CORS policy" FORCE)
endif()

if(BUILD_SERVICE_HTTP_TESTS AND NOT BUILD_SERVICE_HTTP)
    set(BUILD_SERVICE_HTTP ON CACHE BOOL "Build the optional cpp-httplib service adapter" FORCE)
endif()

if(BUILD_SERVICE_HTTP_TESTS AND NOT BUILD_SERVICE_ORIGIN_POLICY_TESTS)
    set(BUILD_SERVICE_ORIGIN_POLICY_TESTS ON CACHE BOOL "Build native tests for HTTP Origin/CORS policy" FORCE)
endif()

if(BUILD_SERVICE_HTTP AND NOT BUILD_SERVICE_ROUTER)
    set(BUILD_SERVICE_ROUTER ON CACHE BOOL "Build the transport-independent BAAS service router" FORCE)
endif()

if(BUILD_SERVICE_HTTP AND NOT BUILD_SERVICE_ORIGIN_POLICY)
    set(BUILD_SERVICE_ORIGIN_POLICY ON CACHE BOOL "Build the standalone HTTP Origin/CORS policy" FORCE)
endif()

check_app_options(
        BUILD_APP_BAAS
        BUILD_APP_ISA
        BUILD_BAAS_OCR
        BUILD_BAAS_AW_CHECKER
)

if(BUILD_BAAS_NMS_BENCHMARK)
    baas_request_dependencies(benchmark opencv_dnn)
endif()

if(BUILD_APP_BAAS)
    option(BAAS_APP_USE_CUDA "BAAS_APP Use CUDA" OFF)
    baas_request_dependencies(opencv ffmpeg lz4 nlohmann_json cpp_httplib spdlog simdutf benchmark)
    if(BAAS_APP_USE_CUDA)
        baas_request_dependencies(
                onnxruntime PROVIDER cuda
        )
    else()
        baas_request_dependencies(onnxruntime)
    endif()
endif()

if(BUILD_APP_ISA)
    baas_request_dependencies(opencv ffmpeg lz4 nlohmann_json cpp_httplib spdlog simdutf benchmark)
    baas_request_dependencies(
            onnxruntime PROVIDER cuda
    )
endif()

if(BUILD_BAAS_OCR)
    option(BAAS_OCR_SERVER_USE_CUDA "Ocr Server Use CUDA" OFF)
    baas_request_dependencies(opencv nlohmann_json cpp_httplib spdlog simdutf)
    if(BAAS_OCR_SERVER_USE_CUDA)
        baas_request_dependencies(
                onnxruntime PROVIDER cuda
        )
    else()
        baas_request_dependencies(onnxruntime)
    endif()
endif()

if(BUILD_BAAS_AW_CHECKER)
    baas_request_dependencies(opencv nlohmann_json spdlog simdutf)
endif()

if(BUILD_SERVICE_HTTP)
    baas_request_dependencies(cpp_httplib)
endif()

baas_load_requested_dependencies()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include_directories(
        ${BAAS_PROJECT_PATH}/include
)

file(
        GLOB_RECURSE
        BAAS_CORE_HEADERS
        ${CMAKE_CURRENT_LIST_DIR}/include/*.h
)

file(
        GLOB_RECURSE
        BAAS_CORE_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp
)

# Standalone libraries must not leak into the legacy application's globbed
# core sources.
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/script/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/protocol/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/trigger/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/router/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/http/.*\\.cpp$")

if(TARGET_OS_NAME STREQUAL "Windows")
    list(REMOVE_ITEM BAAS_CORE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/simdutf.cpp")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/NmsBenchmark.cmake")

set(BAAS_APP_BAAS_CPP_DIR  "${CMAKE_CURRENT_LIST_DIR}/apps/BAAS")
set(BAAS_APP_ISA_DIR       "${CMAKE_CURRENT_LIST_DIR}/apps/ISA")
set(BAAS_OCR_SERVER_DIR    "${CMAKE_CURRENT_LIST_DIR}/apps/ocr_server")
set(BAAS_AW_CHECKER_DIR    "${CMAKE_CURRENT_LIST_DIR}/apps/BAAS_auto_fight_workflow_checker")

# --------------  BAAS APP  ------------------
if (BUILD_APP_BAAS)
    add_subdirectory(apps/BAAS)
endif()

# --------------  ISA APP  -------------------
if (BUILD_APP_ISA)
    add_subdirectory(apps/ISA)
endif()

# --------------  OCR SERVER  ----------------
if (BUILD_BAAS_OCR)
    add_subdirectory(apps/ocr_server)
endif()

# --------------  BAAS AUTO FIGHT WORKFLOW CHECKER  ----------------
if (BUILD_BAAS_AW_CHECKER)
    add_subdirectory(apps/BAAS_auto_fight_workflow_checker)
endif()

if(BUILD_SCRIPT_RUNTIME)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ScriptRuntime.cmake")
endif()

if(BUILD_SERVICE_TRIGGER_CATALOG)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceTriggerCatalog.cmake")
endif()

if(BUILD_SERVICE_PROTOCOL)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceProtocol.cmake")
endif()

if(BUILD_SERVICE_TRIGGER_DISPATCH)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceTriggerDispatch.cmake")
endif()

if(BUILD_SERVICE_PIPE_HOST)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServicePipeHost.cmake")
endif()

if(BUILD_SERVICE_TRIGGER_EXECUTOR)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceTriggerExecutor.cmake")
endif()

if(BUILD_SERVICE_ROUTER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceRouter.cmake")
endif()

if(BUILD_SERVICE_ORIGIN_POLICY)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceOriginPolicy.cmake")
endif()

if(BUILD_SERVICE_HTTP)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceHttp.cmake")
endif()

if(NOT TARGET_OS_NAME STREQUAL "Android")
    add_library(
            BAAS_ipc
            SHARED
            ${BAAS_PROJECT_PATH}/src/BAASExternalIPC.cpp
    )
endif()

baas_install_required_runtime_resources()
