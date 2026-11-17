#include "track.h"
#include "app_status.h"
#include "freertos/FreeRTOS.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LIVE_MS      (3u * 60u * 1000u)   /* tiek laiko negavus padėties – „prarastas“ */
#define FORGET_MS    (60u * 60u * 1000u)  /* po valandos – „šiuo metu ore nėra“ (pvz., tas pats reisas rytoj) */
#define LANDED_KEEP_MS (2u * 60u * 1000u) /* nusileidus tiek laiko dar rodoma „LANDED“, tada sekimas baigiamas */
#define KT_TO_KMH    1.852f

static struct {
    char       query[12];
    char       cs[9];          /* šaukinys, pagal kurį ieškoma */
    aircraft_t ac;             /* paskutinė gauta padėtis */
    uint32_t   got_ms;         /* 0 – dar negauta */
    bool       asked, missing; /* ar jau klausta; ar paskutiniame atsakyme jo nebuvo */
    bool       airborne;       /* nuo sekimo pradžios matytas skrendantis (ne riedantis) */
    uint32_t   ground_ms;      /* nuo kada ant žemės (0 – ore) */
} s_t;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;   /* s_t naudoja ir net, ir render užduotys */
/* Didžiosios raidės, be tarpų (ir be brūkšnelių, jei strip_dash – registracijoms lyginti) */
static void normalize(char *out, size_t n, const char *in, bool strip_dash)
{
    size_t o = 0;
    for (; *in && o + 1 < n; in++) {
        if (*in == ' ' || (strip_dash && *in == '-')) continue;
        out[o++] = (char)toupper((unsigned char)*in);
    }
    out[o] = 0;
}

void track_set_query(const char *q)
{
    char nq[sizeof s_t.query];
    normalize(nq, sizeof nq, q, false);
    portENTER_CRITICAL(&s_lock);
    if (strcmp(nq, s_t.query) != 0) {
        memset(&s_t, 0, sizeof s_t);
        strlcpy(s_t.query, nq, sizeof s_t.query);
        strlcpy(s_t.cs, nq, sizeof s_t.cs);
    }
    portEXIT_CRITICAL(&s_lock);
}

void track_query(char *out, size_t n)
{
    portENTER_CRITICAL(&s_lock);
    strlcpy(out, s_t.query, n);
    portEXIT_CRITICAL(&s_lock);
}

void track_set_callsign(const char *cs)
{
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_t.cs, cs, sizeof s_t.cs);
    portEXIT_CRITICAL(&s_lock);
}

void track_callsign(char *out, size_t n)
{
    portENTER_CRITICAL(&s_lock);
    strlcpy(out, s_t.cs, n);
    portEXIT_CRITICAL(&s_lock);
}

bool track_matches(const aircraft_t *a)
{
    char q[sizeof s_t.query], cs[sizeof s_t.cs], reg[12], qreg[12], hex[8];
    uint32_t icao;
    portENTER_CRITICAL(&s_lock);
    strlcpy(q, s_t.query, sizeof q);
    strlcpy(cs, s_t.cs, sizeof cs);
    icao = s_t.got_ms ? s_t.ac.icao24 : 0;
    portEXIT_CRITICAL(&s_lock);
    if (!q[0]) return false;
    if (icao && a->icao24 == icao) return true;
    if (a->callsign[0] && strcmp(a->callsign, cs) == 0) return true;
    normalize(reg, sizeof reg, a->reg, true);
    normalize(qreg, sizeof qreg, q, true);
    if (reg[0] && strcmp(reg, qreg) == 0) return true;
    snprintf(hex, sizeof hex, "%06lX", (unsigned long)(a->icao24 & 0xFFFFFF));
    return strcmp(hex, q) == 0;
}

void track_found(const aircraft_t *a)
{
    const uint32_t now = sky_now_ms();
    portENTER_CRITICAL(&s_lock);
    s_t.ac = *a;
    s_t.got_ms = now | 1;
    s_t.asked = true;
    s_t.missing = false;
    /* Kad nusileidimą būtų galima atskirti nuo laukimo prie vartų: ar lėktuvas jau skrido (riedėjimas be
     * „ground“ žymės skrydžiu nelaikomas) ir nuo kada jis ant žemės */
    if (!a->on_ground) {
        s_t.ground_ms = 0;
        if (a->speed_kt >= 60 || a->alt_ft >= 2000) s_t.airborne = true;
    } else if (!s_t.ground_ms) {
        s_t.ground_ms = now | 1;
    }
    portEXIT_CRITICAL(&s_lock);
}

