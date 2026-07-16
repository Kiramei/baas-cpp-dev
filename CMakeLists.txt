cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)

get_filename_component(BAAS_PROJECT_PATH "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/utils.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/platform.cmake")

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
option(BAAS_FETCH_RESOURCES         "Download legacy BAAS runtime resources during configure" OFF)

# other
option(BUILD_BAAS_OCR           "build the ocr server app"               OFF)
option(BUILD_BAAS_AW_CHECKER    "build BAAS Auto Fight Workflow Checker" OFF)
option(BUILD_BAAS_NMS_BENCHMARK "Build BAAS vs OpenCV dnn NMS Google Benchmark executable" OFF)
option(BUILD_SCRIPT_RUNTIME     "Build the standalone BAAS script runtime" OFF)
option(BUILD_SCRIPT_TESTS       "Build native tests for the BAAS script runtime" OFF)
option(BUILD_SCRIPT_TOOLS       "Build BAAS script developer tools" OFF)
option(BUILD_RESOURCE_CORE      "Build immutable resource snapshot core" OFF)
option(BUILD_RESOURCE_CORE_TESTS "Build immutable resource snapshot tests" OFF)
option(BUILD_RUNTIME_REPOSITORY "Build read-only runtime repository activation" OFF)
option(BUILD_RUNTIME_REPOSITORY_TESTS "Build runtime repository activation tests" OFF)
option(BUILD_RUNTIME_REPOSITORY_UPDATER "Build the transport-independent runtime repository updater" OFF)
option(BUILD_RUNTIME_REPOSITORY_UPDATER_TESTS "Build runtime repository updater tests" OFF)
option(BUILD_RUNTIME_REPOSITORY_GIT2 "Build the optional libgit2 repository dependency boundary" OFF)
option(BUILD_RUNTIME_REPOSITORY_GIT2_TESTS "Build real optional libgit2 repository backend tests" OFF)
option(BUILD_SCRIPT_RESOURCE_HOST "Build baas/resource script Host adapter" OFF)
option(BUILD_SCRIPT_RESOURCE_HOST_TESTS "Build baas/resource script Host tests" OFF)
option(BUILD_SERVICE_PROTOCOL   "Build the standalone BAAS service protocol library" OFF)
option(BUILD_SERVICE_PROTOCOL_TESTS "Build native tests for the BAAS service protocol" OFF)
option(BUILD_SERVICE_TRIGGER_CATALOG "Build the transport-independent trigger command catalog" OFF)
option(BUILD_SERVICE_TRIGGER_CATALOG_TESTS "Build native tests for the trigger command catalog" OFF)
option(BUILD_SERVICE_TRIGGER_DISPATCH "Build the transport-independent trigger dispatcher" OFF)
option(BUILD_SERVICE_TRIGGER_DISPATCH_TESTS "Build native tests for trigger dispatch" OFF)
option(BUILD_SERVICE_PIPE_HOST "Build the bounded native Pipe host" OFF)
option(BUILD_SERVICE_PIPE_HOST_TESTS "Build deterministic tests for the Pipe host" OFF)
option(BUILD_SERVICE_TRIGGER_PIPE_CHANNEL "Build the production Trigger Pipe adapter" OFF)
option(BUILD_SERVICE_TRIGGER_PIPE_CHANNEL_TESTS "Build Trigger Pipe adapter tests" OFF)
option(BUILD_SERVICE_BUSINESS_PIPE_CHANNEL "Build provider/sync/remote Pipe adapters" OFF)
option(BUILD_SERVICE_BUSINESS_PIPE_CHANNEL_TESTS "Build business Pipe adapter tests" OFF)
option(BUILD_SERVICE_TRIGGER_EXECUTOR "Build the bounded trigger execution owner" OFF)
option(BUILD_SERVICE_TRIGGER_EXECUTOR_TESTS "Build native tests for trigger execution ownership" OFF)
option(BUILD_SERVICE_ROUTER     "Build the transport-independent BAAS service router" OFF)
option(BUILD_SERVICE_ROUTER_TESTS "Build native tests for the BAAS service router" OFF)
option(BUILD_SERVICE_ORIGIN_POLICY "Build the standalone HTTP Origin/CORS policy" OFF)
option(BUILD_SERVICE_ORIGIN_POLICY_TESTS "Build native tests for HTTP Origin/CORS policy" OFF)
option(BUILD_SERVICE_HTTP       "Build the optional cpp-httplib service adapter" OFF)
option(BUILD_SERVICE_HTTP_TESTS "Build native tests for the cpp-httplib service adapter" OFF)
option(BUILD_SERVICE_WEBSOCKET "Build the bounded cpp-httplib WebSocket owner" OFF)
option(BUILD_SERVICE_WEBSOCKET_TESTS "Build deterministic WebSocket owner tests" OFF)
option(BUILD_SERVICE_AUTH_CRYPTO "Build the service v1 cryptographic foundation" OFF)
option(BUILD_SERVICE_AUTH_CRYPTO_TESTS "Build service v1 cryptographic contract tests" OFF)
option(BUILD_SERVICE_AUTH_OWNER "Build the persistent service authentication owner" OFF)
option(BUILD_SERVICE_AUTH_OWNER_TESTS "Build deterministic AuthOwner tests" OFF)
option(BUILD_SERVICE_AUTH_HTTP "Build transport-independent HTTP authentication routes" OFF)
option(BUILD_SERVICE_AUTH_HTTP_TESTS "Build HTTP authentication route tests" OFF)
option(BUILD_SERVICE_CONTROL_SESSION "Build the authenticated control WebSocket driver" OFF)
option(BUILD_SERVICE_CONTROL_SESSION_TESTS "Build deterministic control-driver tests" OFF)
option(BUILD_SERVICE_BUSINESS_SESSION "Build authenticated business WebSocket drivers" OFF)
option(BUILD_SERVICE_BUSINESS_SESSION_TESTS "Build deterministic business-driver tests" OFF)
option(BUILD_SERVICE_PROVIDER_HANDLER "Build the transport-independent provider handler" OFF)
option(BUILD_SERVICE_PROVIDER_HANDLER_TESTS "Build deterministic provider-handler tests" OFF)
option(BUILD_SERVICE_PROVIDER_BACKEND "Build the production provider state backend" OFF)
option(BUILD_SERVICE_PROVIDER_BACKEND_TESTS "Build production provider state backend tests" OFF)
option(BUILD_SERVICE_SYNC_HANDLER "Build the transport-independent sync handler" OFF)
option(BUILD_SERVICE_SYNC_HANDLER_TESTS "Build deterministic sync-handler tests" OFF)
option(BUILD_SERVICE_FILE_RESOURCE_STORE "Build the durable JSON file resource store" OFF)
option(BUILD_SERVICE_FILE_RESOURCE_STORE_TESTS "Build file resource store tests" OFF)
option(BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE "Build the file-watched runtime/provider bridge" OFF)
option(BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE_TESTS "Build runtime/provider bridge tests" OFF)
option(BUILD_SERVICE_RUNTIME_REPOSITORY_OWNER "Build the immutable service runtime repository owner" OFF)
option(BUILD_SERVICE_RUNTIME_REPOSITORY_OWNER_TESTS "Build service runtime repository owner tests" OFF)
option(BUILD_SERVICE_RUNTIME_CONFIGURATION_DEFAULTS "Build generation-bound service configuration defaults" OFF)
option(BUILD_SERVICE_ADB_TRANSPORT "Build the standalone ADB smart-socket transport" OFF)
option(BUILD_SERVICE_ADB_TRANSPORT_TESTS "Build deterministic ADB transport tests" OFF)
option(BUILD_SERVICE_ADB_SYNC "Build the bounded production ADB SYNC client" OFF)
option(BUILD_SERVICE_ADB_SYNC_TESTS "Build deterministic ADB SYNC tests" OFF)
option(BUILD_SERVICE_PRODUCTION_HTTP_HOST "Build the production HTTP host composition root" OFF)
option(BUILD_SERVICE_PRODUCTION_HTTP_HOST_TESTS "Build production HTTP host composition tests" OFF)
option(BUILD_SERVICE_REMOTE_HANDLER "Build the transport-independent remote handler" OFF)
option(BUILD_SERVICE_REMOTE_HANDLER_TESTS "Build deterministic remote-handler tests" OFF)
option(BUILD_SERVICE_REMOTE_BACKEND "Build the production ws-scrcpy remote backend" OFF)
option(BUILD_SERVICE_REMOTE_BACKEND_TESTS "Build deterministic ws-scrcpy backend tests" OFF)
option(BUILD_SERVICE_REMOTE_BACKEND_LIVE_SMOKE "Build the opt-in live ws-scrcpy device smoke" OFF)
option(BUILD_SERVICE_TRIGGER_HANDLER "Build the production trigger business handler" OFF)
option(BUILD_SERVICE_TRIGGER_HANDLER_TESTS "Build deterministic trigger-handler tests" OFF)
option(BUILD_SERVICE_STATUS_TRIGGER "Build the production status trigger registration" OFF)
option(BUILD_SERVICE_STATUS_TRIGGER_TESTS "Build deterministic status trigger registration tests" OFF)
option(BUILD_SERVICE_ADB_DISCOVERY_TRIGGER "Build the production detect_adb trigger registration" OFF)
option(BUILD_SERVICE_ADB_DISCOVERY_TRIGGER_TESTS "Build deterministic detect_adb trigger tests" OFF)
option(BUILD_SERVICE_CONFIGURATION_TRIGGERS "Build production configuration trigger registrations" OFF)
option(BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS "Build configuration trigger production tests" OFF)
option(BUILD_SERVICE_COMMAND_LINE "Build the bounded service command-line contract" OFF)
option(BUILD_SERVICE_COMMAND_LINE_TESTS "Build deterministic service command-line tests" OFF)
option(BUILD_SERVICE_SHUTDOWN_COORDINATOR "Build the service main-thread shutdown coordinator" OFF)
option(BUILD_SERVICE_SHUTDOWN_COORDINATOR_TESTS "Build deterministic service shutdown coordinator tests" OFF)
option(BUILD_SERVICE_APP "Build the production BAAS_service executable" OFF)
option(BUILD_SERVICE_APP_TESTS "Build real loopback BAAS_service application tests" OFF)

