include_guard(GLOBAL)

foreach(required_target
        BAAS_runtime_co_detect_executor
        BAAS_runtime_co_detect_support_bundle
        BAAS::OpenCV)
    if(NOT TARGET ${required_target})
        message(FATAL_ERROR "RuntimeCoDetectProductionAdapter requires ${required_target}")
    endif()
endforeach()

add_library(BAAS_runtime_co_detect_production_adapter STATIC
    "${BAAS_PROJECT_PATH}/src/runtime/procedure/CoDetectProductionAdapter.cpp")
add_library(BAAS::runtime_co_detect_production_adapter
    ALIAS BAAS_runtime_co_detect_production_adapter)
target_compile_features(BAAS_runtime_co_detect_production_adapter PUBLIC cxx_std_20)
target_include_directories(BAAS_runtime_co_detect_production_adapter
    PUBLIC "${BAAS_PROJECT_PATH}/include")
target_link_libraries(BAAS_runtime_co_detect_production_adapter
    PUBLIC BAAS_runtime_co_detect_executor BAAS_runtime_co_detect_support_bundle
    PRIVATE BAAS::OpenCV)
if(MSVC)
    target_compile_options(BAAS_runtime_co_detect_production_adapter
        PUBLIC /utf-8 PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(BAAS_runtime_co_detect_production_adapter
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_RUNTIME_CO_DETECT_PRODUCTION_ADAPTER_TESTS)
    include(CTest)
    add_executable(BAAS_runtime_co_detect_production_adapter_tests
        "${BAAS_PROJECT_PATH}/tests/runtime/CoDetectProductionAdapterFixture.cpp"
        "${BAAS_PROJECT_PATH}/tests/runtime/CoDetectProductionAdapterTests.cpp")
    target_compile_features(BAAS_runtime_co_detect_production_adapter_tests PRIVATE cxx_std_20)
    target_include_directories(BAAS_runtime_co_detect_production_adapter_tests
        PRIVATE "${BAAS_PROJECT_PATH}/tests/runtime")
    target_link_libraries(BAAS_runtime_co_detect_production_adapter_tests
        PRIVATE BAAS_runtime_co_detect_production_adapter BAAS::miniz BAAS::OpenCV)
    if(MSVC)
        target_compile_options(BAAS_runtime_co_detect_production_adapter_tests
            PRIVATE /utf-8 /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(BAAS_runtime_co_detect_production_adapter_tests
            PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME BAAS_runtime_co_detect_production_adapter_tests
        COMMAND BAAS_runtime_co_detect_production_adapter_tests)
    set_tests_properties(BAAS_runtime_co_detect_production_adapter_tests
        PROPERTIES TIMEOUT 90)
endif()
