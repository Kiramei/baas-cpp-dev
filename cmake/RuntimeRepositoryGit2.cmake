include_guard(GLOBAL)

if(NOT TARGET BAAS::libgit2)
    message(FATAL_ERROR
            "BUILD_RUNTIME_REPOSITORY_GIT2=ON requires the optional Conan dependency. "
            "Run conan install with -o \"&:use_libgit2=True\" before configuring CMake.")
endif()

if(NOT TARGET BAAS::miniz)
    message(FATAL_ERROR "BUILD_RUNTIME_REPOSITORY_GIT2=ON requires BAAS::miniz for pack preflight")
endif()

if(NOT TARGET BAAS_runtime_repository_updater)
    message(FATAL_ERROR "The libgit2 backend requires BAAS_runtime_repository_updater")
endif()

add_library(
        BAAS_runtime_repository_git2
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/repository/RuntimeRepositoryGit2.cpp"
        "${BAAS_PROJECT_PATH}/include/runtime/repository/RuntimeRepositoryGit2.h"
)
target_compile_features(BAAS_runtime_repository_git2 PUBLIC cxx_std_20)
target_include_directories(BAAS_runtime_repository_git2 PUBLIC "${BAAS_PROJECT_PATH}/include")
target_link_libraries(
        BAAS_runtime_repository_git2
        PUBLIC BAAS_runtime_repository_updater
        PRIVATE BAAS::libgit2 BAAS::miniz
)

if(BUILD_RUNTIME_REPOSITORY_GIT2_TESTS)
    target_compile_definitions(
            BAAS_runtime_repository_git2
            PRIVATE BAAS_RUNTIME_REPOSITORY_GIT2_TESTING=1
    )
endif()

if(MSVC)
    target_compile_options(BAAS_runtime_repository_git2 PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(BAAS_runtime_repository_git2 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    include(CTest)
    add_executable(
            BAAS_runtime_repository_git2_link_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/RuntimeRepositoryGit2LinkTests.cpp"
    )
    target_compile_features(BAAS_runtime_repository_git2_link_tests PRIVATE cxx_std_20)
    target_link_libraries(
            BAAS_runtime_repository_git2_link_tests
            PRIVATE BAAS_runtime_repository_git2
    )
    if(MSVC)
        target_compile_options(
                BAAS_runtime_repository_git2_link_tests
                PRIVATE /W4 /permissive- /EHsc /utf-8
        )
    else()
        target_compile_options(
                BAAS_runtime_repository_git2_link_tests
                PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
            NAME BAAS_runtime_repository_git2_link_tests
            COMMAND BAAS_runtime_repository_git2_link_tests
    )
    set_tests_properties(BAAS_runtime_repository_git2_link_tests PROPERTIES TIMEOUT 15)

    if(BUILD_RUNTIME_REPOSITORY_GIT2_TESTS)
        add_executable(
                BAAS_runtime_repository_git2_backend_tests
                "${BAAS_PROJECT_PATH}/tests/runtime/RuntimeRepositoryGit2BackendTests.cpp"
        )
        target_compile_features(BAAS_runtime_repository_git2_backend_tests PRIVATE cxx_std_20)
        target_compile_definitions(
                BAAS_runtime_repository_git2_backend_tests
                PRIVATE BAAS_RUNTIME_REPOSITORY_GIT2_TESTING=1
        )
        target_link_libraries(
                BAAS_runtime_repository_git2_backend_tests
                PRIVATE BAAS_runtime_repository_git2 BAAS::libgit2 BAAS::miniz
        )
        if(MSVC)
            target_compile_options(
                    BAAS_runtime_repository_git2_backend_tests
                    PRIVATE /W4 /permissive- /EHsc /utf-8
            )
        else()
            target_compile_options(
                    BAAS_runtime_repository_git2_backend_tests
                    PRIVATE -Wall -Wextra -Wpedantic
            )
        endif()
        add_test(
                NAME BAAS_runtime_repository_git2_backend_tests
                COMMAND BAAS_runtime_repository_git2_backend_tests
        )
        set_tests_properties(BAAS_runtime_repository_git2_backend_tests PROPERTIES TIMEOUT 180)
    endif()
endif()
