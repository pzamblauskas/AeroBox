/* Valdymo puslapis – tas pats nustatymų tinkle (http://192.168.4.1) ir namų tinkle (http://aerobox.local).
 *   GET  /                  – puslapis (www/index.html, įterptas į programą)
 *   GET  /api/status        – JSON: būsena, nustatymai, lėktuvai (API raktas niekada negrąžinamas)
 *   GET  /api/scan          – aplinkiniai Wi-Fi tinklai
 *   GET  /api/frame         – dabartinis ekrano vaizdas (w × h, RGB565) peržiūrai puslapyje
 *   POST /api/settings      – bet kuris nustatymas (vieta, ryškumas, režimas, naktis, sekimas, duomenys, laikas …)
 *   POST /api/wifi          – išbandyti naują Wi-Fi; pavykus jis išsaugomas
 *   POST /api/wifi-reset    – pamiršti Wi-Fi
 *   POST /api/test          – funkcijos demonstracija ekrane kelias sekundes (kind=overhead|rare|map|flap|track|multi)
 *   POST /api/ota           – programos atnaujinimas (firmware.bin)
 *   POST /api/update        – įdiegti serverio pasiūlytą versiją
 *   POST /api/restart, /api/factory-reset
 * POST priimamas tik su antrašte „X-AeroBox: 1“ ir įrenginio vardu ar IP adresu Host antraštėje, todėl kita
 * svetainė naudotojo naršyklėje negali nieko pakeisti (apsauga nuo CSRF ir DNS rebinding).
 * Nustatymų tinkle visi kiti adresai nukreipiami į puslapį (telefonų „interneto patikrai“). */
#include "net.h"
#include "net_priv.h"
#include "aircraft.h"
#include "app_status.h"
#include "gfx.h"
#include "hub75.h"
#include "hub75_layout.h"
#include "mode.h"
#include "settings.h"
#include "track.h"
#include "weather.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "webui";
#define PORTAL_URL "http://192.168.4.1/"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");
extern const char index_gz_start[]   asm("_binary_index_html_gz_start");   /* tas pats puslapis, suspaustas gzip (gz_page.py) */
extern const char index_gz_end[]     asm("_binary_index_html_gz_end");

static httpd_handle_t s_httpd;
#define STATUS_JSON_MAX 8192            /* 32 lėktuvai, spalvos ir atnaujinimo laukai – su atsarga */

/* ---------- Pagalbinės funkcijos ---------- */
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') {
            *o++ = ' ';
        } else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char h[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
}

static bool form_str(const char *body, const char *key, char *out, size_t n)
{
    if (httpd_query_key_value(body, key, out, n) != ESP_OK) return false;
    url_decode(out);
    return true;
}

static bool form_float(const char *body, const char *key, float *out)
{
    char v[24];
    if (!form_str(body, key, v, sizeof v)) return false;
    char *end;
    float f = strtof(v, &end);
    if (end == v || !isfinite(f)) return false;
    *out = f;
    return true;
}

static bool form_int(const char *body, const char *key, int lo, int hi, int *out)
{
    float f;
    if (!form_float(body, key, &f) || f < lo || f > hi) return false;
    *out = (int)f;
    return true;
}

/* JSON eilutė: kabutės, \ ir valdymo simboliai ekranuojami, UTF-8 paliekamas */
static int json_str(char *out, size_t n, const char *in)
{
    size_t o = 0;
    if (n < 3) return 0;
    out[o++] = '"';
    for (; *in && o + 8 < n; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            o += snprintf(out + o, n - o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o++] = '"';
    out[o] = 0;
    return (int)o;
}

/* Visas užklausos turinys į body. Per didelis – 413 ir ryšys uždaromas (kad neperskaityta liekana nebūtų
 * palaikyta kita užklausa) */
static esp_err_t recv_body(httpd_req_t *req, char *body, size_t cap)
{
    if (req->content_len >= cap) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Request too large");
        return ESP_FAIL;
    }
    const int len = (int)req->content_len;
    int got = 0, stalls = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++stalls <= 2) continue;
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    body[got] = 0;
    return ESP_OK;
}

static bool is_ipv4(const char *s)
{
    int dots = 0, digits = 0;
    for (; *s; s++) {
        if (*s == '.') {
            if (!digits) return false;
            dots++, digits = 0;
        } else if (isdigit((unsigned char)*s) && digits < 3) {
            digits++;
        } else {
            return false;
        }
    }
    return dots == 3 && digits;
}

/* Įrenginio vardas: aerobox[.local]; jei viename tinkle keli – aerobox-2[.local] ir t. t. (mDNS) */
static bool is_own_name(const char *v)
{
    if (strncasecmp(v, "aerobox", 7) != 0) return false;
    v += 7;
    if (*v == '-') {
        if (!isdigit((unsigned char)v[1])) return false;
        for (v++; isdigit((unsigned char)*v); v++) { }
    }
    return !*v || strcasecmp(v, ".local") == 0;
}

/* POST priimamas tik iš paties valdymo puslapio. Jis prideda „X-AeroBox: 1“ (kitos svetainės naršyklė be leidimo
 * (CORS) tokios antraštės nusiųsti negali), o Host turi būti įrenginio vardas arba IP (ne svetimas vardas,
 * nukreiptas į įrenginį) */
static bool post_allowed(httpd_req_t *req)
{
    char v[64];
    /* X-SkyPanel – senasis pavadinimas: veikia ir puslapis, atidarytas prieš atnaujinimą (ar iš senesnės versijos) */
    if ((httpd_req_get_hdr_value_str(req, "X-AeroBox", v, sizeof v) != ESP_OK || strcmp(v, "1") != 0) &&
        (httpd_req_get_hdr_value_str(req, "X-SkyPanel", v, sizeof v) != ESP_OK || strcmp(v, "1") != 0))
        return false;
    if (httpd_req_get_hdr_value_str(req, "Host", v, sizeof v) != ESP_OK) return false;
    char *end = v[0] == '[' ? strchr(v, ']') : strchr(v, ':');      /* be prievado; [IPv6] – paliekama kaip yra */
    if (end && v[0] != '[') *end = 0;
    const size_t n = strlen(v);
    if (n && v[n - 1] == '.') v[n - 1] = 0;                           /* "aerobox.local." */
    return v[0] == '[' || is_ipv4(v) || is_own_name(v);
}

