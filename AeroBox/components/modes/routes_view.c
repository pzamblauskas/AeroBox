/* Maršrutų žemėlapis (VIEW_ROUTES, AUTO ciklas, padalinto ekrano langas): tie patys lėktuvai kaip radaro sąraše
 * (sekamas – pirmas, toliau artimiausi, iki MAP_ROUTES) ir tomis pačiomis spalvomis (sekamas – baltas). Žinomas
 * maršrutas – linija per lėktuvą, nežinomas – tik lėktuvas. Paeiliui išskiriamas po vieną: ryškus maršrutas, oro uostų
 * kodai, lėktuvas su žiedu; viršuje – skrydis, tipas ir maršrutas arba „NO DATA“. Jei nežinomas nė vienas maršrutas,
 * žemėlapis priartinamas aplink namus. Piešia route_map.c. */
#include "mode.h"
#include "mode_util.h"
#include <stdio.h>
#include <string.h>

#define HEAD 7                        /* viršutinė eilutė 3×5 šriftu */

#define LIST 4                        /* artimų – tiek, kiek radaro sąraše */

static const uint16_t COLS[MAP_ROUTES] = { C_AMBER, C_CYAN, RGB565(255, 70, 190), RGB565(160, 110, 255),
                                           RGB565(90, 230, 120) };

typedef struct {
    int               n;              /* lėktuvų žemėlapyje */
    bool              routes;         /* žinomas bent vieno maršrutas */
    const aircraft_t *ac[MAP_ROUTES];
    route_t           r[MAP_ROUTES];
    const route_t    *rp[MAP_ROUTES];
    uint16_t          col[MAP_ROUTES];
} pick_t;

static const aircraft_snapshot_t *s_snap;
static uint32_t s_ms, s_t0;

/* Kaip radaro sąraše: sekamas – pirmas, toliau artimiausi; maršrutas – jei žinomas su koordinatėmis ir tinka šiam
 * lėktuvui (has_route) */
static void pick(const aircraft_snapshot_t *s, pick_t *p)
{
    memset(p, 0, sizeof *p);
    const int cnt = s ? s->count : 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < cnt && p->n < LIST; i++) {
            const aircraft_t *a = &s->ac[i];
            if (a->tracked != (pass == 0)) continue;
            p->ac[p->n] = a;
            if (a->has_route && route_cache_get(a->callsign, &p->r[p->n]) && p->r[p->n].known && p->r[p->n].has_pos) {
                p->rp[p->n] = &p->r[p->n];
                p->routes = true;
            }
            p->n++;
        }
}

/* AUTO: kiek lėktuvų būtų rodoma (po ROUTES_HI_MS kiekvienam); 0 – nėra nė vieno maršruto (tada užtenka radaro) */
int routes_count(const aircraft_snapshot_t *s)
{
    pick_t p;
    pick(s, &p);
    return p.routes ? p.n : 0;
}

/* Viršutinė eilutė: išskirtas skrydis savo spalva, tipas, „CRL > TSF“, „2/3“; be maršruto – „NO DATA“ šalia skrydžio,
 * tipas dešinėje */
static void header(gfx_t *g, const pick_t *p, int hi)
{
    const int keep = gfx_font();
    gfx_set_font(GFX_FONT_SMALL);
    const int W = g->w - 1;
    char buf[24];
    if (hi < 0) {
        gfx_text(g, 1, 1, "NO FLIGHTS", C_GREY);
        gfx_set_font(keep);
        return;
    }
    const aircraft_t *a = p->ac[hi];
    int right = W;
    if (p->n > 1 && g->w >= 128) {
        snprintf(buf, sizeof buf, "%d/%d", hi + 1, p->n);
        gfx_text_right(g, W, 1, buf, C_GREY);
        right = W - gfx_text_width(buf) - 4;
    }
    if (!p->rp[hi]) {
        const int nr = gfx_text_width(MAP_NO_ROUTE), tw = a->type[0] ? gfx_text_width(a->type) : 0;
        const int x = gfx_text_max(g, 1, 1, ac_name(a), p->col[hi], right - nr - 4);
        gfx_text(g, x + 3, 1, MAP_NO_ROUTE, C_GREY);
        if (tw && x + 3 + nr + 6 + tw <= right + 1) gfx_text_right(g, right, 1, a->type, C_GREY);
        gfx_set_font(keep);
        return;
    }
    snprintf(buf, sizeof buf, "%s > %s", p->r[hi].orig, p->r[hi].dest);
    if (gfx_text_width(ac_name(a)) + 4 + gfx_text_width(buf) > right)
        snprintf(buf, sizeof buf, "%s>%s", p->r[hi].orig, p->r[hi].dest);
    const int rx = right - gfx_text_width(buf) + 1;
    gfx_text(g, rx, 1, buf, COL(COL_TEXT));
    const int x = gfx_text_max(g, 1, 1, ac_name(a), p->col[hi], rx - 5);
    if (a->type[0] && x + 4 + gfx_text_width(a->type) <= rx - 4) gfx_text(g, x + 4, 1, a->type, C_GREY);   /* ne maršruto spalva */
    gfx_set_font(keep);
}

