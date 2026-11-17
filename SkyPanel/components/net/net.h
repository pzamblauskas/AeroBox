/* Tinklo modulis: Wi-Fi (wifi.c), nustatymų tinklas (portal.c), valdymo puslapis (webui.c)
 * ir duomenų gavimas (adsb_client.c). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Wi-Fi ir SNTP inicializacija (be prisijungimo). Prieš tai turi būti iškviesta nvs_flash_init(). */
void net_init(const char *tz);
/* Jungiasi prie namų Wi-Fi; atsijungus bandoma vėl, vis ilgesniais tarpais */
void net_wifi_connect(const char *ssid, const char *pass);
bool net_wifi_wait(uint32_t timeout_ms);

/* Nustatymų tinklas „AeroBox“ (192.168.4.1) su DNS, kad telefonas pats atidarytų valdymo puslapį.
 * Prie namų Wi-Fi (jei jis nustatytas) fone ir toliau bandoma prisijungti. */
void net_portal_start(void);
void net_portal_stop(void);

/* Valdymo puslapis: nustatymų tinkle – http://192.168.4.1, namų tinkle – http://aerobox.local.
 * Abiem atvejais veikia tas pats HTTP serveris; jis paleidžiamas vieną kartą. */
void net_webui_start(void);

/* FreeRTOS užduotis: lėktuvai (AeroBox serveris arba adsb.fi / adsb.lol), sekamas skrydis, maršrutai (adsbdb),
 * orai (Open-Meteo), programos atnaujinimai */
void net_task(void *arg);
/* Pasikeitus vietai – orus parsiųsti iš naujo (senieji išvalomi) */
void net_refresh_weather(void);
/* Nebesekti skrydžio (kaip „Stop“ valdymo puslapyje): grąžinamas ankstesnis vaizdas, išsaugoma NVS */
void net_track_stop(void);
/* Įdiegti serverio pasiūlytą versiją (g_status.upd_ver): failas parsiunčiamas, tikrinamas SHA-256, įrenginys
 * perkraunamas. Diegia net_task (kad vienu metu nebūtų dviejų TLS ryšių). false – nėra ką diegti arba jau diegiama. */
bool net_update_start(void);