if(BUILD_SERVICE_APP_TESTS AND NOT BUILD_SERVICE_APP)
    set(BUILD_SERVICE_APP ON CACHE BOOL "Build the production BAAS_service executable" FORCE)
endif()

if(BUILD_SERVICE_APP_TESTS)
    set(BUILD_SERVICE_RUNTIME_REPOSITORY_OWNER_TESTS ON CACHE BOOL "Build service runtime repository owner tests" FORCE)
endif()

if(BUILD_SERVICE_APP)
    set(BUILD_RUNTIME_REPOSITORY ON CACHE BOOL "Build read-only runtime repository activation" FORCE)
    set(BUILD_SERVICE_COMMAND_LINE ON CACHE BOOL "Build the bounded service command-line contract" FORCE)
    set(BUILD_SERVICE_SHUTDOWN_COORDINATOR ON CACHE BOOL "Build the service main-thread shutdown coordinator" FORCE)
    set(BUILD_SERVICE_PRODUCTION_HTTP_HOST ON CACHE BOOL "Build the production HTTP host composition root" FORCE)
    set(BUILD_SERVICE_PROVIDER_BACKEND ON CACHE BOOL "Build the production provider state backend" FORCE)
    set(BUILD_SERVICE_FILE_RESOURCE_STORE ON CACHE BOOL "Build the durable JSON file resource store" FORCE)
    set(BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE ON CACHE BOOL "Build the file-watched runtime/provider bridge" FORCE)
    set(BUILD_SERVICE_RUNTIME_REPOSITORY_OWNER ON CACHE BOOL "Build the immutable service runtime repository owner" FORCE)
    set(BUILD_SERVICE_RUNTIME_CONFIGURATION_DEFAULTS ON CACHE BOOL "Build generation-bound service configuration defaults" FORCE)
    set(BUILD_SERVICE_REMOTE_BACKEND ON CACHE BOOL "Build the production ws-scrcpy remote backend" FORCE)
    set(BUILD_SERVICE_TRIGGER_HANDLER ON CACHE BOOL "Build the production trigger business handler" FORCE)
    set(BUILD_SERVICE_STATUS_TRIGGER ON CACHE BOOL "Build the production status trigger registration" FORCE)
    set(BUILD_SERVICE_ADB_DISCOVERY_TRIGGER ON CACHE BOOL "Build the production detect_adb trigger registration" FORCE)
    set(BUILD_SERVICE_CONFIGURATION_TRIGGERS ON CACHE BOOL "Build production configuration trigger registrations" FORCE)
