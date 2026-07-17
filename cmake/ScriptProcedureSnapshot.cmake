include_guard(GLOBAL)

if(NOT TARGET BAAS_resource_core)
    message(FATAL_ERROR "BAAS procedure snapshot requires resource core")
endif()

add_library(
        BAAS_script_procedure_snapshot
        STATIC
        "${BAAS_PROJECT_PATH}/src/script/host/ProcedureSnapshot.cpp"
)
add_library(BAAS::script_procedure_snapshot ALIAS BAAS_script_procedure_snapshot)
target_compile_features(BAAS_script_procedure_snapshot PUBLIC cxx_std_20)
target_include_directories(
        BAAS_script_procedure_snapshot
        PUBLIC "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(BAAS_script_procedure_snapshot PUBLIC BAAS_resource_core)
if(MSVC)
    target_compile_options(
            BAAS_script_procedure_snapshot PUBLIC /utf-8 PRIVATE /W4 /permissive- /EHsc
    )
else()
    target_compile_options(
            BAAS_script_procedure_snapshot PRIVATE -Wall -Wextra -Wpedantic
    )
endif()
