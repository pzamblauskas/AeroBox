/* Lėktuvų lentelė (AC_MAX įrašų) su užraktu. Ekrane lėktuvai juda tolygiai: gavus naują poziciją, jie slenka
 * iki jos (glide), o ne šokinėja kas kelias sekundes. */
#include "aircraft.h"
#include "app_status.h"
#include "track.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>

#define DEG2RAD          0.017453292519943295f
#define GLIDE_MIN_MS     1000          /* iki naujos pozicijos slenkama ne greičiau kaip per 1 s… */
#define GLIDE_MAX_MS     10000         /* …ir ne ilgiau kaip 10 s (kai užklausos retos, minutėmis nelaukiama) */
#define GLIDE_DEF_MS     3000          /* kai pozicijos laikas nežinomas */

/* Ekrane lėktuvas per dur_ms slenka iš taško (lat, lon) iki naujausios tikros pozicijos (s_tab) */
typedef struct {
    float    lat, lon;
    uint32_t start_ms, dur_ms; /* dur_ms = 0 – jau vietoje */
} glide_t;

static aircraft_t        s_tab[AC_MAX];
static glide_t           s_glide[AC_MAX];
static bool              s_used[AC_MAX];
static bool              s_extra[AC_MAX];   /* paskutinį kartą įrašytas iš papildomo šaltinio */
static SemaphoreHandle_t s_mtx;
static home_t            s_home;

void aircraft_table_init(float home_lat, float home_lon, float range_km)
{
    s_mtx = xSemaphoreCreateMutex();
    s_home.lat = home_lat;
    s_home.lon = home_lon;
    s_home.cos_lat = cosf(home_lat * DEG2RAD);
    s_home.range_km = range_km;
    route_cache_init();
}

const home_t *aircraft_table_home(void)
{
    return &s_home;
}

void aircraft_table_set_home(float home_lat, float home_lon, float range_km)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_home.lat = home_lat;
    s_home.lon = home_lon;
    s_home.cos_lat = cosf(home_lat * DEG2RAD);
    s_home.range_km = range_km;
    memset(s_used, 0, sizeof s_used);       /* senos vietos lėktuvai nebeaktualūs */
    xSemaphoreGive(s_mtx);
}

/* Kur lėktuvas nupieštas dabar: tarp ankstesnės ir naujausios tikros pozicijos, ne toliau */
static void shown_pos(int i, uint32_t now, float *lat, float *lon)
{
    const glide_t *g = &s_glide[i];
    const uint32_t t = now - g->start_ms;
    *lat = s_tab[i].lat;
    *lon = s_tab[i].lon;
    if (g->dur_ms == 0 || t >= g->dur_ms) return;
    const float f = (float)t / (float)g->dur_ms;
    *lat = g->lat + (*lat - g->lat) * f;
    *lon = g->lon + (*lon - g->lon) * f;
}

/* Įrašymas; kviesti laikant s_mtx. Nauja vieta parenkama laisva arba seniausiai matyto lėktuvo */
static void upsert_locked(const aircraft_t *a, uint32_t now, bool extra)
{
    int slot = -1, free_slot = -1, oldest = 0;
    for (int i = 0; i < AC_MAX; i++) {
        if (s_used[i] && s_tab[i].icao24 == a->icao24) { slot = i; break; }
        if (!s_used[i] && free_slot < 0) free_slot = i;
        if (s_used[i] && s_tab[i].seen_ms < s_tab[oldest].seen_ms) oldest = i;
    }
    if (slot < 0) {
        slot = free_slot >= 0 ? free_slot : oldest;
        s_glide[slot].dur_ms = 0;                           /* naujas lėktuvas iškart rodomas ten, kur yra */
    } else if (a->lat != s_tab[slot].lat || a->lon != s_tab[slot].lon) {
        /* Nauja pozicija: slinkti nuo dabar nupieštos vietos per tiek laiko, kiek praėjo tarp pozicijų */
        glide_t *g = &s_glide[slot];
        float lat, lon;
        shown_pos(slot, now, &lat, &lon);
        const int32_t dt = (int32_t)(a->pos_ms - s_tab[slot].pos_ms);
        g->lat = lat;
        g->lon = lon;
        g->start_ms = now;
        g->dur_ms = !a->pos_ms || dt <= 0 ? GLIDE_DEF_MS
                  : dt < GLIDE_MIN_MS     ? GLIDE_MIN_MS
                  : dt > GLIDE_MAX_MS     ? GLIDE_MAX_MS : (uint32_t)dt;
    }
    s_tab[slot] = *a;
    s_tab[slot].seen_ms = now;
    s_extra[slot] = extra;
    if (!s_tab[slot].pos_ms) s_tab[slot].pos_ms = now;
    s_used[slot] = true;
}

