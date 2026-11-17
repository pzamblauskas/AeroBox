/* Pagalbinės funkcijos, kurias naudoja keli režimai: reikšmių formatavimas pagal pasirinktus vienetus
 * ir bendri grafiniai elementai (lėktuvo ženklas, eigos juosta, kilimo rodyklė). */
#pragma once
#include <math.h>
#include <stdio.h>
#include "aircraft.h"
#include "gfx.h"
#include "mode.h"

#define KM_TO_MI 0.621371f
#define KM_TO_NM 0.539957f

/* Lėktuvo pavadinimas: šaukinys, jei jo nėra – registracija */
static inline const char *ac_name(const aircraft_t *a)
{
    if (a->callsign[0]) return a->callsign;
    if (a->reg[0]) return a->reg;
    return "?";
}

/* Aukštis: „FL237“ / „23725FT“ / „7231M“ (pagal nustatymą); ant žemės – „GND“ */
static inline void fmt_alt(char *out, size_t n, const aircraft_t *a)
{
    if (a->on_ground) snprintf(out, n, "GND");
    else if (g_mode_prefs.alt_unit == ALT_FT) snprintf(out, n, "%dFT", (int)a->alt_ft);
    else if (g_mode_prefs.alt_unit == ALT_M) snprintf(out, n, "%dM", (int)lroundf(a->alt_ft * 0.3048f));
    else snprintf(out, n, "FL%03d", (int)((a->alt_ft + 50) / 100));
}

/* Kilometrai → pasirinkti vienetai (km / mylios / jūrmylės) */
static inline float dist_conv(float km, const char **unit)
{
    if (g_mode_prefs.dist_unit == DIST_MI) {
        *unit = "MI";
        return km * KM_TO_MI;
    }
    if (g_mode_prefs.dist_unit == DIST_NM) {
        *unit = "NM";
        return km * KM_TO_NM;
    }
    *unit = "KM";
    return km;
}

/* Atstumas: „4.2KM“ / „13MI“ / „2.3NM“ (pagal nustatymą) */
static inline void fmt_dist(char *out, size_t n, float km)
{
    const char *u;
    const float v = dist_conv(km, &u);
    if (v < 10.0f) snprintf(out, n, "%.1f%s", (double)v, u);
    else snprintf(out, n, "%d%s", (int)(v + 0.5f), u);
}

/* Radaro spindulys: „20KM“ / „12MI“ / „11NM“ */
static inline void fmt_range(char *out, size_t n)
{
    const char *u;
    const float v = dist_conv(aircraft_table_home()->range_km, &u);
    snprintf(out, n, "%d%s", (int)(v + 0.5f), u);
}

/* Temperatūra: „13°C“ / „55°F“ */
static inline void fmt_temp(char *out, size_t n, float c)
{
    const bool f = g_mode_prefs.temp_unit == TEMP_F;
    const float v = f ? c * 1.8f + 32.0f : c;
    snprintf(out, n, "%d" GFX_DEG "%s", (int)(v < 0 ? v - 0.5f : v + 0.5f), f ? "F" : "C");
}

/* Greitis: „387KT“ / „717KM/H“ / „445MPH“ (pagal nustatymą) */
static inline void fmt_speed(char *out, size_t n, uint16_t kt)
{
    if (g_mode_prefs.speed_unit == SPEED_KMH) snprintf(out, n, "%dKM/H", (int)(kt * 1.852f + 0.5f));
    else if (g_mode_prefs.speed_unit == SPEED_MPH) snprintf(out, n, "%dMPH", (int)(kt * 1.150779f + 0.5f));
    else snprintf(out, n, "%uKT", (unsigned)kt);
}

/* Kilimo / leidimosi greitis be ženklo (kryptį rodo rodyklė ir spalva): „1200FPM“ arba „6M/S“ (pagal nustatymą).
 * m/s – sveikais skaičiais: ADS-B žingsnis 64 ft/min = 0,33 m/s */
static inline void fmt_vrate(char *out, size_t n, int fpm)
{
    if (fpm < 0) fpm = -fpm;
    if (g_mode_prefs.vs_unit == VS_MS) snprintf(out, n, "%dM/S", (int)lroundf(fpm * 0.00508f));
    else snprintf(out, n, "%dFPM", fpm);
}

/* Ar lėktuvas kyla ar leidžiasi (greičiau nei 500 ft/min) */
static inline bool vs_moving(const aircraft_t *a)
{
    return a->vrate_fpm > 500 || a->vrate_fpm < -500;
}

static inline uint16_t vs_color(const aircraft_t *a)
{
    return a->vrate_fpm > 0 ? COL(COL_UP) : COL(COL_DOWN);
}

/* Lėktuvo ženklas – glotnus trikampis (strėlė su įkirpimu gale), nosis kryptimi deg; k – dydis (1 – 7 taškų ilgio).
 * (x, y) – centras; naudojamas radare ir eigos juostoje */
static inline void draw_kite(gfx_t *g, int x, int y, float deg, uint16_t c, float k)
{
    static const float P[4][2] = { { 0.0f, -4.0f }, { 3.0f, 3.3f }, { 0.0f, 1.2f }, { -3.0f, 3.3f } };
    const float r = deg * 0.017453292519943295f, cs = cosf(r), sn = sinf(r);
    float v[4][2];
    for (int i = 0; i < 4; i++) {
        v[i][0] = x + 0.5f + (P[i][0] * cs - P[i][1] * sn) * k;
        v[i][1] = y + 0.5f + (P[i][0] * sn + P[i][1] * cs) * k;
    }
    gfx_fill_triangle_aa(g, v[0][0], v[0][1], v[1][0], v[1][1], v[2][0], v[2][1], c);
    gfx_fill_triangle_aa(g, v[0][0], v[0][1], v[2][0], v[2][1], v[3][0], v[3][1], c);
}

/* Skrydžio eigos juosta (p 0…1; neigiama – tik pilka linija) su lėktuvo ženklu kaip radare
 * (pilku, kai ryšys su lėktuvu dingęs) */
static inline void draw_progress(gfx_t *g, int x0, int y, int w, float p, bool live)
{
    gfx_hline(g, x0, y, w, C_DIM);
    if (p < 0.0f) return;
    if (p > 1.0f) p = 1.0f;
    const bool big = g->h >= 64;                     /* 64×32 ekrane juosta prie pat apačios – ženklas mažesnis */
    const int px = x0 + (big ? 3 : 2) + (int)(p * (w - (big ? 8 : 6)) + 0.5f);
    gfx_hline(g, x0, y, px - x0, COL(COL_PROGRESS));
    draw_kite(g, px, y, 90.0f, live ? C_WHITE : C_GREY, big ? 1.0f : 0.75f);
}

/* Kilimo / leidimosi rodyklė 3×5 (į viršų / žemyn, temos spalvomis), kai greičiau nei 500 ft/min; grąžina x po jos */
static inline int draw_vs_arrow(gfx_t *g, int x, int y, const aircraft_t *a)
{
    if (a->vrate_fpm > 500) {
        gfx_vline(g, x + 1, y, 5, COL(COL_UP));
        gfx_hline(g, x, y + 1, 3, COL(COL_UP));
    } else if (a->vrate_fpm < -500) {
        gfx_vline(g, x + 1, y, 5, COL(COL_DOWN));
        gfx_hline(g, x, y + 3, 3, COL(COL_DOWN));
    } else {
        return x;
    }
    return x + 4;
}
