/* Radaras. 64×32: kairėje 32×32 radaras, dešinėje – artimiausias (arba sekamas) lėktuvas.
 *          128×64: kairėje 64×64 radaras, dešinėje – 4 lėktuvų sąrašas mažu 3×5 šriftu, „N“ – 4×5;
 *          viršutiniuose radaro kampuose – spindulys ir kiek lėktuvų į sąrašą netilpo.
 * Lėktuvas – rodyklė, taškas arba glotnus trikampis (g_mode_prefs.radar_mark), be pėdsako.
 * Sąraše esantys lėktuvai radare nuspalvinami ta pačia spalva kaip sąraše (be numerių).
 * Sekamas skrydis – baltas, su pulsuojančiu žiedu, sąraše visada pirmas.
 * Animacija (g_mode_prefs.radar_style): jokios / sonaras / klasikinis spindulys.
 * Padalintame ekrane (64×64 langai) radaras ir sąrašas – atskiri langai; šalia kortelės pažymimas jos lėktuvas. */
#include "mode.h"
#include "mode_util.h"
#include <math.h>
#include <string.h>

#define DEG2RAD   0.017453292519943295f
#define LISTED    4
#define SWEEP_MS  4000             /* klasikinis: vieno apsisukimo trukmė */
#define PING_MS   3000             /* sonaras: banga kas 3 s… */
#define PING_RUN  2000             /* …kuri pasiekia kraštą per 2 s */
#define PULSE_MS  1200             /* sekamo lėktuvo žiedo periodas */
#define KITE_BIG   0.8f            /* trikampio dydis (draw_kite): 128×64 – ~6 taškų ilgio… */
#define KITE_SMALL 0.55f           /* …64×32 – ~4 */

static const uint16_t AC_COL[LISTED] = { C_AMBER, C_CYAN, RGB565(255, 70, 190), RGB565(160, 110, 255) };
#define C_TRACKED C_WHITE
#define C_OTHER   RGB565(110, 110, 115)
#define C_LABEL   RGB565(90, 90, 95)
/* Radaro spalva iš temos (numatyta – žalia); žiedai ir „N“ – blankesni jos atspalviai */
#define RADAR_TINT(lvl) gfx_scale(COL(COL_RADAR), (lvl))
#define C_RING    RADAR_TINT(110)
#define C_RING2   RADAR_TINT(50)
#define C_NORTH   RADAR_TINT(170)

static const aircraft_snapshot_t *s_snap;
static uint32_t s_ms;
static int s_cx, s_cy, s_r;           /* radaro centras ir spindulys taškais */
static float s_k;                     /* taškų viename kilometre */
static int s_order[AC_MAX];           /* rodymo tvarka: sekamas – pirmas, toliau pagal atstumą */
static int s_count;
static bool s_has_tracked;
static uint32_t s_focus;              /* padalintame ekrane: kortelėje rodomas lėktuvas (0 – nėra) */

static void km_to_px(float n_km, float e_km, int *x, int *y)
{
    *x = s_cx + (int)lroundf(e_km * s_k);
    *y = s_cy - (int)lroundf(n_km * s_k);
}

static bool inside(int x, int y)
{
    const int dx = x - s_cx, dy = y - s_cy;
    return dx * dx + dy * dy <= (s_r + 1) * (s_r + 1);
}

static void make_order(void)
{
    s_count = s_snap ? s_snap->count : 0;
    s_has_tracked = false;
    int n = 0;
    for (int i = 0; i < s_count; i++)
        if (s_snap->ac[i].tracked) {
            s_order[n++] = i;
            s_has_tracked = true;
        }
    for (int i = 0; i < s_count; i++)
        if (!s_snap->ac[i].tracked) s_order[n++] = i;
}

/* Spalva pagal vietą rodymo tvarkoje: sekamas – baltas, kiti sąraše – savo spalva, likusieji – pilki */
static uint16_t color_for(int rank, int listed)
{
    if (s_snap->ac[s_order[rank]].tracked) return C_TRACKED;
    if (rank >= listed) return C_OTHER;
    return AC_COL[rank - (s_has_tracked ? 1 : 0)];
}

/* 5×5 rodyklė: į šiaurę ir į šiaurės rytus; kitos kryptys gaunamos pasukus po 90°. Bitas 0x10 – kairė */
static const uint8_t ARROW_N[5]  = { 0x04, 0x0E, 0x15, 0x04, 0x04 };
static const uint8_t ARROW_NE[5] = { 0x07, 0x03, 0x05, 0x08, 0x10 };