static esp_err_t forbidden(httpd_req_t *req)
{
    ESP_LOGW(TAG, "atmesta uzklausa %s (ne is valdymo puslapio)", req->uri);
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Forbidden");
}

/* Saugus JSON rinkimas į fiksuoto dydžio buferį: kas netelpa, nukerpama, bet niekada nerašoma už buferio ribų */
typedef struct {
    char  *buf;
    size_t cap, len;
} jbuf_t;

static void jb_add(jbuf_t *j, const char *fmt, ...)
{
    if (j->len + 1 >= j->cap) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(j->buf + j->len, j->cap - j->len, fmt, ap);
    va_end(ap);
    if (n > 0) j->len = j->len + (size_t)n < j->cap ? j->len + (size_t)n : j->cap - 1;
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_err(httpd_req_t *req, const char *text)
{
    char json[256], esc[200];
    json_str(esc, sizeof esc, text);
    snprintf(json, sizeof json, "{\"ok\":0,\"err\":%s}", esc);
    return send_json(req, json);
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    settings_restart();
}

static void restart_later(void)
{
    xTaskCreate(restart_task, "restart", 3072, NULL, 5, NULL);
}

/* ---------- HTTP ---------- */
/* Puslapio apsauga: jo negalima įdėti į svetimą svetainę (clickjacking); skriptai ir paveikslėliai – tik iš paties
 * puslapio, žemėlapio bibliotekos (unpkg) ir OpenStreetMap plytelių. Referer siunčiamas tik be kelio: be Referer
 * OpenStreetMap plytelių neduoda („Access blocked“), todėl ne „no-referrer“ */
#define PAGE_CSP "default-src 'self'; script-src 'self' 'unsafe-inline' https://unpkg.com; " \
                 "style-src 'self' 'unsafe-inline' https://unpkg.com; " \
                 "img-src 'self' data: https://unpkg.com https://tile.openstreetmap.org; connect-src 'self'; " \
                 "frame-ancestors 'none'; base-uri 'none'; form-action 'none'"

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Security-Policy", PAGE_CSP);
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "strict-origin-when-cross-origin");
    /* Po Wi-Fi nustatymo adresas ekrane rodomas, kol puslapis atidaromas per namų Wi-Fi.
     * Kol veikia nustatymų tinklas, užklausa gali ateiti iš telefono nustatymų lango – ji neskaičiuojama. */
    if (!g_status.portal) settings_welcome_done();
    /* Suspaustas puslapis (~17 KB vietoj ~51 KB), jei naršyklė priima gzip (visos šiuolaikinės priima) */
    char enc[64] = "";
    if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", enc, sizeof enc) != ESP_ERR_NOT_FOUND &&
        strstr(enc, "gzip")) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
        return httpd_resp_send(req, index_gz_start, index_gz_end - index_gz_start);
    }
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

/* Sekamas skrydis JSON pavidalu: {"q":…,"st":būsena,…} */
static int track_json(char *out, size_t n)
{
    track_t t;
    if (!track_get(&t)) return snprintf(out, n, "null");
    char q[32], cs[24], ty[16], rg[24], o[12], d[12], city[40], al[56];
    json_str(q, sizeof q, t.query);
    json_str(cs, sizeof cs, t.ac.callsign);
    json_str(ty, sizeof ty, t.ac.type);
    json_str(rg, sizeof rg, t.ac.reg);
    json_str(o, sizeof o, t.ac.has_route ? t.ac.orig : "");
    json_str(d, sizeof d, t.ac.has_route ? t.ac.dest : "");
    json_str(city, sizeof city, t.dest_city);
    json_str(al, sizeof al, t.airline);
    return snprintf(out, n,
                    "{\"q\":%s,\"st\":%d,\"cs\":%s,\"ty\":%s,\"rg\":%s,\"o\":%s,\"de\":%s,\"city\":%s,\"al\":%s,"
                    "\"alt\":%ld,\"gnd\":%d,\"gs\":%u,\"d\":%.1f,\"rem\":%.0f,\"p\":%.3f,\"eta\":%d,\"land\":%d,\"near\":%d,"
                    "\"age\":%lu}",
                    q, (int)t.state, cs, ty, rg, o, d, city, al, (long)t.ac.alt_ft, t.ac.on_ground ? 1 : 0,
                    (unsigned)t.ac.speed_kt, (double)t.ac.dist_km, (double)t.remain_km, (double)t.ac.progress,
                    t.eta_min, t.landed ? 1 : 0, t.in_range ? 1 : 0, (unsigned long)t.age_s);
}

/* Visų langų rodomos spalvos (temos arba naudotojo) pavidalu „ff8c00,00beff,…;…“ (langai atskirti „;“) ir jų temos
 * „0,0,255,…“ – valdymo puslapio spalvų laukams */
static void colors_hex(char *out, size_t n, char *themes, size_t tn)
{
    size_t o = 0, to = 0;
    out[0] = themes[0] = 0;
    for (int w = 0; w < WIN_COUNT; w++) {
        uint16_t c[COL_COUNT];
        if (g_settings.theme[w] == THEME_CUSTOM) memcpy(c, g_settings.colors[w], sizeof c);
        else theme_colors(g_settings.theme[w], w, c);
        to += (size_t)snprintf(themes + to, tn - to, "%s%u", w ? "," : "", (unsigned)g_settings.theme[w]);
        for (int i = 0; i < COL_COUNT && o + 9 <= n; i++) {
            const unsigned r = c[i] >> 11, gg = (c[i] >> 5) & 63, b = c[i] & 31;
            o += (size_t)snprintf(out + o, n - o, "%s%02x%02x%02x", i ? "," : w ? ";" : "", (r << 3) | (r >> 2),
                                  (gg << 2) | (gg >> 4), (b << 3) | (b >> 2));
        }
    }
}

