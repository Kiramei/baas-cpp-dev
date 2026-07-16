baas_require_runtime_resources(
        global_config
        static_config
        adb
        scrcpy_server
        ocr_models
        yolo_models
)

get_filename_component(
        _baas_app_runtime_resource_root
        "${CMAKE_CURRENT_LIST_DIR}/../resource"
        ABSOLUTE
)
baas_copy_local_runtime_resources(
        BASE_DIR "${_baas_app_runtime_resource_root}"
        ITEMS
        config
        image
        feature
        procedure
        auto_fight_workflow
)
unset(_baas_app_runtime_resource_root)
