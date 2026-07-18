include_guard(GLOBAL)

foreach(required_target
        BAAS_resource_core
        BAAS_runtime_resource_snapshot_loader
        BAAS_runtime_strict_json
        BAAS_runtime_co_detect_definition_model
        BAAS::miniz
        BAAS::OpenCV)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR "RuntimeCoDetectSupportBundle requires ${required_target}")
    endif()
endforeach()

add_library(
        BAAS_runtime_co_detect_support_bundle
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/procedure/CoDetectSupportBundle.cpp"
)
add_library(
        BAAS::runtime_co_detect_support_bundle
        ALIAS BAAS_runtime_co_detect_support_bundle
)
target_compile_features(BAAS_runtime_co_detect_support_bundle PUBLIC cxx_std_20)
target_include_directories(
        BAAS_runtime_co_detect_support_bundle
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_runtime_co_detect_support_bundle
        PUBLIC
        BAAS_runtime_resource_snapshot_loader
        BAAS_runtime_co_detect_definition_model
        BAAS_runtime_strict_json
        PRIVATE
        BAAS_resource_core
        BAAS::miniz
        BAAS::OpenCV
)
if(MSVC)
    target_compile_options(
            BAAS_runtime_co_detect_support_bundle
            PUBLIC /utf-8 PRIVATE /W4 /WX /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_runtime_co_detect_support_bundle
            PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(BUILD_RUNTIME_CO_DETECT_SUPPORT_BUNDLE_TESTS)
    include(CTest)
    add_executable(
            BAAS_runtime_co_detect_support_bundle_tests
            "${BAAS_PROJECT_PATH}/tests/runtime/CoDetectSupportBundleTests.cpp"
    )
    target_compile_features(
            BAAS_runtime_co_detect_support_bundle_tests PRIVATE cxx_std_20
    )
    target_link_libraries(
            BAAS_runtime_co_detect_support_bundle_tests
            PRIVATE BAAS_runtime_co_detect_support_bundle BAAS::miniz BAAS::OpenCV
    )
    if(MSVC)
        target_compile_options(
                BAAS_runtime_co_detect_support_bundle_tests
                PRIVATE /utf-8 /W4 /WX /permissive- /EHsc
        )
    else()
        target_compile_options(
                BAAS_runtime_co_detect_support_bundle_tests
                PRIVATE -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(
            NAME BAAS_runtime_co_detect_support_bundle_tests
            COMMAND BAAS_runtime_co_detect_support_bundle_tests
    )
    set_tests_properties(
            BAAS_runtime_co_detect_support_bundle_tests PROPERTIES TIMEOUT 90
    )
endif()
