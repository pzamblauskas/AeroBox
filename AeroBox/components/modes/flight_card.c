/* Skrydžio kortelė.
 * 64×32:  skrydžio Nr. ir tipas / maršrutas ir registracija / aukštis ir greitis / atstumas ir „1/3“ / eigos juosta
 * 128×64: didelis skrydžio Nr. ir maršrutas, tipas, registracija, aukštis, greitis, atstumas, kursas,
 *         vertikalusis greitis, eigos juosta su oro uostų kodais
 * 64×64:  padalinto ekrano langas – tos pačios eilutės viena po kitos.
 * Ką rodyti – g_mode_prefs.card_fields (skrydžio Nr. rodomas visada), spalvos – pagal temą.
 * Ypatingi lėktuvai (retas, karinis …) – skiriamoji juosta su užrašu; reti ir kariniai rodomi, net jei nėra tarp
 * artimiausių. Keičiantis lėktuvui – išvykimų lentos efektas (g_mode_prefs.flap). */
#include "mode.h"
#include "mode_util.h"
#ifdef SKY_FLIGHTWALL
#include "flightwall.h"
#endif
#include <stdlib.h>
#include <string.h>

#define CARD_S   6                 /* kiek sekundžių rodoma viena kortelė, kai nustatymo nėra (peržiūra kompiuteryje) */
#define CARD_MAX 5
#define FLAP_MS  1000
#define SHOW(f)  ((g_mode_prefs.card_fields & (f)) != 0)

static const aircraft_snapshot_t *s_snap;
static uint32_t s_ms;
static uint32_t s_icao;              /* kurio lėktuvo kortelė rodoma (padalinto ekrano radarui) */
static uint32_t s_flip_ms;           /* kada pasikeitė lėktuvas (išvykimų lentos efektui) */
static uint32_t s_card_t;            /* nuo kada rodoma dabartinė kortelė */
static int      s_pos;               /* jos vieta sąraše */

static void card_enter(void)
{
    s_icao = 0;                      /* įjungus – nuo artimiausio, lenta „atsinaujina“ */
}

static void card_update(const aircraft_snapshot_t *s, uint32_t ms)
{
    s_snap = s;
    s_ms = ms;
}

/* Kiek laiko rodoma viena kortelė – nustatymas (puslapyje „Flight cards“, 3…30 s) */
uint32_t card_ms(void)
{
    return (g_mode_prefs.card_s ? g_mode_prefs.card_s : CARD_S) * 1000u;
}

static bool very_special(const aircraft_t *a)
{
    return a->special == SP_RARE || a->special == SP_MILITARY;
}

/* Rodomi lėktuvai (indeksai pagal atstumą): artimiausi CARD_MAX, bet iki 2 retų ar karinių iš toliau – vietoj
 * toliausių įprastų (jei ypatingi žymimi) */
static int card_list(int out[CARD_MAX])
{
    const int n = s_snap->count < CARD_MAX ? s_snap->count : CARD_MAX;
    for (int i = 0; i < n; i++) out[i] = i;
    if (!g_mode_prefs.rare) return n;
    int forced = 0;
    for (int i = n; i < s_snap->count && forced < 2; i++) {
        if (!very_special(&s_snap->ac[i])) continue;
        int k = n - 1;
        while (k >= 0 && very_special(&s_snap->ac[out[k]])) k--;
        if (k < 0) break;
        for (; k < n - 1; k++) out[k] = out[k + 1];
        out[n - 1] = i;
        forced++;
    }
    return n;
}

/* Dabar rodomas lėktuvas. Kiekvienas rodomas visą card_ms(), net jei tuo metu pasikeičia eilės tvarka (vienas
 * priartėja, kitas nutolsta), tada – kitas sąraše po jo; jei išskrido iš sąrašo – tas, kuris dabar jo vietoje.
 * NULL – dangus tuščias */
static const aircraft_t *current(int *idx, int *n)
{
    if (!s_snap || s_snap->count == 0) {
        s_icao = 0;
        return NULL;
    }
    int list[CARD_MAX], pos = -1;
    *n = card_list(list);
    for (int i = 0; i < *n; i++)
        if (s_snap->ac[list[i]].icao24 == s_icao) pos = i;
    if (!s_icao || pos < 0 || s_ms - s_card_t >= card_ms()) {
        pos = !s_icao ? 0 : pos >= 0 ? (pos + 1) % *n : s_pos % *n;
        if (s_snap->ac[list[pos]].icao24 != s_icao) s_flip_ms = s_ms;
        s_icao = s_snap->ac[list[pos]].icao24;
        s_card_t = s_ms;
        s_pos = pos;
    }
    *idx = pos;
    return &s_snap->ac[list[pos]];
}

