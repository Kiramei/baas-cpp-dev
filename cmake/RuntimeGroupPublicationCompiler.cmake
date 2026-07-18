include_guard(GLOBAL)

foreach(required_target
        BAAS_resource_core
        BAAS_runtime_strict_json
        BAAS::libgit2
        BAAS::miniz
        BAAS::nlohmann_json)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR
            "RuntimeGroupPublicationCompiler requires ${required_target}")
    endif()
endforeach()

add_library(
    BAAS_runtime_group_publication_compiler
    STATIC
    "${BAAS_PROJECT_PATH}/src/runtime/publisher/GroupPublicationCompiler.cpp"
)
add_library(
    BAAS::runtime_group_publication_compiler
    ALIAS BAAS_runtime_group_publication_compiler
)
target_compile_features(BAAS_runtime_group_publication_compiler PUBLIC cxx_std_20)
target_include_directories(
    BAAS_runtime_group_publication_compiler
    PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
    BAAS_runtime_group_publication_compiler
    PUBLIC BAAS_resource_core BAAS_runtime_strict_json
    PRIVATE BAAS::libgit2 BAAS::miniz BAAS::nlohmann_json
)
if(MSVC)
    target_compile_options(
        BAAS_runtime_group_publication_compiler
        PUBLIC /utf-8 PRIVATE /W4 /WX /permissive- /EHsc
    )
else()
    target_compile_options(
        BAAS_runtime_group_publication_compiler
        PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(NOT (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android"
        OR TARGET_OS_NAME STREQUAL "Android"))
    add_executable(
        baas-runtime-publisher
        "${BAAS_PROJECT_PATH}/apps/baas-runtime-publisher/main.cpp"
    )
    target_compile_features(baas-runtime-publisher PRIVATE cxx_std_20)
    target_link_libraries(
        baas-runtime-publisher PRIVATE BAAS_runtime_group_publication_compiler
    )
    if(MSVC)
        target_compile_options(
            baas-runtime-publisher PRIVATE /utf-8 /W4 /WX /permissive- /EHsc
        )
    else()
        target_compile_options(
            baas-runtime-publisher PRIVATE -Wall -Wextra -Wpedantic -Werror
        )
    endif()
endif()

if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android"
   OR TARGET_OS_NAME STREQUAL "Android")
    add_executable(
        BAAS_runtime_group_publication_compiler_link_probe
        "${BAAS_PROJECT_PATH}/tests/runtime/GroupPublicationCompilerLinkProbe.cpp"
    )
    target_compile_features(
        BAAS_runtime_group_publication_compiler_link_probe PRIVATE cxx_std_20
    )
    target_link_libraries(
        BAAS_runtime_group_publication_compiler_link_probe
        PRIVATE BAAS_runtime_group_publication_compiler
    )
    target_compile_options(
        BAAS_runtime_group_publication_compiler_link_probe
        PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(BUILD_RUNTIME_GROUP_PUBLICATION_COMPILER_TESTS)
    if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android"
       OR TARGET_OS_NAME STREQUAL "Android")
        message(FATAL_ERROR "Group publication compiler tests are host-only")
    endif()
    foreach(required_test_target
            BAAS_runtime_co_detect_support_bundle
            BAAS_runtime_resource_snapshot_loader
            BAAS_runtime_repository)
        if(NOT TARGET ${required_test_target})
            message(FATAL_ERROR
                "Group publication compiler tests require ${required_test_target}")
        endif()
    endforeach()
    include(CTest)
    add_executable(
        BAAS_runtime_group_publication_compiler_tests
        "${BAAS_PROJECT_PATH}/tests/runtime/GroupPublicationCompilerTests.cpp"
    )
    target_compile_features(
        BAAS_runtime_group_publication_compiler_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
        BAAS_runtime_group_publication_compiler_tests
        PRIVATE
        BAAS_runtime_group_publication_compiler
        BAAS_runtime_co_detect_support_bundle
        BAAS_runtime_resource_snapshot_loader
        BAAS_runtime_repository
        BAAS::libgit2
        BAAS::miniz
    )
    if(MSVC)
        target_compile_options(
            BAAS_runtime_group_publication_compiler_tests
            PRIVATE /utf-8 /W4 /WX /permissive- /EHsc
        )
    else()
        target_compile_options(
            BAAS_runtime_group_publication_compiler_tests
            PRIVATE -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(
        NAME BAAS_runtime_group_publication_compiler_tests
        COMMAND BAAS_runtime_group_publication_compiler_tests
    )
    set_tests_properties(
        BAAS_runtime_group_publication_compiler_tests PROPERTIES TIMEOUT 180
    )
endif()
