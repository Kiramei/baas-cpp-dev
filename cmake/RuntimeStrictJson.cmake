include_guard(GLOBAL)

if(NOT TARGET BAAS::nlohmann_json)
    message(FATAL_ERROR "RuntimeStrictJson requires BAAS::nlohmann_json")
endif()

add_library(
        BAAS_runtime_strict_json
        STATIC
        "${BAAS_PROJECT_PATH}/src/runtime/json/StrictJson.cpp"
)
add_library(BAAS::runtime_strict_json ALIAS BAAS_runtime_strict_json)
target_compile_features(BAAS_runtime_strict_json PUBLIC cxx_std_20)
target_include_directories(BAAS_runtime_strict_json PUBLIC "${BAAS_PROJECT_PATH}/include")
target_link_libraries(BAAS_runtime_strict_json PUBLIC BAAS::nlohmann_json)
if(MSVC)
    target_compile_options(BAAS_runtime_strict_json PUBLIC /utf-8 PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(BAAS_runtime_strict_json PRIVATE -Wall -Wextra -Wpedantic)
endif()
