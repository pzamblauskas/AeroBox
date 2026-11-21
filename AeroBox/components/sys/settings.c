/* Nustatymų nuskaitymas, tikrinimas ir išsaugojimas NVS (sritis „aerobox“).
 * Iki 1.2.4 versijos nustatymai buvo srityje „skypanel“; pirmą kartą paleidus naują programą jie perkeliami
 * (migrate_old_namespace). NVS raktai trumpi, nes NVS raktas gali būti ne ilgesnis nei 15 simbolių. */
#include "settings.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "settings";
#define NVS_NS     "aerobox"
#define NVS_NS_OLD "skypanel"     /* iki 1.2.4 */

settings_t g_settings;

/* Senos versijos nustatymai (sritis NVS_NS_OLD) nukopijuojami į NVS_NS, o senoji sritis ištrinama.
 * Jei perkėlimas nutrūktų (dingtų maitinimas), kitą kartą jis pakartojamas iš naujo – kopijavimas
 * perrašo tas pačias reikšmes. Naujame įrenginyje senos srities nėra, todėl nieko nedaroma. */
static void migrate_old_namespace(void)
{
    nvs_iterator_t it = NULL;
    esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, NVS_NS_OLD, NVS_TYPE_ANY, &it);
    if (e != ESP_OK) return;                       /* senoje srityje įrašų nėra – perkelti nieko */
    nvs_handle_t src, dst;
    if (nvs_open(NVS_NS_OLD, NVS_READWRITE, &src) != ESP_OK) {
        nvs_release_iterator(it);
        return;
    }
    if (nvs_open(NVS_NS, NVS_READWRITE, &dst) != ESP_OK) {
        nvs_release_iterator(it);
        nvs_close(src);
        return;
    }
    int copied = 0;
    while (e == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        switch (info.type) {
        case NVS_TYPE_U8: {
            uint8_t v;
            if (nvs_get_u8(src, info.key, &v) == ESP_OK && nvs_set_u8(dst, info.key, v) == ESP_OK) copied++;
            break;
        }
        case NVS_TYPE_U16: {
            uint16_t v;
            if (nvs_get_u16(src, info.key, &v) == ESP_OK && nvs_set_u16(dst, info.key, v) == ESP_OK) copied++;
            break;
        }
        case NVS_TYPE_U32: {
            uint32_t v;
            if (nvs_get_u32(src, info.key, &v) == ESP_OK && nvs_set_u32(dst, info.key, v) == ESP_OK) copied++;
            break;
        }
        case NVS_TYPE_STR:
        case NVS_TYPE_BLOB: {
            const bool str = info.type == NVS_TYPE_STR;
            size_t n = 0;
            if ((str ? nvs_get_str(src, info.key, NULL, &n) : nvs_get_blob(src, info.key, NULL, &n)) != ESP_OK) break;
            void *buf = malloc(n ? n : 1);
            if (!buf) break;
            if (str ? nvs_get_str(src, info.key, buf, &n) == ESP_OK && nvs_set_str(dst, info.key, buf) == ESP_OK
                    : nvs_get_blob(src, info.key, buf, &n) == ESP_OK && nvs_set_blob(dst, info.key, buf, n) == ESP_OK)
                copied++;
            free(buf);
            break;
        }
        default:                                   /* kitokių tipų programa nenaudoja */
            break;
        }
        e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    if (nvs_commit(dst) == ESP_OK) {               /* senoji sritis trinama tik tada, kai naujoji įrašyta */
        nvs_erase_all(src);
        nvs_commit(src);
        ESP_LOGW(TAG, "nustatymai perkelti is NVS srities '%s' i '%s' (%d irasu)", NVS_NS_OLD, NVS_NS, copied);
    }
    nvs_close(dst);
    nvs_close(src);
}

static void get_float(nvs_handle_t h, const char *key, float *out)
{
    size_t fl = sizeof(float);
    nvs_get_blob(h, key, out, &fl);
}

