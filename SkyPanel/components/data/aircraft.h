/* Lėktuvų lentelė ir pagalbinės funkcijos. Į lentelę rašo net užduotis, o render užduotis skaito jos kopiją
 * (aircraft_table_snapshot), todėl piešimas niekada nelaiko užrakto ilgiau nei kopijavimo metu. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define AC_MAX 32

typedef struct {
    uint32_t icao24;
    char     callsign[9];
    char     type[5];          /* pvz., A320 */
    char     reg[9];           /* pvz., LY-BGA */
    float    lat, lon;         /* laipsniais */
    int32_t  alt_ft;
    bool     on_ground;
    uint16_t speed_kt;
    uint16_t track_deg;
    int16_t  vrate_fpm;
    uint8_t  special;          /* special_t: retas, karinis, sraigtasparnis ar verslo lėktuvas (aircraft_special) */
    uint32_t seen_ms;          /* kada paskutinį kartą atnaujintas (sky_now_ms laikas) */
    uint32_t pos_ms;           /* kada lėktuvas buvo taške lat/lon (sky_now_ms laikas) */

    /* Užpildoma darant kopiją (snapshot) */
    float    dist_km;
    float    bearing_deg;      /* kryptis nuo namų */
    bool     has_route;
    char     orig[5], dest[5]; /* IATA kodas (arba ICAO, jei IATA nėra) */
    float    progress;         /* nuskrista maršruto dalis 0…1; neigiama – nežinoma */
    bool     tracked;          /* naudotojo sekamas skrydis (track.h) */
} aircraft_t;

typedef struct {
    int count;
    aircraft_t ac[AC_MAX];     /* tik spindulio viduje, surikiuoti pagal atstumą */
} aircraft_snapshot_t;

typedef struct {
    float lat, lon, cos_lat;
    float range_km;
} home_t;

void aircraft_table_init(float home_lat, float home_lon, float range_km);
const home_t *aircraft_table_home(void);
/* Iškart pakeičia namų vietą ir spindulį (iš valdymo puslapio) */
void aircraft_table_set_home(float home_lat, float home_lon, float range_km);
void aircraft_table_upsert(const aircraft_t *a);
/* Lėktuvas iš papildomo šaltinio. Jei pagrindinis šaltinis jį matė per paskutines fresh_ms, įrašas nekeičiamas
 * (pagrindinio duomenys išsamesni); kitaip įrašoma, bet tipas ir registracija paliekami. true – įrašyta */
bool aircraft_table_upsert_extra(const aircraft_t *a, uint32_t fresh_ms);
void aircraft_table_prune(uint32_t max_age_ms);
/* Kopija ekranui: tik spindulio viduje, surikiuota pagal atstumą. Pozicija nespėjama: gavus naują tikrą
 * poziciją, lėktuvas tolygiai nuslenka iki jos per tiek laiko, kiek praėjo tarp dviejų paskutinių pozicijų,
 * ir laukia kitos. Todėl jis niekada nenuskrenda toliau, nei rodo duomenys. */
void aircraft_table_snapshot(aircraft_snapshot_t *out);

/* Ypatingi lėktuvai (special.c): nustatoma pagal duomenų šaltinio žymes (dbFlags: 1 – karinis, 2 – įdomus),
 * ADS-B kategoriją („A7“ – sraigtasparnis) ir tipų sąrašus */
typedef enum { SP_NONE = 0, SP_RARE, SP_MILITARY, SP_HELI, SP_BIZJET, SP_COUNT } special_t;
uint8_t     aircraft_special(const char *type, const char *category, int db_flags);
const char *special_label(uint8_t sp);   /* "RARE AIRCRAFT", "MILITARY", "HELICOPTER", "BUSINESS JET" */
const char *special_short(uint8_t sp);   /* "RARE", "MIL", "HELI", "BIZ" */

/* Maršrutų talpykla (pagal skrydžio numerį) */
typedef struct {
    char  callsign[9];
    bool  known;               /* false – ieškota, bet nerasta */
    char  orig[5], dest[5];
    bool  has_pos;
    float orig_lat, orig_lon, dest_lat, dest_lon;
    char  dest_city[16];       /* pvz., Riga */
    char  orig_city[16];       /* iš kur, pvz., London (FlightWall variantui) */
    char  airline[24];         /* pvz., AIR BALTIC */
    uint32_t fetched_ms;
} route_t;

void route_cache_init(void);
bool route_cache_get(const char *callsign, route_t *out);
void route_cache_put(const route_t *r);
bool route_cache_needs_lookup(const char *callsign, uint32_t now_ms);

/* Geometrija */
void  geo_flat_km(const home_t *h, float lat, float lon, float *north_km, float *east_km);
float geo_haversine_km(float lat1, float lon1, float lat2, float lon2);
/* Pradinė kryptis iš pirmo taško į antrą, laipsniais 0…360 */
float geo_bearing_deg(float lat1, float lon1, float lat2, float lon2);
/* Pastumia tašką per km kilometrų kryptimi track_deg */
void  geo_move(float *lat, float *lon, float track_deg, float km);
/* Ar maršruto duomenys (rasti pagal skrydžio numerį) tinka šiam lėktuvui: kelias per jo vietą nedaug ilgesnis
 * už tiesų, o toli nuo oro uostų lėktuvas neskrenda tolyn nuo paskirties. false – duomenys tikriausiai kito
 * reiso, ir parodyti maršrutas būtų klaidingas */
bool  route_plausible(const route_t *r, const aircraft_t *a);
