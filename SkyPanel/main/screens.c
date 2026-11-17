/* Sisteminiai ekranai: paleidimas, Wi-Fi nustatymai, trumpi pranešimai ir pasisveikinimas po Wi-Fi nustatymo.
 * Visi ekrane rodomi tekstai yra čia, šiame faile. */
#include "screens.h"
#include "app_status.h"
#include "mode_util.h"
#include "mode.h"
#include <stdio.h>
#include <string.h>

#define C_ACCENT_DIM RGB565(120, 66, 0)     /* pritemdyta gintaro spalva – pranešimų juostos linijos */
#define C_ALERT_DIM  RGB565(110, 10, 10)    /* pritemdyta raudona – įspėjimo juostos linijos */
#define C_YELLOW     RGB565(255, 196, 0)    /* „BOX“ ir nuskrista linijos dalis paleidimo ekrane */
#define C_FAINT      RGB565(60, 60, 66)     /* versijos numeris paleidimo ekrane */

/* Pranešimas per visą ekrano plotį: fonas patamsinamas iki 40 %, tekstas rodomas juostoje tarp dviejų linijų */
static void overlay(gfx_t *g, const char *text, uint16_t text_c, uint16_t line_c)
{
    for (size_t i = 0, n = (size_t)g->w * g->h; i < n; i++)
        if (g->buf[i]) g->buf[i] = gfx_scale(g->buf[i], 40);
    const int sc = (g->w >= 128 && gfx_text_width_s(text, 2) + 8 <= g->w) ? 2 : 1;   /* jei telpa – dvigubas dydis */
    const int pad = sc == 2 ? 4 : 2;
    const int h = gfx_font_h() * sc + 2 * pad;
    const int y = (g->h - h) / 2;
    gfx_fill_rect(g, 0, y - 1, g->w, h + 2, C_BLACK);
    gfx_hline(g, 0, y - 1, g->w, line_c);
    gfx_hline(g, 0, y + h, g->w, line_c);
    gfx_text_center_fit(g, y + pad, text, text_c, sc);
}

void screen_overlay(gfx_t *g, const char *text)
{
    overlay(g, text, C_WHITE, C_ACCENT_DIM);
}

void screen_overlay_alert(gfx_t *g, const char *text)
{
    overlay(g, text, C_RED, C_ALERT_DIM);
}

/* Paleidimo ekranas – pirmas vaizdas įjungus dėžutę.
 * Viršuje užrašas „AEROBOX“ („AERO“ baltai, „BOX“ geltonai), po juo linija, kuria skrenda lėktuvėlis:
 * p (nuo 0 iki 1) rodo, kiek paleidimo jau atlikta. Apačioje – programos versija (SKY_FW_VERSION,
 * app_status.h) arba raudonas „SAFE MODE“. Užrašui pakeisti užtenka pakeisti eilutes „AERO“ ir „BOX“. */
void screen_boot(gfx_t *g, float p, bool safe_mode)
{
    const bool big = g->w >= 128 && g->h >= 64;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    const int sc = big ? 2 : 1;
    const int w1 = gfx_text_width_s("AERO", sc), w2 = gfx_text_width_s("BOX", sc), gap = big ? 2 : 1;
    const int tx = (g->w - w1 - gap - w2) / 2, ty = big ? 12 : 6;   /* abu žodžiai kartu centruojami */
    gfx_text_s(g, tx, ty, "AERO", C_WHITE, sc);
    gfx_text_s(g, tx + w1 + gap, ty, "BOX", C_YELLOW, sc);

    /* Linija rodo eigą kaip skrydį: nuskrista dalis geltona, lėktuvėlis baltas */
    const int x0 = big ? 16 : 6, x1 = g->w - x0, ly = big ? 41 : 21;
    const int px = x0 + (int)(p * (float)(x1 - x0) + 0.5f);
    gfx_hline(g, x0, ly, x1 - x0, C_DIM);
    gfx_hline(g, x0, ly, px - x0, C_YELLOW);
    draw_kite(g, px, ly, 90.0f, C_WHITE, big ? 1.0f : 0.75f);

    if (safe_mode) gfx_text_center_fit(g, big ? 54 : 26, "SAFE MODE", C_RED, 1);
    else if (big) gfx_text_center_fit(g, 54, "V" SKY_FW_VERSION, C_FAINT, 1);
}

/* Centruotas tekstas mažuoju 5×7 šriftu su mažosiomis raidėmis: tinklo vardas ir slaptažodis turi būti rodomi
 * tiksliai, su didžiosiomis ir mažosiomis raidėmis */
static void text_lc_center(gfx_t *g, int y, const char *s, uint16_t c)
{
    gfx_text_lc(g, (g->w - gfx_text_lc_width(s)) / 2, y, s, c);
}

/* Wi-Fi nustatymai dideliame (128×64) ekrane. Naudotojui parodoma, prie kokio tinklo jungtis telefonu
 * (vardas ir slaptažodis), kokį adresą atidaryti naršyklėje ir kaip sekasi jungtis prie jo namų tinklo.
 * Tekstai rašomi per gfx_text_center_fit: netelpanti eilutė mažinama, o prireikus rodoma 3×5 šriftu. */