/* „ff8c00,00beff,…“ (COL_COUNT spalvų) → RGB565; false – netinkamas formatas */
static bool colors_parse(const char *s, uint16_t out[COL_COUNT])
{
    for (int i = 0; i < COL_COUNT; i++) {
        if (*s == '#') s++;
        char *end;
        const unsigned long v = strtoul(s, &end, 16);
        if (end - s != 6) return false;
        out[i] = RGB565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
        s = end;
        if (i < COL_COUNT - 1 && *s++ != ',') return false;
    }
    return *s == 0;
}

static esp_err_t status_get(httpd_req_t *req)
{
    /* PSRAM: vidinė RAM paliekama HUB75 DMA buferiams */
    static aircraft_snapshot_t *snap;
    static char *json;
    if (!json) {
        json = heap_caps_malloc(STATUS_JSON_MAX, MALLOC_CAP_SPIRAM);
        snap = heap_caps_malloc(sizeof *snap, MALLOC_CAP_SPIRAM);
    }
    if (!json || !snap) return httpd_resp_send_500(req);
    aircraft_table_snapshot(snap);

    wifi_ap_record_t ap = { 0 };
    int rssi = g_status.wifi && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    uint32_t now = sky_now_ms();
    int age = g_status.last_ok_ms ? (int)((now - g_status.last_ok_ms) / 1000) : -1;
    const home_t *h = aircraft_table_home();
    char ssid[72], tiles[64], tz[64], reset[40], apn[32], wmsg[400], label[48], k4[16], osid[2 * SETTINGS_OS_LEN], srvj[2 * SETTINGS_SRV_LEN],
         updj[2 * sizeof g_status.upd_ver], updn[2 * sizeof g_status.upd_notes], upde[96];
    json_str(updj, sizeof updj, g_status.upd_ver);
    json_str(updn, sizeof updn, g_status.upd_notes);
    json_str(upde, sizeof upde, g_status.upd_err ? g_status.upd_err : "");
    json_str(ssid, sizeof ssid, g_settings.wifi_ssid);
    json_str(tiles, sizeof tiles, g_settings.tiles);
    json_str(tz, sizeof tz, g_settings.tz);
    json_str(reset, sizeof reset, g_status.reset_reason);
    json_str(apn, sizeof apn, g_status.portal_ssid);
    json_str(wmsg, sizeof wmsg, g_status.portal_state == PORTAL_FAIL ? net_portal_fail_text() : "");
    json_str(label, sizeof label, g_settings.track_label);
    const size_t kl = strlen(g_settings.api_key);
    json_str(k4, sizeof k4, kl > 4 ? g_settings.api_key + kl - 4 : "");   /* tik paskutiniai 4 simboliai */
    json_str(srvj, sizeof srvj, g_settings.server_url);
    json_str(osid, sizeof osid, g_settings.os_id);                          /* slaptasis raktas negrąžinamas */
    char cols[WIN_COUNT * COL_COUNT * 7 + 1], themes[WIN_COUNT * 4];
    colors_hex(cols, sizeof cols, themes, sizeof themes);
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    jbuf_t j = { .buf = json, .cap = STATUS_JSON_MAX };
    jb_add(&j, "{\"wifi\":%d,\"has_wifi\":%d,\"ssid\":%s,\"rssi\":%d,\"ip\":\"%s\",\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
           "\"portal\":%d,\"ap\":%s,\"wst\":%u,\"wmsg\":%s,\"up\":%lu,\"data\":%d,\"age\":%d,\"seen\":%d,\"n\":%d,",
           g_status.wifi ? 1 : 0, g_settings.wifi_ssid[0] ? 1 : 0, ssid, rssi, g_status.wifi ? g_status.ip : "",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], g_status.portal ? 1 : 0, apn,
           (unsigned)g_status.portal_state, wmsg, (unsigned long)(now / 1000), g_status.data_ok ? 1 : 0, age,
           g_status.seen_total, snap->count);
    jb_add(&j, "\"lat\":%.5f,\"lon\":%.5f,\"range\":%.0f,\"bright\":%u,\"view\":%u,\"flip\":%u,\"ns\":%u,\"ne\":%u,"
           "\"nb\":%u,\"layout\":%u,\"tiles\":%s,\"psu\":%u,\"co\":%u,\"rs\":%u,\"rm\":%u,",
           (double)h->lat, (double)h->lon, (double)h->range_km, (unsigned)g_settings.brightness,
           (unsigned)mode_manager_view(), (unsigned)g_settings.flip, (unsigned)g_settings.night_start,
           (unsigned)g_settings.night_end, (unsigned)g_settings.night_bright, (unsigned)g_settings.layout, tiles,
           (unsigned)g_settings.psu_amps, (unsigned)g_settings.color_order, (unsigned)g_settings.radar_style,
           (unsigned)g_settings.radar_mark);
    jb_add(&j, "\"du\":%u,\"au\":%u,\"vu\":%u,\"tu\":%u,\"cf\":%u,\"theme\":\"%s\",\"cols\":\"%s\",\"pl\":%u,\"pr\":%u,"
           "\"ovh\":%u,\"rare\":%u,\"flap\":%u,\"csec\":%u,\"ms\":%u,\"c12\":%u,\"spd\":%u,\"tsh\":%u,\"tlb\":%s,"
           "\"font\":%u,\"tz\":%s,\"tza\":%u,\"tv\":%d,",
           (unsigned)g_settings.dist_unit, (unsigned)g_settings.alt_unit, (unsigned)g_settings.vs_unit,
           (unsigned)g_settings.temp_unit, (unsigned)g_settings.card_fields, themes, cols,
           (unsigned)g_settings.pane[0], (unsigned)g_settings.pane[1], (unsigned)g_settings.overhead_km,
           (unsigned)g_settings.rare, (unsigned)g_settings.flap, (unsigned)g_settings.card_s,
           (unsigned)g_settings.map_style, (unsigned)g_settings.clock12, (unsigned)g_settings.speed_unit,
           (unsigned)g_settings.track_show, label, (unsigned)g_settings.font, tz, (unsigned)g_settings.tz_auto,
           sky_time_valid() ? 1 : 0);
    jb_add(&j, "\"dm\":%u,\"hk\":%d,\"k4\":%s,\"kps\":%u,\"kst\":%d,\"osid\":%s,\"oss\":%d,\"osst\":%d,\"lsrc\":%u,\"srv\":%s,\"srvk\":%d,\"srvd\":%d,"
               "\"upd\":%s,\"updn\":%s,\"upds\":%d,\"updp\":%u,\"upde\":%s,",
           (unsigned)g_settings.data_mode, g_settings.api_key[0] ? 1 : 0, k4, (unsigned)g_settings.key_poll_s,
           (int)g_status.key_status, osid, g_settings.os_secret[0] ? 1 : 0, (int)g_status.os_status,
           (unsigned)g_settings.loc_src, srvj, g_settings.server_key[0] ? 1 : 0, (int)g_status.srv_down,
           updj, updn, (int)g_status.upd_state, (unsigned)g_status.upd_pct, upde);
    jb_add(&j, "\"ma\":%lu,\"limited\":%d,\"w\":%u,\"h\":%u,\"disp\":%d,\"safe\":%d,\"reset\":%s,\"fw\":\"%s\","
           "\"poll\":%u,\"lim\":%u,\"wait\":%d,\"ps\":%u,\"bud\":%d,\"src\":%u,\"next\":%d,\"heap\":%u,\"psram\":%u,\"ac\":[",
           (unsigned long)hub75_power_ma(), hub75_power_limited() ? 1 : 0, g_status.width, g_status.height,
           g_status.display_ok ? 1 : 0, g_status.safe_mode ? 1 : 0, reset, SKY_FW_VERSION,
           (unsigned)g_status.poll_ms, (unsigned)g_status.limited,
           g_status.limited_until_ms && (int32_t)(g_status.limited_until_ms - now) > 0
               ? (int)((g_status.limited_until_ms - now) / 1000) : 0,
           (unsigned)g_settings.poll_s, (int)g_status.budget, (unsigned)g_settings.data_src,
           (int32_t)(g_status.next_req_ms - now) > 0 ? (int)((g_status.next_req_ms - now) / 1000) : 0,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    /* Lėktuvai rašomi, kol lieka vietos sekamam skrydžiui ir orams (JSON visada užbaigtas) */
    for (int i = 0; i < snap->count && i < 15 && j.len + 1300 < j.cap; i++) {
        const aircraft_t *a = &snap->ac[i];
        char cs[24], ty[16], route[12] = "", rt[24];
        json_str(cs, sizeof cs, a->callsign[0] ? a->callsign : a->reg);
        json_str(ty, sizeof ty, a->type);
        if (a->has_route) snprintf(route, sizeof route, "%s-%s", a->orig, a->dest);
        json_str(rt, sizeof rt, route);
        jb_add(&j, "%s{\"c\":%s,\"t\":%s,\"r\":%s,\"d\":%.1f,\"a\":%ld,\"k\":%d,\"x\":%u}", i ? "," : "", cs, ty, rt,
               (double)a->dist_km, (long)a->alt_ft, a->tracked ? 1 : 0, (unsigned)a->special);
    }
    static char trk[700];                            /* HTTP serveris – viena užduotis, todėl static saugu */
    track_json(trk, sizeof trk);
    jb_add(&j, "],\"trk\":%s", trk);
    weather_t w;
    if (weather_get(&w))
        jb_add(&j, ",\"wx\":{\"t\":%.1f,\"c\":%u,\"day\":%d,\"txt\":\"%s\"}}", (double)w.temp_c, (unsigned)w.code,
               w.is_day ? 1 : 0, weather_text(w.code));
    else
        jb_add(&j, ",\"wx\":null}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, (ssize_t)j.len);
}

/* Ekrano peržiūra: pirma užklausa tik įjungia kadrų kopijavimą (503), vėlesnės gauna paskutinį kadrą */
static esp_err_t frame_get(httpd_req_t *req)
{
    static uint16_t *copy;
    const size_t n = (size_t)g_status.width * g_status.height * sizeof(uint16_t);
    g_status.preview_req_ms = sky_now_ms() | 1;
    if (!copy) copy = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!g_status.preview || !copy || !n) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, NULL, 0);
    }
    memcpy(copy, g_status.preview, n);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)copy, n);
}