endif()

if(BUILD_SERVICE_RUNTIME_CONFIGURATION_DEFAULTS AND NOT BUILD_RUNTIME_REPOSITORY)
    set(BUILD_RUNTIME_REPOSITORY ON CACHE BOOL "Build read-only runtime repository activation" FORCE)
endif()

if(BUILD_SERVICE_RUNTIME_REPOSITORY_OWNER_TESTS
   AND NOT BUILD_SERVICE_RUNTIME_REPOSITORY_OWNER)
    set(BUILD_SERVICE_RUNTIME_REPOSITORY_OWNER ON CACHE BOOL "Build the immutable service runtime repository owner" FORCE)
endif()

if(BUILD_SERVICE_RUNTIME_REPOSITORY_OWNER AND NOT BUILD_RUNTIME_REPOSITORY)
    set(BUILD_RUNTIME_REPOSITORY ON CACHE BOOL "Build read-only runtime repository activation" FORCE)
endif()

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

if(BUILD_RESOURCE_CORE_TESTS AND NOT BUILD_RESOURCE_CORE)
    set(BUILD_RESOURCE_CORE ON CACHE BOOL "Build immutable resource snapshot core" FORCE)
endif()

if(BUILD_RUNTIME_REPOSITORY_TESTS AND NOT BUILD_RUNTIME_REPOSITORY)
    set(BUILD_RUNTIME_REPOSITORY ON CACHE BOOL "Build read-only runtime repository activation" FORCE)
endif()

if(BUILD_RUNTIME_REPOSITORY_UPDATER_TESTS AND NOT BUILD_RUNTIME_REPOSITORY_UPDATER)
    set(BUILD_RUNTIME_REPOSITORY_UPDATER ON CACHE BOOL "Build the transport-independent runtime repository updater" FORCE)
endif()

if(BUILD_RUNTIME_REPOSITORY_GIT2_TESTS AND NOT BUILD_RUNTIME_REPOSITORY_GIT2)
    set(BUILD_RUNTIME_REPOSITORY_GIT2 ON CACHE BOOL "Build the optional libgit2 repository backend" FORCE)
endif()

if(BUILD_RUNTIME_REPOSITORY_GIT2 AND NOT BUILD_RUNTIME_REPOSITORY_UPDATER)
    set(BUILD_RUNTIME_REPOSITORY_UPDATER ON CACHE BOOL "Build the transport-independent runtime repository updater" FORCE)
endif()

if(BUILD_RUNTIME_REPOSITORY_UPDATER AND NOT BUILD_RUNTIME_REPOSITORY)
    set(BUILD_RUNTIME_REPOSITORY ON CACHE BOOL "Build read-only runtime repository activation" FORCE)
endif()