static void portal_big(gfx_t *g, uint32_t ms)
{
    gfx_text_center_fit(g, 2, "WI-FI SETUP", C_CYAN, 1);
    char target[17];
    strlcpy(target, g_status.portal_target, sizeof target);
    if (g_status.portal_state == PORTAL_TESTING) {          /* jungiamasi prie naudotojo nurodyto tinklo */
        gfx_text_center_fit(g, 16, "CONNECTING TO", C_GREY, 1);
        gfx_text_center_fit(g, 26, target, C_AMBER, 2);
        gfx_text_center_fit(g, 46, "PLEASE WAIT", C_GREY, 1);
    } else if (g_status.portal_state == PORTAL_OK) {        /* prisijungta */
        gfx_text_center_fit(g, 14, "CONNECTED!", C_GREEN, 2);
        if (g_status.ip[0]) gfx_text_center_fit(g, 34, g_status.ip, C_AMBER, 1);
        gfx_text_center_fit(g, 47, "ALL SET", C_GREY, 1);
    } else if (g_status.portal_state == PORTAL_FAIL) {      /* nepavyko – priežastis ir prašymas bandyti dar kartą */
        const char *why = sky_wifi_lcd_text(g_status.wifi_reason ? g_status.wifi_reason : 205);
        gfx_text_center_fit(g, 18, why, C_RED, 2);
        gfx_text_center_fit(g, 34, "PLEASE TRY AGAIN", C_GREY, 1);
        gfx_text_center_fit(g, 44, "ON YOUR PHONE", C_GREY, 1);
    } else if (!g_status.portal_client) {                   /* telefonas dar neprisijungęs: tinklo vardas ir slaptažodis */
        const int keep = gfx_font();
        gfx_set_font(GFX_FONT_SMALL);
        gfx_text_center(g, 13, "WI-FI NAME", C_GREY);
        gfx_text_center(g, 32, "PASSWORD", C_GREY);
        gfx_set_font(keep);
        text_lc_center(g, 20, SKY_AP_SSID, C_AMBER);
        text_lc_center(g, 39, g_status.portal_pass, C_AMBER);
    } else {                                                /* telefonas prisijungė: kokį adresą atidaryti */
        gfx_text_center_fit(g, 16, "OPEN IN A BROWSER", C_GREY, 1);
        gfx_text_center_fit(g, 27, SKY_AP_IP, C_AMBER, 2);
    }
    if (g_status.safe_mode) {
        gfx_text_center_fit(g, 56, "SAFE MODE", C_RED, 1);
    } else if (g_status.portal_state == PORTAL_IDLE && !g_status.portal_client) {
        const int keep = gfx_font();
        gfx_set_font(GFX_FONT_SMALL);
        gfx_text_center(g, 56, "THEN OPEN " SKY_AP_IP, C_GREY);
        gfx_set_font(keep);
    }
}

/* Wi-Fi nustatymų ekranas. Dideliam ekranui – portal_big; mažam (64×32) – ta pati informacija sutrumpinta. */
void screen_portal(gfx_t *g, uint32_t ms)
{
    if (g->w >= 128 && g->h >= 64) {
        portal_big(g, ms);
        return;
    }
    gfx_text_center_fit(g, 1, "WI-FI SETUP", C_CYAN, 1);
    char target[17];
    strlcpy(target, g_status.portal_target, sizeof target);
    if (g_status.portal_state == PORTAL_TESTING) {
        gfx_text_center_fit(g, 9, "CONNECTING TO", C_GREY, 1);
        gfx_text_center_fit(g, 16, target, C_AMBER, 1);
        gfx_text_center_fit(g, 25, "PLEASE WAIT", C_GREY, 1);
    } else if (g_status.portal_state == PORTAL_OK) {
        gfx_text_center_fit(g, 9, "CONNECTED!", C_GREEN, 1);
        gfx_text_center_fit(g, 17, g_status.ip[0] ? g_status.ip : "", C_AMBER, 1);
        gfx_text_center_fit(g, 25, "ALL SET", C_GREY, 1);
    } else if (g_status.portal_state == PORTAL_FAIL) {
        gfx_text_center_fit(g, 9, sky_wifi_lcd_text(g_status.wifi_reason ? g_status.wifi_reason : 205), C_RED, 1);
        gfx_text_center_fit(g, 16, "PLEASE TRY", C_GREY, 1);
        gfx_text_center_fit(g, 23, "AGAIN ON PHONE", C_GREY, 1);
    } else if (!g_status.portal_client) {
        /* Vardas ir slaptažodis kartu netelpa, todėl rodomi paeiliui po 3 s */
        const bool pass = (ms / 3000) % 2;
        const int keep = gfx_font();
        gfx_set_font(GFX_FONT_SMALL);
        gfx_text_center(g, 10, pass ? "PASSWORD" : "WI-FI NAME", C_GREY);
        gfx_set_font(keep);
        text_lc_center(g, 18, pass ? g_status.portal_pass : SKY_AP_SSID, C_AMBER);
    } else {
        gfx_text_center_fit(g, 9, "OPEN", C_GREY, 1);
        gfx_text_center_fit(g, 16, SKY_AP_IP, C_AMBER, 1);
        gfx_text_center_fit(g, 25, "IN A BROWSER", C_GREY, 1);
    }
}


/* Pasisveikinimas po Wi-Fi nustatymo: pasaulio žemėlapis, o viršuje – valdymo puslapio adresas namų tinkle.
 * Rodomas, kol puslapis atidaromas (webui.c) arba paspaudžiamas mygtukas (app_main.c). */
void screen_welcome(gfx_t *g, const char *ip)
{
    mode_win(WIN_MAP);
    map_draw_world(g, 0, 0, g->w, g->h);
    mode_win(WIN_CARD);
    if (!ip) return;
    /* Adresas reikalingas, jei naudotojo įrenginys neatpažįsta aerobox.local */
    char t[24];
    const bool big = g->w >= 128;
    snprintf(t, sizeof t, big ? "OPEN %s" : "%s", ip);
    const int h = big ? 11 : 8;
    gfx_fill_rect(g, 0, 0, g->w, h, C_BLACK);
    gfx_hline(g, 0, h, g->w, C_ACCENT_DIM);
    gfx_text_center_fit(g, big ? 2 : 1, t, C_AMBER, 1);
}