static esp_err_t scan_get(httpd_req_t *req)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    uint16_t n = 20;
    static wifi_ap_record_t recs[20];
    if (esp_wifi_scan_start(&sc, true) != ESP_OK || esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) n = 0;

    static char json[2560];
    size_t o = 0;
    json[o++] = '[';
    int count = 0;
    for (int i = 0; i < n; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (!ssid[0]) continue;
        bool dup = false;
        for (int j = 0; j < i; j++)
            if (strcmp(ssid, (const char *)recs[j].ssid) == 0) dup = true;
        if (dup) continue;
        char esc[80];
        json_str(esc, sizeof esc, ssid);
        int w = snprintf(json + o, sizeof json - o, "%s{\"s\":%s,\"r\":%d,\"l\":%d}", count ? "," : "", esc,
                         recs[i].rssi, recs[i].authmode != WIFI_AUTH_OPEN);
        if (w < 0 || (size_t)w >= sizeof json - o - 2) break;
        o += w;
        count++;
    }
    json[o++] = ']';
    json[o] = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, o);
}

/* Sekamas skrydis (q – jau išvalytas; "" – nebesekti). Pradėjus sekti, iškart įjungiamas TRACKING vaizdas (atgal
 * naudotojas persijungia pats); nustojus – grąžinamas ankstesnis vaizdas (išsaugotas NVS, todėl ir po perkrovimo).
 * Išsaugo kviečiančioji pusė. */
