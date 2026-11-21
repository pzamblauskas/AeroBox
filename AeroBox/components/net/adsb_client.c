/* Duomenų gavimas (net_task): lėktuvai, sekamas skrydis, maršrutai, orai, namų vieta ir programos atnaujinimai.
 *
 * LĖKTUVŲ ŠALTINIAI (pirmumo tvarka):
 *   1. AeroBox serveris (g_settings.server_url, jei nurodytas ir data_mode = DATA_BUILTIN). Adresai ir formatai tokie
 *      patys kaip adsb.fi / adsbdb / Open-Meteo (/api/v2, /v0, /v1/forecast), pridedama antraštė X-Device-Key.
 *      Raktas gaunamas automatiškai (POST /v1/register). Užklausa kas SERVER_GAP ± SERVER_JITTER / 2.
 *      401 – raktas nebegalioja, ekranas registruojasi iš naujo. Po SERVER_FAILS nesėkmių iš eilės arba gavus 5xx
 *      SERVER_DOWN_MS duomenys imami tiesiai iš šaltinių (g_status.srv_down), tada vėl bandomas serveris.
 *   2. Naudotojo API raktas (data_mode = DATA_OWN_KEY): ADS-B Exchange per RapidAPI (tas pats v2 formatas),
 *      antraštės X-RapidAPI-Key ir X-RapidAPI-Host; užklausa kas key_poll_s (naudotojas renkasi pagal savo planą).
 *      401/403 – raktas netinka (g_status.key_status, rodoma valdymo puslapyje), kitas bandymas po minutės.
 *   3. Tiesiogiai iš nemokamų šaltinių (g_settings.data_src): adsb.lol arba adsb.fi (tas pats readsb formatas,
 *      tik kitas adresas, o sąrašas vadinasi „aircraft“, ne „ac“).
 *      - adsb.fi riba ~1 užklausa per sekundę, tik nekomerciniam naudojimui; klausiama kas 3 s, paros biudžeto nereikia.
 *      - adsb.lol ribos (x-ratelimit-limit): 4/s, 60/min ir 500 per parą VIENAM VIEŠAM IP (visiems namų įrenginiams).
 *        Todėl numatytasis režimas (poll_s = 0) – „biudžetas“: žetonų kibiras, 450 užklausų per parą (10 % žemiau
 *        ribos), talpa 20, tad bet kuriame 24 val. lange ne daugiau kaip ~470. Žetonai naudojami pagal poreikį:
 *        lėktuvai šalia – kas 15 s, kol yra rezervo; tuščias dangus – kas 5 min; naktį – kas 10 min. Rezervas
 *        saugomas NVS kartu su laiku, todėl perkrovimas nieko neprideda.
 *      - poll_s 5…15 – pastovus periodas bandymams (viršija paros ribą).
 *   Gavus 429: naujas ryšys, laukiama tiek, kiek prašo serveris, ekrane – „DATA SOURCE BUSY“.
 *   Tarp atnaujinimų lėktuvai ekrane juda tolygiai (aircraft_table_snapshot).
 *
 * PAPILDOMI LĖKTUVAI (kai įvesta nemokama OpenSky paskyra – API klientas): OpenSky turi savo imtuvų tinklą ir mato
 * skrydžių, kurių adsb.fi / adsb.lol nemato. /states/all stačiakampiui aplink namus, OAuth2 (client credentials,
 * raktas galioja 30 min). Paskyrai skiriama 4000 kreditų per parą; užklausa kainuoja 1–4 kreditus pagal
 * stačiakampio plotą, todėl klausiama taip, kad per parą būtų išnaudota ne daugiau 3600, bet ne dažniau kaip kas 30 s.
 * Į lentelę įrašomi tik tie, kurių pagrindinis šaltinis nemato (aircraft_table_upsert_extra).
 *
 * SEKAMAS SKRYDIS (track.h): spinduliu atpažįstamas bendrame atsakyme; toliau – atskira užklausa tam pačiam
 * šaltiniui (adsb.fi kas 10 s, adsb.lol kas 30 s, biudžeto režime kas 5 min iš to paties biudžeto), visada ne anksčiau
 * kaip 1,2 s po bendros užklausos. Skrydžio Nr. (BT658) paverčiamas šaukiniu (BTI658) pagal adsbdb aviakompanijas.
 *
 * MARŠRUTAI (iš kur – į kur, oro uostų koordinatės, miestas, aviakompanija): adsbdb.com, ne dažniau kaip kas 3 s,
 * sekamas skrydis – pirmas. ORAI: Open-Meteo kas 15 min. Šie serveriai į adsb ribas neįskaičiuojami.
 *
 * ATNAUJINIMAI: jei naudojamas AeroBox serveris, naujos versijos tikrinamos praėjus UPD_FIRST_MS po įjungimo ir
 * kas UPD_CHECK_MS; diegiama tik paspaudus „Update“ (update_install). */
#include "net.h"
#include "aircraft.h"
#include "app_status.h"
#include "settings.h"
#include "track.h"
#include "weather.h"
#include <time.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "mbedtls/sha256.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "adsb";

#define RESP_MAX       (96 * 1024)     /* didžiausias priimamas atsakymas */
#define USER_AGENT     "AeroBox/" SKY_FW_VERSION " (ESP32-S3)"
#define FI_BASE        "https://opendata.adsb.fi/api/v2"
#define LOL_BASE       "https://api.adsb.lol/v2"
#define ADSBX_HOST     "adsbexchange-com1.p.rapidapi.com"
#define ADSBX_BASE     "https://" ADSBX_HOST "/v2"
#define KEY_BAD_WAIT   60000           /* raktas atmestas (401/403) – kitas bandymas po minutės */
#define TRACK_KEY_MIN  30000           /* su naudotojo raktu sekamo skrydžio užklausa – ne dažniau */
#define ADSBDB_URL     "https://api.adsbdb.com/v0/"
#define BACKOFF_MAX_MS 60000
#define BACKOFF_STEP   3000            /* po kiekvieno 429 periodas pailgėja tiek */
#define RECOVER_MS     600000          /* tiek laiko be 429 – periodas sutrumpėja 1 s (iki pasirinkto) */
#define RETRY_MAX_S    900             /* ilgiausias laukimas po 429 (net jei serveris prašo daugiau) */
#define LOG_EVERY_MS   30000
#define BUDGET_PER_DAY 450
#define BUDGET_CAP     20.0f
#define TOKEN_MS       (86400000u / BUDGET_PER_DAY)   /* 192 s vienam žetonui */
#define GAP_AIRCRAFT   15000           /* lėktuvai šalia */
#define GAP_EMPTY      300000          /* dangus tuščias */
#define GAP_NIGHT      600000          /* nakties valandos */
#define BUDGET_SAVE_MS 600000
#define ADSBFI_GAP     3000            /* adsb.fi: riba ~1/s, klausiama kas 3 s */
#define SRC_SPACING_MS 1200            /* mažiausias tarpas tarp dviejų užklausų tam pačiam šaltiniui */
#define TRACK_FI_MS    10000           /* atskira sekamo skrydžio užklausa (adsb.fi; toliau – adsb.lol ir biudžetas) */
#define TRACK_LOL_MS   30000
#define TRACK_BUDGET_MS 300000
#define TRACK_AREA_MS  20000           /* matytas bendrame atsakyme – atskiros užklausos nereikia */
#define ROUTE_GAP_MS   3000            /* adsbdb – ne dažniau kaip kas 3 s */
#define ROUTE_FAIL_MS  60000           /* adsbdb neatsako – pertrauka */
#define ROUTE_NEAREST  8               /* maršrutų ieškoma tik tiek artimiausių lėktuvų */
#define WEATHER_MS     900000          /* orai kas 15 min (Open-Meteo tiek laiko ir atnaujina) */
#define WEATHER_RETRY  120000
#define OS_TOKEN_URL   "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
#define OS_STATES_URL  "https://opensky-network.org/api/states/all"
#define OS_DAY_CREDITS 3600            /* 10 % mažiau už paskyrai skiriamus 4000 */
#define OS_MIN_GAP     30000
#define OS_BAD_WAIT    300000          /* paskyra atmesta – kitas bandymas po 5 min */
#define OS_FRESH_MS    15000           /* jei pagrindinis šaltinis lėktuvą matė per tiek laiko, OpenSky jo nekeičia */
#define OS_TOKEN_MAX   3072
#define SERVER_GAP     10000           /* AeroBox serveris: užklausa kas 10 s… */
#define SERVER_JITTER  3000            /* …±1,5 s, kad tūkstančiai ekranų neklaustų tą pačią sekundę */
#define SERVER_FAILS   3               /* tiek nesėkmių iš eilės – serveris laikomas neveikiančiu… */
#define SERVER_DOWN_MS 300000          /* …ir 5 min duomenys imami tiesiogiai, tada vėl bandomas serveris */
#define SERVER_REG_MS  60000           /* registracija nepavyko – kitas bandymas po tiek */
#define UPD_FIRST_MS   30000           /* pirmas atnaujinimų patikrinimas po įjungimo… */
#define UPD_CHECK_MS   (6u * 3600u * 1000u)   /* …ir toliau kas 6 val. */
#define UPD_RETRY_MS   600000          /* patikrinti nepavyko – kitas bandymas po tiek */

