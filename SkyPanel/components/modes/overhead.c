/* „Lėktuvas virš galvos“ (LOOK UP!): kai lėktuvas arčiau nei g_mode_prefs.overhead_km nuo namų, kelias sekundes
 * rodoma animacija – pulsuojantis užrašas, didelis lėktuvas skrenda per namus (iš namų sklinda bangos), ir kuris tai
 * lėktuvas, jo aukštis, atstumas ir į kurią pusę žiūrėti. Kada pradėti, sprendžia mode_manager. */
#include "mode.h"
#include "mode_util.h"
#include <math.h>
#include <string.h>

#define DEG2RAD 0.017453292519943295f

static aircraft_t s_ac;               /* paskutinis žinomas (jei dingtų iš sąrašo, vis tiek rodomas) */
static uint32_t s_start, s_ms;

void overhead_start(const aircraft_t *a, uint32_t ms)
{
    s_ac = *a;
    s_start = ms;
}

static void ov_enter(void) { }

static void ov_update(const aircraft_snapshot_t *s, uint32_t ms)
{
    s_ms = ms;
    for (int i = 0; s && i < s->count; i++)
        if (s->ac[i].icao24 == s_ac.icao24) {
            s_ac = s->ac[i];
            break;
        }
}

/* Dviejų spalvų mišinys: t 0 (a) … 255 (b) */
static uint16_t mix(uint16_t a, uint16_t b, uint8_t t)
{
    const int r = ((a >> 11) * (255 - t) + (b >> 11) * t) / 255;
    const int g = (((a >> 5) & 63) * (255 - t) + ((b >> 5) & 63) * t) / 255;
    const int bl = ((a & 31) * (255 - t) + (b & 31) * t) / 255;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

static const char *compass(float deg)
{
    static const char *const D[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    return D[(int)((deg + 22.5f) / 45.0f) & 7];
}

/* Lėktuvas skrenda per (cx, cy) savo kursu: kas 2 s iš naujo, nuo krašto iki krašto (span taškų) */
static void flying_plane(gfx_t *g, int cx, int cy, float span, float k, const aircraft_t *a, uint32_t t)
{
    const float f = (float)(t % 2000) / 2000.0f - 0.5f, r = a->track_deg * DEG2RAD;
    draw_kite(g, cx + (int)lroundf(sinf(r) * span * f), cy - (int)lroundf(cosf(r) * span * f), a->track_deg,
              C_WHITE, k);
}

void overhead_draw_for(gfx_t *g, const aircraft_t *a, uint32_t t)
{
    const float pulse = 0.5f + 0.5f * sinf((float)t * (6.2831853f / 700.0f));
    const uint16_t title = mix(COL(COL_FLIGHT), C_WHITE, (uint8_t)(pulse * 200.0f));
    char buf[24], d[12];
    fmt_dist(d, sizeof d, a->dist_km);
    snprintf(buf, sizeof buf, "%s %s", d, compass(a->bearing_deg));

    if (g->h < 64) {                                 /* 64×32: tekstas, apačioje praskrendantis lėktuvas */
        gfx_text_center_fit(g, 1, "LOOK UP!", title, 1);
        const int type_w = a->type[0] ? gfx_text_width(a->type) + 2 : 0;
        gfx_text_max(g, 0, 9, ac_name(a), a->tracked ? C_WHITE : COL(COL_FLIGHT), 64 - type_w);
        if (a->type[0]) gfx_text_right(g, 63, 9, a->type, COL(COL_TYPE));
        gfx_text_center_fit(g, 17, buf, COL(COL_TEXT), 1);
        gfx_clip(0, 24, g->w, 8);
        const float span = (float)g->w + 16.0f, f = (float)(t % 2000) / 2000.0f;
        const bool east = a->track_deg < 180;
        draw_kite(g, east ? (int)(f * span) - 8 : g->w + 8 - (int)(f * span), 28, east ? 90.0f : 270.0f, C_WHITE, 0.8f);
        gfx_clip_off();
        return;
    }

    gfx_text_center_fit(g, 1, "LOOK UP!", title, 2);

    /* Kairėje: namai, iš jų sklindančios bangos ir per juos skrendantis lėktuvas */
    const int cx = 31, cy = 41;
    gfx_clip(0, 17, 64, 47);
    for (int i = 0; i < 2; i++) {
        const uint32_t ph = (t + (uint32_t)i * 600) % 1200;
        const int r = 3 + (int)(ph * 22 / 1200);
        gfx_circle(g, cx, cy, r, gfx_scale(COL(COL_RADAR), (uint8_t)(200 - ph * 180 / 1200)));
    }
    gfx_hline(g, cx - 1, cy, 3, C_WHITE);
    gfx_vline(g, cx, cy - 1, 3, C_WHITE);
    flying_plane(g, cx, cy, 76.0f, 2.2f, a, t);
    gfx_clip_off();

    /* Dešinėje: kas skrenda, aukštis, kur žiūrėti */
    const int x = 67, R = 125, rp = gfx_font_h() + 3;
    int y = 19;
    gfx_text_max(g, x, y, ac_name(a), a->tracked ? C_WHITE : COL(COL_FLIGHT), R - x + 1);
    y += rp;
    int xe = x;
    if (a->type[0]) xe = gfx_text(g, x, y, a->type, COL(COL_TYPE)) + 3;
    if (g_mode_prefs.rare && a->special && xe + gfx_text_width(special_short(a->special)) <= R + 1)
        gfx_text_right(g, R, y, special_short(a->special), C_RARE);
    y += rp;
    fmt_alt(d, sizeof d, a);
    xe = gfx_text(g, x, y, d, COL(COL_ALT));
    draw_vs_arrow(g, xe + 1, y + (gfx_font_h() - 5) / 2, a);
    y += rp;
    gfx_text_max(g, x, y, buf, COL(COL_TEXT), R - x + 1);
}

static void ov_draw(gfx_t *g)
{
    mode_win(WIN_CARD);
    overhead_draw_for(g, &s_ac, s_ms - s_start);
}

const display_mode_t MODE_OVERHEAD = {
    .name = "overhead", .enter = ov_enter, .update = ov_update, .draw = ov_draw,
};
