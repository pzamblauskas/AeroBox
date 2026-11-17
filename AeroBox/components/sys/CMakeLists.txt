idf_component_register(SRCS "settings.c" "button.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nvs_flash driver)