typedef struct {
    char  *buf;
    size_t len, cap;
    bool   overflow;
    int    retry_s;            /* retry-after arba x-ratelimit-reset, s (0 – nėra) */
    int    remaining;          /* x-ratelimit-remaining (-1 – nėra) */
} http_buf_t;

static void *psram_malloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

static void buf_reset(http_buf_t *b)
{
    b->len = 0;
    b->overflow = false;
    b->buf[0] = 0;
    b->retry_s = 0;
    b->remaining = -1;
}

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    http_buf_t *b = e->user_data;
    if (!b) return ESP_OK;
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value) {
        if (strcasecmp(e->header_key, "retry-after") == 0 || strcasecmp(e->header_key, "x-ratelimit-reset") == 0) {
            int v = atoi(e->header_value);
            if (v > b->retry_s) b->retry_s = v;
        } else if (strcasecmp(e->header_key, "x-rate-limit-retry-after-seconds") == 0) {   /* OpenSky */
            int v = atoi(e->header_value);
            if (v > b->retry_s) b->retry_s = v;
        } else if (strcasecmp(e->header_key, "x-ratelimit-remaining") == 0) {
            b->remaining = atoi(e->header_value);
        }
    } else if (e->event_id == HTTP_EVENT_ON_DATA && e->data_len > 0) {
        if (b->len + (size_t)e->data_len + 1 > b->cap) {
            b->overflow = true;
            return ESP_OK;
        }
        memcpy(b->buf + b->len, e->data, e->data_len);
        b->len += e->data_len;
        b->buf[b->len] = 0;
    }
    return ESP_OK;
}

/* Ar naudojamas AeroBox serveris: yra adresas ir ekrano raktas, ir serveris laikinai neišjungtas dėl nesėkmių */
static uint32_t s_srv_down_until;
static int      s_srv_fails;

static bool srv_on(void)
{
    return g_settings.data_mode == DATA_BUILTIN && g_settings.server_url[0] && g_settings.server_key[0] &&
           !g_status.srv_down;
}

/* Serverio atsakymo rezultatas: kelios nesėkmės iš eilės → atsarginis kelias (duomenys tiesiai iš šaltinių) */
static void srv_result(bool ok)
{
    if (ok) {
        s_srv_fails = 0;
        return;
    }
    if (++s_srv_fails >= SERVER_FAILS) {
        s_srv_fails = 0;
        s_srv_down_until = (sky_now_ms() + SERVER_DOWN_MS) | 1;
        g_status.srv_down = 1;
        ESP_LOGW(TAG, "serveris neatsako - %d min duomenys tiesiogiai is saltiniu", SERVER_DOWN_MS / 60000);
    }
}

