set(srcs "mode_manager.c" "flight_card.c" "radar.c" "clock.c" "track_view.c" "custom.c" "theme.c" "overhead.c"
         "route_map.c" "land.c" "borders.c" "routes_view.c" "demo.c")
# FlightWall variantas (Kconfig SKY_FLIGHTWALL): kortelė su logotipais; logotipai (~150 KB) kompiliuojami tik į jį
if(CONFIG_SKY_FLIGHTWALL)
    list(APPEND srcs "fw_card.c" "logos.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       REQUIRES gfx data)
if(CONFIG_SKY_FLIGHTWALL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SKY_FLIGHTWALL=1)
endif()
