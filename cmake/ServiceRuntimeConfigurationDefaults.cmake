include_guard(GLOBAL)

if(NOT TARGET BAAS_runtime_repository)
    message(FATAL_ERROR
        "BAAS_service_runtime_configuration_defaults requires BAAS_runtime_repository")
endif()

add_library(
        BAAS_service_runtime_configuration_defaults
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/RuntimeConfigurationDefaults.cpp"
        "${BAAS_PROJECT_PATH}/include/service/app/RuntimeConfigurationDefaults.h"
        "${BAAS_PROJECT_PATH}/include/service/adapters/ConfigurationDefaults.h"
        "${BAAS_PROJECT_PATH}/include/service/adapters/BoundedJson.h"
)
target_compile_features(
        BAAS_service_runtime_configuration_defaults PUBLIC cxx_std_20
)
target_include_directories(
        BAAS_service_runtime_configuration_defaults
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_runtime_configuration_defaults
        PUBLIC BAAS_runtime_repository
        PRIVATE BAAS::nlohmann_json
)

if(MSVC)
    target_compile_options(
            BAAS_service_runtime_configuration_defaults
            PRIVATE /W4 /permissive- /EHsc /utf-8
    )
else()
    target_compile_options(
            BAAS_service_runtime_configuration_defaults
            PRIVATE -Wall -Wextra -Wpedantic
    )
endif()