static void track_apply(const char *q)
{
    strlcpy(g_settings.track, q, sizeof g_settings.track);
    if (!q[0]) g_settings.track_label[0] = 0;       /* užrašas skirtas šiam skrydžiui, ne kitam */
    track_set_query(q);
    if (q[0] && g_settings.view != VIEW_TRACK) {
        g_settings.view_prev = g_settings.view;
        g_settings.view = VIEW_TRACK;
    } else if (!q[0] && g_settings.view == VIEW_TRACK) {
        g_settings.view = g_settings.view_prev != VIEW_TRACK ? g_settings.view_prev : VIEW_AUTO;
    }
    mode_manager_set_view((view_t)g_settings.view);
}

void net_track_stop(void)
{
    if (!g_settings.track[0]) return;
    track_apply("");
    settings_save();
}

/* Sekamo skrydžio užrašas: raidės, skaičiai, tarpai ir paprasti skyrybos ženklai (tokie yra ekrano šriftuose), iki 20
 * simbolių; kraštiniai tarpai nukerpami. Puslapis lietuviškas ir kitas raides paverčia lotyniškomis. */
static bool label_clean(const char *in, char *out, size_t n)
{
    while (*in == ' ') in++;
    size_t o = 0;
    for (; *in; in++) {
        const unsigned char c = (unsigned char)*in;
        if (!isalnum(c) && !strchr(" .,!?'-&+:/()#*@", c)) return false;
        if (o + 1 >= n) return false;
        out[o++] = (char)c;
    }
    while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
    return true;
}