if(BUILD_SCRIPT_RESOURCE_HOST_TESTS AND NOT BUILD_SCRIPT_RESOURCE_HOST)
    set(BUILD_SCRIPT_RESOURCE_HOST ON CACHE BOOL "Build baas/resource script Host adapter" FORCE)
endif()

if(BUILD_SCRIPT_RESOURCE_HOST)
    set(BUILD_RESOURCE_CORE ON CACHE BOOL "Build immutable resource snapshot core" FORCE)
    set(BUILD_SCRIPT_RUNTIME ON CACHE BOOL "Build the standalone BAAS script runtime" FORCE)
endif()

if(BUILD_SERVICE_PROTOCOL_TESTS AND NOT BUILD_SERVICE_PROTOCOL)
    set(BUILD_SERVICE_PROTOCOL ON CACHE BOOL "Build the standalone BAAS service protocol library" FORCE)
endif()

if(BUILD_SERVICE_AUTH_CRYPTO_TESTS AND NOT BUILD_SERVICE_AUTH_CRYPTO)
    set(BUILD_SERVICE_AUTH_CRYPTO ON CACHE BOOL "Build the service v1 cryptographic foundation" FORCE)
endif()

if(BUILD_SERVICE_AUTH_OWNER_TESTS AND NOT BUILD_SERVICE_AUTH_OWNER)
    set(BUILD_SERVICE_AUTH_OWNER ON CACHE BOOL "Build the persistent service authentication owner" FORCE)
endif()

if(BUILD_SERVICE_AUTH_OWNER AND NOT BUILD_SERVICE_AUTH_CRYPTO)
    set(BUILD_SERVICE_AUTH_CRYPTO ON CACHE BOOL "Build the service v1 cryptographic foundation" FORCE)
endif()

if(BUILD_SERVICE_AUTH_HTTP_TESTS AND NOT BUILD_SERVICE_AUTH_HTTP)
    set(BUILD_SERVICE_AUTH_HTTP ON CACHE BOOL "Build transport-independent HTTP authentication routes" FORCE)
endif()

if(BUILD_SERVICE_AUTH_HTTP)
    set(BUILD_SERVICE_AUTH_OWNER ON CACHE BOOL "Build the persistent service authentication owner" FORCE)
    set(BUILD_SERVICE_AUTH_CRYPTO ON CACHE BOOL "Build the service v1 cryptographic foundation" FORCE)
    set(BUILD_SERVICE_ROUTER ON CACHE BOOL "Build the transport-independent BAAS service router" FORCE)
endif()

if(BUILD_SERVICE_CONTROL_SESSION_TESTS AND NOT BUILD_SERVICE_CONTROL_SESSION)
    set(BUILD_SERVICE_CONTROL_SESSION ON CACHE BOOL "Build the authenticated control WebSocket driver" FORCE)
endif()

if(BUILD_SERVICE_CONTROL_SESSION)
    set(BUILD_SERVICE_WEBSOCKET ON CACHE BOOL "Build the bounded cpp-httplib WebSocket owner" FORCE)
    set(BUILD_SERVICE_AUTH_OWNER ON CACHE BOOL "Build the persistent service authentication owner" FORCE)
    set(BUILD_SERVICE_AUTH_CRYPTO ON CACHE BOOL "Build the service v1 cryptographic foundation" FORCE)
endif()

if(BUILD_SERVICE_PROVIDER_BACKEND_TESTS AND NOT BUILD_SERVICE_PROVIDER_BACKEND)
    set(BUILD_SERVICE_PROVIDER_BACKEND ON CACHE BOOL "Build the production provider state backend" FORCE)
endif()

if(BUILD_SERVICE_PROVIDER_BACKEND)
    set(BUILD_SERVICE_PROVIDER_HANDLER ON CACHE BOOL "Build the transport-independent provider handler" FORCE)
    set(BUILD_SERVICE_AUTH_CRYPTO ON CACHE BOOL "Build the service v1 cryptographic foundation" FORCE)
endif()

if(BUILD_SERVICE_PROVIDER_HANDLER_TESTS AND NOT BUILD_SERVICE_PROVIDER_HANDLER)
    set(BUILD_SERVICE_PROVIDER_HANDLER ON CACHE BOOL "Build the transport-independent provider handler" FORCE)
endif()

if(BUILD_SERVICE_PROVIDER_HANDLER AND NOT BUILD_SERVICE_BUSINESS_SESSION)
    set(BUILD_SERVICE_BUSINESS_SESSION ON CACHE BOOL "Build authenticated business WebSocket drivers" FORCE)
endif()

if(BUILD_SERVICE_SYNC_HANDLER_TESTS AND NOT BUILD_SERVICE_SYNC_HANDLER)
    set(BUILD_SERVICE_SYNC_HANDLER ON CACHE BOOL "Build the transport-independent sync handler" FORCE)
endif()

if(BUILD_SERVICE_SYNC_HANDLER AND NOT BUILD_SERVICE_BUSINESS_SESSION)
    set(BUILD_SERVICE_BUSINESS_SESSION ON CACHE BOOL "Build authenticated business WebSocket drivers" FORCE)
endif()

if(BUILD_SERVICE_FILE_RESOURCE_STORE_TESTS AND NOT BUILD_SERVICE_FILE_RESOURCE_STORE)
    set(BUILD_SERVICE_FILE_RESOURCE_STORE ON CACHE BOOL "Build the durable JSON file resource store" FORCE)
