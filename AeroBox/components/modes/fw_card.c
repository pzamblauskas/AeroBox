/* FlightWall stiliaus 128×64 skrydžio kortelė (atskiras variantas, Kconfig SKY_FLIGHTWALL):
 *   kairėje viršuje aviakompanijos logotipas 32×32, dešinėje 3 eilutės – aviakompanija, maršrutas „STN-VNO“, tipas
 *   („737 MAX 8“); apačioje per visą plotį paeiliui keičiasi 2 eilutės: „From London“ / „To Vilnius“, tada
 *   „Alt:23.7kft,Spd:387kt“ / „Trk:245°,Vr:-1200fpm“. Šriftas – 5×7 su mažosiomis (gfx_text_lc), viena spalva. */
#include "flightwall.h"
#include "mode.h"
#include "mode_util.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FW_TEXT RGB565(225, 225, 255)     /* FlightWall spalva: balsvai melsva */
#define FW_DIM  RGB565(110, 110, 130)
#define TX      38                        /* dešiniųjų eilučių pradžia (logotipas 2…33 ir tarpas) */
#define PAGE_MS 5000                      /* apatinės eilutės keičiasi ne rečiau nei kas tiek */

/* ICAO tipo kodas → pavadinimas, kaip FlightWall („737-900“, „A321neo“) */
static const struct {
    char        icao[5];
    const char *name;
} TYPES[] = {
    { "A124", "An-124" }, { "A139", "AW139" }, { "A169", "AW169" }, { "A19N", "A319neo" }, { "A20N", "A320neo" },
    { "A21N", "A321neo" }, { "A306", "A300-600" }, { "A318", "A318" }, { "A319", "A319" }, { "A320", "A320" },
    { "A321", "A321" }, { "A332", "A330-200" }, { "A333", "A330-300" }, { "A338", "A330-800" },
    { "A339", "A330-900" }, { "A343", "A340-300" }, { "A346", "A340-600" }, { "A359", "A350-900" },
    { "A35K", "A350-1000" }, { "A388", "A380" }, { "A400", "A400M" }, { "AS50", "H125" }, { "AT43", "ATR 42-300" },
    { "AT45", "ATR 42-500" }, { "AT46", "ATR 42-600" }, { "AT72", "ATR 72" }, { "AT75", "ATR 72-500" },
    { "AT76", "ATR 72-600" }, { "B190", "Beech 1900" }, { "B350", "King Air 350" }, { "B37M", "737 MAX 7" },
    { "B38M", "737 MAX 8" }, { "B39M", "737 MAX 9" }, { "B3XM", "737 MAX 10" }, { "B712", "717-200" },
    { "B733", "737-300" }, { "B734", "737-400" }, { "B735", "737-500" }, { "B736", "737-600" }, { "B737", "737-700" },
    { "B738", "737-800" }, { "B739", "737-900" }, { "B744", "747-400" }, { "B748", "747-8" }, { "B752", "757-200" },
    { "B753", "757-300" }, { "B762", "767-200" }, { "B763", "767-300" }, { "B764", "767-400" }, { "B772", "777-200" },
    { "B77L", "777-200LR" }, { "B773", "777-300" }, { "B77W", "777-300ER" }, { "B778", "777-8" }, { "B779", "777-9" },
    { "B788", "787-8" }, { "B789", "787-9" }, { "B78X", "787-10" }, { "BCS1", "A220-100" }, { "BCS3", "A220-300" },
    { "BE20", "King Air 200" }, { "BE9L", "King Air 90" }, { "C130", "C-130" }, { "C150", "Cessna 150" },
    { "C152", "Cessna 152" }, { "C17", "C-17" }, { "C172", "Cessna 172" }, { "C182", "Cessna 182" },
    { "C208", "Caravan" }, { "C25A", "Citation CJ2" }, { "C25B", "Citation CJ3" }, { "C25C", "Citation CJ4" },
    { "C30J", "C-130J" }, { "C510", "Mustang" }, { "C525", "CitationJet" }, { "C560", "Citation V" },
    { "C56X", "Citation XLS" }, { "C680", "Sovereign" }, { "C68A", "Latitude" }, { "C700", "Longitude" },
    { "CL30", "Challenger 300" }, { "CL35", "Challenger 350" }, { "CL60", "Challenger 600" }, { "CRJ2", "CRJ200" },
    { "CRJ7", "CRJ700" }, { "CRJ9", "CRJ900" }, { "CRJX", "CRJ1000" }, { "DA40", "DA40" }, { "DA42", "DA42" },
    { "DA62", "DA62" }, { "DH8D", "Dash 8-400" }, { "E135", "ERJ-135" }, { "E145", "ERJ-145" }, { "E170", "E170" },
    { "E175", "E175" }, { "E190", "E190" }, { "E195", "E195" }, { "E290", "E190-E2" }, { "E295", "E195-E2" },
    { "E35L", "Legacy 600" }, { "E50P", "Phenom 100" }, { "E55P", "Phenom 300" }, { "E75L", "E175" },
    { "EC35", "H135" }, { "EC45", "H145" }, { "EUFI", "Typhoon" }, { "F100", "Fokker 100" }, { "F16", "F-16" },
    { "F2TH", "Falcon 2000" }, { "F35", "F-35" }, { "F900", "Falcon 900" }, { "FA7X", "Falcon 7X" },
    { "FA8X", "Falcon 8X" }, { "G280", "G280" }, { "GL5T", "Global 5000" }, { "GL7T", "Global 7500" },
    { "GLEX", "Global Express" }, { "GLF4", "Gulfstream IV" }, { "GLF5", "Gulfstream V" }, { "GLF6", "G650" },
    { "H25B", "Hawker 800" }, { "K35R", "KC-135" }, { "LJ45", "Learjet 45" }, { "LJ60", "Learjet 60" },
    { "MD11", "MD-11" }, { "P28A", "Piper PA-28" }, { "P8", "P-8 Poseidon" }, { "PC12", "PC-12" }, { "PC24", "PC-24" },
    { "R22", "Robinson R22" }, { "R44", "Robinson R44" }, { "R66", "Robinson R66" }, { "SB20", "Saab 2000" },
    { "SF34", "Saab 340" }, { "SR22", "Cirrus SR22" }, { "SU95", "Superjet 100" }, { "TBM7", "TBM 700" },
    { "TBM9", "TBM 900" },
};