static void flap_begin(void)
{
    if (g_mode_prefs.flap) gfx_flap(s_ms - s_flip_ms, FLAP_MS);
}

static bool marked(const aircraft_t *a)
{
    return g_mode_prefs.rare && a->special;
}

/* Ypatingas lėktuvas: rožinė juosta su užrašu („RARE AIRCRAFT“) laisvame tarpe virš eigos juostos, 3×5 šriftu
 * (y – teksto viršus; abipus – po vieną tarpo tašką) */
void special_banner(gfx_t *g, int y, const aircraft_t *a)
{
    if (!marked(a)) return;
    const int keep = gfx_font();
    gfx_set_font(GFX_FONT_SMALL);
    const char *t = special_label(a->special);
    const int w = gfx_text_width(t), x = (g->w - w) / 2;
    const uint16_t line = gfx_scale(C_RARE, 110);
    gfx_hline(g, 2, y + 2, x - 6, line);
    gfx_hline(g, x + w + 4, y + 2, 126 - (x + w + 4), line);
    gfx_text(g, x, y, t, C_RARE);
    gfx_set_font(keep);
}

uint32_t card_pane_icao(void)
{
    return s_icao;
}

/* Sekamas skrydis – baltas, kiti – temos spalva */
static uint16_t name_color(const aircraft_t *a)
{
    return a->tracked ? C_WHITE : COL(COL_FLIGHT);
}

static bool show_reg(const aircraft_t *a)
{
    return SHOW(CF_REG) && a->reg[0] && strcmp(a->reg, ac_name(a)) != 0;
}

/* Kilimas / leidimasis: rodyklė ir „1200FPM“ arba „6M/S“ (pagal nustatymą). Be ženklo – kryptį rodo rodyklė ir spalva */
static void draw_vrate(gfx_t *g, int x, int y, int arrow_y, const aircraft_t *a)
{
    char buf[16];
    fmt_vrate(buf, sizeof buf, a->vrate_fpm);
    gfx_text(g, draw_vs_arrow(g, x, arrow_y, a) + 2, y, buf, vs_color(a));
}

static void draw_small(gfx_t *g, const aircraft_t *a, int idx, int n)
{
    char buf[20];
    /* Platesniu šriftu ne viskas telpa: svarbiausias – skrydžio Nr., registracija – tik jei lieka vietos */
    const bool type = SHOW(CF_TYPE) && a->type[0];
    const int type_w = type ? gfx_text_width(a->type) + 2 : 0;
    gfx_text_max(g, 0, 1, ac_name(a), name_color(a), 64 - type_w);
    if (type) gfx_text_right(g, 63, 1, a->type, marked(a) ? C_RARE : COL(COL_TYPE));

    int x = 0;
    if (SHOW(CF_ROUTE)) {
        if (a->has_route) {
            x = gfx_text(g, 0, 8, a->orig, COL(COL_TEXT));
            x = gfx_text(g, x, 8, ">", C_GREY);
            x = gfx_text(g, x, 8, a->dest, COL(COL_TEXT));
        } else {
            x = gfx_text(g, 0, 8, "NO ROUTE", C_GREY);
        }
    }
    if (show_reg(a) && x + 2 + gfx_text_width(a->reg) <= 64) gfx_text_right(g, 63, 8, a->reg, C_GREY);

    x = 0;
    if (SHOW(CF_ALT)) {
        fmt_alt(buf, sizeof buf, a);
        x = gfx_text(g, 0, 15, buf, COL(COL_ALT));
    }
    if (SHOW(CF_SPEED)) {
        fmt_speed(buf, sizeof buf, a->speed_kt);
        if (x + 2 + gfx_text_width(buf) <= 64) gfx_text_right(g, 63, 15, buf, COL(COL_TEXT));
    }

    x = 0;
    if (SHOW(CF_DIST)) {
        fmt_dist(buf, sizeof buf, a->dist_km);
        x = gfx_text(g, 0, 22, buf, COL(COL_TEXT)) + 1;
    }
    if (SHOW(CF_VS)) draw_vs_arrow(g, x, 22, a);
    if (SHOW(CF_COUNTER)) {
        snprintf(buf, sizeof buf, "%d/%d", idx + 1, n);
        gfx_text_right(g, 63, 22, buf, C_GREY);
    }

    if (SHOW(CF_PROGRESS) && a->has_route) draw_progress(g, 0, 29, 64, a->progress, true);
}

