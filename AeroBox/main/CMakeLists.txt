idf_component_register(SRCS "app_main.c" "screens.c"
                       INCLUDE_DIRS "."
                       REQUIRES hub75 gfx data net modes sys esp_timer app_update)