endif()

if(BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE_TESTS AND NOT BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE)
    set(BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE ON CACHE BOOL "Build the file-watched runtime/provider bridge" FORCE)
endif()

if(BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE)
    set(BUILD_SERVICE_FILE_RESOURCE_STORE ON CACHE BOOL "Build the durable JSON file resource store" FORCE)
    set(BUILD_SERVICE_PROVIDER_BACKEND ON CACHE BOOL "Build the production provider state backend" FORCE)
    set(BUILD_SERVICE_PROVIDER_HANDLER ON CACHE BOOL "Build the transport-independent provider handler" FORCE)
    set(BUILD_SERVICE_AUTH_CRYPTO ON CACHE BOOL "Build the service v1 cryptographic foundation" FORCE)
    set(BUILD_SERVICE_BUSINESS_SESSION ON CACHE BOOL "Build authenticated business WebSocket drivers" FORCE)
    set(BUILD_SERVICE_CONTROL_SESSION ON CACHE BOOL "Build the authenticated control WebSocket driver" FORCE)
    set(BUILD_SERVICE_WEBSOCKET ON CACHE BOOL "Build the bounded cpp-httplib WebSocket owner" FORCE)
    set(BUILD_SERVICE_AUTH_OWNER ON CACHE BOOL "Build the persistent service authentication owner" FORCE)
endif()

if(BUILD_SERVICE_ADB_TRANSPORT_TESTS AND NOT BUILD_SERVICE_ADB_TRANSPORT)
    set(BUILD_SERVICE_ADB_TRANSPORT ON CACHE BOOL "Build the standalone ADB smart-socket transport" FORCE)
endif()

if(BUILD_SERVICE_ADB_SYNC_TESTS AND NOT BUILD_SERVICE_ADB_SYNC)
    set(BUILD_SERVICE_ADB_SYNC ON CACHE BOOL "Build the bounded production ADB SYNC client" FORCE)
endif()

if(BUILD_SERVICE_ADB_SYNC AND NOT BUILD_SERVICE_ADB_TRANSPORT)
    set(BUILD_SERVICE_ADB_TRANSPORT ON CACHE BOOL "Build the standalone ADB smart-socket transport" FORCE)
endif()

if(BUILD_SERVICE_PRODUCTION_HTTP_HOST_TESTS AND NOT BUILD_SERVICE_PRODUCTION_HTTP_HOST)
    set(BUILD_SERVICE_PRODUCTION_HTTP_HOST ON CACHE BOOL "Build the production HTTP host composition root" FORCE)
endif()

if(BUILD_SERVICE_PRODUCTION_HTTP_HOST)
    set(BUILD_SERVICE_AUTH_HTTP ON CACHE BOOL "Build transport-independent HTTP authentication routes" FORCE)
    set(BUILD_SERVICE_BUSINESS_SESSION ON CACHE BOOL "Build authenticated business WebSocket drivers" FORCE)
    set(BUILD_SERVICE_PROVIDER_HANDLER ON CACHE BOOL "Build the transport-independent provider handler" FORCE)
    set(BUILD_SERVICE_SYNC_HANDLER ON CACHE BOOL "Build the transport-independent sync handler" FORCE)
    set(BUILD_SERVICE_HTTP ON CACHE BOOL "Build the optional cpp-httplib service adapter" FORCE)
endif()

if(BUILD_SERVICE_REMOTE_HANDLER_TESTS AND NOT BUILD_SERVICE_REMOTE_HANDLER)
    set(BUILD_SERVICE_REMOTE_HANDLER ON CACHE BOOL "Build the transport-independent remote handler" FORCE)
endif()

if(BUILD_SERVICE_REMOTE_HANDLER AND NOT BUILD_SERVICE_BUSINESS_SESSION)
    set(BUILD_SERVICE_BUSINESS_SESSION ON CACHE BOOL "Build authenticated business WebSocket drivers" FORCE)
endif()

if(BUILD_SERVICE_BUSINESS_PIPE_CHANNEL_TESTS AND NOT BUILD_SERVICE_BUSINESS_PIPE_CHANNEL)
    set(BUILD_SERVICE_BUSINESS_PIPE_CHANNEL ON CACHE BOOL "Build provider/sync/remote Pipe adapters" FORCE)
endif()

if(BUILD_SERVICE_BUSINESS_PIPE_CHANNEL)
    set(BUILD_SERVICE_PIPE_HOST ON CACHE BOOL "Build the bounded native Pipe host" FORCE)
    set(BUILD_SERVICE_BUSINESS_SESSION ON CACHE BOOL "Build authenticated business WebSocket drivers" FORCE)
endif()

if(BUILD_SERVICE_BUSINESS_PIPE_CHANNEL_TESTS)
    set(BUILD_SERVICE_PROVIDER_HANDLER ON CACHE BOOL "Build the transport-independent provider handler" FORCE)
    set(BUILD_SERVICE_PROVIDER_BACKEND ON CACHE BOOL "Build the production provider state backend" FORCE)
    set(BUILD_SERVICE_SYNC_HANDLER ON CACHE BOOL "Build the transport-independent sync handler" FORCE)
    set(BUILD_SERVICE_REMOTE_HANDLER ON CACHE BOOL "Build the transport-independent remote handler" FORCE)
