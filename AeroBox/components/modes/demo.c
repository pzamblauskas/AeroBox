/* Valdymo puslapio testai („Test“): kelias sekundes vietoj įprasto vaizdo parodo funkciją, nelaukiant tikro įvykio.
 * Naudojami tikri lėktuvai (ir sekamas skrydis), o jei jų nėra – pavyzdiniai. */
#include "mode.h"
#include "app_status.h"
#include "esp_heap_caps.h"
#include <string.h>

static aircraft_snapshot_t *s_syn;          /* tikri lėktuvai ir pavyzdinis retas lėktuvas (PSRAM) */

static aircraft_t sample(const char *cs, const char *ty, const char *reg, float dist, float brg, int trk, int alt,
                         int spd, int vr, const char *o, const char *d, float prog)
{
    const home_t *h = aircraft_table_home();
    aircraft_t a = { .icao24 = 0xD0D000 + (uint32_t)(dist * 10), .alt_ft = alt, .speed_kt = (uint16_t)spd,
                     .track_deg = (uint16_t)trk, .vrate_fpm = (int16_t)vr, .dist_km = dist, .bearing_deg = brg,
                     .progress = -1.0f };
    strlcpy(a.callsign, cs, sizeof a.callsign);
    strlcpy(a.type, ty, sizeof a.type);
    strlcpy(a.reg, reg, sizeof a.reg);
    a.lat = h->lat, a.lon = h->lon;
    geo_move(&a.lat, &a.lon, brg, dist);
    if (o) {
        a.has_route = true;
        strlcpy(a.orig, o, sizeof a.orig);
        strlcpy(a.dest, d, sizeof a.dest);
        a.progress = prog;
    }
    return a;
}

/* LOOK UP: artimiausias tikras lėktuvas ore arba pavyzdinis */
static void demo_overhead(gfx_t *g, const aircraft_snapshot_t *real, uint32_t t)
{
    for (int i = 0; real && i < real->count; i++)
        if (!real->ac[i].on_ground) {
            overhead_draw_for(g, &real->ac[i], t);
            return;
        }
    const aircraft_t a = sample("RYR3PE", "B38M", "SP-RZL", 1.2f, 45, 245, 4500, 210, -900, "CRL", "TSF", 0.95f);
    overhead_draw_for(g, &a, t);
}

/* Retas lėktuvas: 4 s kortelė, tada radaras su juo (rožinis žiedas) ir jo kortelė */
static void demo_rare(gfx_t *g, const aircraft_snapshot_t *real, uint32_t t)
{
    aircraft_t rare = sample("UAE183", "A388", "A6-EVB", 6.0f, 200, 320, 14000, 300, -1500, "DXB", "WAW", 0.92f);
    rare.special = SP_RARE;
    if (t < 4000 || g->w < 128 || g->h < 64) {
        card_show(g, &rare);
        return;
    }
    if (!s_syn) s_syn = heap_caps_calloc(1, sizeof *s_syn, MALLOC_CAP_SPIRAM);
    if (!s_syn) return;
    /* Tikri lėktuvai ir retasis – savo vietoje pagal atstumą */
    int n = 0;
    for (int i = 0; real && i < real->count && n < AC_MAX - 1; i++) {
        if (n == i && real->ac[i].dist_km > rare.dist_km) s_syn->ac[n++] = rare;
        s_syn->ac[n++] = real->ac[i];
    }
    if (n == (real ? real->count : 0)) s_syn->ac[n++] = rare;
    s_syn->count = n;
    MODE_RADAR.update(s_syn, t);
    gfx_t *p = pane_begin();
    if (!p) return;
    radar_pane(p, rare.icao24, false);
    pane_end(g, p, 0);
    p = pane_begin();
    card_show(p, &rare);
    pane_end(g, p, 1);
}

/* Taškas didžiuoju apskritimu: mažais žingsniais vis paskirties link (frac – kelio dalis) */
static void along(aircraft_t *a, const route_t *r, float frac)
{
    a->lat = r->orig_lat, a->lon = r->orig_lon;
    const float step = geo_haversine_km(r->orig_lat, r->orig_lon, r->dest_lat, r->dest_lon) * frac / 24.0f;
    for (int i = 0; i < 24; i++) geo_move(&a->lat, &a->lon, geo_bearing_deg(a->lat, a->lon, r->dest_lat, r->dest_lon), step);
    a->track_deg = (uint16_t)geo_bearing_deg(a->lat, a->lon, r->dest_lat, r->dest_lon);
    a->progress = frac;
}