static void draw_big(gfx_t *g, const aircraft_t *a, int idx, int n)
{
    char buf[24];
    const int R = 125;

    gfx_text_s(g, 2, 2, ac_name(a), name_color(a), 2);
    if (SHOW(CF_TYPE) && a->type[0]) gfx_text_right(g, R, 2, a->type, COL(COL_TYPE));
    if (show_reg(a)) gfx_text_right(g, R, 9, a->reg, C_GREY);

    if (SHOW(CF_ROUTE)) {
        if (a->has_route) {
            int x = gfx_text_s(g, 2, 17, a->orig, COL(COL_TEXT), 2);
            x = gfx_text_s(g, x + 2, 17, ">", C_GREY, 2);
            gfx_text_s(g, x + 2, 17, a->dest, COL(COL_TEXT), 2);
        } else {
            gfx_text(g, 2, 20, "NO ROUTE DATA", C_GREY);
        }
    }
    if (SHOW(CF_COUNTER)) {
        snprintf(buf, sizeof buf, "%d/%d", idx + 1, n);
        gfx_text_right(g, R, 17, buf, C_GREY);
    }
    if (SHOW(CF_SPEED)) {
        fmt_speed(buf, sizeof buf, a->speed_kt);
        gfx_text_right(g, R, 24, buf, COL(COL_TEXT));
    }

    gfx_hline(g, 2, 31, 124, RGB565(30, 30, 32));

    int x = 2;
    if (SHOW(CF_ALT)) {
        fmt_alt(buf, sizeof buf, a);
        x = gfx_text(g, 2, 35, buf, COL(COL_ALT)) + 3;
    }
    if (SHOW(CF_DIST)) {
        fmt_dist(buf, sizeof buf, a->dist_km);
        gfx_text(g, x > 34 ? x : 34, 35, buf, COL(COL_TEXT));   /* ilgas aukštis („23725FT“) – toliau dešinėn */
    }
    if (SHOW(CF_HDG)) {
        snprintf(buf, sizeof buf, "HDG %03u", (unsigned)a->track_deg);
        gfx_text_right(g, R, 35, buf, COL(COL_HDG));
    }

    if (SHOW(CF_VS)) {
        if (vs_moving(a)) draw_vrate(g, 2, 44, 44, a);
        else gfx_text(g, 2, 44, "LEVEL FLIGHT", C_GREY);
    }
    special_banner(g, 50, a);

    if (SHOW(CF_PROGRESS) && a->has_route) {
        gfx_text(g, 2, 56, a->orig, C_GREY);
        gfx_text_right(g, R, 56, a->dest, C_GREY);
        draw_progress(g, 17, 58, 94, a->progress, true);
    }
}