endif()

if(BUILD_SERVICE_REMOTE_BACKEND_TESTS AND NOT BUILD_SERVICE_REMOTE_BACKEND)
    set(BUILD_SERVICE_REMOTE_BACKEND ON CACHE BOOL "Build the production ws-scrcpy remote backend" FORCE)
endif()

if(BUILD_SERVICE_REMOTE_BACKEND_LIVE_SMOKE AND NOT BUILD_SERVICE_REMOTE_BACKEND)
    set(BUILD_SERVICE_REMOTE_BACKEND ON CACHE BOOL "Build the production ws-scrcpy remote backend" FORCE)
endif()

if(BUILD_SERVICE_REMOTE_BACKEND_LIVE_SMOKE AND NOT BUILD_SERVICE_FILE_RESOURCE_STORE)
    set(BUILD_SERVICE_FILE_RESOURCE_STORE ON CACHE BOOL "Build the durable JSON file resource store" FORCE)
endif()

if(BUILD_SERVICE_REMOTE_BACKEND)
    set(BUILD_SERVICE_REMOTE_HANDLER ON CACHE BOOL "Build the transport-independent remote handler" FORCE)
    set(BUILD_SERVICE_BUSINESS_SESSION ON CACHE BOOL "Build authenticated business WebSocket drivers" FORCE)
    set(BUILD_SERVICE_ADB_SYNC ON CACHE BOOL "Build the bounded production ADB SYNC client" FORCE)
    set(BUILD_SERVICE_ADB_TRANSPORT ON CACHE BOOL "Build the standalone ADB smart-socket transport" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_HANDLER_TESTS AND NOT BUILD_SERVICE_TRIGGER_HANDLER)
    set(BUILD_SERVICE_TRIGGER_HANDLER ON CACHE BOOL "Build the production trigger business handler" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_HANDLER)
    set(BUILD_SERVICE_BUSINESS_SESSION ON CACHE BOOL "Build authenticated business WebSocket drivers" FORCE)
    set(BUILD_SERVICE_TRIGGER_EXECUTOR ON CACHE BOOL "Build the bounded trigger execution owner" FORCE)
endif()

if(BUILD_SERVICE_STATUS_TRIGGER_TESTS AND NOT BUILD_SERVICE_STATUS_TRIGGER)
    set(BUILD_SERVICE_STATUS_TRIGGER ON CACHE BOOL "Build the production status trigger registration" FORCE)
endif()

if(BUILD_SERVICE_ADB_DISCOVERY_TRIGGER_TESTS AND NOT BUILD_SERVICE_ADB_DISCOVERY_TRIGGER)
    set(BUILD_SERVICE_ADB_DISCOVERY_TRIGGER ON CACHE BOOL "Build the production detect_adb trigger registration" FORCE)
endif()

if(BUILD_SERVICE_ADB_DISCOVERY_TRIGGER AND NOT BUILD_SERVICE_TRIGGER_DISPATCH)
    set(BUILD_SERVICE_TRIGGER_DISPATCH ON CACHE BOOL "Build the transport-independent trigger dispatcher" FORCE)
endif()

if(BUILD_SERVICE_ADB_DISCOVERY_TRIGGER_TESTS AND NOT BUILD_SERVICE_TRIGGER_EXECUTOR)
    set(BUILD_SERVICE_TRIGGER_EXECUTOR ON CACHE BOOL "Build the bounded trigger execution owner" FORCE)
endif()

if(BUILD_SERVICE_STATUS_TRIGGER AND NOT BUILD_SERVICE_TRIGGER_DISPATCH)
    set(BUILD_SERVICE_TRIGGER_DISPATCH ON CACHE BOOL "Build the transport-independent trigger dispatcher" FORCE)
endif()

if(BUILD_SERVICE_STATUS_TRIGGER_TESTS AND NOT BUILD_SERVICE_TRIGGER_EXECUTOR)
    set(BUILD_SERVICE_TRIGGER_EXECUTOR ON CACHE BOOL "Build the bounded trigger execution owner" FORCE)
endif()

if(BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS AND NOT BUILD_SERVICE_CONFIGURATION_TRIGGERS)
    set(BUILD_SERVICE_CONFIGURATION_TRIGGERS ON CACHE BOOL "Build production configuration trigger registrations" FORCE)
endif()

if(BUILD_SERVICE_CONFIGURATION_TRIGGERS)
    set(BUILD_SERVICE_FILE_RESOURCE_STORE ON CACHE BOOL "Build the durable JSON file resource store" FORCE)
    set(BUILD_SERVICE_TRIGGER_DISPATCH ON CACHE BOOL "Build the transport-independent trigger dispatcher" FORCE)
endif()

if(BUILD_SERVICE_CONFIGURATION_TRIGGER_TESTS AND NOT BUILD_SERVICE_TRIGGER_EXECUTOR)
    set(BUILD_SERVICE_TRIGGER_EXECUTOR ON CACHE BOOL "Build the bounded trigger execution owner" FORCE)
endif()