/* Leidžiami naudotojo rakto užklausų periodai (s) – tokie patys kaip valdymo puslapio sąraše */
bool settings_key_poll_ok(unsigned s)
{
    static const uint16_t OK[] = { 5, 10, 15, 30, 60, 120, 300 };
    for (size_t i = 0; i < sizeof OK / sizeof OK[0]; i++)
        if (OK[i] == s) return true;
    return false;
}

/* Eilutėje leidžiami tik spausdinami ASCII simboliai (be kabučių ir \), nes ji įterpiama į JSON.
 * Radus kitokį simbolį, eilutė pakeičiama fallback reikšme. */
static void keep_printable(char *s, const char *fallback, size_t n)
{
    for (const char *c = s; *c; c++)
        if (!isprint((unsigned char)*c) || *c == '"' || *c == '\\') {
            strlcpy(s, fallback, n);
            return;
        }
}

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    migrate_old_namespace();

    g_settings.home_lat     = strtof(CONFIG_SKY_HOME_LAT, NULL);
    g_settings.home_lon     = strtof(CONFIG_SKY_HOME_LON, NULL);
    g_settings.range_km     = CONFIG_SKY_RANGE_KM;
    g_settings.brightness   = CONFIG_SKY_BRIGHTNESS;
    g_settings.view         = 0;
    g_settings.view_prev    = 0;
    g_settings.night_start  = CONFIG_SKY_NIGHT_START_H * 60;
    g_settings.night_end    = CONFIG_SKY_NIGHT_END_H * 60;
    g_settings.night_bright = CONFIG_SKY_NIGHT_BRIGHTNESS;
    g_settings.layout       = CONFIG_SKY_PANEL_LAYOUT;
    g_settings.flip         = 0;
    g_settings.psu_amps     = CONFIG_SKY_PSU_AMPS;
    g_settings.color_order  = 1;            /* AeroBox panelės: R-B-G (su R-G-B geltona atrodo rožinė) */
    g_settings.radar_style  = 1;            /* sonaras */
    g_settings.radar_mark   = 0;            /* rodyklės */
    g_settings.font         = 2;            /* glotnusis (64×32 ekrane vietoj jo naudojamas 4×5) */
    g_settings.poll_s       = 0;            /* pagal šaltinio taisykles */
    g_settings.data_src     = 1;            /* adsb.fi, nes adsb.lol riboja užklausas */
    g_settings.data_mode    = DATA_BUILTIN;
    g_settings.key_poll_s   = 60;
    g_settings.api_key[0]   = 0;
    g_settings.os_id[0]     = 0;
    strlcpy(g_settings.server_url, SKY_SERVER_URL, sizeof g_settings.server_url);
    g_settings.server_key[0] = 0;
    g_settings.os_secret[0] = 0;
    g_settings.clock12      = 0;
    g_settings.speed_unit   = 0;            /* mazgai */
    g_settings.card_fields  = 0x3FF;        /* CF_ALL – visi laukai */
    g_settings.pane[0]      = 0;            /* radaras */
    g_settings.pane[1]      = 1;            /* skrydžio kortelė */
    g_settings.overhead_km  = 2;
    g_settings.rare         = 1;
    g_settings.flap         = 1;
    g_settings.card_s       = 6;            /* iki v0.12.3 buvo 4 s – per mažai, kad spėtum perskaityti */
    g_settings.map_style    = 0;            /* sausuma ir vanduo užpildyti (spalvos – iš colors arba temos) */
    g_settings.track_show   = TRACK_SHOW_CARD | TRACK_SHOW_MAP | TRACK_SHOW_RADAR;
    g_settings.track[0]     = 0;
    g_settings.track_label[0] = 0;
    g_settings.tiles[0]     = 0;
    strlcpy(g_settings.tz, CONFIG_SKY_TZ, sizeof g_settings.tz);
    g_settings.tz_auto      = 1;
    strlcpy(g_settings.wifi_ssid, CONFIG_SKY_WIFI_SSID, sizeof g_settings.wifi_ssid);
    strlcpy(g_settings.wifi_pass, CONFIG_SKY_WIFI_PASS, sizeof g_settings.wifi_pass);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "bright", &g_settings.brightness);
        nvs_get_u8(h, "view", &g_settings.view);
        nvs_get_u8(h, "vprev", &g_settings.view_prev);
        /* Iki v1.0 nakties pradžia ir pabaiga buvo saugomos valandomis */
        uint8_t hour;
        if (nvs_get_u16(h, "n_startm", &g_settings.night_start) != ESP_OK && nvs_get_u8(h, "n_start", &hour) == ESP_OK)
            g_settings.night_start = (uint16_t)(hour * 60);
        if (nvs_get_u16(h, "n_endm", &g_settings.night_end) != ESP_OK && nvs_get_u8(h, "n_end", &hour) == ESP_OK)
            g_settings.night_end = (uint16_t)(hour * 60);
        nvs_get_u8(h, "n_bright", &g_settings.night_bright);
        nvs_get_u8(h, "layout", &g_settings.layout);
        nvs_get_u8(h, "flip", &g_settings.flip);
        nvs_get_u8(h, "psu", &g_settings.psu_amps);
        nvs_get_u8(h, "corder", &g_settings.color_order);
        nvs_get_u8(h, "rstyle", &g_settings.radar_style);
        nvs_get_u8(h, "rmark", &g_settings.radar_mark);
        nvs_get_u8(h, "font", &g_settings.font);
        nvs_get_u8(h, "poll", &g_settings.poll_s);
        nvs_get_u8(h, "src", &g_settings.data_src);
        nvs_get_u8(h, "dmode", &g_settings.data_mode);
        nvs_get_u16(h, "kpoll", &g_settings.key_poll_s);
        size_t kl = sizeof g_settings.api_key;
        if (nvs_get_str(h, "apikey", g_settings.api_key, &kl) != ESP_OK) g_settings.api_key[0] = 0;
        kl = sizeof g_settings.server_url;
        nvs_get_str(h, "srvurl", g_settings.server_url, &kl);      /* nėra – lieka numatytasis */
        kl = sizeof g_settings.server_key;
        if (nvs_get_str(h, "srvkey", g_settings.server_key, &kl) != ESP_OK) g_settings.server_key[0] = 0;
        kl = sizeof g_settings.os_id;
        if (nvs_get_str(h, "osid", g_settings.os_id, &kl) != ESP_OK) g_settings.os_id[0] = 0;
        kl = sizeof g_settings.os_secret;
        if (nvs_get_str(h, "ossec", g_settings.os_secret, &kl) != ESP_OK) g_settings.os_secret[0] = 0;
        uint8_t imperial = 0;              /* iki v0.10 – vienas „imperial“ nustatymas atstumui ir temperatūrai */
        nvs_get_u8(h, "imper", &imperial);
        g_settings.dist_unit = g_settings.temp_unit = imperial ? 1 : 0;
        nvs_get_u8(h, "du", &g_settings.dist_unit);
        nvs_get_u8(h, "au", &g_settings.alt_unit);
        nvs_get_u8(h, "vu", &g_settings.vs_unit);
        nvs_get_u8(h, "tu", &g_settings.temp_unit);
        nvs_get_u16(h, "cardf", &g_settings.card_fields);
        /* Iki v0.13.0 buvo viena tema ir spalvos visiems langams – jos tampa kiekvieno lango pradinėmis */
        uint8_t theme = 0;
        uint16_t colors[SETTINGS_COLORS] = { 0 };
        nvs_get_u8(h, "theme", &theme);
        size_t cl = sizeof colors;
        if (nvs_get_blob(h, "cols", colors, &cl) == ESP_OK && cl < sizeof colors) {
            /* Senesnėse versijose naudotojo spalvų buvo mažiau (iki v0.12.6 – 11, v0.12.6 – 13). Trūkstamos
             * užpildomos kaip temose (theme.c): sausuma tamsiai ruda, vanduo – 22 % radaro spalvos,
             * sienos – blankiai pilkos */
            const uint16_t r = colors[10];
            if (cl < 12 * 2) colors[11] = 0x1881;           /* RGB565(26, 18, 8) */
            if (cl < 13 * 2)
                colors[12] = (uint16_t)(((r >> 11) * 22 / 255) << 11 | (((r >> 5) & 63) * 22 / 255) << 5 |
                                        (r & 31) * 22 / 255);
            colors[13] = 0x3187;                            /* RGB565(50, 50, 58) */
        }
        for (int w = 0; w < SETTINGS_WINS; w++) {
            g_settings.theme[w] = theme;
            memcpy(g_settings.colors[w], colors, sizeof colors);
        }
        size_t wl = sizeof g_settings.theme;
        nvs_get_blob(h, "wthm", g_settings.theme, &wl);
        wl = sizeof g_settings.colors;
        nvs_get_blob(h, "wcols", g_settings.colors, &wl);
        nvs_get_u8(h, "paneL", &g_settings.pane[0]);
        nvs_get_u8(h, "paneR", &g_settings.pane[1]);
        nvs_get_u8(h, "ovh", &g_settings.overhead_km);
        nvs_get_u8(h, "rare", &g_settings.rare);
        nvs_get_u8(h, "flap", &g_settings.flap);
        nvs_get_u8(h, "cardsec", &g_settings.card_s);
        nvs_get_u8(h, "mapst", &g_settings.map_style);
        nvs_get_u8(h, "clk12", &g_settings.clock12);
        nvs_get_u8(h, "spd", &g_settings.speed_unit);
        /* Iki v1.0 buvo tik nustatymas „rodyti radarą“ (kortelė ir žemėlapis rodyti visada) */
        if (nvs_get_u8(h, "trkshow", &g_settings.track_show) != ESP_OK) {
            uint8_t radar = 1;
            nvs_get_u8(h, "trkrad", &radar);
            g_settings.track_show = TRACK_SHOW_CARD | TRACK_SHOW_MAP | (radar ? TRACK_SHOW_RADAR : 0);
        }
        size_t n = sizeof g_settings.track;
        if (nvs_get_str(h, "track", g_settings.track, &n) != ESP_OK) g_settings.track[0] = 0;
        n = sizeof g_settings.track_label;
        if (nvs_get_str(h, "trklbl", g_settings.track_label, &n) != ESP_OK) g_settings.track_label[0] = 0;
        n = sizeof g_settings.tiles;
        if (nvs_get_str(h, "tiles", g_settings.tiles, &n) != ESP_OK) g_settings.tiles[0] = 0;
        n = sizeof g_settings.tz;
        const bool tz_saved = nvs_get_str(h, "tz", g_settings.tz, &n) == ESP_OK;
        if (!tz_saved) strlcpy(g_settings.tz, CONFIG_SKY_TZ, sizeof g_settings.tz);
        /* Iki v1.0 laiko juosta buvo renkama tik ranka – tokia ji ir paliekama */
        if (nvs_get_u8(h, "tzauto", &g_settings.tz_auto) != ESP_OK) g_settings.tz_auto = tz_saved ? 0 : 1;
        n = sizeof g_settings.wifi_ssid;
        if (nvs_get_str(h, "ssid", g_settings.wifi_ssid, &n) == ESP_OK) {
            n = sizeof g_settings.wifi_pass;
            if (nvs_get_str(h, "pass", g_settings.wifi_pass, &n) != ESP_OK) g_settings.wifi_pass[0] = 0;
        }
        size_t ll = sizeof(float);
        const bool has_loc = nvs_get_blob(h, "lat", NULL, &ll) == ESP_OK;
        if (nvs_get_u8(h, "locsrc", &g_settings.loc_src) != ESP_OK) g_settings.loc_src = has_loc ? LOC_USER : LOC_NONE;
        get_float(h, "lat", &g_settings.home_lat);
        get_float(h, "lon", &g_settings.home_lon);
        get_float(h, "range", &g_settings.range_km);
        nvs_close(h);
    }
    /* Sugadintos ar senų versijų reikšmės grąžinamos į leistinas ribas */
    if (!isfinite(g_settings.home_lat) || fabsf(g_settings.home_lat) > 90 || !isfinite(g_settings.home_lon) ||
        fabsf(g_settings.home_lon) > 180) {
        g_settings.home_lat = strtof(CONFIG_SKY_HOME_LAT, NULL);
        g_settings.home_lon = strtof(CONFIG_SKY_HOME_LON, NULL);
    }
    if (!isfinite(g_settings.range_km) || g_settings.range_km < 5 || g_settings.range_km > 100)
        g_settings.range_km = CONFIG_SKY_RANGE_KM;
    if (g_settings.night_start > 1439) g_settings.night_start = CONFIG_SKY_NIGHT_START_H * 60;
    if (g_settings.night_end > 1439) g_settings.night_end = CONFIG_SKY_NIGHT_END_H * 60;
    /* Iki v0.12 puslapio skalė buvo 0…255, todėl mažesnės reikšmės pakeliamos iki naujojo 1 %.
     * Nakties ryškumas 0 – naktį ekranas nešviečia (tai leidžiama). */
    if (g_settings.brightness < SETTINGS_BRIGHT_MIN) g_settings.brightness = SETTINGS_BRIGHT_MIN;
    if (g_settings.night_bright && g_settings.night_bright < SETTINGS_BRIGHT_MIN) g_settings.night_bright = SETTINGS_BRIGHT_MIN;
    if (g_settings.view > 6) g_settings.view = 0;                 /* VIEW_COUNT - 1 */
    if (g_settings.view_prev > 6) g_settings.view_prev = 0;
    if (g_settings.data_mode > DATA_OWN_KEY) g_settings.data_mode = DATA_BUILTIN;
    if (g_settings.data_src > 1) g_settings.data_src = 1;
    if (!settings_key_poll_ok(g_settings.key_poll_s)) g_settings.key_poll_s = 60;
    keep_printable(g_settings.api_key, "", sizeof g_settings.api_key);
    keep_printable(g_settings.os_id, "", sizeof g_settings.os_id);
    keep_printable(g_settings.server_url, SKY_SERVER_URL, sizeof g_settings.server_url);
    keep_printable(g_settings.server_key, "", sizeof g_settings.server_key);
    keep_printable(g_settings.os_secret, "", sizeof g_settings.os_secret);
    keep_printable(g_settings.track_label, "", sizeof g_settings.track_label);
    keep_printable(g_settings.tz, CONFIG_SKY_TZ, sizeof g_settings.tz);
    g_settings.track_show &= TRACK_SHOW_CARD | TRACK_SHOW_MAP | TRACK_SHOW_RADAR | TRACK_SHOW_PULSE;
    if (!(g_settings.track_show & (TRACK_SHOW_CARD | TRACK_SHOW_MAP))) g_settings.track_show |= TRACK_SHOW_CARD;
    if (g_settings.tz_auto > 1) g_settings.tz_auto = 1;
    if (g_settings.poll_s != 0 && (g_settings.poll_s < 5 || g_settings.poll_s > 15)) g_settings.poll_s = 0;
    if (g_settings.radar_mark > 2) g_settings.radar_mark = 0;     /* MARK_COUNT - 1 */
    if (g_settings.font > 3) g_settings.font = 2;                 /* glotnusis; JetBrains Mono (4) nebepasirenkamas */
    if (g_settings.speed_unit > 2) g_settings.speed_unit = 0;     /* SPEED_UNIT_COUNT - 1 */
    if (g_settings.dist_unit > 2) g_settings.dist_unit = 0;
    if (g_settings.alt_unit > 2) g_settings.alt_unit = 0;
    if (g_settings.vs_unit > 1) g_settings.vs_unit = 0;
    if (g_settings.temp_unit > 1) g_settings.temp_unit = 0;
    g_settings.card_fields &= 0x3FF;
    if (g_settings.card_s < 3 || g_settings.card_s > 30) g_settings.card_s = 6;
    if (g_settings.map_style > 1) g_settings.map_style = 1;     /* v0.12.5 „tik krantai“ buvo 2 */
    for (int w = 0; w < SETTINGS_WINS; w++)
        if (g_settings.theme[w] > 5 && g_settings.theme[w] != 0xFF) g_settings.theme[w] = 0;   /* THEME_COUNT - 1 / THEME_CUSTOM */
    for (int i = 0; i < 2; i++)
        if (g_settings.pane[i] > 5) g_settings.pane[i] = (uint8_t)i;           /* PANE_COUNT - 1 */
    if (g_settings.pane[0] == g_settings.pane[1])                               /* dvi vienodos pusės atrodo keistai */
        g_settings.pane[1] = g_settings.pane[0] == 1 ? 0 : 1;
    if (g_settings.overhead_km > 5) g_settings.overhead_km = 2;
    ESP_LOGI(TAG, "wifi '%s'; namai %.4f, %.4f; spindulys %.0f km; ryskumas %u; isdestymas %u",
             g_settings.wifi_ssid, (double)g_settings.home_lat, (double)g_settings.home_lon,
             (double)g_settings.range_km, (unsigned)g_settings.brightness, (unsigned)g_settings.layout);
    return ESP_OK;
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "bright", g_settings.brightness);
    nvs_set_u8(h, "view", g_settings.view);
    nvs_set_u8(h, "vprev", g_settings.view_prev);
    nvs_set_u16(h, "n_startm", g_settings.night_start);
    nvs_set_u16(h, "n_endm", g_settings.night_end);
    nvs_set_u8(h, "n_bright", g_settings.night_bright);
    nvs_set_u8(h, "layout", g_settings.layout);
    nvs_set_u8(h, "flip", g_settings.flip);
    nvs_set_u8(h, "psu", g_settings.psu_amps);
    nvs_set_u8(h, "corder", g_settings.color_order);
    nvs_set_u8(h, "rstyle", g_settings.radar_style);
    nvs_set_u8(h, "rmark", g_settings.radar_mark);
    nvs_set_u8(h, "font", g_settings.font);
    nvs_set_u8(h, "poll", g_settings.poll_s);
    nvs_set_u8(h, "src", g_settings.data_src);
    nvs_set_u8(h, "dmode", g_settings.data_mode);
    nvs_set_u16(h, "kpoll", g_settings.key_poll_s);
    nvs_set_str(h, "apikey", g_settings.api_key);
    nvs_set_str(h, "osid", g_settings.os_id);
    nvs_set_str(h, "srvurl", g_settings.server_url);
    nvs_set_str(h, "srvkey", g_settings.server_key);
    nvs_set_str(h, "ossec", g_settings.os_secret);
    nvs_set_u8(h, "du", g_settings.dist_unit);
    nvs_set_u8(h, "au", g_settings.alt_unit);
    nvs_set_u8(h, "vu", g_settings.vs_unit);
    nvs_set_u8(h, "tu", g_settings.temp_unit);
    nvs_set_u16(h, "cardf", g_settings.card_fields);
    nvs_set_blob(h, "wthm", g_settings.theme, sizeof g_settings.theme);
    nvs_set_blob(h, "wcols", g_settings.colors, sizeof g_settings.colors);
    nvs_set_u8(h, "paneL", g_settings.pane[0]);
    nvs_set_u8(h, "paneR", g_settings.pane[1]);
    nvs_set_u8(h, "ovh", g_settings.overhead_km);
    nvs_set_u8(h, "rare", g_settings.rare);
    nvs_set_u8(h, "flap", g_settings.flap);
    nvs_set_u8(h, "cardsec", g_settings.card_s);
    nvs_set_u8(h, "mapst", g_settings.map_style);
    nvs_set_u8(h, "clk12", g_settings.clock12);
    nvs_set_u8(h, "spd", g_settings.speed_unit);
    nvs_set_u8(h, "trkshow", g_settings.track_show);
    nvs_set_str(h, "track", g_settings.track);
    nvs_set_str(h, "trklbl", g_settings.track_label);
    nvs_set_str(h, "tiles", g_settings.tiles);
    nvs_set_str(h, "tz", g_settings.tz);
    nvs_set_u8(h, "tzauto", g_settings.tz_auto);
    nvs_erase_key(h, "tokens");                    /* 1.0.0 versijos naršyklių prisijungimai – nebenaudojami */
    nvs_commit(h);
    nvs_close(h);
}

