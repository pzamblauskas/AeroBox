/* Sekamas skrydis (track.h): skrydis, tipas, registracija, maršrutas ir aviakompanija, aukštis, greitis,
 * atstumas nuo namų, kiek liko iki nusileidimo ir kada nusileis, eigos juosta.
 * Kai nieko nesekama – kaip pradėti sekti. 64×64 – padalinto ekrano langas. Spalvos – pagal temą.
 * MODE_TRACK_MAP – sekamo skrydžio maršruto žemėlapis per visą ekraną (TRACKING ir AUTO cikluose po kortelės). */
#include "mode.h"
#include "mode_util.h"
#include "app_status.h"
#include "track.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define C_LABEL RGB565(90, 90, 95)
#define C_ETA   COL(COL_FLIGHT)

static uint32_t s_ms;
static uint32_t s_card_t0;             /* kada įjungta kortelė (užrašo ir kortelės ciklas skaičiuojamas nuo čia) */
static bool s_card_new;

static void track_enter(void) { }

static void track_card_enter(void)
{
    s_card_new = true;
}

static void track_update(const aircraft_snapshot_t *s, uint32_t ms)
{
    s_ms = ms;
    if (s_card_new) {
        s_card_new = false;
        s_card_t0 = ms;
    }
}

uint32_t track_cycle_ms(void)
{
    const uint32_t card = card_ms() > TRACK_CARD_MS ? card_ms() : TRACK_CARD_MS;
    return card + (g_mode_prefs.track_label[0] ? TRACK_LABEL_MS : 0);
}

/* „12 MIN“ / „1H 25M“ */
static void fmt_eta(char *out, size_t n, int min)
{
    min = min < 0 ? 0 : min > 9999 ? 9999 : min;   /* ilgiausias tekstas „166H 39M“ telpa į trumpiausią buferį */
    if (min < 60) snprintf(out, n, "%d MIN", min);
    else snprintf(out, n, "%dH %02dM", min / 60, min % 60);
}

/* Trumpai: „IN 12 MIN“; mažiau nei minutė – „LANDING“ */
static void short_eta(char *out, size_t n, int min)
{
    char e[12];
    if (min <= 0) {
        snprintf(out, n, "LANDING");
        return;
    }
    fmt_eta(e, sizeof e, min);
    snprintf(out, n, "IN %s", e);
}