if(BUILD_SERVICE_COMMAND_LINE_TESTS AND NOT BUILD_SERVICE_COMMAND_LINE)
    set(BUILD_SERVICE_COMMAND_LINE ON CACHE BOOL "Build the bounded service command-line contract" FORCE)
endif()

if(BUILD_SERVICE_SHUTDOWN_COORDINATOR_TESTS AND NOT BUILD_SERVICE_SHUTDOWN_COORDINATOR)
    set(BUILD_SERVICE_SHUTDOWN_COORDINATOR ON CACHE BOOL "Build the service main-thread shutdown coordinator" FORCE)
endif()

if(BUILD_SERVICE_SHUTDOWN_COORDINATOR AND NOT BUILD_SERVICE_ROUTER)
    set(BUILD_SERVICE_ROUTER ON CACHE BOOL "Build the transport-independent BAAS service router" FORCE)
endif()

if(BUILD_SERVICE_BUSINESS_SESSION_TESTS AND NOT BUILD_SERVICE_BUSINESS_SESSION)
    set(BUILD_SERVICE_BUSINESS_SESSION ON CACHE BOOL "Build authenticated business WebSocket drivers" FORCE)
endif()

if(BUILD_SERVICE_BUSINESS_SESSION)
    set(BUILD_SERVICE_CONTROL_SESSION ON CACHE BOOL "Build the authenticated control WebSocket driver" FORCE)
    set(BUILD_SERVICE_WEBSOCKET ON CACHE BOOL "Build the bounded cpp-httplib WebSocket owner" FORCE)
    set(BUILD_SERVICE_AUTH_OWNER ON CACHE BOOL "Build the persistent service authentication owner" FORCE)
    set(BUILD_SERVICE_AUTH_CRYPTO ON CACHE BOOL "Build the service v1 cryptographic foundation" FORCE)
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

if(BUILD_SERVICE_TRIGGER_PIPE_CHANNEL_TESTS AND NOT BUILD_SERVICE_TRIGGER_PIPE_CHANNEL)
    set(BUILD_SERVICE_TRIGGER_PIPE_CHANNEL ON CACHE BOOL "Build the production Trigger Pipe adapter" FORCE)
endif()

if(BUILD_SERVICE_TRIGGER_PIPE_CHANNEL)
    set(BUILD_SERVICE_PIPE_HOST ON CACHE BOOL "Build the bounded native Pipe host" FORCE)
    set(BUILD_SERVICE_TRIGGER_EXECUTOR ON CACHE BOOL "Build the bounded trigger execution owner" FORCE)
endif()

if(BUILD_SERVICE_PIPE_HOST)
    if(NOT BUILD_SERVICE_PROTOCOL)
        set(BUILD_SERVICE_PROTOCOL ON CACHE BOOL "Build the standalone BAAS service protocol library" FORCE)
    endif()
    # ServiceProtocol links the trigger catalog.  Pipe-only configurations must
    # not rely on a different test option to happen to create that target.
    if(NOT BUILD_SERVICE_TRIGGER_CATALOG)
        set(BUILD_SERVICE_TRIGGER_CATALOG ON CACHE BOOL "Build the transport-independent trigger command catalog" FORCE)
    endif()
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

if(BUILD_SERVICE_WEBSOCKET_TESTS AND NOT BUILD_SERVICE_WEBSOCKET)
    set(BUILD_SERVICE_WEBSOCKET ON CACHE BOOL "Build the bounded cpp-httplib WebSocket owner" FORCE)
endif()

if(BUILD_SERVICE_WEBSOCKET_TESTS AND NOT BUILD_SERVICE_HTTP)
    set(BUILD_SERVICE_HTTP ON CACHE BOOL "Build the cpp-httplib host for WebSocket wire tests" FORCE)
endif()

if(BUILD_SERVICE_HTTP AND NOT BUILD_SERVICE_WEBSOCKET)
    set(BUILD_SERVICE_WEBSOCKET ON CACHE BOOL "Build the bounded cpp-httplib WebSocket owner" FORCE)
endif()

if(BUILD_SERVICE_WEBSOCKET AND NOT BUILD_SERVICE_ORIGIN_POLICY)
    set(BUILD_SERVICE_ORIGIN_POLICY ON CACHE BOOL "Build the standalone HTTP Origin/CORS policy" FORCE)
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

# Configure-time download/copy tooling belongs only to the legacy applications.
# Keep one shared registry and one final install pass so each supported legacy
# entry point continues to receive the union of its requested resources.
set(_baas_legacy_resource_tooling_enabled OFF)
if(BUILD_APP_BAAS OR BUILD_APP_ISA OR BUILD_BAAS_OCR OR BUILD_BAAS_AW_CHECKER)
    set(_baas_legacy_resource_tooling_enabled ON)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/BAASResources.cmake")
endif()

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

if(BUILD_SERVICE_HTTP OR BUILD_SERVICE_WEBSOCKET OR BUILD_SERVICE_REMOTE_BACKEND)
    baas_request_dependencies(cpp_httplib)
endif()

if(BUILD_SERVICE_AUTH_CRYPTO OR BUILD_SERVICE_AUTH_OWNER)
    baas_request_dependencies(sodium)
endif()

