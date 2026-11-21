/* Sisteminiai ekranai (ne lėktuvų režimai): paleidimas, Wi-Fi nustatymai, pranešimai.
 * Funkcijos tik piešia į gfx_t ir nenaudoja FreeRTOS, todėl jas galima sukompiliuoti ir peržiūrėti kompiuteryje. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

/* Trumpas pranešimas ekrano viduryje, juostoje tarp dviejų linijų (režimas, ryškumas, IP adresas) */
void screen_overlay(gfx_t *g, const char *text);
/* Tas pats, tik raudonas – įspėjimas apie problemą (pvz., „NO WI-FI“) */
void screen_overlay_alert(gfx_t *g, const char *text);
/* Paleidimo ekranas: užrašas „AEROBOX“ ir lėktuvėlis linijoje; p (nuo 0 iki 1) – paleidimo eiga */
void screen_boot(gfx_t *g, float p, bool safe_mode);
/* Wi-Fi nustatymų ekranas (būsena imama iš g_status) */
void screen_portal(gfx_t *g, uint32_t ms);
/* Vieta dar nepasirinkta: virš patamsinto vaizdo juosta „CHOOSE YOUR LOCATION“ ir valdymo puslapio adresas */
void screen_choose_location(gfx_t *g, const char *ip);