static esp_err_t settings_post(httpd_req_t *req)
{
    if (!post_allowed(req)) return forbidden(req);
    char body[1024];
    if (recv_body(req, body, sizeof body) != ESP_OK) return ESP_FAIL;
    settings_welcome_done();                     /* naudotojas jau keičia nustatymus – žemėlapio su adresu nebereikia */

    /* Vieta: abi koordinatės kartu */
    float lat, lon, range = g_settings.range_km;
    bool has_lat = form_float(body, "lat", &lat), has_lon = form_float(body, "lon", &lon);
    char tmp[64];
    if (has_lat || has_lon || form_str(body, "lat", tmp, sizeof tmp)) {
        if (!has_lat || !has_lon || lat < -90 || lat > 90 || lon < -180 || lon > 180)
            return send_err(req, "Invalid coordinates. Example: 51.4700, -0.4543");
        form_float(body, "range", &range);
        range = roundf(range);                      /* įvedamas ranka; didesnis spindulys – per daug lėktuvų ir duomenų */
        if (range < 5 || range > 100) return send_err(req, "Range must be 5-100 km.");
        const bool moved = fabsf(lat - g_settings.home_lat) > 0.01f || fabsf(lon - g_settings.home_lon) > 0.01f;
        settings_save_location(lat, lon, range);
        aircraft_table_set_home(lat, lon, range);
        if (moved) net_refresh_weather();
    }

    /* Sekamas skrydis: raidės, skaičiai, brūkšnelis; tuščia – nebesekti */
    char trk[24];
    if (form_str(body, "track", trk, sizeof trk)) {
        char clean[sizeof g_settings.track];
        size_t n = 0;
        for (const char *c = trk; *c; c++) {
            if (*c == ' ') continue;
            if (!isalnum((unsigned char)*c) && *c != '-') return send_err(req, "Use only letters, digits and '-'.");
            if (n + 1 >= sizeof clean) return send_err(req, "Too long - at most 11 characters.");
            clean[n++] = (char)toupper((unsigned char)*c);
        }
        clean[n] = 0;
        if (strcmp(clean, g_settings.track) != 0) track_apply(clean);
    }
    char lbl[64];
    if (form_str(body, "tlb", lbl, sizeof lbl)) {
        char clean[SETTINGS_LABEL_LEN];
        if (!label_clean(lbl, clean, sizeof clean))
            return send_err(req, "Use letters, digits, spaces and simple punctuation, at most 20 characters.");
        strlcpy(g_settings.track_label, clean, sizeof g_settings.track_label);
    }

    int v;
    if (form_int(body, "bright", SETTINGS_BRIGHT_MIN, 255, &v)) g_settings.brightness = (uint8_t)v;
    if (form_int(body, "view", 0, VIEW_COUNT - 1, &v)) {
        g_settings.view = (uint8_t)v;
        mode_manager_set_view((view_t)v);
    }
    if (form_int(body, "flip", 0, 1, &v)) g_settings.flip = (uint8_t)v;
    if (form_int(body, "ns", 0, 1439, &v)) g_settings.night_start = (uint16_t)v;     /* minutės nuo vidurnakčio */
    if (form_int(body, "ne", 0, 1439, &v)) g_settings.night_end = (uint16_t)v;
    if (form_int(body, "nb", 0, 255, &v))                                          /* 0 – naktį nešviečia */
        g_settings.night_bright = (uint8_t)(v == 0 || v >= SETTINGS_BRIGHT_MIN ? v : SETTINGS_BRIGHT_MIN);
    if (form_int(body, "tsh", 1, TRACK_SHOW_CARD | TRACK_SHOW_MAP | TRACK_SHOW_RADAR | TRACK_SHOW_PULSE, &v))
        g_settings.track_show = (uint8_t)(v & (TRACK_SHOW_CARD | TRACK_SHOW_MAP) ? v : v | TRACK_SHOW_CARD);
    if (form_int(body, "psu", 0, 40, &v)) g_settings.psu_amps = (uint8_t)v;
    if (form_int(body, "co", 0, 5, &v)) g_settings.color_order = (uint8_t)v;
    if (form_int(body, "rs", 0, RADAR_STYLE_COUNT - 1, &v)) g_settings.radar_style = (uint8_t)v;
    if (form_int(body, "rm", 0, MARK_COUNT - 1, &v)) g_settings.radar_mark = (uint8_t)v;
    if (form_int(body, "font", 0, GFX_FONT_CLASSIC, &v)) g_settings.font = (uint8_t)v;   /* MONO nesiūlomas */
    if (form_int(body, "poll", 0, 15, &v) && (v == 0 || v >= 5)) g_settings.poll_s = (uint8_t)v;
    if (form_int(body, "src", 0, 1, &v)) g_settings.data_src = (uint8_t)v;
    if (form_int(body, "du", 0, DIST_UNIT_COUNT - 1, &v)) g_settings.dist_unit = (uint8_t)v;
    if (form_int(body, "au", 0, ALT_UNIT_COUNT - 1, &v)) g_settings.alt_unit = (uint8_t)v;
    if (form_int(body, "vu", 0, VS_UNIT_COUNT - 1, &v)) g_settings.vs_unit = (uint8_t)v;
    if (form_int(body, "tu", 0, TEMP_UNIT_COUNT - 1, &v)) g_settings.temp_unit = (uint8_t)v;
    if (form_int(body, "cf", 0, CF_ALL, &v)) g_settings.card_fields = (uint16_t)v;
    /* Padalinto ekrano pusėse – skirtingi langai: pasirinkus kitos pusės langą, pusės susikeičia */
    const uint8_t pane_was[2] = { g_settings.pane[0], g_settings.pane[1] };
    if (form_int(body, "pl", 0, PANE_COUNT - 1, &v)) g_settings.pane[0] = (uint8_t)v;
    if (form_int(body, "pr", 0, PANE_COUNT - 1, &v)) g_settings.pane[1] = (uint8_t)v;
    if (g_settings.pane[0] == g_settings.pane[1]) {
        if (g_settings.pane[0] != pane_was[0]) g_settings.pane[1] = pane_was[0];
        else if (g_settings.pane[1] != pane_was[1]) g_settings.pane[0] = pane_was[1];
        if (g_settings.pane[0] == g_settings.pane[1]) g_settings.pane[1] = g_settings.pane[0] == PANE_CARD ? PANE_RADAR : PANE_CARD;
    }
    if (form_int(body, "ovh", 0, 5, &v)) g_settings.overhead_km = (uint8_t)v;
    if (form_int(body, "rare", 0, 1, &v)) g_settings.rare = (uint8_t)v;
    if (form_int(body, "flap", 0, 1, &v)) g_settings.flap = (uint8_t)v;
    if (form_int(body, "csec", 3, 30, &v)) g_settings.card_s = (uint8_t)v;
    if (form_int(body, "ms", 0, MAP_STYLE_COUNT - 1, &v)) g_settings.map_style = (uint8_t)v;
    /* Spalvos – lango w (win_t); jei w nenurodytas – visų langų */
    int w0 = 0, w1 = WIN_COUNT - 1;
    if (form_int(body, "w", 0, WIN_COUNT - 1, &v)) w0 = w1 = v;
    if (form_int(body, "theme", 0, THEME_COUNT - 1, &v))
        for (int w = w0; w <= w1; w++) {
            g_settings.theme[w] = (uint8_t)v;
            theme_colors(v, w, g_settings.colors[w]);   /* naudotojo spalvos pradedamos nuo šios temos */
        }
    char cols[COL_COUNT * 9 + 8];
    if (form_str(body, "cols", cols, sizeof cols) && cols[0]) {
        uint16_t c[COL_COUNT];
        if (!colors_parse(cols, c)) return send_err(req, "Invalid colours.");
        for (int w = w0; w <= w1; w++) {
            memcpy(g_settings.colors[w], c, sizeof c);
            g_settings.theme[w] = THEME_CUSTOM;
        }
    }
    if (form_int(body, "c12", 0, 1, &v)) g_settings.clock12 = (uint8_t)v;
    if (form_int(body, "spd", 0, SPEED_UNIT_COUNT - 1, &v)) g_settings.speed_unit = (uint8_t)v;

    /* Lėktuvų duomenys: įtaisytas šaltinis arba naudotojo API raktas (raktas niekada negrąžinamas atgal) */
    if (form_int(body, "dm", DATA_BUILTIN, DATA_OWN_KEY, &v)) g_settings.data_mode = (uint8_t)v;
    char key[SETTINGS_KEY_LEN + 8];
    if (form_str(body, "key", key, sizeof key) && key[0]) {
        if (strlen(key) >= SETTINGS_KEY_LEN) return send_err(req, "The API key is too long.");
        for (const char *c = key; *c; c++)
            if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_')
                return send_err(req, "The API key may contain only letters, digits, '-' and '_'.");
        strlcpy(g_settings.api_key, key, sizeof g_settings.api_key);
        g_status.key_status = 0;
    }
    if (g_settings.data_mode == DATA_OWN_KEY && !g_settings.api_key[0]) {
        g_settings.data_mode = DATA_BUILTIN;
        return send_err(req, "Enter your API key.");
    }
    if (form_int(body, "kps", 5, 300, &v) && settings_key_poll_ok((unsigned)v)) g_settings.key_poll_s = (uint16_t)v;

    /* AeroBox serveris: adresas (tuščias – duomenys tiesiai iš šaltinių); pakeitus adresą, raktas gaunamas iš naujo */
    char srv[SETTINGS_SRV_LEN + 8];
    if (form_str(body, "srv", srv, sizeof srv)) {
        size_t n = strlen(srv);
        while (n && srv[n - 1] == '/') srv[--n] = 0;
        if (n >= SETTINGS_SRV_LEN) return send_err(req, "The server address is too long.");
        if (n && strncmp(srv, "http://", 7) != 0 && strncmp(srv, "https://", 8) != 0)
            return send_err(req, "The server address must start with http:// or https://");
        for (const char *c = srv; *c; c++)
            if (!isgraph((unsigned char)*c) || *c == '"' || *c == '\\') return send_err(req, "Invalid server address.");
        if (strcmp(srv, g_settings.server_url) != 0) {
            strlcpy(g_settings.server_url, srv, sizeof g_settings.server_url);
            g_settings.server_key[0] = 0;
        }
    }

    /* Papildomi lėktuvai: OpenSky API klientas (ID ir slaptasis raktas; tuščias ID – išjungta) */
    char osv[SETTINGS_OS_LEN + 8];
    if (form_str(body, "osid", osv, sizeof osv)) {
        if (strlen(osv) >= SETTINGS_OS_LEN) return send_err(req, "The client ID is too long.");
        for (const char *c = osv; *c; c++)
            if (!isgraph((unsigned char)*c) || *c == '"' || *c == '\\') return send_err(req, "Invalid client ID.");
        strlcpy(g_settings.os_id, osv, sizeof g_settings.os_id);
        if (!osv[0]) g_settings.os_secret[0] = 0;
        g_status.os_status = 0;
    }
    if (form_str(body, "ossec", osv, sizeof osv) && osv[0]) {
        if (strlen(osv) >= SETTINGS_OS_LEN) return send_err(req, "The client secret is too long.");
        for (const char *c = osv; *c; c++)
            if (!isgraph((unsigned char)*c) || *c == '"' || *c == '\\') return send_err(req, "Invalid client secret.");
        strlcpy(g_settings.os_secret, osv, sizeof g_settings.os_secret);
        g_status.os_status = 0;
    }
    if (g_settings.os_id[0] && !g_settings.os_secret[0]) return send_err(req, "Enter the client secret.");

    /* Laiko juosta: automatiškai (puslapis atsiunčia telefono ar kompiuterio laiko juostą) arba pasirinkta ranka */
    if (form_int(body, "tza", 0, 1, &v)) g_settings.tz_auto = (uint8_t)v;
    char tz[sizeof g_settings.tz];
    if (form_str(body, "tz", tz, sizeof tz) && tz[0]) {
        for (char *c = tz; *c; c++)
            if (!isprint((unsigned char)*c) || *c == '"' || *c == '\\') return send_err(req, "Invalid time zone.");
        strlcpy(g_settings.tz, tz, sizeof g_settings.tz);
        setenv("TZ", g_settings.tz, 1);
        tzset();
    }
    /* Laikas iš naršyklės, kol SNTP dar nenustatė tikro laiko (pvz., nustatymų tinkle be interneto) */
    char now_s[24];
    if (form_str(body, "now", now_s, sizeof now_s) && !sky_time_valid()) {
        const long long t = strtoll(now_s, NULL, 10);
        if (t > 1700000000LL && t < 4102444800LL) {                    /* 2023 … 2100 */
            const struct timeval tv = { .tv_sec = (time_t)t };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "laikas nustatytas is narsykles");
        }
    }

    /* Panelių išdėstymas įsigalioja po perkrovimo */
    bool restart = false;
    if (form_int(body, "layout", 0, LAYOUT_COUNT - 1, &v)) {
        char tiles[SETTINGS_TILES_LEN] = "";
        if (v == LAYOUT_CUSTOM) {
            hub75_tile_t t[4];
            if (!form_str(body, "tiles", tiles, sizeof tiles) || !hub75_layout_parse(tiles, t))
                return send_err(req, "Invalid custom layout. Example: 1,0,1;1,1,0;0,1,0;0,0,1");
        } else {
            strlcpy(tiles, g_settings.tiles, sizeof tiles);
        }
        if (v != g_settings.layout || strcmp(tiles, g_settings.tiles) != 0) {
            g_settings.layout = (uint8_t)v;
            strlcpy(g_settings.tiles, tiles, sizeof g_settings.tiles);
            restart = true;
        }
    }
    settings_save();
    if (restart) {
        ESP_LOGW(TAG, "naujas isdestymas %s - perkraunama", hub75_layout_name(g_settings.layout));
        restart_later();
        return send_json(req, "{\"ok\":1,\"restart\":1}");
    }
    return send_json(req, "{\"ok\":1}");
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    if (!post_allowed(req)) return forbidden(req);
    char body[320], ssid[33] = "", pass[65] = "";
    if (recv_body(req, body, sizeof body) != ESP_OK) return ESP_FAIL;
    form_str(body, "ssid", ssid, sizeof ssid);
    form_str(body, "pass", pass, sizeof pass);
    if (!ssid[0]) return send_err(req, "Choose a Wi-Fi network.");
    if (pass[0] && strlen(pass) < 8) return send_err(req, "Password too short (at least 8 characters).");
    if (!net_portal_try_wifi(ssid, pass)) return send_err(req, "Already connecting - please wait.");
    return send_json(req, "{\"ok\":1}");
}