if(BUILD_SERVICE_SYNC_HANDLER OR BUILD_SERVICE_REMOTE_HANDLER
   OR BUILD_SERVICE_FILE_RESOURCE_STORE
   OR BUILD_SERVICE_RUNTIME_CONFIGURATION_DEFAULTS)
    baas_request_dependencies(nlohmann_json)
endif()

if(BUILD_SERVICE_FILE_RESOURCE_STORE)
    baas_request_dependencies(miniz)
endif()

if(BUILD_RUNTIME_REPOSITORY_GIT2)
    baas_request_dependencies(libgit2 miniz cpp_httplib)
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
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/runtime/repository/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/protocol/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/trigger/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/router/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/http/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/websocket/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/auth/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/adapters/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/adb/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/app/.*\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/channels/SyncHandler\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/channels/RemoteHandler\\.cpp$")
list(FILTER BAAS_CORE_SOURCES EXCLUDE REGEX "/src/service/channels/TriggerHandler\\.cpp$")

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

if(BUILD_RESOURCE_CORE)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ResourceCore.cmake")
endif()

if(BUILD_RUNTIME_REPOSITORY)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/RuntimeRepository.cmake")
endif()

if(BUILD_RUNTIME_REPOSITORY_UPDATER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/RuntimeRepositoryUpdater.cmake")
endif()

if(BUILD_RUNTIME_REPOSITORY_GIT2)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/RuntimeRepositoryGit2.cmake")
endif()

if(BUILD_SCRIPT_RUNTIME)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ScriptRuntime.cmake")
endif()

if(BUILD_SCRIPT_RESOURCE_HOST)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ScriptResourceHost.cmake")
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

if(BUILD_SERVICE_TRIGGER_PIPE_CHANNEL)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceTriggerPipeChannel.cmake")
endif()

if(BUILD_SERVICE_STATUS_TRIGGER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceStatusTrigger.cmake")
endif()

if(BUILD_SERVICE_ADB_DISCOVERY_TRIGGER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceAdbDiscoveryTrigger.cmake")
endif()

if(BUILD_SERVICE_CONFIGURATION_TRIGGERS)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceConfigurationTriggers.cmake")
endif()

if(BUILD_SERVICE_ROUTER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceRouter.cmake")
endif()

if(BUILD_SERVICE_ORIGIN_POLICY)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceOriginPolicy.cmake")
endif()

if(BUILD_SERVICE_WEBSOCKET)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceWebSocket.cmake")
endif()

if(BUILD_SERVICE_AUTH_CRYPTO OR BUILD_SERVICE_AUTH_OWNER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceAuth.cmake")
endif()

if(BUILD_SERVICE_AUTH_HTTP)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceAuthHttp.cmake")
endif()

if(BUILD_SERVICE_CONTROL_SESSION)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceControlSession.cmake")
endif()

if(BUILD_SERVICE_BUSINESS_SESSION)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceBusinessSession.cmake")
endif()

if(BUILD_SERVICE_PROVIDER_HANDLER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceProviderHandler.cmake")
endif()

if(BUILD_SERVICE_PROVIDER_BACKEND)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceProviderBackend.cmake")
endif()

if(BUILD_SERVICE_SYNC_HANDLER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceSyncHandler.cmake")
endif()

if(BUILD_SERVICE_FILE_RESOURCE_STORE)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceFileResourceStore.cmake")
endif()

if(BUILD_SERVICE_RUNTIME_PROVIDER_BRIDGE)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceRuntimeProviderBridge.cmake")
endif()

if(BUILD_SERVICE_RUNTIME_REPOSITORY_OWNER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceRuntimeRepositoryOwner.cmake")
endif()

if(BUILD_SERVICE_RUNTIME_CONFIGURATION_DEFAULTS)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceRuntimeConfigurationDefaults.cmake")
endif()

if(BUILD_SERVICE_ADB_TRANSPORT)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceAdbTransport.cmake")
endif()

if(BUILD_SERVICE_ADB_SYNC)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceAdbSync.cmake")
endif()

if(BUILD_SERVICE_REMOTE_HANDLER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceRemoteHandler.cmake")
endif()

if(BUILD_SERVICE_BUSINESS_PIPE_CHANNEL)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceBusinessPipeChannel.cmake")
endif()

if(BUILD_SERVICE_REMOTE_BACKEND)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceRemoteBackend.cmake")
endif()

if(BUILD_SERVICE_TRIGGER_HANDLER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceTriggerHandler.cmake")
endif()

if(BUILD_SERVICE_HTTP)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceHttp.cmake")
endif()

if(BUILD_SERVICE_PRODUCTION_HTTP_HOST)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceProductionHttpHost.cmake")
endif()

if(BUILD_SERVICE_COMMAND_LINE)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceCommandLine.cmake")
endif()

if(BUILD_SERVICE_SHUTDOWN_COORDINATOR)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceShutdownCoordinator.cmake")
endif()

if(BUILD_SERVICE_APP)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/ServiceApplication.cmake")
endif()

if(NOT TARGET_OS_NAME STREQUAL "Android")
    add_library(
            BAAS_ipc
            SHARED
            ${BAAS_PROJECT_PATH}/src/BAASExternalIPC.cpp
    )
endif()

if(_baas_legacy_resource_tooling_enabled)
    baas_install_required_runtime_resources()
endif()
