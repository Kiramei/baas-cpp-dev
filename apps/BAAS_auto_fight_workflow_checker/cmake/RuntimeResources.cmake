baas_fetch_resources(yolo_models)

baas_copy_local_runtime_resources(
        BASE_DIR "${BAAS_APP_BAAS_CPP_DIR}/resource/image/CN/zh-cn/image_info"
        DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        ITEMS skill_active.json
)
baas_copy_local_runtime_resources(
        BASE_DIR "${BAAS_FETCHED_RESOURCE_ROOT}/yolo_models"
        DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        ITEMS data.yaml
)
