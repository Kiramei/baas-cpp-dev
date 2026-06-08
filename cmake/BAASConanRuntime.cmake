include_guard(GLOBAL)

function(_baas_collect_runtime_dirs_from_target output target)
    set(_runtime_dirs "")
    set(_visited ${ARGN})
    if("${target}" IN_LIST _visited)
        set(${output} "" PARENT_SCOPE)
        return()
    endif()
    list(APPEND _visited "${target}")

    if(NOT TARGET ${target})
        set(${output} "" PARENT_SCOPE)
        return()
    endif()

    get_target_property(_imported_configs ${target} IMPORTED_CONFIGURATIONS)
    set(_configs RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    if(_imported_configs)
        list(APPEND _configs ${_imported_configs})
    endif()
    list(REMOVE_DUPLICATES _configs)

    foreach(_config IN LISTS _configs)
        string(TOUPPER "${_config}" _config_upper)
        foreach(_property IMPORTED_LOCATION IMPORTED_IMPLIB)
            get_target_property(_location ${target} ${_property}_${_config_upper})
            if(_location AND EXISTS "${_location}")
                get_filename_component(_runtime_dir "${_location}" DIRECTORY)
                list(APPEND _runtime_dirs "${_runtime_dir}")
            endif()
        endforeach()
    endforeach()

    foreach(_property IMPORTED_LOCATION IMPORTED_IMPLIB)
        get_target_property(_location ${target} ${_property})
        if(_location AND EXISTS "${_location}")
            get_filename_component(_runtime_dir "${_location}" DIRECTORY)
            list(APPEND _runtime_dirs "${_runtime_dir}")
        endif()
    endforeach()

    foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_linked_libraries ${target} ${_property})
        if(NOT _linked_libraries)
            continue()
        endif()
        foreach(_linked_library IN LISTS _linked_libraries)
            if(TARGET ${_linked_library})
                _baas_collect_runtime_dirs_from_target(_linked_runtime_dirs ${_linked_library} ${_visited})
                list(APPEND _runtime_dirs ${_linked_runtime_dirs})
            elseif(IS_ABSOLUTE "${_linked_library}" AND EXISTS "${_linked_library}")
                get_filename_component(_runtime_dir "${_linked_library}" DIRECTORY)
                list(APPEND _runtime_dirs "${_runtime_dir}")
            else()
                string(
                        REGEX
                        MATCHALL
                        "[A-Za-z0-9_.+-]+::[A-Za-z0-9_.+-]+"
                        _linked_targets
                        "${_linked_library}"
                )
                foreach(_linked_target IN LISTS _linked_targets)
                    if(TARGET ${_linked_target})
                        _baas_collect_runtime_dirs_from_target(_linked_runtime_dirs ${_linked_target} ${_visited})
                        list(APPEND _runtime_dirs ${_linked_runtime_dirs})
                    endif()
                endforeach()
            endif()
        endforeach()
    endforeach()

    if(_runtime_dirs)
        list(REMOVE_DUPLICATES _runtime_dirs)
    endif()
    set(${output} ${_runtime_dirs} PARENT_SCOPE)
endfunction()

function(baas_copy_conan_runtime_dependencies target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "baas_copy_conan_runtime_dependencies target does not exist: ${target}")
    endif()

    cmake_parse_arguments(
            BAAS_COPY_CONAN_RUNTIME
            ""
            "DESTINATION"
            "PACKAGES"
            ${ARGN}
    )
    if(BAAS_COPY_CONAN_RUNTIME_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
                "Unknown baas_copy_conan_runtime_dependencies arguments: "
                "${BAAS_COPY_CONAN_RUNTIME_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT BAAS_COPY_CONAN_RUNTIME_PACKAGES)
        message(FATAL_ERROR "baas_copy_conan_runtime_dependencies requires PACKAGES")
    endif()

    if(BAAS_COPY_CONAN_RUNTIME_DESTINATION)
        set(_destination "${BAAS_COPY_CONAN_RUNTIME_DESTINATION}")
    else()
        set(_destination "$<TARGET_FILE_DIR:${target}>")
    endif()

    set(_runtime_dirs "")
    _baas_collect_runtime_dirs_from_target(_target_runtime_dirs ${target})
    list(APPEND _runtime_dirs ${_target_runtime_dirs})

    foreach(_package IN LISTS BAAS_COPY_CONAN_RUNTIME_PACKAGES)
        foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            foreach(_kind BIN_DIRS LIB_DIRS)
                set(_var "${_package}_${_kind}_${_config}")
                if(DEFINED ${_var})
                    list(APPEND _runtime_dirs ${${_var}})
                endif()
            endforeach()
        endforeach()
    endforeach()
    if(_runtime_dirs)
        list(REMOVE_DUPLICATES _runtime_dirs)
        list(JOIN _runtime_dirs "::BAAS_RUNTIME_DIR::" _runtime_dirs_arg)
    endif()

    add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                    "-Ddirectories=${_runtime_dirs_arg}"
                    "-Ddestination=${_destination}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/copy_conan_runtime_deps.cmake"
            VERBATIM
    )
endfunction()