static void draw_arrow(gfx_t *g, int cx, int cy, uint16_t track_deg, uint16_t c)
{
    const int oct = ((track_deg + 22) / 45) % 8;
    const uint8_t *base = oct % 2 ? ARROW_NE : ARROW_N;
    const int rot = oct / 2;                         /* kiek kartų pasukti 90° pagal laikrodžio rodyklę */
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++) {
            int sx = x, sy = y;                      /* pasukama atgal, kad būtų rastas pradinis taškas */
            for (int r = 0; r < rot; r++) {
                int t = sx;
                sx = sy;
                sy = 4 - t;
            }
            if (base[sy] & (0x10 >> sx)) gfx_pixel(g, cx - 2 + x, cy - 2 + y, c);
        }
}

/* Apvalus 3×3 taškas: kryžius ryškus, kampai blankūs */
static void draw_dot(gfx_t *g, int x, int y, uint16_t c)
{
    gfx_pixel(g, x, y, c);
    gfx_pixel(g, x - 1, y, c);
    gfx_pixel(g, x + 1, y, c);
    gfx_pixel(g, x, y - 1, c);
    gfx_pixel(g, x, y + 1, c);
    gfx_blend(g, x - 1, y - 1, c, 4);
    gfx_blend(g, x + 1, y - 1, c, 4);
    gfx_blend(g, x - 1, y + 1, c, 4);
    gfx_blend(g, x + 1, y + 1, c, 4);
}

static void draw_marker(gfx_t *g, int x, int y, uint16_t track_deg, uint16_t c, bool big)
{
    if (g_mode_prefs.radar_mark == MARK_DOT) draw_dot(g, x, y, c);
    else if (g_mode_prefs.radar_mark == MARK_TRIANGLE) draw_kite(g, x, y, track_deg, c, big ? KITE_BIG : KITE_SMALL);
    else draw_arrow(g, x, y, track_deg, c);
}

static void radar_enter(void) { }

static void radar_update(const aircraft_snapshot_t *s, uint32_t ms)
{
    s_snap = s;
    s_ms = ms;
}

static void ray(gfx_t *g, float deg, uint16_t c)
{
    float r = deg * DEG2RAD;
    gfx_line(g, s_cx, s_cy, s_cx + (int)lroundf(s_r * sinf(r)), s_cy - (int)lroundf(s_r * cosf(r)), c);
}

/* Ar animacija šiuo metu „užkliudo“ lėktuvą (tada jis trumpam šviečia baltai) */
static bool lit_by_animation(int x, int y, float bearing)
{
    if (g_mode_prefs.radar_style == RADAR_SONAR) {
        const uint32_t t = s_ms % PING_MS;
        if (t >= PING_RUN) return false;
        const float ring = (float)s_r * t / PING_RUN;
        const float d = sqrtf((float)((x - s_cx) * (x - s_cx) + (y - s_cy) * (y - s_cy)));
        return fabsf(d - ring) < 2.0f;
    }
    if (g_mode_prefs.radar_style == RADAR_SWEEP) {
        const float sweep = (float)(s_ms % SWEEP_MS) * 360.0f / SWEEP_MS;
        return fmodf(sweep - bearing + 360.0f, 360.0f) < 12.0f;
    }
    return false;
}

static void draw_animation(gfx_t *g)
{
    if (g_mode_prefs.radar_style == RADAR_SONAR) {
        const uint32_t t = s_ms % PING_MS;
        if (t < PING_RUN) {
            const int r = s_r * (int)t / PING_RUN;
            const uint8_t lvl = (uint8_t)(230 - 130 * t / PING_RUN);
            gfx_circle(g, s_cx, s_cy, r, RADAR_TINT(lvl));
            if (r > 2) gfx_circle(g, s_cx, s_cy, r - 2, RADAR_TINT(lvl / 4));
        }
    } else if (g_mode_prefs.radar_style == RADAR_SWEEP) {
        const float sweep = (float)(s_ms % SWEEP_MS) * 360.0f / SWEEP_MS;
        for (int i = 10; i >= 1; i--)                /* blėstanti uodega už spindulio */
            ray(g, sweep - i * 3.0f, RADAR_TINT((uint8_t)(70 - i * 6)));
        ray(g, sweep, RADAR_TINT(200));
    }
}