/* 128×64 su 7 taškų šriftu (glotniuoju ar klasikiniu): eilutės 7 taškų aukščio, todėl kitas išdėstymas (6 eilutės vietoj 8) */
static void draw_big_tall(gfx_t *g, const aircraft_t *a, int idx, int n)
{
    char buf[24], pos[24] = "";
    const int R = 125;

    /* Maršrutas – pirmas: nuo jo pločio priklauso, ar toje eilutėje dar telpa registracija */
    int rx = 2;
    if (SHOW(CF_ROUTE)) {
        if (a->has_route) {
            rx = gfx_text(g, 2, 18, a->orig, COL(COL_TEXT));
            rx = gfx_text(g, rx + 2, 18, ">", C_GREY);
            rx = gfx_text(g, rx + 2, 18, a->dest, COL(COL_TEXT));
        } else {
            rx = gfx_text(g, 2, 18, "NO ROUTE DATA", C_GREY);
        }
    }

    /* Skrydžio Nr. dvigubo dydžio, jei telpa šalia tipo ir registracijos. Jei ilgas (8 simbolių, pvz., „ARGON316“)
     * netelpa, registracija nusileidžia į maršruto eilutę vietoj „1/5“, o „1/5“ pakyla po tipu.
     * Jei netelpa ir taip – skrydžio Nr. įprasto dydžio. */
    if (SHOW(CF_COUNTER)) snprintf(pos, sizeof pos, "%d/%d", idx + 1, n);
    const char *cs = ac_name(a);
    const bool type = SHOW(CF_TYPE) && a->type[0], reg = show_reg(a);
    const int cs_w = gfx_text_width_s(cs, 2), type_w = type ? gfx_text_width(a->type) : 0;
    const int reg_w = reg ? gfx_text_width(a->reg) : 0, pos_w = pos[0] ? gfx_text_width(pos) : 0;
    const char *mid = reg ? a->reg : NULL, *low = pos;          /* dešinėje, 9-oje ir 18-oje eilutėse */
    bool big = cs_w <= R - 2 - ((type_w > reg_w ? type_w : reg_w) + 4);
    if (!big && reg && cs_w <= R - 2 - ((type_w > pos_w ? type_w : pos_w) + 4) && rx + 4 + reg_w <= R + 1) {
        big = true;
        mid = pos[0] ? pos : NULL;
        low = a->reg;
    }
    if (big) gfx_text_s(g, 2, 1, cs, name_color(a), 2);
    else gfx_text(g, 2, 4, cs, name_color(a));
    if (type) gfx_text_right(g, R, 1, a->type, COL(COL_TYPE));
    if (mid) gfx_text_right(g, R, 9, mid, C_GREY);
    if (low[0]) gfx_text_right(g, R, 18, low, C_GREY);

    gfx_hline(g, 2, 27, 124, RGB565(30, 30, 32));

    int x = 2;
    if (SHOW(CF_ALT)) {
        fmt_alt(buf, sizeof buf, a);
        x = gfx_text(g, 2, 30, buf, COL(COL_ALT)) + 5;
    }
    if (SHOW(CF_DIST)) {
        fmt_dist(buf, sizeof buf, a->dist_km);
        gfx_text(g, x > 40 ? x : 40, 30, buf, COL(COL_TEXT));   /* ilgas aukštis („23725FT“) – toliau dešinėn */
    }
    if (SHOW(CF_SPEED)) {
        fmt_speed(buf, sizeof buf, a->speed_kt);
        gfx_text_right(g, R, 30, buf, COL(COL_TEXT));
    }

    if (SHOW(CF_VS)) {
        if (vs_moving(a)) draw_vrate(g, 2, 40, 41, a);
        else gfx_text(g, 2, 40, "LEVEL", C_GREY);
    }
    if (SHOW(CF_HDG)) {
        snprintf(buf, sizeof buf, "HDG %03u", (unsigned)a->track_deg);
        gfx_text_right(g, R, 40, buf, COL(COL_HDG));
    }
    special_banner(g, 48, a);

    if (SHOW(CF_PROGRESS) && a->has_route) {
        gfx_text(g, 2, 54, a->orig, C_GREY);
        gfx_text_right(g, R, 54, a->dest, C_GREY);
        const int x0 = 2 + gfx_text_width(a->orig) + 4, x1 = R - gfx_text_width(a->dest) - 4;
        draw_progress(g, x0, 58, x1 - x0, a->progress, true);
    }
}

static void draw_empty(gfx_t *g, bool big)
{
    char rg[12], buf[24];
    fmt_range(rg, sizeof rg);
    snprintf(buf, sizeof buf, "WITHIN %s", rg);
    gfx_text_center_fit(g, big ? 20 : 8, "NO FLIGHTS", C_GREY, big ? 2 : 1);
    gfx_text_center_fit(g, big ? 38 : 17, buf, RGB565(60, 60, 65), 1);
}