/* Vienkartinė užklausa (orai). Grąžina HTTP būseną arba neigiamą reikšmę klaidos atveju */
static int http_request(const char *url, http_buf_t *b)
{
    buf_reset(b);
    esp_http_client_config_t cfg = {
        .url               = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 8000,
        .event_handler     = http_evt,
        .user_data         = b,
        .user_agent        = USER_AGENT,
        .buffer_size       = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    if (srv_on() && strncmp(url, g_settings.server_url, strlen(g_settings.server_url)) == 0)
        esp_http_client_set_header(c, "X-Device-Key", g_settings.server_key);
    esp_err_t err = esp_http_client_perform(c);
    int status = err == ESP_OK ? esp_http_client_get_status_code(c) : -1;
    if (err != ESP_OK) ESP_LOGW(TAG, "%s: %s", url, esp_err_to_name(err));
    esp_http_client_cleanup(c);
    if (b->overflow) {
        ESP_LOGW(TAG, "atsakymas per didelis (> %u B)", (unsigned)b->cap);
        return -2;
    }
    return status;
}

/* Užklausa per nuolatinį (keep-alive) ryšį su tuo pačiu serveriu; klaidos atveju ryšys uždaromas ir kitą kartą
 * kuriamas iš naujo. s_point – lėktuvų šaltinis, s_db – adsbdb. key – naudotojo API raktas (antraštės nustatomos
 * kuriant ryšį; pasikeitus šaltiniui ar raktui ryšys uždaromas – close_point()) arba NULL. */
static esp_http_client_handle_t s_point, s_db;

static int keep_get(esp_http_client_handle_t *h, const char *url, http_buf_t *b, const char *key)
{
    int status = -1;
    /* Jei serveris jau uždarė senąjį ryšį – dar kartą su nauju */
    for (int attempt = 0; attempt < 2; attempt++) {
        buf_reset(b);
        const bool reused = *h != NULL;
        if (!reused) {
            esp_http_client_config_t cfg = {
                .url               = url,
                .crt_bundle_attach = esp_crt_bundle_attach,
                .timeout_ms        = 5000,
                .event_handler     = http_evt,
                .user_data         = b,
                .user_agent        = USER_AGENT,
                .buffer_size       = 2048,
                .keep_alive_enable = true,
            };
            *h = esp_http_client_init(&cfg);
            if (!*h) return -1;
            if (key) {
                esp_http_client_set_header(*h, "X-RapidAPI-Key", key);
                esp_http_client_set_header(*h, "X-RapidAPI-Host", ADSBX_HOST);
            } else if (srv_on()) {
                esp_http_client_set_header(*h, "X-Device-Key", g_settings.server_key);
            }
        } else {
            esp_http_client_set_url(*h, url);
        }
        esp_err_t err = esp_http_client_perform(*h);
        status = err == ESP_OK ? esp_http_client_get_status_code(*h) : -1;
        if (err == ESP_OK && status > 0) break;
        ESP_LOGW(TAG, "uzklausa: %s - jungiamasi is naujo", esp_err_to_name(err));
        esp_http_client_cleanup(*h);
        *h = NULL;
        if (!reused) break;
    }
    if (b->overflow) {
        ESP_LOGW(TAG, "atsakymas per didelis (> %u B)", (unsigned)b->cap);
        return -2;
    }
    return status;
}

static void close_point(void)
{
    if (s_point) {
        esp_http_client_cleanup(s_point);
        s_point = NULL;
    }
}

static const char *jstr(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static double jnum(const cJSON *o, const char *k, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static void copy_trim(char *dst, size_t n, const char *src)
{
    strlcpy(dst, src, n);
    size_t l = strlen(dst);
    while (l && dst[l - 1] == ' ') dst[--l] = 0;
}

/* UTF-8 lotyniškos raidės su diakritika → ASCII (Zürich → Zurich, Šiauliai → Siauliai), kitos praleidžiamos */
static void ascii_fold(char *dst, size_t n, const char *src)
{
    static const char L1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    static const char LA[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSs"
                             "TtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
    size_t o = 0;
    for (const unsigned char *s = (const unsigned char *)src; *s && o + 1 < n; s++) {
        if (*s < 0x80) {
            dst[o++] = (char)*s;
        } else if ((*s & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
            const unsigned cp = (unsigned)(*s & 0x1F) << 6 | (s[1] & 0x3F);
            s++;
            if (cp >= 0xC0 && cp <= 0xFF) dst[o++] = L1[cp - 0xC0];
            else if (cp >= 0x100 && cp < 0x100 + sizeof LA - 1) dst[o++] = LA[cp - 0x100];
        } else {
            while ((s[1] & 0xC0) == 0x80) s++;    /* kiti simboliai praleidžiami */
        }
    }
    dst[o] = 0;
}

/* ---------- Lėktuvai ---------- */

static const cJSON *ac_list(const cJSON *root)
{
    /* Sąrašo vardas: adsb.lol ir adsb.fi paieškoje – „ac“, adsb.fi vietovės užklausoje – „aircraft“ */
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "ac");
    return list ? list : cJSON_GetObjectItemCaseSensitive(root, "aircraft");
}

static bool parse_one(const cJSON *it, aircraft_t *a, uint32_t now)
{
    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(it, "lat");
    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(it, "lon");
    const char *hex = jstr(it, "hex");
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) || !hex) return false;

    memset(a, 0, sizeof *a);
    if (*hex == '~') hex++;                      /* ne ICAO (TIS-B) adresas */
    a->icao24 = (uint32_t)strtoul(hex, NULL, 16);
    const char *s;
    if ((s = jstr(it, "flight"))) copy_trim(a->callsign, sizeof a->callsign, s);
    if ((s = jstr(it, "t")))      strlcpy(a->type, s, sizeof a->type);
    if ((s = jstr(it, "r")))      strlcpy(a->reg, s, sizeof a->reg);
    a->lat = (float)lat->valuedouble;
    a->lon = (float)lon->valuedouble;

    /* alt_baro kartais būna tekstas „ground“ */
    const cJSON *alt = cJSON_GetObjectItemCaseSensitive(it, "alt_baro");
    if (cJSON_IsString(alt) && strcmp(alt->valuestring, "ground") == 0) a->on_ground = true;
    else if (cJSON_IsNumber(alt)) a->alt_ft = (int32_t)alt->valuedouble;
    else a->alt_ft = (int32_t)jnum(it, "alt_geom", 0);

    a->speed_kt  = (uint16_t)fmax(0.0, jnum(it, "gs", 0));
    a->track_deg = (uint16_t)fmod(jnum(it, "track", 0) + 360.0, 360.0);
    double vr = jnum(it, "baro_rate", jnum(it, "geom_rate", 0));
    a->vrate_fpm = (int16_t)fmax(-32000.0, fmin(32000.0, vr));
    /* Kada pozicija tikrai buvo tokia (seen_pos – prieš kiek sekundžių); nuo šio momento skaičiuojamas judėjimas ekrane */
    a->pos_ms = now - (uint32_t)(fmax(0.0, fmin(60.0, jnum(it, "seen_pos", 0))) * 1000.0);
    a->special = aircraft_special(a->type, jstr(it, "category"), (int)jnum(it, "dbFlags", 0));
    return true;
}

static uint32_t s_track_area_ms;         /* kada sekamas skrydis paskutinį kartą matytas bendrame atsakyme */

/* Bendras atsakymas: visi lėktuvai į lentelę, sekamas – dar ir į track */
static int parse_aircraft(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    const uint32_t now = sky_now_ms();
    int n = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, ac_list(root)) {
        aircraft_t a;
        if (!parse_one(it, &a, now)) continue;
        aircraft_table_upsert(&a);
        if (track_matches(&a)) {
            track_found(&a);
            s_track_area_ms = now | 1;
        }
        n++;
    }
    cJSON_Delete(root);
    return n;
}

/* ---------- Sekamas skrydis ---------- */

static struct {
    char     query[12];      /* kuriai įvesčiai paruošta */
    char     cs[9];          /* šaukinys paieškai */
    bool     resolved;       /* skrydžio Nr. → šaukinys jau išspręsta (arba nereikia) */
    bool     try_hex;        /* 6 šešioliktainiai simboliai: paeiliui bandomas šaukinys ir ICAO adresas */
    uint32_t next_ms;
} s_trk;

static bool is_hex6(const char *s)
{
    if (strlen(s) != 6) return false;
    for (; *s; s++)
        if (!isxdigit((unsigned char)*s)) return false;
    return true;
}

/* Ar tai skrydžio numeris (BT658, FR1234, W61234): 2 simbolių aviakompanijos kodas (ne abu skaitmenys) ir skaitmenys */
static bool looks_flight_number(const char *s)
{
    const size_t n = strlen(s);
    if (n < 3 || n > 7 || !isalnum((unsigned char)s[0]) || !isalnum((unsigned char)s[1])) return false;
    if (isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1])) return false;
    if (!isdigit((unsigned char)s[2])) return false;
    for (size_t i = 3; i < n; i++)
        if (!isalnum((unsigned char)s[i])) return false;
    return true;
}

static void track_prepare(void)
{
    char q[sizeof s_trk.query];
    track_query(q, sizeof q);
    if (strcmp(q, s_trk.query) == 0) return;
    memset(&s_trk, 0, sizeof s_trk);
    strlcpy(s_trk.query, q, sizeof s_trk.query);
    strlcpy(s_trk.cs, q, sizeof s_trk.cs);
    s_trk.resolved = !looks_flight_number(q) || strchr(q, '-');
    s_track_area_ms = 0;
    if (q[0]) ESP_LOGI(TAG, "sekamas skrydis: %s", q);
}

/* BT658 → BTI658 (adsbdb aviakompanija pagal IATA kodą); nepavykus ieškoma pagal įvestą tekstą */
static void db_url(char *out, size_t n, const char *kind, const char *ident, int len)
{
    if (srv_on()) snprintf(out, n, "%s/v0/%s/%.*s", g_settings.server_url, kind, len, ident);
    else snprintf(out, n, ADSBDB_URL "%s/%.*s", kind, len, ident);
}