/* Sekamas lėktuvas: iš jo plintantis ir blėstantis žiedas */
static void draw_pulse(gfx_t *g, int x, int y, bool big)
{
    const uint32_t t = s_ms % PULSE_MS;
    const int r = (big ? 3 : 2) + (int)t * (big ? 5 : 3) / PULSE_MS;
    gfx_circle(g, x, y, r, gfx_scale(C_WHITE, (uint8_t)(200 - 170 * t / PULSE_MS)));
}

static void draw_scope(gfx_t *g, bool big, int listed)
{
    s_k = s_r / aircraft_table_home()->range_km;
    gfx_clip(s_cx - s_r - 2, 0, 2 * s_r + 5, g->h);    /* niekas neišeina už radaro kvadrato */

    /* Fonas: žiedai ir taškų kryžius */
    for (int d = -s_r + 2; d <= s_r - 2; d += 3) {
        gfx_pixel(g, s_cx + d, s_cy, C_RING2);
        gfx_pixel(g, s_cx, s_cy + d, C_RING2);
    }
    gfx_circle(g, s_cx, s_cy, s_r / 2, C_RING2);
    if (big) gfx_circle(g, s_cx, s_cy, s_r / 4 * 3 + 1, C_RING2);
    draw_animation(g);
    gfx_circle(g, s_cx, s_cy, s_r, C_RING);
    if (big) {
        /* „N“ – visada vidutiniu 4×5 šriftu */
        const int keep = gfx_font();
        gfx_set_font(GFX_FONT_WIDE);
        gfx_fill_rect(g, s_cx - 3, 0, 7, 6, C_BLACK);
        gfx_text(g, s_cx - 1, 0, "N", C_NORTH);
        gfx_set_font(keep);
    } else {
        gfx_pixel(g, s_cx, s_cy - s_r, COL(COL_RADAR));
    }

    /* Namai – baltas kryželis */
    gfx_hline(g, s_cx - 1, s_cy, 3, C_WHITE);
    gfx_vline(g, s_cx, s_cy - 1, 3, C_WHITE);

    /* Piešiama nuo galo, kad sąraše esantys būtų viršuje; kortelės lėktuvas (s_focus) – pačiame viršuje */
    int focus = -1;
    for (int k = s_count - 1; k >= 0; k--) {
        const aircraft_t *a = &s_snap->ac[s_order[k]];
        if (s_focus && a->icao24 == s_focus) {
            focus = k;
            continue;
        }
        int x, y;
        km_to_px(a->dist_km * cosf(a->bearing_deg * DEG2RAD), a->dist_km * sinf(a->bearing_deg * DEG2RAD), &x, &y);
        if (!inside(x, y)) continue;
        const uint16_t c = s_focus && !a->tracked ? C_OTHER : color_for(k, listed);
        if (g_mode_prefs.rare && a->special) gfx_circle(g, x, y, big ? 5 : 3, C_RARE);   /* ypatingas – rožinis žiedas */
        if (a->tracked && g_mode_prefs.track_pulse) draw_pulse(g, x, y, big);
        draw_marker(g, x, y, a->track_deg, lit_by_animation(x, y, a->bearing_deg) ? C_WHITE : c, big);
    }
    if (focus >= 0) {
        const aircraft_t *a = &s_snap->ac[s_order[focus]];
        int x, y;
        km_to_px(a->dist_km * cosf(a->bearing_deg * DEG2RAD), a->dist_km * sinf(a->bearing_deg * DEG2RAD), &x, &y);
        if (inside(x, y)) {
            if (g_mode_prefs.rare && a->special) gfx_circle(g, x, y, big ? 5 : 3, C_RARE);
            if (!a->tracked || g_mode_prefs.track_pulse) draw_pulse(g, x, y, big);
            draw_marker(g, x, y, a->track_deg, a->tracked ? C_TRACKED : COL(COL_FLIGHT), big);
        }
    }
    gfx_clip_off();
}

