idf_component_register(
    SRCS
        "main.c"
        "core/core.c" "core/ui.c" "core/settings.c"
        "features/transit.c" "features/transit_service.c" "features/slides.c"
        "platform/device.c" "platform/storage.c" "platform/oled.c"
        "platform/network.c" "platform/api.c"
    INCLUDE_DIRS "core" "platform" "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES esp_driver_i2c esp_wifi esp_event esp_netif esp_http_client
             esp_http_server esp_timer nvs_flash lwip json
    EMBED_TXTFILES "web/index.html"
)

idf_build_get_property(glance_python PYTHON)
set(glance_config "${CMAKE_CURRENT_SOURCE_DIR}/../config/slides.json")
set(glance_generator "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_slides.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${glance_config}" "${glance_generator}")
execute_process(
    COMMAND "${glance_python}" "${glance_generator}" "${glance_config}"
            "${CMAKE_CURRENT_BINARY_DIR}/glance_config.h"
    COMMAND_ERROR_IS_FATAL ANY
)