static void track_resolve(http_buf_t *b)
{
    char url[128];
    db_url(url, sizeof url, "airline", s_trk.query, 2);
    const int st = keep_get(&s_db, url, b, NULL);
    if (st == 200) {
        cJSON *root = cJSON_Parse(b->buf);
        const cJSON *it;
        cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "response")) {
            const char *icao = jstr(it, "icao");
            if (icao && strlen(icao) == 3 && strlen(s_trk.query + 2) <= sizeof s_trk.cs - 4) {
                memcpy(s_trk.cs, icao, 3);          /* šaukinys – iki 8 simbolių */
                strlcpy(s_trk.cs + 3, s_trk.query + 2, sizeof s_trk.cs - 3);
                break;
            }
        }
        cJSON_Delete(root);
    }
    if (st == 200 || st == 404) {
        s_trk.resolved = true;
        track_set_callsign(s_trk.cs);
        ESP_LOGI(TAG, "skrydis %s -> saukinys %s", s_trk.query, s_trk.cs);
    } else {
        s_trk.next_ms = sky_now_ms() + 30000;       /* adsbdb neatsako – bandyti po 30 s */
    }
}

static bool track_due(uint32_t now, bool budget_mode, float tokens)
{
    if (!s_trk.query[0] || !s_trk.resolved || (int32_t)(now - s_trk.next_ms) < 0) return false;
    if (s_track_area_ms && now - s_track_area_ms < TRACK_AREA_MS) return false;   /* jau matomas spinduliu */
    return !budget_mode || tokens >= 1.0f;
}

/* Atskira sekamo skrydžio užklausa tam pačiam šaltiniui (visame pasaulyje): base – adsb.fi / adsb.lol, arba su
 * naudotojo raktu (key) – ADS-B Exchange (adresai su „/“ gale, ICAO adresas – /icao/). Grąžina HTTP būseną. */
static int track_request(http_buf_t *b, const char *base, const char *key)
{
    char url[160];
    const char *end = key ? "/" : "";
    if (strchr(s_trk.query, '-')) {
        snprintf(url, sizeof url, "%s/registration/%s%s", base, s_trk.query, end);
    } else if (s_trk.try_hex) {
        char hex[8];
        for (int i = 0; i < 7; i++) hex[i] = (char)tolower((unsigned char)s_trk.query[i]);
        snprintf(url, sizeof url, "%s/%s/%s%s", base, key ? "icao" : "hex", hex, end);
    } else {
        snprintf(url, sizeof url, "%s/callsign/%s%s", base, s_trk.cs, end);
    }
    const int st = keep_get(&s_point, url, b, key);
    if (st >= 200 && st < 300) {
        cJSON *root = cJSON_Parse(b->buf);
        const cJSON *it;
        bool found = false;
        cJSON_ArrayForEach(it, ac_list(root)) {
            aircraft_t a;
            if (parse_one(it, &a, sky_now_ms())) {
                track_found(&a);
                found = true;
                break;
            }
        }
        cJSON_Delete(root);
        if (!found) {
            track_missing();
            if (is_hex6(s_trk.query)) s_trk.try_hex = !s_trk.try_hex;
        }
    } else {
        ESP_LOGW(TAG, "sekamo skrydzio uzklausa HTTP %d", st);
    }
    return st;
}

/* ---------- Maršrutai (adsbdb) ---------- */

static uint32_t s_route_next;

static void parse_airport(const cJSON *ap, char code[5], float *lat, float *lon, bool *pos)
{
    const char *c = jstr(ap, "iata_code");
    if (!c || strlen(c) != 3) c = jstr(ap, "icao_code");
    if (c) strlcpy(code, c, 5);
    const cJSON *la = cJSON_GetObjectItemCaseSensitive(ap, "latitude");
    const cJSON *lo = cJSON_GetObjectItemCaseSensitive(ap, "longitude");
    if (cJSON_IsNumber(la) && cJSON_IsNumber(lo)) {
        *lat = (float)la->valuedouble;
        *lon = (float)lo->valuedouble;
    } else {
        *pos = false;
    }
}

static void route_lookup(http_buf_t *b, const char *cs)
{
    char url[128];
    const uint32_t now = sky_now_ms();
    db_url(url, sizeof url, "callsign", cs, 8);
    const int st = keep_get(&s_db, url, b, NULL);
    route_t r = { .fetched_ms = now };
    strlcpy(r.callsign, cs, sizeof r.callsign);
    if (st == 200) {
        cJSON *root = cJSON_Parse(b->buf);
        const cJSON *fr = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "response"),
                                                           "flightroute");
        const cJSON *o = cJSON_GetObjectItemCaseSensitive(fr, "origin");
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(fr, "destination");
        if (o && d) {
            r.has_pos = true;
            parse_airport(o, r.orig, &r.orig_lat, &r.orig_lon, &r.has_pos);
            parse_airport(d, r.dest, &r.dest_lat, &r.dest_lon, &r.has_pos);
            r.known = r.orig[0] && r.dest[0];
            const char *s;
            if ((s = jstr(d, "municipality"))) ascii_fold(r.dest_city, sizeof r.dest_city, s);
            if ((s = jstr(o, "municipality"))) ascii_fold(r.orig_city, sizeof r.orig_city, s);
            if ((s = jstr(cJSON_GetObjectItemCaseSensitive(fr, "airline"), "name")))
                ascii_fold(r.airline, sizeof r.airline, s);
        }
        cJSON_Delete(root);
        route_cache_put(&r);
        ESP_LOGI(TAG, "marsrutas %s: %s-%s", cs, r.known ? r.orig : "?", r.known ? r.dest : "?");
    } else if (st == 404) {
        route_cache_put(&r);                        /* nežinomas – kitas bandymas po valandos */
    } else {
        ESP_LOGW(TAG, "adsbdb HTTP %d - pertrauka", st);
        s_route_next = now + ROUTE_FAIL_MS;
    }
}

/* Kuriam skrydžiui ieškoti maršruto: sekamas – pirmas, toliau artimiausi */
static bool route_candidate(char *out, size_t n)
{
    static aircraft_snapshot_t *snap;               /* PSRAM */
    const uint32_t now = sky_now_ms();
    track_t t;
    if (track_get(&t) && (t.state == TRACK_LIVE || t.state == TRACK_LOST) &&
        route_cache_needs_lookup(t.ac.callsign, now)) {
        strlcpy(out, t.ac.callsign, n);
        return true;
    }
    if (!snap) snap = psram_malloc(sizeof *snap);
    if (!snap) return false;
    aircraft_table_snapshot(snap);
    for (int i = 0; i < snap->count && i < ROUTE_NEAREST; i++)
        if (route_cache_needs_lookup(snap->ac[i].callsign, now)) {
            strlcpy(out, snap->ac[i].callsign, n);
            return true;
        }
    return false;
}

/* ---------- Orai (Open-Meteo) ---------- */

static uint32_t s_weather_next;
static volatile bool s_weather_now;

void net_refresh_weather(void)
{
    weather_clear();
    s_weather_now = true;
}

static void weather_fetch(http_buf_t *b)
{
    char url[240];
    const home_t *h = aircraft_table_home();
    snprintf(url, sizeof url, "%s/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,weather_code,is_day",
             srv_on() ? g_settings.server_url : "https://api.open-meteo.com", (double)h->lat, (double)h->lon);
    const uint32_t now = sky_now_ms();
    s_weather_next = now + WEATHER_RETRY;
    if (http_request(url, b) != 200) return;
    cJSON *root = cJSON_Parse(b->buf);
    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(cur, "temperature_2m");
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(cur, "weather_code");
    if (cJSON_IsNumber(t) && cJSON_IsNumber(c)) {
        weather_t w = {
            .valid = true, .temp_c = (float)t->valuedouble, .code = (uint8_t)c->valueint,
            .is_day = jnum(cur, "is_day", 1) != 0, .fetched_ms = now,
        };
        weather_set(&w);
        s_weather_next = now + WEATHER_MS;
        ESP_LOGI(TAG, "orai: %.1f C, kodas %d", (double)w.temp_c, w.code);
    }
    cJSON_Delete(root);
}