/* 64×64 langas: eilutės viena po kitos; nustatymuose išjungtos vietos neužima */
static void pane_layout(gfx_t *g, const aircraft_t *a, int idx, int n)
{
    const int rp = gfx_font_h() + 2, R = 62;
    char buf[24];
    int y = 1, x;

    /* Skrydžio Nr. ir „1/5“ */
    snprintf(buf, sizeof buf, "%d/%d", idx + 1, n);
    const bool cnt = SHOW(CF_COUNTER) && n > 1;
    const int cnt_w = cnt ? gfx_text_width(buf) + 3 : 0;
    gfx_text_max(g, 1, y, ac_name(a), name_color(a), R - cnt_w);
    if (cnt) gfx_text_right(g, R, y, buf, C_GREY);
    y += rp;

    /* Tipas ir registracija; ypatingo lėktuvo vietoj registracijos – „RARE“ / „MIL“ / „HELI“ / „BIZ“ */
    const bool type = SHOW(CF_TYPE) && a->type[0], reg = show_reg(a), sp = marked(a);
    if (type || reg || sp) {
        x = type ? gfx_text(g, 1, y, a->type, COL(COL_TYPE)) + 3 : 1;
        if (sp) gfx_text_right(g, R, y, special_short(a->special), C_RARE);
        else if (reg && x + gfx_text_width(a->reg) <= R + 1) gfx_text_right(g, R, y, a->reg, C_GREY);
        else if (!type) gfx_text_max(g, 1, y, a->reg, C_GREY, R);
        y += rp;
    }

    if (SHOW(CF_ROUTE)) {
        if (a->has_route) {
            x = gfx_text(g, 1, y, a->orig, COL(COL_TEXT));
            x = gfx_text(g, x + 1, y, ">", C_GREY);
            gfx_text(g, x + 1, y, a->dest, COL(COL_TEXT));
        } else {
            gfx_text(g, 1, y, "NO ROUTE", C_GREY);
        }
        y += rp;
    }

    /* Aukštis; vertikalusis greitis dešinėje, jei telpa (kitaip – tik rodyklė) */
    if (SHOW(CF_ALT) || SHOW(CF_VS)) {
        x = 1;
        if (SHOW(CF_ALT)) {
            fmt_alt(buf, sizeof buf, a);
            x = gfx_text(g, 1, y, buf, COL(COL_ALT)) + 2;
        }
        if (SHOW(CF_VS) && vs_moving(a)) {
            const int ay = y + (gfx_font_h() - 5) / 2;
            fmt_vrate(buf, sizeof buf, a->vrate_fpm);
            const int vw = gfx_text_width(buf);
            if (R - vw - 5 >= x - 1) {                   /* rodyklė, tarpas ir reikšmė iki dešiniojo krašto */
                draw_vs_arrow(g, R - vw - 5, ay, a);
                gfx_text_right(g, R, y, buf, vs_color(a));
            } else {
                draw_vs_arrow(g, x, ay, a);
            }
        } else if (SHOW(CF_VS) && !SHOW(CF_ALT)) {
            gfx_text(g, 1, y, "LEVEL", C_GREY);
        }
        y += rp;
    }

    /* Atstumas ir kursas */
    if (SHOW(CF_DIST) || SHOW(CF_HDG)) {
        x = 1;
        if (SHOW(CF_DIST)) {
            fmt_dist(buf, sizeof buf, a->dist_km);
            x = gfx_text(g, 1, y, buf, COL(COL_TEXT)) + 3;
        }
        if (SHOW(CF_HDG)) {
            snprintf(buf, sizeof buf, "%03u" GFX_DEG, (unsigned)a->track_deg);
            if (x + gfx_text_width(buf) <= R + 1) gfx_text_right(g, R, y, buf, COL(COL_HDG));
        }
        y += rp;
    }

    if (SHOW(CF_SPEED)) {
        fmt_speed(buf, sizeof buf, a->speed_kt);
        gfx_text(g, 1, y, buf, COL(COL_TEXT));
        y += rp;
    }

    if (SHOW(CF_PROGRESS) && a->has_route && y <= 57) draw_progress(g, 1, 60, 62, a->progress, true);
}

/* Padalinto ekrano 64×64 langas */
void card_pane(gfx_t *g)
{
    mode_win(WIN_CARD);
    int idx, n;
    const aircraft_t *a = current(&idx, &n);
    if (!a) {
        gfx_text_center_fit(g, 22, "NO FLIGHTS", C_GREY, 1);
        char rg[12], buf[24];
        fmt_range(rg, sizeof rg);
        snprintf(buf, sizeof buf, "WITHIN %s", rg);
        gfx_text_center_fit(g, 22 + gfx_font_h() + 3, buf, RGB565(60, 60, 65), 1);
        return;
    }
    flap_begin();
    pane_layout(g, a, idx, n);
    gfx_flap_off();
}

static void draw_one(gfx_t *g, const aircraft_t *a, int idx, int n)
{
    const bool big = g->w >= 128 && g->h >= 64;
#ifdef SKY_FLIGHTWALL
    if (big && g_mode_prefs.flightwall) {           /* atskiras variantas: kortelė kaip FlightWall */
        fw_card_draw(g, a, s_ms - s_card_t);
        return;
    }
#endif
    if (g->w < 128 && g->h >= 64) pane_layout(g, a, idx, n);
    else if (big && gfx_font_h() >= 7) draw_big_tall(g, a, idx, n);
    else if (big) draw_big(g, a, idx, n);
    else draw_small(g, a, idx, n);
}

void card_show(gfx_t *g, const aircraft_t *a)
{
    draw_one(g, a, 0, 1);
}

static void card_draw(gfx_t *g)
{
    mode_win(WIN_CARD);
    int idx, n;
    const aircraft_t *a = current(&idx, &n);
    if (!a) {
        draw_empty(g, g->w >= 128 && g->h >= 64);
        return;
    }
    flap_begin();
    draw_one(g, a, idx, n);
    gfx_flap_off();
}

const display_mode_t MODE_FLIGHT_CARD = {
    .name = "card", .enter = card_enter, .update = card_update, .draw = card_draw,
};