void settings_save_wifi(const char *ssid, const char *pass)
{
    strlcpy(g_settings.wifi_ssid, ssid, sizeof g_settings.wifi_ssid);
    strlcpy(g_settings.wifi_pass, pass, sizeof g_settings.wifi_pass);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "issaugotas wifi '%s'", ssid);
}

void settings_save_server(const char *url, const char *key)
{
    strlcpy(g_settings.server_url, url, sizeof g_settings.server_url);
    strlcpy(g_settings.server_key, key, sizeof g_settings.server_key);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "srvurl", g_settings.server_url);
    nvs_set_str(h, "srvkey", g_settings.server_key);
    nvs_commit(h);
    nvs_close(h);
}

void settings_save_location(float lat, float lon, float range_km)
{
    g_settings.home_lat = lat;
    g_settings.home_lon = lon;
    g_settings.range_km = range_km;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "lat", &lat, sizeof lat);
    nvs_set_blob(h, "lon", &lon, sizeof lon);
    nvs_set_blob(h, "range", &range_km, sizeof range_km);
    g_settings.loc_src = LOC_USER;
    nvs_set_u8(h, "locsrc", LOC_USER);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "nauja vieta: %.5f, %.5f, %.0f km", (double)lat, (double)lon, (double)range_km);
}