void aircraft_table_upsert(const aircraft_t *a)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    upsert_locked(a, sky_now_ms(), false);
    xSemaphoreGive(s_mtx);
}

bool aircraft_table_upsert_extra(const aircraft_t *a, uint32_t fresh_ms)
{
    aircraft_t m = *a;
    const uint32_t now = sky_now_ms();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (int i = 0; i < AC_MAX; i++) {
        if (!s_used[i] || s_tab[i].icao24 != a->icao24) continue;
        if (!s_extra[i] && now - s_tab[i].seen_ms < fresh_ms) {     /* pagrindinis šaltinis jį mato */
            xSemaphoreGive(s_mtx);
            return false;
        }
        if (!m.type[0]) memcpy(m.type, s_tab[i].type, sizeof m.type);
        if (!m.reg[0]) memcpy(m.reg, s_tab[i].reg, sizeof m.reg);
        if (!m.special) m.special = s_tab[i].special;
        if (!m.callsign[0]) memcpy(m.callsign, s_tab[i].callsign, sizeof m.callsign);
        break;
    }
    upsert_locked(&m, now, true);
    xSemaphoreGive(s_mtx);
    return true;
}

void aircraft_table_prune(uint32_t max_age_ms)
{
    uint32_t now = sky_now_ms();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (int i = 0; i < AC_MAX; i++)
        if (s_used[i] && now - s_tab[i].seen_ms > max_age_ms) s_used[i] = false;
    xSemaphoreGive(s_mtx);
}

/* Maršrutas ir nuskrista jo dalis iš maršrutų talpyklos */
static void fill_route(aircraft_t *a)
{
    route_t r;
    a->has_route = false;
    a->progress = -1.0f;
    if (!a->callsign[0] || !route_cache_get(a->callsign, &r) || !r.known) return;
    if (!route_plausible(&r, a)) return;            /* kito reiso maršrutas – geriau nerodyti jokio */
    a->has_route = true;
    memcpy(a->orig, r.orig, sizeof a->orig);
    memcpy(a->dest, r.dest, sizeof a->dest);
    if (r.has_pos) {
        float d1 = geo_haversine_km(r.orig_lat, r.orig_lon, a->lat, a->lon);
        float d2 = geo_haversine_km(a->lat, a->lon, r.dest_lat, r.dest_lon);
        if (d1 + d2 > 1.0f) a->progress = d1 / (d1 + d2);
    }
}

void aircraft_table_snapshot(aircraft_snapshot_t *out)
{
    int n = 0;
    const uint32_t now = sky_now_ms();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (int i = 0; i < AC_MAX; i++) {
        if (!s_used[i] || s_tab[i].on_ground) continue;
        aircraft_t a = s_tab[i];
        shown_pos(i, now, &a.lat, &a.lon);
        float nk, ek;
        geo_flat_km(&s_home, a.lat, a.lon, &nk, &ek);
        a.dist_km = sqrtf(nk * nk + ek * ek);
        if (a.dist_km > s_home.range_km) continue;
        float b = atan2f(ek, nk) * 57.29577951f;
        a.bearing_deg = b < 0 ? b + 360.0f : b;
        out->ac[n++] = a;
    }
    xSemaphoreGive(s_mtx);

    /* Rikiavimas įterpimu pagal atstumą (n ≤ 32, todėl paprasčiausias būdas ir greičiausias) */
    for (int i = 1; i < n; i++) {
        aircraft_t t = out->ac[i];
        int j = i - 1;
        while (j >= 0 && out->ac[j].dist_km > t.dist_km) {
            out->ac[j + 1] = out->ac[j];
            j--;
        }
        out->ac[j + 1] = t;
    }
    for (int i = 0; i < n; i++) {
        fill_route(&out->ac[i]);
        out->ac[i].tracked = track_is(&out->ac[i]);
    }
    out->count = n;
}
