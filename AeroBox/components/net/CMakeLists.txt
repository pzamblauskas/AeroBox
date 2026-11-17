# Valdymo puslapis: www/index.html.gz sukuria gz_page.py (PlatformIO, prieš kiekvieną kompiliavimą);
# naršyklė gauna suspaustą puslapį, o jei gzip nepriima – originalų (webui.c, page_get).
idf_component_register(SRCS "wifi.c" "portal.c" "webui.c" "adsb_client.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "www/index.html"
                       EMBED_FILES "www/index.html.gz"
                       REQUIRES data
                       PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_client esp_http_server
                                     esp-tls mbedtls json esp_timer lwip sys modes hub75 app_update
                                     bootloader_support esp_app_format espressif__mdns)