static esp_err_t wifi_reset_post(httpd_req_t *req)
{
    if (!post_allowed(req)) return forbidden(req);
    settings_clear_wifi();
    send_json(req, "{\"ok\":1}");
    restart_later();
    return ESP_OK;
}

static esp_err_t restart_post(httpd_req_t *req)
{
    if (!post_allowed(req)) return forbidden(req);
    send_json(req, "{\"ok\":1}");
    restart_later();
    return ESP_OK;
}

static esp_err_t factory_post(httpd_req_t *req)
{
    if (!post_allowed(req)) return forbidden(req);
    settings_factory_reset();
    send_json(req, "{\"ok\":1}");
    restart_later();
    return ESP_OK;
}

static esp_err_t test_post(httpd_req_t *req)
{
    if (!post_allowed(req)) return forbidden(req);
    char body[64], kind[12] = "";
    if (recv_body(req, body, sizeof body) != ESP_OK) return ESP_FAIL;
    form_str(body, "kind", kind, sizeof kind);
    int secs = 10;
    form_int(body, "s", 1, 60, &secs);
    static const struct { const char *name; uint8_t kind; } K[] = {
        { "overhead", TEST_OVERHEAD }, { "rare", TEST_RARE }, { "map", TEST_MAP },
        { "flap", TEST_FLAP }, { "track", TEST_TRACK }, { "multi", TEST_MULTI },
    };
    uint8_t k = TEST_NONE;
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++)
        if (strcmp(kind, K[i].name) == 0) k = K[i].kind;
    if (k == TEST_NONE) return send_err(req, "Unknown test.");
    g_status.test_start_ms = sky_now_ms();
    g_status.test_kind = k;
    g_status.test_until_ms = g_status.test_start_ms + (uint32_t)secs * 1000;
    return send_json(req, "{\"ok\":1}");
}