/* Nusileidimo laikas vietos laiku: „16:02“ / „4:02PM“; tuščia – laikas nežinomas */
static void fmt_arrival(char *out, size_t n, int min)
{
    out[0] = 0;
    if (!sky_time_valid()) return;
    time_t t = time(NULL) + (time_t)min * 60;
    struct tm tm;
    localtime_r(&t, &tm);
    if (g_mode_prefs.clock12)
        snprintf(out, n, "%d:%02d%s", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    else
        snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

/* Pagrindinė būsenos eilutė: kiek liko / leidžiasi / nusileido / ryšys dingo / ant žemės (dar neskrido) / atstumas */
static void status_text(const track_t *t, char *left, size_t ln, char *right, size_t rn, uint16_t *col)
{
    char buf[16];
    right[0] = 0;
    *col = C_ETA;
    if (t->landed) {
        if (t->ac.has_route) snprintf(left, ln, "LANDED AT %s", t->ac.dest);
        else snprintf(left, ln, "LANDED");
        *col = COL(COL_RADAR);
    } else if (t->state == TRACK_LOST) {
        snprintf(left, ln, "NO SIGNAL %u MIN", (unsigned)(t->age_s / 60));
        *col = C_GREY;
    } else if (t->ac.on_ground) {
        snprintf(left, ln, "ON GROUND");
        fmt_dist(right, rn, t->ac.dist_km);
        *col = C_GREY;
    } else if (t->eta_min == 0) {
        snprintf(left, ln, "LANDING NOW");
    } else if (t->eta_min > 0) {
        fmt_eta(buf, sizeof buf, t->eta_min);
        snprintf(left, ln, "LANDS IN %s", buf);
        fmt_arrival(right, rn, t->eta_min);
    } else if (t->in_range) {
        fmt_dist(buf, sizeof buf, t->ac.dist_km);
        snprintf(left, ln, "NEARBY %s", buf);
        *col = COL(COL_RADAR);
    } else {
        fmt_dist(buf, sizeof buf, t->ac.dist_km);
        snprintf(left, ln, "%s AWAY", buf);
        snprintf(right, rn, "HDG %03u", (unsigned)t->ac.track_deg);
        *col = COL(COL_HDG);
    }
}

/* Ypatingo lėktuvo (jei žymima) tipas 64 taškų pločio ekrane – rožinis, kaip kortelėse (128 taškų – juosta) */
static uint16_t type_color(const aircraft_t *a)
{
    return g_mode_prefs.rare && a->special ? C_RARE : COL(COL_TYPE);
}

/* Ar būsenos eilutė jau rodo atstumą („120KM AWAY“, „NEARBY 12KM“) – kitoje vietoje jo nekartoti */
static bool status_has_dist(const track_t *t)
{
    return !t->landed && t->state != TRACK_LOST && t->eta_min < 0;
}

/* Nieko nesekama / ieškoma / šiuo metu ore nėra */
static bool draw_waiting(gfx_t *g, const track_t *t, bool on, bool big)
{
    const char *l1, *l2, *l3 = NULL;
    uint16_t c2 = C_GREY;
    const bool narrow = g->w < 128;                  /* 64 taškų pločio ekrane – trumpesni užrašai */
    if (!on) {
        l1 = narrow ? "NOT TRACKING" : "NO FLIGHT TRACKED";
        l2 = "SET ONE AT";
        l3 = "AEROBOX.LOCAL";
    } else if (t->state == TRACK_SEARCHING) {
        l1 = g_mode_prefs.track_label[0] ? g_mode_prefs.track_label : t->query;   /* naudotojo užrašas rodomas vietoj įvesties */
        l2 = (s_ms / 400) % 4 == 0 ? "SEARCHING" : (s_ms / 400) % 4 == 1 ? "SEARCHING." :
             (s_ms / 400) % 4 == 2 ? "SEARCHING.." : "SEARCHING...";
    } else if (t->state == TRACK_NOT_FOUND) {
        l1 = g_mode_prefs.track_label[0] ? g_mode_prefs.track_label : t->query;
        l2 = "NOT FLYING NOW";
        l3 = narrow ? "WAITING" : "SHOWN WHEN AIRBORNE";
        c2 = COL(COL_FLIGHT);
    } else {
        return false;
    }
    const uint16_t c1 = on ? COL(COL_FLIGHT) : C_GREY, c3 = on ? C_LABEL : COL(COL_FLIGHT);
    if (big) {
        gfx_text_center_fit(g, on ? 10 : 14, l1, c1, on ? 2 : 1);
        gfx_text_center_fit(g, on ? 32 : 30, l2, c2, 1);
        if (l3) gfx_text_center_fit(g, on ? 44 : 42, l3, c3, 1);
    } else {                                         /* 64×32 ir 64×64 langas (pastarajame – per vidurį) */
        const int rp = gfx_font_h() + 2, y0 = g->h >= 64 ? (g->h - 3 * rp) / 2 : 3;
        gfx_text_center_fit(g, y0, l1, c1, 1);
        gfx_text_center_fit(g, y0 + rp, l2, c2, 1);
        if (l3) gfx_text_center_fit(g, y0 + 2 * rp, l3, c3, 1);
    }
    return true;
}

static void draw_big(gfx_t *g, const track_t *t)
{
    const aircraft_t *a = &t->ac;
    const int R = 125, fh = gfx_font_h();
    const bool live = t->state == TRACK_LIVE;
    char buf[24], right[16];

    /* Viršus: skrydis dvigubo dydžio, dešinėje tipas ir registracija. Jei ilgas skrydžio Nr. (pvz., „ARGON316“) šalia
     * abiejų netelpa, registracija nusileidžia į maršruto eilutę (vietoj aviakompanijos); netelpa ir taip – įprasto dydžio */
    const char *cs = ac_name(a);
    const int cs_w = gfx_text_width_s(cs, 2), type_w = a->type[0] ? gfx_text_width(a->type) : 0;
    const bool show_reg = a->reg[0] && strcmp(a->reg, cs) != 0;
    const int reg_w = show_reg ? gfx_text_width(a->reg) : 0;
    const int route_w = a->has_route ? gfx_text_width(a->orig) + gfx_text_width(">") + gfx_text_width(a->dest) + 4
                                     : gfx_text_width("NO ROUTE DATA");
    bool big = cs_w <= R - 2 - ((type_w > reg_w ? type_w : reg_w) + 4), reg_low = false;
    if (!big && show_reg && cs_w <= R - 2 - (type_w + 4) && 2 + route_w + 6 + reg_w <= R) big = reg_low = true;
    const int ycs = fh >= 7 ? 1 : 2;
    if (big) gfx_text_s(g, 2, ycs, cs, C_WHITE, 2);
    else gfx_text(g, 2, ycs + 3, cs, C_WHITE);
    if (a->type[0]) gfx_text_right(g, R, ycs, a->type, COL(COL_TYPE));
    if (show_reg && !reg_low) gfx_text_right(g, R, ycs + fh + 2, a->reg, C_GREY);

    /* Maršrutas ir aviakompanija (arba žemyn perkelta registracija) */
    const int yr = fh >= 7 ? 18 : 17;
    int x;
    if (a->has_route) {
        x = gfx_text(g, 2, yr, a->orig, COL(COL_TEXT));
        x = gfx_text(g, x + 2, yr, ">", C_GREY);
        x = gfx_text(g, x + 2, yr, a->dest, COL(COL_TEXT));
    } else {
        x = gfx_text(g, 2, yr, "NO ROUTE DATA", C_GREY);
    }
    if (reg_low) gfx_text_right(g, R, yr, a->reg, C_GREY);
    else if (a->has_route && t->airline[0] && x + 6 + gfx_text_width(t->airline) <= R)
        gfx_text_right(g, R, yr, t->airline, C_LABEL);
    else if (a->has_route && t->dest_city[0] && x + 6 + gfx_text_width(t->dest_city) <= R)
        gfx_text_right(g, R, yr, t->dest_city, C_LABEL);
    gfx_hline(g, 2, 27, 124, RGB565(30, 30, 32));

    /* Aukštis, greitis, atstumas nuo namų */
    const int y1 = fh >= 7 ? 30 : 31;
    fmt_alt(buf, sizeof buf, a);
    x = gfx_text(g, 2, y1, buf, live ? COL(COL_ALT) : C_GREY);
    x = draw_vs_arrow(g, x + 1, y1 + (fh - 5) / 2, a);
    fmt_speed(buf, sizeof buf, a->speed_kt);
    x = gfx_text(g, x + 4, y1, buf, live ? COL(COL_TEXT) : C_GREY);
    if (!status_has_dist(t)) {
        fmt_dist(buf, sizeof buf, a->dist_km);
        gfx_text_right(g, R, y1, buf, t->in_range ? COL(COL_RADAR) : C_LABEL);
    } else if (live && vs_moving(a)) {               /* atstumas jau rodomas būsenos eilutėje, todėl čia – vertikalusis greitis */
        fmt_vrate(buf, sizeof buf, a->vrate_fpm);
        if (x + 6 + gfx_text_width(buf) <= R) gfx_text_right(g, R, y1, buf, vs_color(a));
    }

    /* Kiek liko iki nusileidimo ir kada nusileis */
    uint16_t col;
    status_text(t, buf, sizeof buf, right, sizeof right, &col);
    if (right[0] && t->eta_min > 0 && !t->landed && gfx_text_width(buf) + 6 + gfx_text_width(right) > R - 2)
        short_eta(buf, sizeof buf, t->eta_min);      /* nusileidimo laikas netelpa – rodoma „IN 10 MIN“ */
    const int y2 = 41;
    gfx_text(g, 2, y2, buf, col);
    if (right[0] && gfx_text_width(buf) + 6 + gfx_text_width(right) <= R - 2) gfx_text_right(g, R, y2, right, COL(COL_TEXT));
    /* Ypatingas (karinis, retas …) – rožinė juosta; su 7 taškų šriftu eigos juosta tada 2 taškais žemiau */
    const bool sp = g_mode_prefs.rare && a->special;
    special_banner(g, !a->has_route ? 53 : fh >= 7 ? 49 : 48, a);

    /* Eigos juosta tarp oro uostų */
    if (a->has_route) {
        const int yb = fh >= 7 && !sp ? 54 : 56;
        gfx_text(g, 2, yb, a->orig, C_GREY);
        gfx_text_right(g, R, yb, a->dest, C_GREY);
        const int x0 = 2 + gfx_text_width(a->orig) + 4, x1 = R - gfx_text_width(a->dest) - 4;
        draw_progress(g, x0, yb + (fh >= 7 ? 4 : 2), x1 - x0, a->progress, live);
    }
}

static void draw_small(gfx_t *g, const track_t *t)
{
    const aircraft_t *a = &t->ac;
    char buf[24], right[16];
    const int type_w = a->type[0] ? gfx_text_width(a->type) + 2 : 0;
    gfx_text_max(g, 0, 1, ac_name(a), C_WHITE, 64 - type_w);
    if (a->type[0]) gfx_text_right(g, 63, 1, a->type, type_color(a));

    if (a->has_route) {
        int x = gfx_text(g, 0, 8, a->orig, COL(COL_TEXT));
        x = gfx_text(g, x, 8, ">", C_GREY);
        gfx_text(g, x, 8, a->dest, COL(COL_TEXT));
    } else {
        gfx_text(g, 0, 8, "NO ROUTE", C_GREY);
    }
    if (!status_has_dist(t)) {
        fmt_dist(buf, sizeof buf, a->dist_km);
        gfx_text_right(g, 63, 8, buf, t->in_range ? COL(COL_RADAR) : C_LABEL);
    }

    uint16_t col;
    status_text(t, buf, sizeof buf, right, sizeof right, &col);
    if (t->eta_min >= 0 && !t->landed) short_eta(buf, sizeof buf, t->eta_min);   /* „IN 12 MIN      16:02“ */
    gfx_text_max(g, 0, 15, buf, col, 64);
    if (right[0] && gfx_text_width(buf) + 3 + gfx_text_width(right) <= 64) gfx_text_right(g, 63, 15, right, COL(COL_TEXT));

    fmt_alt(buf, sizeof buf, a);
    int x = gfx_text(g, 0, 22, buf, COL(COL_ALT));
    x = draw_vs_arrow(g, x + 1, 22, a);
    fmt_speed(buf, sizeof buf, a->speed_kt);
    if (x + 2 + gfx_text_width(buf) <= 64) gfx_text_right(g, 63, 22, buf, COL(COL_TEXT));
    if (a->has_route) draw_progress(g, 0, 30, 64, a->progress, t->state == TRACK_LIVE);
}

void track_card_show(gfx_t *g, const track_t *t)
{
    if (g->w >= 128 && g->h >= 64) draw_big(g, t);
    else draw_small(g, t);
}

/* Užrašas kuo didesnis 14 taškų aukščio vietoje nuo y: 14 taškų, 10 taškų (glotnusis) arba įprastas šriftas */
static void label_big(gfx_t *g, int y, const char *s, uint16_t c)
{
    if (gfx_text_width_s(s, 2) <= g->w - 2) {
        gfx_text_center_s(g, y, s, c, 2);
        return;
    }
    const int keep = gfx_font();
    if (keep != GFX_FONT_CLASSIC) {                  /* taškiniam šriftui – be glotnaus tarpinio dydžio */
        gfx_set_font(GFX_FONT_WIDE);                 /* 4×5 × 2 = glotnus 10 taškų */
        const bool fit = gfx_text_width_s(s, 2) <= g->w - 2;
        if (fit) gfx_text_center_s(g, y + 2, s, c, 2);
        gfx_set_font(keep);
        if (fit) return;
    }
    gfx_text_center_fit(g, y + 4, s, c, 1);
}

/* Ilgas užrašas siauram ekranui – dviem eilutėmis: laužoma ties tarpu arčiausiai vidurio (be tarpų – per vidurį) */
static void label_split(const char *s, char *l1, char *l2, size_t n)
{
    const int len = (int)strlen(s);
    int cut = -1;
    for (int i = 1; i < len; i++)
        if (s[i] == ' ' && (cut < 0 || abs(2 * i - len) < abs(2 * cut - len))) cut = i;
    const int k = cut > 0 ? cut : len / 2;
    snprintf(l1, n, "%.*s", k, s);
    snprintf(l2, n, "%s", s + k + (cut > 0 ? 1 : 0));
}

/* Naudotojo užrašas („MOM'S FLIGHT“) dideliu šriftu, po juo skrydis ir maršrutas, būsena, o apačioje lėktuvas skrenda
 * linija tarp oro uostų (kaip kortelėje) */
static void draw_label(gfx_t *g, const track_t *t)
{
    const aircraft_t *a = &t->ac;
    const bool big = g->w >= 128 && g->h >= 64;
    const char *label = g_mode_prefs.track_label;
    char line[40], buf[48], right[16];
    uint16_t col;
    status_text(t, buf, sizeof buf, right, sizeof right, &col);
    if (big) {
        if (a->has_route) snprintf(line, sizeof line, "%s  %s > %s", ac_name(a), a->orig, a->dest);
        else snprintf(line, sizeof line, "%s", ac_name(a));
        label_big(g, 4, label, C_WHITE);
        gfx_text_center_fit(g, 26, line, COL(COL_TEXT), 1);
        if (right[0]) {
            char both[sizeof buf + sizeof right + 2];
            snprintf(both, sizeof both, "%s  %s", buf, right);
            if (gfx_text_width(both) <= g->w - 4) strlcpy(buf, both, sizeof buf);
        }
        gfx_text_center_fit(g, 38, buf, col, 1);
        if (a->has_route) {
            const int fh = gfx_font_h(), yb = 54, R = g->w - 3;
            gfx_text(g, 2, yb, a->orig, C_GREY);
            gfx_text_right(g, R, yb, a->dest, C_GREY);
            const int x0 = 2 + gfx_text_width(a->orig) + 4, x1 = R - gfx_text_width(a->dest) - 4;
            draw_progress(g, x0, yb + (fh >= 7 ? 4 : 2), x1 - x0, a->progress, t->state == TRACK_LIVE);
        }
        return;
    }
    /* 64×32: jei telpa viena eilute – užrašas, maršrutas, būsena; jei ne – užrašas dviem eilutėmis ir būsena */
    if (t->eta_min >= 0 && !t->landed) short_eta(buf, sizeof buf, t->eta_min);
    const int keep = gfx_font();
    gfx_set_font(GFX_FONT_SMALL);
    const bool one = gfx_text_width(label) <= g->w - 2;
    gfx_set_font(keep);
    if (one) {
        gfx_text_center_fit(g, 1, label, C_WHITE, 1);
        if (a->has_route) {
            snprintf(line, sizeof line, "%s>%s", a->orig, a->dest);
            gfx_text_center_fit(g, 9, line, COL(COL_TEXT), 1);
        }
    } else {
        char l1[sizeof g_mode_prefs.track_label], l2[sizeof g_mode_prefs.track_label];
        label_split(label, l1, l2, sizeof l1);
        gfx_text_center_fit(g, 1, l1, C_WHITE, 1);
        gfx_text_center_fit(g, 8, l2, C_WHITE, 1);
    }
    gfx_text_center_fit(g, 17, buf, col, 1);
    if (a->has_route) draw_progress(g, 0, 30, g->w, a->progress, t->state == TRACK_LIVE);
}

static void track_draw(gfx_t *g)
{
    mode_win(WIN_TRACK);
    track_t t;
    const bool on = track_get(&t);
    if (draw_waiting(g, &t, on, g->w >= 128 && g->h >= 64)) return;
    /* Su užrašu: kiekvieno ciklo pradžioje – užrašo langas, tada kortelė */
    if (g_mode_prefs.track_label[0] && (s_ms - s_card_t0) % track_cycle_ms() < TRACK_LABEL_MS) {
        draw_label(g, &t);
        return;
    }
    track_card_show(g, &t);
}

/* Padalinto ekrano 64×64 langas: eilutės viena po kitos */
void track_pane(gfx_t *g)
{
    mode_win(WIN_TRACK);
    track_t t;
    const bool on = track_get(&t);
    if (draw_waiting(g, &t, on, false)) return;
    const aircraft_t *a = &t.ac;
    const bool live = t.state == TRACK_LIVE;
    const int rp = gfx_font_h() + 2, R = 62;
    char buf[24], right[16];
    int y = 1, x;

    const int type_w = a->type[0] ? gfx_text_width(a->type) + 3 : 0;
    gfx_text_max(g, 1, y, ac_name(a), C_WHITE, R - type_w);
    if (a->type[0]) gfx_text_right(g, R, y, a->type, type_color(a));
    y += rp;

    if (a->has_route) {
        x = gfx_text(g, 1, y, a->orig, COL(COL_TEXT));
        x = gfx_text(g, x + 1, y, ">", C_GREY);
        gfx_text(g, x + 1, y, a->dest, COL(COL_TEXT));
    } else {
        gfx_text(g, 1, y, "NO ROUTE", C_GREY);
    }
    y += rp;

    /* Būsena trumpai („IN 12 MIN“), po ja – nusileidimo laikas */
    uint16_t col;
    status_text(&t, buf, sizeof buf, right, sizeof right, &col);
    if (t.eta_min >= 0 && !t.landed) short_eta(buf, sizeof buf, t.eta_min);
    gfx_text_max(g, 1, y, buf, col, R);
    y += rp;
    if (right[0]) {
        gfx_text(g, 1, y, right, COL(COL_TEXT));
        y += rp;
    }

    fmt_alt(buf, sizeof buf, a);
    x = gfx_text(g, 1, y, buf, live ? COL(COL_ALT) : C_GREY);
    draw_vs_arrow(g, x + 1, y + (gfx_font_h() - 5) / 2, a);
    y += rp;

    fmt_speed(buf, sizeof buf, a->speed_kt);
    x = gfx_text(g, 1, y, buf, live ? COL(COL_TEXT) : C_GREY) + 3;
    fmt_dist(buf, sizeof buf, a->dist_km);
    if (!status_has_dist(&t) && x + gfx_text_width(buf) <= R + 1)
        gfx_text_right(g, R, y, buf, t.in_range ? COL(COL_RADAR) : C_LABEL);
    y += rp;

    if (a->has_route && y <= 57) draw_progress(g, 1, 60, 62, a->progress, live);
}

const display_mode_t MODE_TRACK = {
    .name = "track", .enter = track_card_enter, .update = track_update, .draw = track_draw,
};

/* Sekamo skrydžio maršruto žemėlapis: viršuje skrydis ir būsena 3×5 šriftu, po jais – žemėlapis (route_map.c) */
void track_map_show(gfx_t *g, const track_t *t, const route_t *r, uint32_t ms)
{
    mode_win(WIN_MAP);
    const aircraft_t *a = &t->ac;
    route_t rc;
    if (!r && a->has_route && route_cache_get(a->callsign, &rc) && rc.known && rc.has_pos) r = &rc;
    const bool route = r && r->has_pos && a->has_route;

    /* Kairėje skrydis (be maršruto – dar ir „NO DATA“), dešinėje būsena ir nusileidimo laikas („LANDS IN 45 MIN  16:02“);
     * jei netelpa – trumpiau; jei netelpa ir trumpiausias – be „NO DATA“ */
    const int keep = gfx_font();
    gfx_set_font(GFX_FONT_SMALL);
    char buf[24], right[16];
    uint16_t col;
    const int W = g->w - 1, cs_w = gfx_text_width(ac_name(a));
    int nr_w = route ? 0 : gfx_text_width(MAP_NO_ROUTE) + 4;
    for (;;) {
        const int room = W - cs_w - 5 - nr_w;
        status_text(t, buf, sizeof buf, right, sizeof right, &col);
        if (gfx_text_width(buf) + (right[0] ? gfx_text_width(right) + 5 : 0) > room) right[0] = 0;
        if (gfx_text_width(buf) > room) {
            if (t->landed) {
                snprintf(buf, sizeof buf, "LANDED");
            } else if (t->state == TRACK_LOST) {
                snprintf(buf, sizeof buf, "NO SIGNAL");
            } else if (t->eta_min >= 0) {
                short_eta(buf, sizeof buf, t->eta_min);
                if (gfx_text_width(buf) > room && t->eta_min > 0) fmt_eta(buf, sizeof buf, t->eta_min);
            } else {
                fmt_dist(buf, sizeof buf, a->dist_km);
            }
        }
        if (!nr_w || gfx_text_width(buf) <= room) break;
        nr_w = 0;
    }
    const int tw = right[0] ? gfx_text_width(right) + 5 : 0, sx = W - tw - gfx_text_width(buf) + 1;
    if (right[0]) gfx_text_right(g, W, 1, right, COL(COL_TEXT));
    gfx_text(g, sx, 1, buf, col);
    const int x = gfx_text_max(g, 1, 1, ac_name(a), C_WHITE, sx - 5 - nr_w);
    if (nr_w) gfx_text(g, x + 3, 1, MAP_NO_ROUTE, C_GREY);
    gfx_set_font(keep);

    const int pulse = g_mode_prefs.track_pulse && t->state == TRACK_LIVE && !t->landed ? (int)(ms % MAP_PULSE_MS) : -1;
    map_draw_in(g, 0, 7, g->w, g->h - 7, a, route ? r : NULL, pulse);
}

/* Padėties nėra – laukimo užrašai kaip kortelėje */
static void track_map_draw(gfx_t *g)
{
    mode_win(WIN_MAP);
    track_t t;
    const bool on = track_get(&t);
    if (draw_waiting(g, &t, on, g->w >= 128 && g->h >= 64)) return;
    track_map_show(g, &t, NULL, s_ms);
}

const display_mode_t MODE_TRACK_MAP = {
    .name = "track_map", .enter = track_enter, .update = track_update, .draw = track_map_draw,
};
