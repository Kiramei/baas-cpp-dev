if(NOT DEFINED SERVICE_BINARY OR NOT EXISTS "${SERVICE_BINARY}")
    message(FATAL_ERROR "BAAS_service binary is missing: ${SERVICE_BINARY}")
endif()

file(READ "${SERVICE_BINARY}" service_binary HEX)
string(TOLOWER "${service_binary}" service_binary)

set(forbidden_business_payloads
    "\"purchase_arena_ticket_times\":\"0\",\"screenshot_interval\":\"0.3\",\"autostart\":false"
    "\"current_game_activity\":{\"CN\":\"PandemicHazardAMiraclePancake\""
    "\"PC_app_process_name\":{\"Global\":\"Blue Archive\""
    "\"cafe_reward_invite1_criterion\":\"starred\""
)

foreach(payload IN LISTS forbidden_business_payloads)
    string(HEX "${payload}" payload_hex)
    string(TOLOWER "${payload_hex}" payload_hex)
    string(FIND "${service_binary}" "${payload_hex}" payload_position)
    if(NOT payload_position EQUAL -1)
        message(FATAL_ERROR
            "BAAS_service contains a compiled runtime configuration payload")
    endif()
endforeach()

message(STATUS "BAAS_service contains no compiled runtime configuration payload")
