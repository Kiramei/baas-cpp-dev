include_guard(GLOBAL)

if(NOT TARGET BAAS::nlohmann_json)
    message(FATAL_ERROR
        "Runtime repository trusted plan state requires BAAS::nlohmann_json")
endif()

add_library(
        BAAS_service_runtime_repository_trusted_plan_state
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/app/RuntimeRepositoryTrustedPlanState.cpp"
        "${BAAS_PROJECT_PATH}/include/service/app/RuntimeRepositoryTrustedPlanState.h"
        "${BAAS_PROJECT_PATH}/include/service/adapters/BoundedJson.h"
)
target_compile_features(
        BAAS_service_runtime_repository_trusted_plan_state PUBLIC cxx_std_20
)
target_include_directories(
        BAAS_service_runtime_repository_trusted_plan_state
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(
        BAAS_service_runtime_repository_trusted_plan_state
        PRIVATE BAAS::nlohmann_json
)

if(MSVC)
    target_compile_options(
            BAAS_service_runtime_repository_trusted_plan_state
            PRIVATE /W4 /WX /permissive- /EHsc /utf-8
    )
else()
    target_compile_options(
            BAAS_service_runtime_repository_trusted_plan_state
            PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
endif()