/* Maršrutų žemėlapis: tikri artimi skrydžiai su žinomais maršrutais arba pavyzdiniai per Europą */
static void demo_map(gfx_t *g, const aircraft_snapshot_t *real, uint32_t t)
{
    if (routes_count(real) > 0) {
        routes_draw_snap(g, real, t);
        return;
    }
    static const struct { const char *cs, *ty, *o, *d; float olat, olon, dlat, dlon, prog; } S[] = {
        { "RYR3PE", "B38M", "STN", "VNO", 51.885f, 0.235f, 54.634f, 25.286f, 0.85f },
        { "LOT141", "B738", "WAW", "LCA", 52.166f, 20.967f, 34.875f, 33.625f, 0.30f },
        { "FIN1929", "A320", "HEL", "FRA", 60.317f, 24.963f, 50.033f, 8.570f, 0.40f },
    };
    aircraft_t ac[3];                            /* rinkinys laikomas ne vidinėje RAM */
    route_t r[3];
    const aircraft_t *ap[3];
    const route_t *rp[3];
    for (int i = 0; i < 3; i++) {
        ac[i] = sample(S[i].cs, S[i].ty, "", 0.0f, 0, 0, 33000 - 9000 * i, 440, 0, S[i].o, S[i].d, S[i].prog);
        ac[i].icao24 = 0xD0D100 + (uint32_t)i;
        r[i] = (route_t){ .known = true, .has_pos = true, .orig_lat = S[i].olat, .orig_lon = S[i].olon,
                          .dest_lat = S[i].dlat, .dest_lon = S[i].dlon };
        strcpy(r[i].orig, S[i].o);
        strcpy(r[i].dest, S[i].d);
        along(&ac[i], &r[i], S[i].prog);
        ap[i] = &ac[i];
        rp[i] = &r[i];
    }
    routes_show(g, ap, rp, 3, t);
}

/* Išvykimų lenta: kortelės keičiasi kas 2,5 s, kiekvienos pradžioje raidės „prasisuka“ */
static void demo_flap(gfx_t *g, const aircraft_snapshot_t *real, uint32_t t)
{
    static aircraft_t list[3];
    int n = real ? (real->count < 5 ? real->count : 5) : 0;
    const aircraft_t *pick;
    if (n) {
        pick = &real->ac[(t / 2500) % (uint32_t)n];
    } else {
        list[0] = sample("RYR3PE", "B38M", "SP-RZL", 4.2f, 60, 245, 23725, 387, -1200, "CRL", "TSF", 0.62f);
        list[1] = sample("WZZ8R", "A21N", "HA-LGO", 13.1f, 320, 110, 32275, 422, 0, "BUD", "VNO", 0.4f);
        list[2] = sample("BTI34P", "BCS3", "YL-CSJ", 9.5f, 200, 30, 5075, 172, 1500, "RIX", "VNO", 0.85f);
        pick = &list[(t / 2500) % 3];
    }
    gfx_flap(t % 2500, 1000);
    card_show(g, pick);
    gfx_flap_off();
}