/* ---------- Papildomi lėktuvai: OpenSky ---------- */

static char    *s_os_tok;                /* OAuth2 raktas (PSRAM) */
static uint32_t s_os_tok_until, s_os_next;
static char     s_os_used[2 * SETTINGS_OS_LEN];   /* kuriai paskyrai skirtas raktas (paskyrai pasikeitus – naujas) */

static bool os_enabled(void)
{
    return g_settings.data_mode == DATA_BUILTIN && g_settings.os_id[0] && g_settings.os_secret[0];
}

static void url_enc(char *dst, size_t n, const char *src)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *src && o + 4 < n; src++) {
        const unsigned char c = (unsigned char)*src;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[o++] = (char)c;
        } else {
            dst[o++] = '%', dst[o++] = HEX[c >> 4], dst[o++] = HEX[c & 15];
        }
    }
    dst[o] = 0;
}

/* Vienkartinė OpenSky užklausa: post != NULL – rakto gavimas, kitaip GET su raktu */
static int os_request(const char *url, const char *post, http_buf_t *b)
{
    buf_reset(b);
    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = post ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
        .event_handler     = http_evt,
        .user_data         = b,
        .user_agent        = USER_AGENT,
        .buffer_size       = 2048,
        .buffer_size_tx    = 2048 + OS_TOKEN_MAX,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    if (post) {
        esp_http_client_set_header(c, "Content-Type", "application/x-www-form-urlencoded");
        esp_http_client_set_post_field(c, post, (int)strlen(post));
    } else {
        char *auth = psram_malloc(OS_TOKEN_MAX + 8);
        if (!auth) {
            esp_http_client_cleanup(c);
            return -1;
        }
        snprintf(auth, OS_TOKEN_MAX + 8, "Bearer %s", s_os_tok);
        esp_http_client_set_header(c, "Authorization", auth);
        free(auth);                              /* antraštė nukopijuojama, todėl auth galima atlaisvinti */
    }
    esp_err_t err = esp_http_client_perform(c);
    int status = err == ESP_OK ? esp_http_client_get_status_code(c) : -1;
    if (err != ESP_OK) ESP_LOGW(TAG, "OpenSky: %s", esp_err_to_name(err));
    esp_http_client_cleanup(c);
    return b->overflow ? -2 : status;
}

/* Raktas: turimas, jei dar galioja (su minutės atsarga), arba naujas. false – nepavyko (os_status ir laukimas nustatyti) */
static bool os_token(http_buf_t *b, uint32_t now)
{
    char who[sizeof s_os_used];
    snprintf(who, sizeof who, "%s\n%s", g_settings.os_id, g_settings.os_secret);
    if (strcmp(who, s_os_used) != 0) {
        strlcpy(s_os_used, who, sizeof s_os_used);
        s_os_tok_until = 0;
        g_status.os_status = 0;
    }
    if (s_os_tok && s_os_tok_until && (int32_t)(s_os_tok_until - now) > 60000) return true;
    if (!s_os_tok && !(s_os_tok = psram_malloc(OS_TOKEN_MAX))) return false;
    char id[3 * SETTINGS_OS_LEN], sec[3 * SETTINGS_OS_LEN], body[7 * SETTINGS_OS_LEN];
    url_enc(id, sizeof id, g_settings.os_id);
    url_enc(sec, sizeof sec, g_settings.os_secret);
    snprintf(body, sizeof body, "grant_type=client_credentials&client_id=%s&client_secret=%s", id, sec);
    const int st = os_request(OS_TOKEN_URL, body, b);
    s_os_tok_until = 0;
    if (st != 200) {
        ESP_LOGW(TAG, "OpenSky: raktas negautas (HTTP %d)", st);
        g_status.os_status = (int16_t)(st == 400 || st == 401 ? 401 : st);
        s_os_next = now + (st == 400 || st == 401 ? OS_BAD_WAIT : 60000);
        return false;
    }
    cJSON *root = cJSON_Parse(b->buf);
    const char *tok = jstr(root, "access_token");
    if (tok && strlen(tok) < OS_TOKEN_MAX) {
        strlcpy(s_os_tok, tok, OS_TOKEN_MAX);
        s_os_tok_until = (now + (uint32_t)jnum(root, "expires_in", 1800) * 1000u) | 1;
    }
    cJSON_Delete(root);
    if (!s_os_tok_until) s_os_next = now + 60000;
    return s_os_tok_until != 0;
}

