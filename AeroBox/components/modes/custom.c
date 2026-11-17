/* Padalintas ekranas (VIEW_CUSTOM): 128×64 padalijamas į dvi 64×64 puses, kiekvienoje – naudotojo pasirinktas langas
 * (g_mode_prefs.pane): radaras, skrydžio kortelė, artimiausių sąrašas, laikrodis su orais, sekamas skrydis,
 * maršruto žemėlapis. Radaras ir žemėlapis šalia kortelės rodo jos lėktuvą, radaras šalia sąrašo – jo spalvomis.
 * Langas piešiamas atskirame 64×64 buferyje (PSRAM) ir nukopijuojamas į savo pusę.
 * Kitokio dydžio ekrane (64×32) per visą ekraną rodomas kairės pusės langas. */
#include "mode.h"
#include "esp_heap_caps.h"
#include <string.h>

#define PANE 64

static gfx_t s_pane;
static const aircraft_snapshot_t *s_snap;
static uint32_t s_ms;

gfx_t *pane_begin(void)
{
    if (!s_pane.buf) {
        s_pane.w = s_pane.h = PANE;
        s_pane.buf = heap_caps_calloc(PANE * PANE, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!s_pane.buf) return NULL;
    }
    gfx_clear(&s_pane);
    return &s_pane;
}

void pane_end(gfx_t *g, gfx_t *pane, int side)
{
    gfx_clip_off();
    for (int y = 0; y < PANE && y < g->h; y++)
        memcpy(&g->buf[(size_t)y * g->w + side * PANE], &pane->buf[y * PANE], PANE * sizeof(uint16_t));
}

static void custom_enter(void)
{
    MODE_FLIGHT_CARD.enter();
}

static void custom_update(const aircraft_snapshot_t *s, uint32_t ms)
{
    s_snap = s;
    s_ms = ms;
    MODE_RADAR.update(s, ms);
    MODE_FLIGHT_CARD.update(s, ms);
    MODE_CLOCK.update(s, ms);
    MODE_TRACK.update(s, ms);
}

static bool has(pane_t p)
{
    return g_mode_prefs.pane[0] == p || g_mode_prefs.pane[1] == p;
}

static void draw_one(gfx_t *g, pane_t p)
{
    const uint32_t focus = has(PANE_CARD) ? card_pane_icao() : 0;
    switch (p) {
    case PANE_CARD:  card_pane(g); break;
    case PANE_LIST:  radar_list_pane(g); break;
    case PANE_CLOCK: clock_pane(g); break;
    case PANE_TRACK: track_pane(g); break;
    case PANE_MAP:   map_pane(g, s_snap, focus, s_ms); break;
    default:         radar_pane(g, focus, has(PANE_LIST)); break;
    }
}

static const display_mode_t *full_mode(pane_t p)
{
    switch (p) {
    case PANE_CARD:  return &MODE_FLIGHT_CARD;
    case PANE_CLOCK: return &MODE_CLOCK;
    case PANE_TRACK: return &MODE_TRACK;
    default:         return &MODE_RADAR;
    }
}

static void custom_draw(gfx_t *g)
{
    if (g->w < 2 * PANE || g->h < PANE) {
        full_mode((pane_t)g_mode_prefs.pane[0])->draw(g);
        return;
    }
    /* Kortelė piešiama pirma, kad radaras ir žemėlapis žinotų, kurio lėktuvo kortelė rodoma */
    const int first = g_mode_prefs.pane[1] == PANE_CARD ? 1 : 0;
    for (int i = 0; i < 2; i++) {
        const int side = i ? 1 - first : first;
        gfx_t *p = pane_begin();
        if (!p) return;
        draw_one(p, (pane_t)g_mode_prefs.pane[side]);
        pane_end(g, p, side);
    }
}

const display_mode_t MODE_CUSTOM = {
    .name = "custom", .enter = custom_enter, .update = custom_update, .draw = custom_draw,
};