static int cmp_icao(const void *key, const void *el)
{
    return strncmp((const char *)key, ((const fw_airline_t *)el)->icao, 3);
}

const fw_airline_t *fw_airline(const char *cs)
{
    if (!cs || !isupper((unsigned char)cs[0]) || !isupper((unsigned char)cs[1]) || !isupper((unsigned char)cs[2]) ||
        !isdigit((unsigned char)cs[3]))
        return NULL;
    return bsearch(cs, FW_AIRLINES, (size_t)FW_AIRLINES_N, sizeof FW_AIRLINES[0], cmp_icao);
}

static const char *type_name(const char *icao)
{
    for (size_t i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++)
        if (strcmp(TYPES[i].icao, icao) == 0) return TYPES[i].name;
    return icao;
}

/* Tekstas iki max_w pločio: jei netelpa – alt (jei pateiktas ir telpa), kitaip nukerpamas */
static void text_fit(gfx_t *g, int x, int y, const char *s, const char *alt, int max_w)
{
    if (gfx_text_lc_width(s) > max_w && alt && gfx_text_lc_width(alt) <= max_w) s = alt;
    char buf[32];
    strlcpy(buf, s, sizeof buf);
    for (size_t n = strlen(buf); n && gfx_text_lc_width(buf) > max_w; n--) buf[n - 1] = 0;
    gfx_text_lc(g, x, y, buf, FW_TEXT);
}

/* Logotipas; nežinomos aviakompanijos – rėmelis su jos kodu, privataus lėktuvo – lėktuvo ženklas jo kursu */
static void draw_logo(gfx_t *g, int x0, int y0, const fw_airline_t *al, const aircraft_t *a)
{
    if (al && al->logo) {
        for (int y = 0; y < FW_LOGO; y++)
            for (int x = 0; x < FW_LOGO; x++)
                if (al->logo[y * FW_LOGO + x]) gfx_pixel(g, x0 + x, y0 + y, al->logo[y * FW_LOGO + x]);
        return;
    }
    const char *cs = a->callsign;
    if (isupper((unsigned char)cs[0]) && isupper((unsigned char)cs[1]) && isupper((unsigned char)cs[2]) &&
        isdigit((unsigned char)cs[3])) {
        const char code[4] = { cs[0], cs[1], cs[2], 0 };
        gfx_hline(g, x0 + 2, y0, FW_LOGO - 4, FW_DIM);
        gfx_hline(g, x0 + 2, y0 + FW_LOGO - 1, FW_LOGO - 4, FW_DIM);
        gfx_vline(g, x0, y0 + 2, FW_LOGO - 4, FW_DIM);
        gfx_vline(g, x0 + FW_LOGO - 1, y0 + 2, FW_LOGO - 4, FW_DIM);
        gfx_pixel(g, x0 + 1, y0 + 1, FW_DIM);
        gfx_pixel(g, x0 + FW_LOGO - 2, y0 + 1, FW_DIM);
        gfx_pixel(g, x0 + 1, y0 + FW_LOGO - 2, FW_DIM);
        gfx_pixel(g, x0 + FW_LOGO - 2, y0 + FW_LOGO - 2, FW_DIM);
        gfx_text_lc(g, x0 + (FW_LOGO - gfx_text_lc_width(code)) / 2, y0 + 12, code, FW_TEXT);
        return;
    }
    draw_kite(g, x0 + FW_LOGO / 2, y0 + FW_LOGO / 2, (float)a->track_deg, FW_TEXT, 3.4f);
}