static double jarr_num(const cJSON *a, int i, double def)
{
    const cJSON *v = cJSON_GetArrayItem(a, i);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static void opensky_fetch(http_buf_t *b)
{
    const uint32_t now = sky_now_ms();
    if (!os_token(b, now)) return;
    /* Stačiakampis aplink namus; užklausos kaina priklauso nuo ploto (kv. laipsniais), nuo kainos – ir periodas */
    const home_t *h = aircraft_table_home();
    const float dla = h->range_km / 111.32f, dlo = h->range_km / (111.32f * fmaxf(h->cos_lat, 0.05f));
    const float area = (2 * dla) * (2 * dlo);
    const int cost = area <= 25.0f ? 1 : area <= 100.0f ? 2 : area <= 400.0f ? 3 : 4;
    const uint32_t gap = (uint32_t)(86400000ull * (uint32_t)cost / OS_DAY_CREDITS);
    s_os_next = now + (gap > OS_MIN_GAP ? gap : OS_MIN_GAP);
    char url[200];
    snprintf(url, sizeof url, OS_STATES_URL "?lamin=%.3f&lomin=%.3f&lamax=%.3f&lomax=%.3f", (double)(h->lat - dla),
             (double)(h->lon - dlo), (double)(h->lat + dla), (double)(h->lon + dlo));
    const int st = os_request(url, NULL, b);
    g_status.os_status = (int16_t)st;
    if (st == 401) {                             /* raktas nebegalioja – kitą kartą bus gautas naujas */
        s_os_tok_until = 0;
        s_os_next = now + 5000;
        return;
    }
    if (st == 429) {
        const int w = b->retry_s > 0 && b->retry_s < 86400 ? b->retry_s : 3600;
        s_os_next = now + (uint32_t)w * 1000u;
        ESP_LOGW(TAG, "OpenSky: kreditai baigesi, laukiama %d s", w);
        return;
    }
    if (st != 200) return;
    cJSON *root = cJSON_Parse(b->buf);
    if (!root) return;
    const double t = jnum(root, "time", 0);
    int n = 0, added = 0;
    const cJSON *sv;
    cJSON_ArrayForEach(sv, cJSON_GetObjectItemCaseSensitive(root, "states")) {
        const cJSON *hex = cJSON_GetArrayItem(sv, 0), *lon = cJSON_GetArrayItem(sv, 5), *lat = cJSON_GetArrayItem(sv, 6);
        if (!cJSON_IsString(hex) || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) continue;
        if (geo_haversine_km(h->lat, h->lon, (float)lat->valuedouble, (float)lon->valuedouble) > h->range_km)
            continue;                            /* stačiakampio kampai – už spindulio */
        aircraft_t a;
        memset(&a, 0, sizeof a);
        a.icao24 = (uint32_t)strtoul(hex->valuestring, NULL, 16);
        const cJSON *cs = cJSON_GetArrayItem(sv, 1);
        if (cJSON_IsString(cs)) copy_trim(a.callsign, sizeof a.callsign, cs->valuestring);
        a.lat = (float)lat->valuedouble;
        a.lon = (float)lon->valuedouble;
        a.on_ground = cJSON_IsTrue(cJSON_GetArrayItem(sv, 8));
        a.alt_ft = (int32_t)(jarr_num(sv, 7, jarr_num(sv, 13, 0)) * 3.28084);        /* metrai → pėdos */
        a.speed_kt = (uint16_t)fmax(0.0, jarr_num(sv, 9, 0) * 1.943844);             /* m/s → mazgai */
        a.track_deg = (uint16_t)fmod(jarr_num(sv, 10, 0) + 360.0, 360.0);
        a.vrate_fpm = (int16_t)fmax(-32000.0, fmin(32000.0, jarr_num(sv, 11, 0) * 196.8504));  /* m/s → ft/min */
        const double age = t > 0 ? t - jarr_num(sv, 3, t) : 0;
        a.pos_ms = now - (uint32_t)(fmax(0.0, fmin(60.0, age)) * 1000.0);
        n++;
        if (aircraft_table_upsert_extra(&a, OS_FRESH_MS)) {
            added++;
            if (track_matches(&a)) track_found(&a);
        }
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "OpenSky: %d lektuvu, %d papildomu", n, added);
}

/* ---------- Ekrano registracija AeroBox serveryje ---------- */

static uint32_t s_reg_next;

static void server_register(http_buf_t *b)
{
    s_reg_next = sky_now_ms() + SERVER_REG_MS;
    char url[SETTINGS_SRV_LEN + 64];
    uint8_t mac[6] = { 0 };                           /* įrenginio numeris: pagal jį serveris atpažįsta tą patį ekraną */
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(url, sizeof url, "%s/v1/register?fw=%s&id=%02x%02x%02x%02x%02x%02x", g_settings.server_url,
             SKY_FW_VERSION, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const int st = os_request(url, "", b);
    if (st != 200) {
        ESP_LOGW(TAG, "serveris: registracija nepavyko (HTTP %d)", st);
        return;
    }
    cJSON *root = cJSON_Parse(b->buf);
    const char *key = jstr(root, "key");
    if (key && strlen(key) < SETTINGS_SRV_KEY_LEN) {
        settings_save_server(g_settings.server_url, key);
        ESP_LOGI(TAG, "serveris: skydelis uzregistruotas (%s)", jstr(root, "id") ? jstr(root, "id") : "?");
    }
    cJSON_Delete(root);
}

/* ---------- Programos atnaujinimai iš AeroBox serverio ---------- */

static uint32_t s_upd_next = UPD_FIRST_MS;
static volatile bool s_upd_req;

/* „1.2.10“ > „1.2.9“: lyginama skaičiais iki pirmo simbolio, kuris nėra skaitmuo ar taškas (variantas „-fw“ nelyginamas) */
static bool ver_newer(const char *a, const char *b)
{
    for (;;) {
        const long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x > y;
        if (*a != '.' && *b != '.') return false;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
}

static void update_check(http_buf_t *b)
{
    s_upd_next = sky_now_ms() + UPD_RETRY_MS;
    char url[SETTINGS_SRV_LEN + 48];
    snprintf(url, sizeof url, "%s/v1/update?fw=%s", g_settings.server_url, SKY_FW_VERSION);
    if (http_request(url, b) != 200) return;
    s_upd_next = sky_now_ms() + UPD_CHECK_MS;
    cJSON *root = cJSON_Parse(b->buf);
    const char *v = jstr(root, "version"), *sha = jstr(root, "sha256"), *notes = jstr(root, "notes");
    const double size = jnum(root, "size", 0);
    const bool offer = v && *v && strlen(v) < sizeof g_status.upd_ver && sha && strlen(sha) == 64 && size > 0 &&
                       size < 8e6 && ver_newer(v, SKY_FW_VERSION);
    if (offer) {
        if (strcmp(v, g_status.upd_ver) != 0) ESP_LOGI(TAG, "atnaujinimas: siuloma %s", v);
        strlcpy(g_status.upd_sha, sha, sizeof g_status.upd_sha);
        strlcpy(g_status.upd_notes, notes ? notes : "", sizeof g_status.upd_notes);
        g_status.upd_size = (uint32_t)size;
        strlcpy(g_status.upd_ver, v, sizeof g_status.upd_ver);
    } else if (root) {
        g_status.upd_ver[0] = 0;                         /* naujesnės nėra (arba ji atšaukta) */
    }
    cJSON_Delete(root);
}

bool net_update_start(void)
{
    if (!g_status.upd_ver[0] || g_status.upd_state == 1 || s_upd_req) return false;
    g_status.upd_err = NULL;
    g_status.upd_pct = 0;
    g_status.upd_state = 1;
    s_upd_req = true;
    return true;
}

static void update_install(void)
{
    s_upd_req = false;
    const char *fail = NULL;
    char url[SETTINGS_SRV_LEN + 48], ver[sizeof g_status.upd_ver], want[65];
    strlcpy(ver, g_status.upd_ver, sizeof ver);
    strlcpy(want, g_status.upd_sha, sizeof want);
    const uint32_t size = g_status.upd_size;
    snprintf(url, sizeof url, "%s/v1/firmware/%s", g_settings.server_url, ver);
    ESP_LOGW(TAG, "atnaujinimas: diegiama %s (%u B)", ver, (unsigned)size);

    close_point();                                       /* atlaisvinami kiti ryšiai (atmintis) */
    if (s_db) {
        esp_http_client_cleanup(s_db);
        s_db = NULL;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    esp_http_client_config_t cfg = {
        .url               = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 15000,
        .user_agent        = USER_AGENT,
        .buffer_size       = 4096,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    char *buf = malloc(4096);
    esp_ota_handle_t ota = 0;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    if (!part || !c || !buf) {
        fail = "Out of memory.";
    } else {
        esp_http_client_set_header(c, "X-Device-Key", g_settings.server_key);
        if (esp_http_client_open(c, 0) != ESP_OK) fail = "Could not reach the server.";
    }
    if (!fail) {
        const int64_t len = esp_http_client_fetch_headers(c);
        const int st = esp_http_client_get_status_code(c);
        if (st != 200) fail = "The server did not send the file.";
        else if (len != (int64_t)size || size > part->size) fail = "Wrong file size.";
        else if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) fail = "Could not start the update.";
        const size_t desc_off = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
        uint32_t got = 0;
        while (!fail && got < size) {
            const int r = esp_http_client_read(c, buf, 4096);
            if (r <= 0) {
                fail = "Connection lost.";
                break;
            }
            if (got == 0 && (size_t)r >= desc_off + sizeof(esp_app_desc_t)) {
                const esp_app_desc_t *d = (const esp_app_desc_t *)(buf + desc_off);
                if ((uint8_t)buf[0] != ESP_IMAGE_HEADER_MAGIC || d->magic_word != ESP_APP_DESC_MAGIC_WORD ||
                    strncmp(d->project_name, esp_app_get_description()->project_name, sizeof d->project_name) != 0) {
                    fail = "This file is not AeroBox firmware.";
                    break;
                }
            }
            mbedtls_sha256_update(&sha, (const unsigned char *)buf, (size_t)r);
            if (esp_ota_write(ota, buf, (size_t)r) != ESP_OK) {
                fail = "Write failed.";
                break;
            }
            got += (uint32_t)r;
            g_status.upd_pct = (uint8_t)((uint64_t)got * 100 / size);
        }
    }
    uint8_t dig[32];
    mbedtls_sha256_finish(&sha, dig);
    mbedtls_sha256_free(&sha);
    if (!fail) {
        char hex[65];
        for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", dig[i]);
        if (strcasecmp(hex, want) != 0) fail = "The download is damaged (checksum).";
    }
    if (ota) {
        if (fail) esp_ota_abort(ota);
        else if (esp_ota_end(ota) != ESP_OK) fail = "The file is damaged.";
    }
    if (!fail && esp_ota_set_boot_partition(part) != ESP_OK) fail = "Could not activate the new firmware.";
    if (c) esp_http_client_cleanup(c);
    free(buf);
    if (fail) {
        ESP_LOGE(TAG, "atnaujinimas nepavyko: %s", fail);
        g_status.upd_err = fail;
        g_status.upd_state = -1;
        return;
    }
    ESP_LOGW(TAG, "atnaujinta i %s - perkraunama", ver);
    g_status.upd_pct = 100;
    g_status.upd_state = 2;
    vTaskDelay(pdMS_TO_TICKS(2500));                     /* puslapis spės parodyti eigą, o ekranas – 100 % */
    esp_restart();
}

/* Užklausos kitiems serveriams (ne lėktuvų šaltiniui), kol laukiama kitos bendros užklausos.
 * Ne daugiau kaip viena per kartą; true – kažkas atlikta. */
static bool side_request(http_buf_t *b)
{
    const uint32_t now = sky_now_ms();
    if (s_trk.query[0] && !s_trk.resolved && (int32_t)(now - s_trk.next_ms) >= 0) {
        track_resolve(b);
        return true;
    }
    if (srv_on() && g_status.upd_state != 1 && (int32_t)(now - s_upd_next) >= 0) {
        update_check(b);
        return true;
    }
    if (os_enabled() && (int32_t)(now - s_os_next) >= 0) {
        opensky_fetch(b);
        return true;
    }
    if (s_weather_now || (int32_t)(now - s_weather_next) >= 0) {
        s_weather_now = false;
        weather_fetch(b);
        return true;
    }
#if CONFIG_SKY_ROUTE_LOOKUP
    char cs[9];
    if ((int32_t)(now - s_route_next) >= 0 && route_candidate(cs, sizeof cs)) {
        s_route_next = now + ROUTE_GAP_MS;
        route_lookup(b, cs);
        return true;
    }
#endif
    return false;
}

void net_task(void *arg)
{
    cJSON_Hooks hooks = { .malloc_fn = psram_malloc, .free_fn = free };
    cJSON_InitHooks(&hooks);

    http_buf_t b = { .cap = RESP_MAX };
    b.buf = psram_malloc(b.cap);
    if (!b.buf) {
        ESP_LOGE(TAG, "nera atminties atsakymo buferiui");
        vTaskDelete(NULL);
        return;
    }

    char url[160], last_url[160] = "", tbase[SETTINGS_SRV_LEN + 12], used_srv[SETTINGS_SRV_LEN + SETTINGS_SRV_KEY_LEN + 2] = "";
    uint32_t period = 0;                        /* papildomas laukimas po 429 */
    uint32_t srv_jitter = esp_random() % SERVER_JITTER;   /* keičiamas po kiekvienos užklausos serveriui */
    uint32_t last_log = 0, requests = 0, last_429 = 0, last_req = 0;

    /* Biudžetas: likutis iš NVS ir tai, kas prisipildė, kol įrenginys buvo išjungtas */
    float tokens = 5.0f;
    int64_t saved_epoch = 0;
    bool budget_synced = false;                 /* ar jau pridėtas laikas nuo išsaugojimo */
    uint32_t last_refill = sky_now_ms(), last_save = 0;
    if (settings_budget_load(&tokens, &saved_epoch)) ESP_LOGI(TAG, "biudzetas is NVS: %.1f", (double)tokens);

    static char used_key[SETTINGS_KEY_LEN];
    bool used_own = false;
    for (;;) {
        while (!net_wifi_wait(60000)) { }
        if (s_upd_req) {                                     /* valdymo puslapyje paspausta „Update“ */
            if (g_settings.server_url[0] && g_settings.server_key[0]) {
                update_install();
            } else {
                s_upd_req = false;
                g_status.upd_err = "No AeroBox server set.";
                g_status.upd_state = -1;
            }
            last_req = 0;
        }
        /* Šaltinis: naudotojo raktas (ADS-B Exchange) arba įtaisytas (adsb.fi / adsb.lol). Pasikeitus šaltiniui ar
         * raktui – naujas ryšys (kitos antraštės) ir užklausa iškart */
        const bool own = g_settings.data_mode == DATA_OWN_KEY && g_settings.api_key[0];
        if (own != used_own || (own && strcmp(used_key, g_settings.api_key) != 0)) {
            close_point();
            used_own = own;
            strlcpy(used_key, own ? g_settings.api_key : "", sizeof used_key);
            last_req = 0;
            period = 0;
            g_status.key_status = 0;
            g_status.limited_until_ms = 0;
            ESP_LOGI(TAG, "lektuvu duomenys: %s", own ? "savo API raktas (ADS-B Exchange)" : "integruoti");
        }
        const char *key = own ? used_key : NULL;
        /* AeroBox serveris: jei rakto dar nėra, registruojamasi (kol nepavyksta – duomenys tiesiai iš šaltinių) */
        if (!own && g_settings.data_mode == DATA_BUILTIN && g_settings.server_url[0] && !g_settings.server_key[0] &&
            (int32_t)(sky_now_ms() - s_reg_next) >= 0)
            server_register(&b);
        if (g_status.srv_down && (int32_t)(sky_now_ms() - s_srv_down_until) >= 0) {
            g_status.srv_down = 0;                           /* atsarginio kelio laikas baigėsi – vėl naudojamas serveris */
            ESP_LOGI(TAG, "serveris: bandoma vel");
        }
        const bool srv = !own && srv_on();
        char sig[sizeof used_srv];
        snprintf(sig, sizeof sig, "%s\n%s", srv ? g_settings.server_url : "", srv ? g_settings.server_key : "");
        if (strcmp(sig, used_srv) != 0) {                    /* pasikeitė serveris ar raktas – kuriami nauji ryšiai */
            strlcpy(used_srv, sig, sizeof used_srv);
            close_point();
            if (s_db) {
                esp_http_client_cleanup(s_db);
                s_db = NULL;
            }
            last_req = 0;
            snprintf(tbase, sizeof tbase, "%s/api/v2", g_settings.server_url);
            if (srv) ESP_LOGI(TAG, "lektuvu duomenys: savo serveris %s", g_settings.server_url);
        }
        const bool fi = !own && !srv && g_settings.data_src == 1;
        const bool budget_mode = !own && !srv && g_settings.poll_s == 0 && !fi;   /* paros biudžetas taikomas tik adsb.lol */
        track_prepare();

        /* Žetonai prisipildo pagal praėjusį laiką */
        uint32_t now = sky_now_ms();
        tokens += (float)(now - last_refill) / TOKEN_MS;
        last_refill = now;
        if (!budget_synced && sky_time_valid()) {
            budget_synced = true;
            int64_t off = (int64_t)time(NULL) - saved_epoch;
            if (saved_epoch > 0 && off > 0) tokens += (float)(off * 1000 / TOKEN_MS);
        }
        if (tokens > BUDGET_CAP) tokens = BUDGET_CAP;
        g_status.budget = tokens;

        /* Kada kita užklausa */
        uint32_t gap;
        if (budget_mode) {
            const int sh = g_settings.night_start, eh = g_settings.night_end;   /* minutės nuo vidurnakčio */
            bool night = false;
            if (sh != eh && sky_time_valid()) {
                time_t t = time(NULL);
                struct tm tm;
                localtime_r(&t, &tm);
                const int m = tm.tm_hour * 60 + tm.tm_min;
                night = sh > eh ? (m >= sh || m < eh) : (m >= sh && m < eh);
            }
            gap = night ? GAP_NIGHT : g_status.seen_total > 0 ? GAP_AIRCRAFT : GAP_EMPTY;
            if (tokens < 1.0f) {                    /* žetono nėra – laukti, kol prisipildys */
                uint32_t need = (uint32_t)((1.0f - tokens) * TOKEN_MS);
                if (gap < need) gap = need;
            }
            if (last_req == 0 && tokens >= 1.0f) gap = 0;   /* pirma užklausa po įjungimo */
        } else if (own) {
            gap = g_settings.key_poll_s * 1000u;
        } else if (srv) {
            gap = SERVER_GAP - SERVER_JITTER / 2 + srv_jitter;
        } else {
            gap = g_settings.poll_s ? g_settings.poll_s * 1000u : ADSBFI_GAP;
        }
        if (period > gap) gap = period;
        const uint32_t due = last_req + gap;
        g_status.next_req_ms = last_req ? due : now;
        g_status.poll_ms = (uint16_t)(gap > 65000 ? 65000 : gap);
        if (last_req && (int32_t)(due - now) > 0) {
            /* Laukiant atliekami kiti darbai (orai, maršrutai); kas sekundę tikrinama, ar kas nepasikeitė (pvz., nustatymai) */
            if (!side_request(&b)) {
                const uint32_t w = due - now;
                vTaskDelay(pdMS_TO_TICKS(w > 1000 ? 1000 : w));
            }
            continue;
        }

        /* Vieta gali būti pakeista valdymo puslapyje, todėl adresas sudaromas kiekvieną kartą */
        const home_t *h = aircraft_table_home();
        int radius_nm = (int)ceilf(h->range_km / 1.852f) + 2;   /* jūrmylės su atsarga */
        if (own)
            snprintf(url, sizeof url, ADSBX_BASE "/lat/%.4f/lon/%.4f/dist/%d/", h->lat, h->lon, radius_nm);
        else if (srv)
            snprintf(url, sizeof url, "%s/api/v2/lat/%.4f/lon/%.4f/dist/%d", g_settings.server_url, h->lat, h->lon,
                     radius_nm);
        else if (fi)
            snprintf(url, sizeof url, FI_BASE "/lat/%.4f/lon/%.4f/dist/%d", h->lat, h->lon, radius_nm);
        else
            snprintf(url, sizeof url, LOL_BASE "/point/%.4f/%.4f/%d", h->lat, h->lon, radius_nm);
        if (strcmp(url, last_url) != 0) {
            ESP_LOGI(TAG, "saltinis: %s", url);
            strlcpy(last_url, url, sizeof last_url);
        }

        last_req = sky_now_ms() | 1;
        if (budget_mode) tokens -= 1.0f;
        uint32_t extra_wait = 0;
        int st = keep_get(&s_point, url, &b, key);
        requests++;
        if (srv) {
            srv_jitter = esp_random() % SERVER_JITTER;
            srv_result(st > 0 && st < 500);              /* 5xx ar ryšio klaida – nesėkmė */
        }
        if (own && st > 0) g_status.key_status = (int16_t)st;
        if (st >= 200 && st < 300) {
            int n = parse_aircraft(b.buf);
            if (n >= 0) {
                g_status.data_ok = true;
                g_status.last_ok_ms = sky_now_ms();
                g_status.seen_total = n;
            }
            g_status.limited_until_ms = 0;
            if (period && sky_now_ms() - last_429 > RECOVER_MS) {
                period = period > 1000 ? period - 1000 : 0;
                last_429 = sky_now_ms();
            }
            /* Sekamas skrydis toli – atskira užklausa, bet ne iškart po bendrosios */
            if (track_due(sky_now_ms(), budget_mode, tokens)) {
                vTaskDelay(pdMS_TO_TICKS(SRC_SPACING_MS));
                if (budget_mode) tokens -= 1.0f;
                const uint32_t key_gap = g_settings.key_poll_s * 1000u > TRACK_KEY_MIN ? g_settings.key_poll_s * 1000u
                                                                                        : TRACK_KEY_MIN;
                s_trk.next_ms = sky_now_ms() + (own ? key_gap : (fi || srv) ? TRACK_FI_MS : budget_mode ? TRACK_BUDGET_MS
                                                                                               : TRACK_LOL_MS);
                if (track_request(&b, own ? ADSBX_BASE : srv ? tbase : fi ? FI_BASE : LOL_BASE, key) == 429)
                    s_trk.next_ms = sky_now_ms() + BACKOFF_MAX_MS;
                requests++;
            }
        } else if (st == 429) {
            /* Per daug užklausų iš šio IP: naujas ryšys, laukiama tiek, kiek prašo serveris, ir užklausos retinamos */
            int retry = b.retry_s > 0 ? b.retry_s : 30;
            extra_wait = (uint32_t)(retry < RETRY_MAX_S ? retry : RETRY_MAX_S) * 1000;
            period = period + BACKOFF_STEP < BACKOFF_MAX_MS ? period + BACKOFF_STEP : BACKOFF_MAX_MS;
            last_429 = sky_now_ms();
            g_status.limited++;
            g_status.limited_until_ms = (last_429 + extra_wait) | 1;
            close_point();
            ESP_LOGW(TAG, "429 - per daug uzklausu, serveris praso laukti %d s", retry);
        } else if (own && (st == 401 || st == 403)) {
            ESP_LOGW(TAG, "API raktas atmestas (HTTP %d)", st);
            close_point();
            extra_wait = KEY_BAD_WAIT;
        } else if (srv && st == 401) {
            ESP_LOGW(TAG, "serveris nepazista rakto - registruojamasi is naujo");
            settings_save_server(g_settings.server_url, "");
            s_reg_next = 0;
            close_point();
            extra_wait = 1000;
        } else {
            ESP_LOGW(TAG, "uzklausa nepavyko (HTTP %d)", st);
            extra_wait = 2000;
        }
        if (b.remaining == 0 && b.retry_s > 0 && extra_wait == 0) extra_wait = (uint32_t)b.retry_s * 1000;
        /* „NO FLIGHT DATA“ – tik jei nepavyksta ilgai (15 min); retos biudžeto užklausos to nesukelia */
        if (g_status.data_ok && sky_now_ms() - g_status.last_ok_ms > 900000) g_status.data_ok = false;

        g_status.budget = tokens;
        now = sky_now_ms();
        if (budget_mode && sky_time_valid() && (last_save == 0 || now - last_save >= BUDGET_SAVE_MS)) {
            settings_budget_save(tokens, (int64_t)time(NULL));
            last_save = now | 1;
        }
        if (now - last_log >= LOG_EVERY_MS) {
            ESP_LOGI(TAG, "%d lektuvu, %u uzklausu per %u s, rezervas %.1f", g_status.seen_total, (unsigned)requests,
                     (unsigned)((now - last_log) / 1000), (double)tokens);
            last_log = now;
            requests = 0;
        }
        /* Po klaidos laukiama; pakeitus šaltinį ar raktą valdymo puslapyje – iškart iš naujo */
        for (uint32_t waited = 0; waited < extra_wait; waited += 1000) {
            const bool own_now = g_settings.data_mode == DATA_OWN_KEY && g_settings.api_key[0];
            if (own_now != used_own || (own_now && strcmp(used_key, g_settings.api_key) != 0)) break;
            vTaskDelay(pdMS_TO_TICKS(extra_wait - waited < 1000 ? extra_wait - waited : 1000));
        }
    }
}
