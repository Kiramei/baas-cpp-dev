include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/BAASConanRuntime.cmake")

function(baas_append_onnxruntime_cuda_provider out_var error_message)
    if(NOT TARGET BAAS::ONNXRuntimeCUDAProvider)
        message(FATAL_ERROR "${error_message}")
    endif()

    set(_libraries ${${out_var}})
    list(APPEND _libraries BAAS::ONNXRuntimeCUDAProvider)
    set(${out_var} ${_libraries} PARENT_SCOPE)
endfunction()

function(baas_append_benchmark_libraries out_var)
    if(NOT TARGET BAAS::benchmark OR NOT TARGET BAAS::benchmark_main)
        message(FATAL_ERROR "BAAS::benchmark package was not found. Run conan install with -o \"&:use_benchmark=True\"")
    endif()

    set(_libraries ${${out_var}})
    list(APPEND _libraries BAAS::benchmark BAAS::benchmark_main)
    set(${out_var} ${_libraries} PARENT_SCOPE)
endfunction()

function(baas_link_runtime_target target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "baas_link_runtime_target target does not exist: ${target}")
    endif()

    cmake_parse_arguments(
            BAAS_LINK_RUNTIME
            ""
            "SCOPE;DESTINATION"
            "LIBRARIES;PACKAGES"
            ${ARGN}
    )

    if(BAAS_LINK_RUNTIME_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unknown baas_link_runtime_target arguments: ${BAAS_LINK_RUNTIME_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT BAAS_LINK_RUNTIME_SCOPE)
        set(BAAS_LINK_RUNTIME_SCOPE PRIVATE)
    endif()

    if(NOT BAAS_LINK_RUNTIME_LIBRARIES)
        message(FATAL_ERROR "baas_link_runtime_target requires LIBRARIES")
    endif()

    LOG_LINE()
    message(STATUS "Conan LIB RAW (${target}) :")
    foreach(_library IN LISTS BAAS_LINK_RUNTIME_LIBRARIES)
        message(STATUS "${_library}")
    endforeach()

    target_link_libraries(
            ${target}
            ${BAAS_LINK_RUNTIME_SCOPE}
            ${BAAS_LINK_RUNTIME_LIBRARIES}
    )

    if(BAAS_LINK_RUNTIME_PACKAGES)
        if(BAAS_LINK_RUNTIME_DESTINATION)
            baas_copy_conan_runtime_dependencies(
                    ${target}
                    DESTINATION "${BAAS_LINK_RUNTIME_DESTINATION}"
                    PACKAGES ${BAAS_LINK_RUNTIME_PACKAGES}
            )
        else()
            baas_copy_conan_runtime_dependencies(
                    ${target}
                    PACKAGES ${BAAS_LINK_RUNTIME_PACKAGES}
            )
        endif()
    endif()
endfunction()