void settings_clear_wifi(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", "");
    nvs_set_str(h, "pass", "");
    nvs_commit(h);
    nvs_close(h);
}

void settings_factory_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "visi nustatymai istrinti");
}

/* Kviečiama paleidžiant po strigimo; skaitiklis nunulinamas settings_boot_ok() */
uint8_t settings_boot_begin(void)
{
    uint8_t n = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return 0;
    nvs_get_u8(h, "boots", &n);
    if (n < 255) n++;
    nvs_set_u8(h, "boots", n);
    nvs_commit(h);
    nvs_close(h);
    return n;
}

void settings_boot_ok(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "boots", 0);
    nvs_commit(h);
    nvs_close(h);
}

/* Tyčinis perkrovimas: prieš jį skaitiklis nunulinamas, kad perkrovimas nebūtų palaikytas strigimu */
void settings_restart(void)
{
    settings_boot_ok();
    esp_restart();
}

/* adsb.lol užklausų biudžetas („token bucket“) išsaugomas su laiku, kad po perkrovimo nebūtų pilnas iš naujo */
typedef struct {
    float   tokens;
    int64_t epoch_s;
} budget_blob_t;

void settings_budget_save(float tokens, int64_t epoch_s)
{
    budget_blob_t b = { tokens, epoch_s };
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "budget", &b, sizeof b);
    nvs_commit(h);
    nvs_close(h);
}

bool settings_budget_load(float *tokens, int64_t *epoch_s)
{
    budget_blob_t b;
    size_t n = sizeof b;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t err = nvs_get_blob(h, "budget", &b, &n);
    nvs_close(h);
    if (err != ESP_OK || n != sizeof b) return false;
    *tokens = b.tokens;
    *epoch_s = b.epoch_s;
    return true;
}