/* Viršuje išskirtasis, po juo žemėlapis; t – laikas, pagal kurį nustatomas išskirtasis; spalvos – kaip radaro sąraše */
static void show(gfx_t *g, pick_t *p, uint32_t t, uint32_t ms)
{
    mode_win(WIN_MAP);
    /* Sekamas – pasirinkta lėktuvo spalva (Colours → Plane and flight); kiti – sąrašo spalvos, išskyrus tą pačią */
    const uint16_t tc = COL(COL_FLIGHT);
    bool tracked = false;
    for (int i = 0; i < p->n; i++) tracked |= p->ac[i]->tracked;
    for (int i = 0, k = 0; i < p->n; i++) {
        if (p->ac[i]->tracked) {
            p->col[i] = tc;
            continue;
        }
        if (tracked && k < MAP_ROUTES - 1 && COLS[k] == tc) k++;
        p->col[i] = COLS[k < MAP_ROUTES ? k : MAP_ROUTES - 1];
        k++;
    }
    const int hi = p->n ? (int)((t / ROUTES_HI_MS) % (uint32_t)p->n) : -1;
    header(g, p, hi);
    map_draw_multi(g, 0, HEAD, g->w, g->h - HEAD, p->ac, p->rp, p->col, p->n, hi, (int)(ms % MAP_PULSE_MS));
}

static void draw_routes(gfx_t *g, const aircraft_snapshot_t *s, uint32_t t, uint32_t ms)
{
    pick_t p;
    pick(s, &p);
    show(g, &p, t, ms);
}

void routes_draw_snap(gfx_t *g, const aircraft_snapshot_t *s, uint32_t t)
{
    draw_routes(g, s, t, t);
}

void routes_show(gfx_t *g, const aircraft_t *const *ac, const route_t *const *r, int n, uint32_t t)
{
    pick_t p;
    memset(&p, 0, sizeof p);
    for (int i = 0; i < n && i < MAP_ROUTES; i++) {
        p.ac[i] = ac[i];
        p.r[i] = *r[i];
        p.rp[i] = &p.r[i];
        p.n++;
    }
    p.routes = p.n > 0;
    show(g, &p, t, t);
}

static void routes_enter(void)
{
    s_t0 = 0;                         /* įsijungus – nuo artimiausio */
}

static void routes_update(const aircraft_snapshot_t *s, uint32_t ms)
{
    s_snap = s;
    s_ms = ms;
    if (!s_t0) s_t0 = ms ? ms : 1;
}

static void routes_draw(gfx_t *g)
{
    draw_routes(g, s_snap, s_ms - s_t0, s_ms);
}

const display_mode_t MODE_ROUTES = {
    .name = "routes", .enter = routes_enter, .update = routes_update, .draw = routes_draw,
};

/* Padalinto ekrano langas: kortelės lėktuvas (be teksto, nes šalia esanti kortelė pati rodo skrydį); jei kortelės
 * nėra – sekamas skrydis (ir už spindulio; viršuje skrydis ir maršrutas); kitaip artimiausių maršrutai paeiliui,
 * kaip MODE_ROUTES */
void map_pane(gfx_t *g, const aircraft_snapshot_t *s, uint32_t focus_icao, uint32_t ms)
{
    mode_win(WIN_MAP);
    const int n = s ? s->count : 0;
    route_t r;
    if (focus_icao) {
        for (int i = 0; i < n; i++)
            if (s->ac[i].icao24 == focus_icao) {
                const bool route = route_cache_get(s->ac[i].callsign, &r) && r.known && r.has_pos;
                map_draw_in(g, 0, 0, g->w, g->h, &s->ac[i], route ? &r : NULL, -1);
                return;
            }
    }
    track_t t;
    if (track_get(&t) && (t.state == TRACK_LIVE || t.state == TRACK_LOST)) {
        const aircraft_t *a = &t.ac;                  /* spinduliu esantis imamas iš sąrašo (jis tolygiai slenka) */
        for (int i = 0; i < n; i++)
            if (s->ac[i].tracked) a = &s->ac[i];
        const bool route = a->has_route && route_cache_get(a->callsign, &r) && r.known && r.has_pos;
        /* Viršuje skrydis ir maršrutas (be jo – „NO DATA“), po jais žemėlapis */
        const int keep = gfx_font();
        gfx_set_font(GFX_FONT_SMALL);
        char buf[16];
        if (route) snprintf(buf, sizeof buf, "%s>%s", r.orig, r.dest);
        const char *rt = route ? buf : MAP_NO_ROUTE;
        const int rx = g->w - gfx_text_width(rt);
        gfx_text(g, rx, 1, rt, route ? COL(COL_TEXT) : C_GREY);
        gfx_text_max(g, 1, 1, ac_name(a), C_WHITE, rx - 5);
        gfx_set_font(keep);
        const bool live = t.state == TRACK_LIVE && !t.landed;
        map_draw_in(g, 0, HEAD, g->w, g->h - HEAD, a, route ? &r : NULL,
                    live && g_mode_prefs.track_pulse ? (int)(ms % MAP_PULSE_MS) : -1);
        return;
    }
    draw_routes(g, s, ms, ms);
}
