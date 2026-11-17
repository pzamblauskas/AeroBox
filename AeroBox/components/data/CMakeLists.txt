idf_component_register(SRCS "aircraft_table.c" "route_cache.c" "geo.c" "app_status.c" "weather.c" "track.c" "special.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_timer)
