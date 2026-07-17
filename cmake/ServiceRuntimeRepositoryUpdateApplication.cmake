include_guard(GLOBAL)

foreach(required_target
        BAAS_runtime_repository_git2
        BAAS_service_runtime_repository_plan)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR
            "BAAS runtime repository update application requires ${required_target}")
    endif()
endforeach()

add_library(
        BAAS_service_runtime_repository_update_application
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/RuntimeRepositoryUpdateApplication.cpp"
        "${BAAS_PROJECT_PATH}/include/service/app/RuntimeRepositoryUpdateApplication.h"
)
target_compile_features(
        BAAS_service_runtime_repository_update_application PUBLIC cxx_std_20
)
target_include_directories(
        BAAS_service_runtime_repository_update_application
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_compile_definitions(
        BAAS_service_runtime_repository_update_application
        PUBLIC BAAS_RUNTIME_REPOSITORY_UPDATE_VERSION="${PROJECT_VERSION}"
        PRIVATE BAAS_RUNTIME_REPOSITORY_TRUSTED_PUBLIC_KEY_HEX="${BAAS_RUNTIME_REPOSITORY_TRUSTED_PUBLIC_KEY_HEX}"
)
target_link_libraries(
        BAAS_service_runtime_repository_update_application
        PUBLIC BAAS_service_runtime_repository_plan
        PRIVATE BAAS_runtime_repository_git2
)

add_executable(
        BAAS_runtime_repository_update
        "${BAAS_PROJECT_PATH}/apps/BAAS_runtime_repository_update/main.cpp"
)
target_compile_features(BAAS_runtime_repository_update PRIVATE cxx_std_20)
target_link_libraries(
        BAAS_runtime_repository_update
        PRIVATE BAAS_service_runtime_repository_update_application
)
set_target_properties(
        BAAS_runtime_repository_update
        PROPERTIES OUTPUT_NAME "BAAS_runtime_repository_update"
)

if(MSVC)
    target_compile_options(
            BAAS_service_runtime_repository_update_application
            PRIVATE /W4 /WX /permissive- /EHsc /utf-8
    )
    target_compile_options(
            BAAS_runtime_repository_update
            PRIVATE /W4 /WX /permissive- /EHsc /utf-8
    )
else()
    target_compile_options(
            BAAS_service_runtime_repository_update_application
            PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(
            BAAS_runtime_repository_update
            PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(BUILD_SERVICE_RUNTIME_REPOSITORY_UPDATE_APP_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_runtime_repository_update_application_tests
            "${BAAS_PROJECT_PATH}/tests/service/RuntimeRepositoryUpdateApplicationTests.cpp"
    )
    target_compile_features(
            BAAS_service_runtime_repository_update_application_tests
            PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_service_runtime_repository_update_application_tests
            PRIVATE
            BAAS_service_runtime_repository_update_application
            BAAS_service_auth_crypto
            BAAS::nlohmann_json
    )
    if(MSVC)
        target_compile_options(
                BAAS_service_runtime_repository_update_application_tests
                PRIVATE /W4 /WX /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_service_runtime_repository_update_application_tests
                PRIVATE -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(
            NAME BAAS_service_runtime_repository_update_application_tests
            COMMAND BAAS_service_runtime_repository_update_application_tests
    )
    set_tests_properties(
            BAAS_service_runtime_repository_update_application_tests
            PROPERTIES TIMEOUT 30
    )

    add_test(
            NAME BAAS_runtime_repository_update_version_cli
            COMMAND BAAS_runtime_repository_update --version
    )
    set_tests_properties(
            BAAS_runtime_repository_update_version_cli
            PROPERTIES
            PASS_REGULAR_EXPRESSION
                "BAAS_runtime_repository_update ${PROJECT_VERSION}"
            TIMEOUT 10
    )
    add_test(
            NAME BAAS_runtime_repository_update_help_cli
            COMMAND BAAS_runtime_repository_update --help
    )
    set_tests_properties(
            BAAS_runtime_repository_update_help_cli
            PROPERTIES
            PASS_REGULAR_EXPRESSION
                "Usage: BAAS_runtime_repository_update"
            TIMEOUT 10
    )
endif()