/* Programos atnaujinimas: firmware.bin (ne merged!) rašomas į kitą OTA skirsnį */
static esp_err_t ota_post(httpd_req_t *req)
{
    if (!post_allowed(req)) return forbidden(req);
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) return send_err(req, "No update partition found.");
    if (req->content_len <= 0 || (size_t)req->content_len > part->size)
        return send_err(req, "Wrong file size.");

    char *buf = malloc(4096);
    if (!buf) return send_err(req, "Out of memory.");
    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    const char *fail = err == ESP_OK ? NULL : "Could not start the update.";
    int left = (int)req->content_len;
    bool first = true;
    ESP_LOGI(TAG, "atnaujinimas: %d B -> %s", left, part->label);
    /* Tikrinama, ar tai programos failas (ne bootloader ir ne merged): antraštė E9 ir po jos esp_app_desc_t.
     * Tikrinama iš pirmo gabalo, todėl jis kaupiamas, kol duomenų užtenka */
    const size_t desc_off = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    const int head = (int)(desc_off + sizeof(esp_app_desc_t));
    int stalls = 0;                                  /* kiek kartų iš eilės negauta duomenų (po recv_wait_timeout) */
    while (!fail && left > 0) {
        const int want = left < 4096 ? left : 4096;
        int r = 0;
        do {
            const int got = httpd_req_recv(req, buf + r, want - r);
            if (got == HTTPD_SOCK_ERR_TIMEOUT && ++stalls <= 3) continue;
            if (got <= 0) {
                fail = "Connection lost.";
                break;
            }
            stalls = 0;
            r += got;
        } while (first && r < head && r < want);
        if (fail) break;
        if (r == 0) continue;                        /* laukimo laikas baigėsi – skaityti toliau */
        if (first) {
            first = false;
            const esp_app_desc_t *d = (const esp_app_desc_t *)(buf + desc_off);
            if ((uint8_t)buf[0] != ESP_IMAGE_HEADER_MAGIC || r < head || d->magic_word != ESP_APP_DESC_MAGIC_WORD) {
                fail = "Not a AeroBox firmware file. Use firmware.bin (not firmware_merged.bin).";
                break;
            }
            /* Kito projekto programa (ne AeroBox) nepriimama */
            if (strncmp(d->project_name, esp_app_get_description()->project_name, sizeof d->project_name) != 0) {
                fail = "This file is not AeroBox firmware.";
                break;
            }
        }
        if (esp_ota_write(ota, buf, r) != ESP_OK) {
            fail = "Write failed.";
            break;
        }
        left -= r;
    }
    free(buf);
    if (fail) {
        if (ota) esp_ota_abort(ota);
        ESP_LOGE(TAG, "atnaujinimas nepavyko: %s", fail);
        return send_err(req, fail);
    }
    /* Patikrinamas visas atvaizdas (kontrolinės sumos) */
    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "atnaujinimas atmestas: %s", esp_err_to_name(err));
        return send_err(req, "The file is damaged. Download firmware.bin again.");
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) return send_err(req, "Could not activate the new firmware.");
    ESP_LOGW(TAG, "atnaujinta - perkraunama");
    send_json(req, "{\"ok\":1}");
    restart_later();
    return ESP_OK;
}

/* Programos atnaujinimas iš AeroBox serverio (siūloma versija – g_status.upd_ver) */
static esp_err_t update_post(httpd_req_t *req)
{
    if (!post_allowed(req)) return forbidden(req);
    if (!net_update_start()) return send_err(req, g_status.upd_state == 1 ? "Already updating." : "No update available.");
    return send_json(req, "{\"ok\":1}");
}

/* Visi kiti adresai: nustatymų tinkle – nukreipimas į puslapį (telefonų „interneto patikrai“), namų tinkle – 404 */
static esp_err_t other_get(httpd_req_t *req)
{
    if (!g_status.portal) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", PORTAL_URL);
    return httpd_resp_send(req, NULL, 0);
}

void net_webui_start(void)
{
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 16;
    cfg.recv_wait_timeout = 20;          /* programos atnaujinimui per lėtą ryšį */
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "valdymo puslapio paleisti nepavyko");
        s_httpd = NULL;
        return;
    }
    /* Tvarka svarbi: laimi pirmas tinkamas adresas, todėl bendrasis (wildcard) – paskutinis */
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page_get },
        { .uri = "/index.html", .method = HTTP_GET, .handler = page_get },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/api/scan", .method = HTTP_GET, .handler = scan_get },
        { .uri = "/api/frame", .method = HTTP_GET, .handler = frame_get },
        { .uri = "/api/settings", .method = HTTP_POST, .handler = settings_post },
        { .uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post },
        { .uri = "/api/wifi-reset", .method = HTTP_POST, .handler = wifi_reset_post },
        { .uri = "/api/restart", .method = HTTP_POST, .handler = restart_post },
        { .uri = "/api/factory-reset", .method = HTTP_POST, .handler = factory_post },
        { .uri = "/api/test", .method = HTTP_POST, .handler = test_post },
        { .uri = "/api/ota", .method = HTTP_POST, .handler = ota_post },
        { .uri = "/api/update", .method = HTTP_POST, .handler = update_post },
        { .uri = "/*", .method = HTTP_GET, .handler = other_get },
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) httpd_register_uri_handler(s_httpd, &uris[i]);

    if (mdns_init() == ESP_OK) {
        mdns_hostname_set("aerobox");
        mdns_instance_name_set("AeroBox");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    ESP_LOGI(TAG, "valdymo puslapis: http://aerobox.local arba http://192.168.4.1 (AeroBox tinkle)");
}
