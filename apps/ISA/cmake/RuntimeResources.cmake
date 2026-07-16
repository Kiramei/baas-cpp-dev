baas_require_runtime_resources(
        global_config
        static_config
        adb
        scrcpy_server
        ocr_models
)

get_filename_component(
        _isa_runtime_resource_root
        "${CMAKE_CURRENT_LIST_DIR}/../resource"
        ABSOLUTE
)
baas_copy_local_runtime_resources(
        BASE_DIR "${_isa_runtime_resource_root}"
        ITEMS
        config
        image
        feature
        procedure
)
unset(_isa_runtime_resource_root)