/* Registracija tokia pati kaip šaukinys (SPHTW ir SP-HTW) – antrą kartą nerodoma */
static bool same_reg(const char *reg, const char *cs)
{
    for (; *reg || *cs; reg++, cs++) {
        if (*reg == '-') reg++;
        if (*reg != *cs) return false;
        if (!*reg) return true;
    }
    return true;
}

/* Aukštis kaip FlightWall: „23.7kft“ / „800ft“ / „7230m“ / „FL237“; ant žemės – „Ground“ */
static void fw_alt(char *out, size_t n, const aircraft_t *a)
{
    if (a->on_ground) snprintf(out, n, "Ground");
    else if (g_mode_prefs.alt_unit == ALT_M) snprintf(out, n, "%dm", (int)(a->alt_ft * 0.3048f + 0.5f));
    else if (g_mode_prefs.alt_unit == ALT_FL) snprintf(out, n, "FL%03d", (int)((a->alt_ft + 50) / 100));
    else if (a->alt_ft < 1000) snprintf(out, n, "%dft", (int)a->alt_ft);
    else snprintf(out, n, "%.1fkft", (double)(a->alt_ft / 1000.0f));
}

static void fw_speed(char *out, size_t n, uint16_t kt)
{
    if (g_mode_prefs.speed_unit == SPEED_KMH) snprintf(out, n, "%dkm/h", (int)(kt * 1.852f + 0.5f));
    else if (g_mode_prefs.speed_unit == SPEED_MPH) snprintf(out, n, "%dmph", (int)(kt * 1.150779f + 0.5f));
    else snprintf(out, n, "%ukt", (unsigned)kt);
}

/* Vertikalusis greitis su ženklu, kaip FlightWall („Vr:-18ft/s“): „-1200fpm“ / „+6m/s“ */
static void fw_vrate(char *out, size_t n, int fpm)
{
    const int v = g_mode_prefs.vs_unit == VS_MS ? (int)(fpm * 0.00508f + (fpm < 0 ? -0.5f : 0.5f)) : fpm;
    snprintf(out, n, v > 0 ? "+%d%s" : "%d%s", v, g_mode_prefs.vs_unit == VS_MS ? "m/s" : "fpm");
}

void fw_card_draw(gfx_t *g, const aircraft_t *a, uint32_t shown_ms)
{
    const int W = g->w - 2;
    const fw_airline_t *al = fw_airline(a->callsign);
    route_t r;
    const bool route = a->has_route && route_cache_get(a->callsign, &r) && r.known;
    char buf[40], b1[16], b2[16];

    draw_logo(g, 2, 2, al, a);

    /* Dešinėje: aviakompanija (nežinoma – skrydžio Nr.), maršrutas (nežinomas – skrydžio Nr. ar registracija), tipas */
    const char *name = al ? al->name : route && r.airline[0] ? r.airline : NULL;
    const char *cs = ac_name(a);
    text_fit(g, TX, 3, name ? name : cs, cs, W - TX);
    if (a->has_route) snprintf(buf, sizeof buf, "%s-%s", a->orig, a->dest);
    else strlcpy(buf, name ? cs : same_reg(a->reg, cs) ? "" : a->reg, sizeof buf);
    text_fit(g, TX, 15, buf, NULL, W - TX);
    if (a->type[0]) text_fit(g, TX, 27, type_name(a->type), a->type, W - TX);

    /* Apačioje paeiliui: iš kur – į kur, tada aukštis, greitis, kursas, vertikalusis greitis (be maršruto miestų – tik
 * pastarieji) */
    const uint32_t half = card_ms() / 2, page = half < PAGE_MS ? half : PAGE_MS;
    const bool cities = route && (r.orig_city[0] || r.dest_city[0]);
    if (cities && (shown_ms / page) % 2 == 0) {
        snprintf(buf, sizeof buf, "From %s", r.orig_city[0] ? r.orig_city : r.orig);
        text_fit(g, 2, 40, buf, NULL, W - 2);
        snprintf(buf, sizeof buf, "To %s", r.dest_city[0] ? r.dest_city : r.dest);
        text_fit(g, 2, 52, buf, NULL, W - 2);
        return;
    }
    fw_alt(b1, sizeof b1, a);
    fw_speed(b2, sizeof b2, a->speed_kt);
    snprintf(buf, sizeof buf, "Alt:%s,Spd:%s", b1, b2);
    char alt[40];
    snprintf(alt, sizeof alt, "%s %s", b1, b2);
    text_fit(g, 2, 40, buf, alt, W - 2);
    fw_vrate(b2, sizeof b2, a->vrate_fpm);
    snprintf(buf, sizeof buf, "Trk:%03u" GFX_DEG ",Vr:%s", (unsigned)a->track_deg, b2);
    snprintf(alt, sizeof alt, "%03u" GFX_DEG " %s", (unsigned)a->track_deg, b2);
    text_fit(g, 2, 52, buf, alt, W - 2);
}