/* Sekamas skrydis: po 4 s kortelė ir maršruto žemėlapis paeiliui; sekamas tikras arba pavyzdinis LHR → VNO */
static void demo_track(gfx_t *g, uint32_t t)
{
    track_t tr;
    route_t r = { .known = true, .has_pos = true, .orig_lat = 51.470f, .orig_lon = -0.454f, .dest_lat = 54.634f,
                  .dest_lon = 25.286f };
    const route_t *rp = NULL;
    if (!track_get(&tr) || (tr.state != TRACK_LIVE && tr.state != TRACK_LOST)) {
        memset(&tr, 0, sizeof tr);
        tr.state = TRACK_LIVE;
        strcpy(r.orig, "LHR");
        strcpy(r.dest, "VNO");
        rp = &r;
        aircraft_t *a = &tr.ac;
        *a = (aircraft_t){ .icao24 = 0x400F01, .alt_ft = 36000, .speed_kt = 452, .track_deg = 80, .has_route = true,
                           .tracked = true, .progress = 0.72f };
        strcpy(a->callsign, "BAW882");
        strcpy(a->type, "A320");
        strcpy(a->reg, "G-EUUA");
        strcpy(a->orig, "LHR");
        strcpy(a->dest, "VNO");
        strcpy(tr.query, "BAW882");
        strcpy(tr.airline, "BRITISH AIRWAYS");
        strcpy(tr.dest_city, "VILNIUS");
        along(a, &r, 0.72f);                    /* nuskrista 72 % kelio */
        const home_t *h = aircraft_table_home();
        a->dist_km = geo_haversine_km(h->lat, h->lon, a->lat, a->lon);
        a->bearing_deg = geo_bearing_deg(h->lat, h->lon, a->lat, a->lon);
        tr.in_range = a->dist_km <= h->range_km;
        tr.remain_km = geo_haversine_km(a->lat, a->lon, r.dest_lat, r.dest_lon);
        tr.eta_min = (int)(tr.remain_km / (a->speed_kt * 1.852f) * 60.0f + 0.5f);
    }
    if ((t / 4000) % 2 == 0) track_card_show(g, &tr);
    else track_map_show(g, &tr, rp, t);
}

/* Bandymas: 5 sekami skrydžiai viename žemėlapyje (dar ne nustatymas), taip pat ir labai toli skrendantys (į Niujorką,
 * Seulą): kiekvienas paeiliui išskiriamas, esantys už žemėlapio ribų – rodykle prie krašto */
static void demo_multi(gfx_t *g, uint32_t t)
{
    static const struct { const char *cs, *ty, *o, *d; float olat, olon, dlat, dlon, prog; } S[] = {
        { "LOT26", "B788", "WAW", "JFK", 52.166f, 20.967f, 40.640f, -73.779f, 0.40f },
        { "LOT41", "B789", "WAW", "ICN", 52.166f, 20.967f, 37.460f, 126.440f, 0.35f },
        { "RYR3PE", "B38M", "STN", "VNO", 51.885f, 0.235f, 54.634f, 25.286f, 0.85f },
        { "BTI6TE", "BCS3", "RIX", "AMS", 56.924f, 23.971f, 52.310f, 4.768f, 0.50f },
        { "WZZ7TE", "A21N", "VNO", "TLV", 54.634f, 25.286f, 32.011f, 34.887f, 0.60f },
    };
    aircraft_t ac[5];
    route_t r[5];
    const aircraft_t *ap[5];
    const route_t *rp[5];
    for (int i = 0; i < 5; i++) {
        ac[i] = sample(S[i].cs, S[i].ty, "", 0.0f, 0, 0, 37000 - 3000 * i, 480, 0, S[i].o, S[i].d, S[i].prog);
        ac[i].icao24 = 0xD0D200 + (uint32_t)i;
        r[i] = (route_t){ .known = true, .has_pos = true, .orig_lat = S[i].olat, .orig_lon = S[i].olon,
                          .dest_lat = S[i].dlat, .dest_lon = S[i].dlon };
        strcpy(r[i].orig, S[i].o);
        strcpy(r[i].dest, S[i].d);
        along(&ac[i], &r[i], S[i].prog);
        ap[i] = &ac[i];
        rp[i] = &r[i];
    }
    routes_show(g, ap, rp, 5, t);
}

void demo_draw(gfx_t *g, int kind, const aircraft_snapshot_t *real, uint32_t elapsed_ms)
{
    switch (kind) {
    case TEST_OVERHEAD: demo_overhead(g, real, elapsed_ms); break;
    case TEST_RARE:     demo_rare(g, real, elapsed_ms); break;
    case TEST_MAP:      demo_map(g, real, elapsed_ms); break;
    case TEST_FLAP:     demo_flap(g, real, elapsed_ms); break;
    case TEST_TRACK:    demo_track(g, elapsed_ms); break;
    case TEST_MULTI:    demo_multi(g, elapsed_ms); break;
    default:            break;
    }
}
