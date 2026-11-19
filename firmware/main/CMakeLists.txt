idf_component_register(
    SRCS "main.c" "light.c" "button.c" "settings.c" "zb_dimmer.c" "zb_ota.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash esp_driver_ledc esp_driver_gpio esp_timer esp_app_format app_update
)