static void draw_small(gfx_t *g)
{
    s_cx = 16; s_cy = 16; s_r = 15;
    draw_scope(g, false, 1);
    gfx_vline(g, 33, 0, 32, C_DIM);

    const int tx = 35;
    char buf[12];
    if (s_count == 0) {
        const int keep = gfx_font();
        gfx_set_font(GFX_FONT_SMALL);
        gfx_text(g, tx, 9, "NO", C_GREY);
        gfx_text(g, tx, 16, "FLIGHTS", C_GREY);
        gfx_set_font(keep);
    } else {
        const aircraft_t *a = &s_snap->ac[s_order[0]];
        gfx_text_max(g, tx, 0, ac_name(a), color_for(0, 1), 64 - tx);
        fmt_dist(buf, sizeof buf, a->dist_km);
        gfx_text(g, tx, 7, buf, COL(COL_TEXT));
        fmt_alt(buf, sizeof buf, a);
        int x = gfx_text(g, tx, 13, buf, COL(COL_TEXT));
        draw_vs_arrow(g, x + 1, 13, a);
        if (a->type[0]) gfx_text(g, tx, 19, a->type, C_LABEL);
    }
    char rg[10];
    fmt_range(rg, sizeof rg);
    snprintf(buf, sizeof buf, gfx_font() == GFX_FONT_SMALL ? "R%s" : "%s", rg);
    gfx_text(g, tx, 26, buf, C_LABEL);
    if (s_count > 1) {
        snprintf(buf, sizeof buf, "%d", s_count);
        gfx_text_right(g, 63, 26, buf, C_GREY);
    }
}

/* 64×64 radaro kampai už žiedo (3×5 šriftu): kairėje – spindulys „20KM“, dešinėje – „+2“, kai ne visi telpa į sąrašą.
 * Piešiama prieš lėktuvus, kad prie krašto atskridę lėktuvai būtų viršuje. */
static void draw_corners(gfx_t *g, bool with_list)
{
    const int keep = gfx_font();
    gfx_set_font(GFX_FONT_SMALL);
    char buf[16];
    fmt_range(buf, sizeof buf);
    if (gfx_text_width(buf) > 15) {               /* „100KM“ liestų žiedą – vienetai rašomi eilute žemiau, kampe */
        const size_t d = strspn(buf, "0123456789");
        gfx_text(g, 0, 6, buf + d, C_LABEL);
        buf[d] = 0;
    }
    gfx_text(g, 0, 0, buf, C_LABEL);
    if (with_list && s_count > LISTED) {
        snprintf(buf, sizeof buf, "+%d", s_count - LISTED);
        gfx_text_right(g, 62, 0, buf, C_GREY);
    }
    gfx_set_font(keep);
}

/* Sąrašas tarp tx ir right (58 taškai): iki 4 lėktuvų po 2 eilutes per visą aukštį, visada mažu 3×5 šriftu */
static void draw_list(gfx_t *g, int tx, int right)
{
    const int keep = gfx_font();
    gfx_set_font(GFX_FONT_SMALL);
    char buf[20];
    if (s_count == 0) {
        gfx_text(g, tx + (right - tx + 1 - gfx_text_width("NO FLIGHTS")) / 2, 29, "NO FLIGHTS", C_GREY);
    }
    for (int k = 0; k < s_count && k < LISTED; k++) {
        const aircraft_t *a = &s_snap->ac[s_order[k]];
        const int y = 2 + k * 16;
        fmt_dist(buf, sizeof buf, a->dist_km);
        gfx_text_right(g, right, y, buf, COL(COL_TEXT));
        gfx_text_max(g, tx, y, ac_name(a), color_for(k, LISTED), right - gfx_text_width(buf) - 3 - tx);
        fmt_alt(buf, sizeof buf, a);
        const int x = gfx_text(g, tx, y + 7, buf, COL(COL_TEXT));
        draw_vs_arrow(g, x + 1, y + 7, a);
        if (a->type[0]) gfx_text_right(g, right, y + 7, a->type, C_LABEL);
    }
    gfx_set_font(keep);
}

static void draw_big(gfx_t *g)
{
    s_cx = 31; s_cy = 32; s_r = 30;
    s_focus = 0;
    draw_corners(g, true);
    draw_scope(g, true, LISTED);
    gfx_vline(g, 64, 0, 64, C_DIM);
    draw_list(g, 68, 125);
}

/* Padalinto ekrano 64×64 langai */
void radar_pane(gfx_t *g, uint32_t focus_icao, bool with_list)
{
    mode_win(WIN_RADAR);
    make_order();
    s_cx = 31; s_cy = 32; s_r = 30;
    s_focus = focus_icao;
    draw_corners(g, with_list);
    draw_scope(g, true, LISTED);
    s_focus = 0;
}

void radar_list_pane(gfx_t *g)
{
    mode_win(WIN_RADAR);
    make_order();
    draw_list(g, 3, 60);
}

static void radar_draw(gfx_t *g)
{
    mode_win(WIN_RADAR);
    make_order();
    if (g->w >= 128 && g->h >= 64) draw_big(g);
    else draw_small(g);
}

const display_mode_t MODE_RADAR = {
    .name = "radar", .enter = radar_enter, .update = radar_update, .draw = radar_draw,
};