void track_missing(void)
{
    portENTER_CRITICAL(&s_lock);
    s_t.asked = true;
    s_t.missing = true;
    portEXIT_CRITICAL(&s_lock);
}

uint32_t track_last_ms(void)
{
    return s_t.got_ms;
}

bool track_is(const aircraft_t *a)
{
    return s_t.got_ms && s_t.query[0] && a->icao24 == s_t.ac.icao24;
}

bool track_get(track_t *out)
{
    memset(out, 0, sizeof *out);
    out->remain_km = -1.0f;
    out->eta_min = -1;
    out->ac.progress = -1.0f;

    portENTER_CRITICAL(&s_lock);
    strlcpy(out->query, s_t.query, sizeof out->query);
    aircraft_t a = s_t.ac;
    const uint32_t got = s_t.got_ms, ground = s_t.ground_ms;
    const bool asked = s_t.asked, missing = s_t.missing, airborne = s_t.airborne;
    portEXIT_CRITICAL(&s_lock);

    if (!out->query[0]) return false;
    const uint32_t now = sky_now_ms();
    const uint32_t age = (int32_t)(now - got) > 0 ? now - got : 0;   /* dėl got_ms | 1 laikas gali būti 1 ms „ateityje“ */
    if (!got || age > FORGET_MS) {
        out->state = got || (asked && missing) ? TRACK_NOT_FOUND : TRACK_SEARCHING;
        return true;
    }
    out->age_s = age / 1000;
    out->state = age < LIVE_MS ? TRACK_LIVE : TRACK_LOST;

    const home_t *h = aircraft_table_home();
    a.dist_km = geo_haversine_km(h->lat, h->lon, a.lat, a.lon);
    a.bearing_deg = geo_bearing_deg(h->lat, h->lon, a.lat, a.lon);
    a.tracked = true;
    out->in_range = out->state == TRACK_LIVE && !a.on_ground && a.dist_km <= h->range_km;

    route_t r;
    if (route_cache_get(a.callsign, &r) && r.known && route_plausible(&r, &a)) {
        a.has_route = true;
        memcpy(a.orig, r.orig, sizeof a.orig);
        memcpy(a.dest, r.dest, sizeof a.dest);
        strlcpy(out->dest_city, r.dest_city, sizeof out->dest_city);
        strlcpy(out->airline, r.airline, sizeof out->airline);
        if (r.has_pos) {
            const float d1 = geo_haversine_km(r.orig_lat, r.orig_lon, a.lat, a.lon);
            const float d2 = geo_haversine_km(a.lat, a.lon, r.dest_lat, r.dest_lon);
            out->remain_km = d2;
            if (d1 + d2 > 1.0f) a.progress = d1 / (d1 + d2);
            if (!a.on_ground && a.speed_kt > 60)
                out->eta_min = (int)(d2 / (a.speed_kt * KT_TO_KMH) * 60.0f + 0.5f);
        }
    }
    /* Nusileido, jei: ant žemės po skrydžio arba prie paskirties (sekti pradėta jau nusileidus), arba dingo
     * nedideliame aukštyje šalia paskirties (išjungtas transponderis). Ant žemės dar neskridęs lėktuvas laukia
     * išvykimo – tai ne „LANDED“. */
    const bool near_dest = out->remain_km >= 0 && out->remain_km < 40.0f;
    out->landed = (a.on_ground && (airborne || near_dest)) || (out->state == TRACK_LOST && a.alt_ft < 4000 && near_dest);
    if (out->landed) {
        out->eta_min = -1;
        if (a.has_route) a.progress = 1.0f;
        const uint32_t since = a.on_ground && ground ? ground : got + LIVE_MS;   /* dingus – nuo „prarastas“ momento */
        out->landed_s = (int32_t)(now - since) > 0 ? (now - since) / 1000 : 0;
    }
    out->ac = a;
    return true;
}

bool track_finished(void)
{
    track_t t;
    return track_get(&t) && t.landed && t.landed_s >= LANDED_KEEP_MS / 1000;
}
