if(NOT DEFINED destination OR destination STREQUAL "")
    message(FATAL_ERROR "copy_conan_runtime_deps.cmake requires destination")
endif()

if(NOT DEFINED directories OR directories STREQUAL "")
    return()
endif()

set(_runtime_files "")
string(REPLACE "::BAAS_RUNTIME_DIR::" ";" _runtime_dirs "${directories}")
foreach(_runtime_dir IN LISTS _runtime_dirs)
    if(NOT EXISTS "${_runtime_dir}")
        continue()
    endif()
    file(
            GLOB
            _package_runtime_files
            LIST_DIRECTORIES false
            "${_runtime_dir}/*.dll"
            "${_runtime_dir}/*.so"
            "${_runtime_dir}/*.so.*"
            "${_runtime_dir}/*.dylib"
            "${_runtime_dir}/*.dylib.*"
    )
    list(APPEND _runtime_files ${_package_runtime_files})
endforeach()

if(NOT _runtime_files)
    return()
endif()

list(REMOVE_DUPLICATES _runtime_files)
foreach(_runtime_file IN LISTS _runtime_files)
    if(IS_DIRECTORY "${_runtime_file}")
        continue()
    endif()
    if(EXISTS "${_runtime_file}")
        get_filename_component(_runtime_name "${_runtime_file}" NAME)
        file(COPY_FILE "${_runtime_file}" "${destination}/${_runtime_name}" ONLY_IF_DIFFERENT)
    endif()
endforeach()
