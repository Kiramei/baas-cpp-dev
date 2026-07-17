include_guard(GLOBAL)

if(TARGET_OS_NAME STREQUAL "Android")
    message(FATAL_ERROR
        "The pure WebUI BAAS_service process supervisor is desktop-only")
endif()

find_package(Threads REQUIRED)

add_library(
        BAAS_webui_service_process_owner
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/supervisor/ServiceProcessOwner.cpp"
        "${BAAS_PROJECT_PATH}/include/service/supervisor/ServiceProcessOwner.h"
)
target_compile_features(
        BAAS_webui_service_process_owner PUBLIC cxx_std_20
)
target_include_directories(
        BAAS_webui_service_process_owner
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_webui_service_process_owner PUBLIC Threads::Threads
)

if(MSVC)
    target_compile_options(
            BAAS_webui_service_process_owner
            PRIVATE /W4 /WX /permissive- /EHsc /utf-8
    )
elseif(APPLE)
    target_compile_options(
            BAAS_webui_service_process_owner
            PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
else()
    target_compile_definitions(
            BAAS_webui_service_process_owner PRIVATE _GNU_SOURCE
    )
    target_compile_options(
            BAAS_webui_service_process_owner
            PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(BUILD_WEBUI_SERVICE_SUPERVISOR_TESTS)
    include(CTest)
    add_executable(
            BAAS_webui_service_process_test_child
            "${BAAS_PROJECT_PATH}/tests/service/ServiceProcessTestChild.cpp"
    )
    target_compile_features(
            BAAS_webui_service_process_test_child PRIVATE cxx_std_20
    )
    set_target_properties(
            BAAS_webui_service_process_test_child
            PROPERTIES OUTPUT_NAME "BAAS_service_process_test_child"
    )

    add_executable(
            BAAS_webui_service_process_owner_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceProcessOwnerTests.cpp"
    )
    target_compile_features(
            BAAS_webui_service_process_owner_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_webui_service_process_owner_tests
            PRIVATE BAAS_webui_service_process_owner Threads::Threads
    )
    add_dependencies(
            BAAS_webui_service_process_owner_tests
            BAAS_webui_service_process_test_child
    )

    foreach(target
            BAAS_webui_service_process_test_child
            BAAS_webui_service_process_owner_tests)
        if(MSVC)
            target_compile_options(
                    ${target} PRIVATE /W4 /WX /permissive- /EHsc /utf-8
            )
        else()
            target_compile_options(
                    ${target} PRIVATE -Wall -Wextra -Wpedantic -Werror
            )
        endif()
    endforeach()

    add_test(
            NAME BAAS_webui_service_process_owner_tests
            COMMAND BAAS_webui_service_process_owner_tests
                    "$<TARGET_FILE:BAAS_webui_service_process_test_child>"
    )
    set_tests_properties(
            BAAS_webui_service_process_owner_tests PROPERTIES TIMEOUT 45
    )
endif()
