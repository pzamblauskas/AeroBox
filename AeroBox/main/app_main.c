/* AeroBox – skrydžių sekimo LED ekranas (ESP32-S3-N16R8 ir P2.5 HUB75 panelės).
 *
 * FreeRTOS užduotys:
 *   hub75  (1 branduolys) – nuolat atnaujina HUB75 paneles per LCD_CAM DMA (hub75.c)
 *   render (1 branduolys) – ~30 kartų per sekundę piešia aktyvų režimą (render_task)
 *   net    (0 branduolys) – Wi-Fi, lėktuvų duomenys, maršrutai, orai, sekamas skrydis (adsb_client.c)
 *   sys    (0 branduolys) – mygtukas, nakties ryškumas, pasenusių lėktuvų šalinimas (system_task)
 *
 * Valdoma per naršyklę: nustatymų tinkle „AeroBox“ – http://192.168.4.1, namų tinkle – http://aerobox.local.
 *
 * Įjungimo eiga:
 *   1. Paleidimo ekranas (screens.c, screen_boot): užrašas „AEROBOX“ ir linija, kuria skrenda lėktuvėlis,
 *      kol jungiamasi prie išsaugoto Wi-Fi (iki WIFI_BOOT_MS).
 *   2. Jei Wi-Fi dar nenustatytas arba per WIFI_BOOT_MS prisijungti nepavyko (pvz., įrenginys perkeltas),
 *      įjungiamas nustatymų tinklas, o ekrane rodomas jo vardas ir slaptažodis (screen_portal).
 *      Fone ir toliau bandoma jungtis prie išsaugoto tinklo.
 *   3. Jei Wi-Fi dingsta jau veikiant, ekrane rodomas raudonas „NO WI-FI“ užrašas. */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aircraft.h"
#include "app_status.h"
#include "button.h"
#include "gfx.h"
#include "hub75.h"
#include "hub75_layout.h"
#include "mode.h"
#include "net.h"
#include "pins.h"
#include "screens.h"
#include "settings.h"
#include "track.h"

#ifdef CONFIG_SKY_FLIGHTWALL
#define FLIGHTWALL true          /* atskiras programos variantas: skrydžio kortelė FlightWall stiliumi */
#else
#define FLIGHTWALL false
#endif

_Static_assert(SETTINGS_COLORS == COL_COUNT, "settings.colors[] ir color_role_t turi sutapti");
_Static_assert(SETTINGS_WINS == WIN_COUNT, "settings.theme[] ir win_t turi sutapti");

static const char *TAG = "aerobox";

#define FRAME_MS           33      /* ~30 kadrų per sekundę */
#define STALE_MS           60000   /* lėktuvas pašalinamas, jei apie jį nėra duomenų ilgiau nei 60 s */
#define OVERLAY_MS         1500    /* trumpo pranešimo (režimas, ryškumas) rodymo trukmė */
#define BOOT_ANIM_MS       3000    /* paleidimo animacija, kai nėra prie ko jungtis (pirmas kartas, nustatymų tinklas) */
#define WIFI_BOOT_MS       10000   /* tiek laukiama prisijungimo prie išsaugoto Wi-Fi; nepavyko – nustatymų tinklas */
#define BOOT_FINISH_MS     500     /* prisijungus lėktuvėlis per tiek laiko nuskrenda iki linijos galo */
#define NO_WIFI_AFTER_MS   3000    /* trumpas atsijungimas (pvz., perėjimas tarp prieigos taškų) – be pranešimo */
#define FADE_MS            500     /* perėjimas tarp sisteminių ekranų (paleidimas, Wi-Fi nustatymai, IP adresas) */
#define WAKE_MS            60000   /* naktį išjungtas ekranas po mygtuko paspaudimo šviečia tiek laiko */
#define BOOT_STABLE_MS     20000   /* tiek laiko veikus paleidimas laikomas sėkmingu */
#define SAFE_MODE_FAILS    3       /* tiek nesėkmingų paleidimų iš eilės → saugus režimas */
#define SAFE_BRIGHTNESS    40      /* saugiame režime ryškumas ribojamas iki šios reikšmės (nustatymas nekeičiamas) */

/* Ryškumo lygiai, kuriuos perjungia ilgas mygtuko paspaudimas; puslapyje jie rodomi kaip 1 / 25 / 50 / 75 / 100 % */
static const uint8_t BRIGHT_LEVELS[] = { SETTINGS_BRIGHT_MIN, 160, 191, 223, 255 };

static gfx_t             s_gfx;
static hub75_layout_t    s_layout;
static volatile uint8_t  s_bright_req;       /* ryškumas, kurį render_task turi nustatyti */
static volatile uint32_t s_overlay_until;    /* iki kada rodomas trumpas pranešimas */
static char              s_overlay_text[24];
static SemaphoreHandle_t s_display_ready;    /* render_task jį atiduoda, kai HUB75 inicializuotas */
static volatile bool     s_booting = true;   /* rodomas paleidimo ekranas */
static volatile uint32_t s_wifi_t0;          /* kada pradėta jungtis prie išsaugoto Wi-Fi */
static volatile uint32_t s_wake_until;       /* iki kada naktį išjungtas ekranas pažadintas mygtuku */

static void show_overlay_for(const char *text, uint32_t ms)
{
    strlcpy(s_overlay_text, text, sizeof s_overlay_text);
    s_overlay_until = sky_now_ms() + ms;
}

static void show_overlay(const char *text)
{
    show_overlay_for(text, OVERLAY_MS);
}

/* Dviejų RGB565 spalvų mišinys: t = 0 – tik a, t = 256 – tik b */
static uint16_t blend565(uint16_t a, uint16_t b, int t)
{
    const int r = (((a >> 11) & 31) * (256 - t) + ((b >> 11) & 31) * t) >> 8;
    const int gg = (((a >> 5) & 63) * (256 - t) + ((b >> 5) & 63) * t) >> 8;
    const int bl = ((a & 31) * (256 - t) + (b & 31) * t) >> 8;
    return (uint16_t)(r << 11 | gg << 5 | bl);
}

/* Sklandus perėjimas tarp sisteminių ekranų. Pasikeitus scene numeriui, FADE_MS laiko senasis kadras
 * palaipsniui užleidžia vietą naujam; kitu atveju kadras tik įsimenamas kitam perėjimui. */
static void fade_scene(gfx_t *g, int scene, uint32_t ms)
{
    static uint16_t *last, *from;
    static int cur = -1;
    static uint32_t t0;
    const size_t n = (size_t)g->w * g->h;
    if (!last) {
        last = heap_caps_calloc(n, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        from = heap_caps_calloc(n, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!last || !from) return;
    }
    if (scene != cur) {
        if (cur >= 0) {
            memcpy(from, last, n * sizeof(uint16_t));
            t0 = ms | 1;                         /* | 1 – kad laikas niekada nebūtų 0 („neaktyvu“) */
        }
        cur = scene;
    }
    if (t0) {
        const uint32_t el = ms - t0;
        if (el >= FADE_MS) t0 = 0;
        else {
            const int t = (int)(el * 256 / FADE_MS);
            for (size_t i = 0; i < n; i++) g->buf[i] = blend565(from[i], g->buf[i], t);
        }
    }
    memcpy(last, g->buf, n * sizeof(uint16_t));
}

/* Paleidimo animacijos eiga nuo 0 iki 1; neigiama reikšmė – paleidimas baigtas.
 * Kol jungiamasi prie išsaugoto Wi-Fi, lėktuvėlis skrenda WIFI_BOOT_MS, o prisijungus per BOOT_FINISH_MS
 * nuskrenda iki galo. Jei jungtis nėra prie ko, animacija trunka BOOT_ANIM_MS. */
static float boot_progress(uint32_t ms)
{
    static uint32_t t0, done_ms;
    static float done_p;
    if (!t0) t0 = ms | 1;
    const uint32_t el = ms - t0;
    if (!g_settings.wifi_ssid[0] || g_status.portal) return el < BOOT_ANIM_MS ? (float)el / BOOT_ANIM_MS : -1.0f;
    if (g_status.wifi) {
        if (!done_ms) {
            done_ms = ms | 1;
            done_p = el < WIFI_BOOT_MS ? (float)el / WIFI_BOOT_MS : 1.0f;
        }
        const float p = done_p + (float)(ms - done_ms) / BOOT_FINISH_MS * (1.0f - done_p);
        return p < 1.0f ? p : -1.0f;
    }
    /* Prisijungti nepavyko: lėktuvėlis laukia linijos gale, kol system_task įjungs nustatymų tinklą (iki 1 s) */
    if (el < WIFI_BOOT_MS) return (float)el / WIFI_BOOT_MS;
    return el < WIFI_BOOT_MS + 3000 ? 1.0f : -1.0f;
}

/* Ar rodyti „NO WI-FI“: Wi-Fi nustatytas, ryšys dingo ilgiau nei NO_WIFI_AFTER_MS, o nustatymų tinklas neįjungtas */
static bool no_wifi_now(uint32_t ms)
{
    static uint32_t lost;
    if (g_status.wifi || !g_settings.wifi_ssid[0] || g_status.portal) {
        lost = 0;
        return false;
    }
    if (!lost) lost = ms | 1;
    return ms - lost > NO_WIFI_AFTER_MS;
}

/* Srovės riba pagal maitinimo bloką: panelėms – 85 % bloko galios, ~0,3 A paliekama ESP32
 * (jei jis maitinamas iš to paties bloko). psu_amps = 0 – riba išjungta. */
static void apply_power_limit(uint32_t full_ma, uint8_t *applied)
{
    if (g_settings.psu_amps == *applied) return;
    *applied = g_settings.psu_amps;
    hub75_set_power_limit(full_ma, *applied ? (uint32_t)*applied * 850 - 300 : 0);
}

/* Kadro kopija valdymo puslapio peržiūrai. Daroma prieš apvertimą, kad puslapyje vaizdas būtų teisingas. */
static void copy_preview(void)
{
    const size_t n = (size_t)s_gfx.w * s_gfx.h * sizeof(uint16_t);
    if (!g_status.preview) {
        uint16_t *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
        if (!p) return;
        g_status.preview = p;
    }
    memcpy(g_status.preview, s_gfx.buf, n);
}

/* Visą vaizdą pasuka 180° (kai ekranas pakabintas apverstas) */
static void flip180(gfx_t *g)
{
    uint16_t *a = g->buf, *b = g->buf + (size_t)g->w * g->h - 1;
    while (a < b) {
        uint16_t t = *a;
        *a++ = *b;
        *b-- = t;
    }
}

/* Vaizdo užduotis: kiekvieną kadrą pasirenka, ką rodyti (sisteminis ekranas, testas ar įprastas režimas),
 * nupiešia jį į s_gfx ir perduoda HUB75 tvarkyklei. */
static void render_task(void *arg)
{
    static aircraft_snapshot_t snap;
    uint8_t bright = s_bright_req;

    /* HUB75 inicializuojamas šiame branduolyje, kad DMA pertraukimai būtų vykdomi čia, o ne kartu su Wi-Fi */
    hub75_config_t pc = {
        .width = s_layout.tile_w, .height = s_layout.tile_h,
        .chain = s_layout.chain, .cols = s_layout.cols, .rows = s_layout.rows,
        .r1 = PANEL_PIN_R1, .g1 = PANEL_PIN_G1, .b1 = PANEL_PIN_B1,
        .r2 = PANEL_PIN_R2, .g2 = PANEL_PIN_G2, .b2 = PANEL_PIN_B2,
        .a = PANEL_PIN_A, .b = PANEL_PIN_B, .c = PANEL_PIN_C, .d = PANEL_PIN_D,
        .e = s_layout.tile_h > 32 ? PANEL_PIN_E : -1,
        .lat = PANEL_PIN_LAT, .oe = PANEL_PIN_OE, .clk = PANEL_PIN_CLK,
        .spare = PANEL_PIN_SPARE,
        .pclk_hz = PANEL_PCLK_HZ,
        .clk_falling_edge = PANEL_CLK_FALLING,
        .brightness = bright,
    };
    memcpy(pc.tiles, s_layout.tiles, sizeof s_layout.tiles);
    bool ok = s_gfx.buf && hub75_init(&pc);
    g_status.display_ok = ok;
    xSemaphoreGive(s_display_ready);
    if (!ok) {
        ESP_LOGE(TAG, "HUB75 inicializacija nepavyko - ekranas neveiks, valdymo puslapis veikia");
        vTaskDelete(NULL);
        return;
    }
    /* Srovė, kai visas ekranas baltas. Ji didėja tiek kartų, kiek daugiau taškų šviečia vienu metu nei 64×32:
     * (taškų / 2048) × (16 / eilučių porų) = plotis / 64; pvz., 128×64 su 1/32 skenavimu → 2 kartus. */
    const uint32_t full_ma = (uint32_t)PANEL_FULL_WHITE_MA * s_layout.tile_w / 64;
    uint8_t psu = 0xFF;                          /* 0xFF – riba dar nepritaikyta */
    uint32_t last_preview = 0;
    apply_power_limit(full_ma, &psu);            /* riba galioja jau paleidimo ekranui */
    hub75_set_color_order(g_settings.color_order);

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        uint32_t ms = sky_now_ms();
        if (s_bright_req != bright) {
            bright = s_bright_req;
            hub75_set_brightness(bright);
        }
        apply_power_limit(full_ma, &psu);
        hub75_set_color_order(g_settings.color_order);   /* pakeitimas valdymo puslapyje įsigalioja iškart */
        /* Nustatymai, kuriuos naudoja režimai, nukopijuojami kiekvienam kadrui */
        g_mode_prefs = (mode_prefs_t){
            .radar_style = g_settings.radar_style, .radar_mark = g_settings.radar_mark,
            .speed_unit = g_settings.speed_unit, .dist_unit = g_settings.dist_unit, .alt_unit = g_settings.alt_unit,
            .vs_unit = g_settings.vs_unit, .temp_unit = g_settings.temp_unit, .clock12 = g_settings.clock12,
            .track_card = (g_settings.track_show & TRACK_SHOW_CARD) != 0,
            .track_map = (g_settings.track_show & TRACK_SHOW_MAP) != 0,
            .track_radar = (g_settings.track_show & TRACK_SHOW_RADAR) != 0,
            .track_pulse = (g_settings.track_show & TRACK_SHOW_PULSE) != 0, .card_fields = g_settings.card_fields,
            .pane = { g_settings.pane[0], g_settings.pane[1] }, .overhead_km = g_settings.overhead_km,
            .rare = g_settings.rare, .flap = g_settings.flap, .card_s = g_settings.card_s, .flightwall = FLIGHTWALL,
            .map_style = g_settings.map_style,
        };
        strlcpy(g_mode_prefs.track_label, g_settings.track_label, sizeof g_mode_prefs.track_label);
        for (int w = 0; w < WIN_COUNT; w++) {
            if (g_settings.theme[w] == THEME_CUSTOM) memcpy(g_mode_prefs.wcol[w], g_settings.colors[w], sizeof g_mode_prefs.wcol[w]);
            else theme_colors(g_settings.theme[w], w, g_mode_prefs.wcol[w]);
        }
        mode_win(WIN_CARD);                          /* užrašai ir testai, kurie neturi savo lango, naudoja kortelės spalvas */
        /* 7 taškų aukščio šriftai (glotnusis ir 5×7) 64×32 ekrane netelpa – ten naudojamas 4×5 */
        gfx_set_font(s_gfx.h >= 64 || g_settings.font < GFX_FONT_SMOOTH ? g_settings.font : GFX_FONT_WIDE);

        gfx_clear(&s_gfx);
        const bool testing = (int32_t)(g_status.test_until_ms - ms) > 0;
        const float boot = s_booting ? boot_progress(ms) : -1.0f;
        if (s_booting && boot < 0) s_booting = false;
        /* Ką rodyti, nusprendžiama šia pirmumo tvarka. scene – ekrano numeris; jam pasikeitus, fade_scene
         * padaro sklandų perėjimą. */
        int scene;
        if (s_booting) {
            scene = 1;
            screen_boot(&s_gfx, boot, g_status.safe_mode);   /* paleidimo ekranas – žr. screens.c */
        } else if (testing && g_status.test_kind >= TEST_OVERHEAD) {
            scene = 3;
            aircraft_table_snapshot(&snap);      /* valdymo puslapio „Test“: funkcijos demonstracija su tikrais lėktuvais */
            demo_draw(&s_gfx, g_status.test_kind, &snap, ms - g_status.test_start_ms);
        } else if ((g_status.portal && !g_status.wifi) || g_status.portal_state == PORTAL_TESTING ||
                   g_status.portal_state == PORTAL_OK) {
            scene = 10 + 2 * (int)g_status.portal_state + (g_status.portal_client ? 1 : 0);
            if (s_gfx.h < 64 && g_status.portal_state == PORTAL_IDLE && !g_status.portal_client)
                scene += 100 * (int)((ms / 3000) % 2);   /* mažame ekrane vardas ir slaptažodis rodomi paeiliui */
            screen_portal(&s_gfx, ms);           /* rodoma ir tada, kai Wi-Fi keičiamas iš namų tinklo */
        } else if (g_status.upd_state >= 1) {
            scene = 25;                          /* diegiamas atnaujinimas iš serverio */
            char t[24];
            snprintf(t, sizeof t, g_status.upd_state == 2 ? "RESTARTING" : "UPDATING %u%%", (unsigned)g_status.upd_pct);
            screen_overlay(&s_gfx, t);
        } else if (g_settings.welcome && g_status.wifi && g_status.ip[0]) {
            scene = 20;                          /* ką tik nustatytas Wi-Fi: pasaulio žemėlapis ir valdymo puslapio adresas */
            screen_welcome(&s_gfx, g_status.ip);
        } else {
            scene = 30;                          /* įprastas darbas: aktyvus režimas */
            aircraft_table_snapshot(&snap);      /* trumpam užrakina lentelę ir ją nukopijuoja */
            const display_mode_t *m = mode_manager_select(&snap, ms);
            m->update(&snap, ms);
            m->draw(&s_gfx);
        }
        if (!s_booting) {
            if ((int32_t)(s_overlay_until - ms) > 0) screen_overlay(&s_gfx, s_overlay_text);
            else if (no_wifi_now(ms)) screen_overlay_alert(&s_gfx, "NO WI-FI");
        }
        fade_scene(&s_gfx, scene, ms);
        /* Peržiūros kadras – tik kai puslapis jo prašė per paskutines 3 s, ne dažniau kaip kas 250 ms */
        if (g_status.preview_req_ms && ms - g_status.preview_req_ms < 3000 && ms - last_preview >= 250) {
            last_preview = ms;
            copy_preview();
        }
        if (g_settings.flip) flip180(&s_gfx);
        hub75_present(s_gfx.buf);                /* perduoda kadrą ir sukeičia buferius */

        vTaskDelayUntil(&last, pdMS_TO_TICKS(FRAME_MS));
    }
}

/* Dabartinis ryškumas. Nakties laikotarpiu (minutėmis, gali kirsti vidurnaktį) taikomas nakties ryškumas;
 * 0 reiškia, kad ekranas nešviečia. Paleidimo ir Wi-Fi nustatymų ekranai matomi visada, o mygtuko paspaudimas
 * naktį išjungtą ekraną trumpam pažadina. */
static uint8_t effective_brightness(uint32_t now)
{
    uint8_t b = g_settings.brightness;
    const int start = g_settings.night_start, end = g_settings.night_end;
    if (start != end && sky_time_valid()) {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        const int m = tm.tm_hour * 60 + tm.tm_min;
        const bool night = start > end ? (m >= start || m < end) : (m >= start && m < end);
        if (night && b > g_settings.night_bright) b = g_settings.night_bright;
    }
    if (b == 0 && (s_booting || g_status.portal || (int32_t)(s_wake_until - now) > 0)) b = SETTINGS_BRIGHT_MIN;
    if (g_status.safe_mode && b > SAFE_BRIGHTNESS) b = SAFE_BRIGHTNESS;
    return b;
}

/* Ryškumas procentais, kaip rodo puslapis: SETTINGS_BRIGHT_MIN – 1 %, 255 – 100 % */
static int bright_pct(uint8_t b)
{
    return b <= SETTINGS_BRIGHT_MIN ? 1 : 1 + (b - SETTINGS_BRIGHT_MIN) * 99 / (255 - SETTINGS_BRIGHT_MIN);
}

/* Mygtukas: trumpas paspaudimas – kitas vaizdas; ilgas – kitas ryškumo lygis; 5 s – Wi-Fi nustatymų ištrynimas */
static void on_button(button_event_t ev)
{
    char buf[24];
    if (ev != BTN_NONE && s_bright_req == 0) {
        /* Naktį išjungtas ekranas: trumpas paspaudimas tik pažadina, vaizdas nekeičiamas */
        s_wake_until = sky_now_ms() + WAKE_MS;
        s_bright_req = SETTINGS_BRIGHT_MIN;
        if (ev == BTN_SHORT) return;
    }
    if ((ev == BTN_SHORT || ev == BTN_LONG) && g_settings.welcome && g_status.wifi) {
        settings_welcome_done();                 /* mygtukas uždaro po Wi-Fi nustatymo rodomą žemėlapį su adresu */
        return;
    }
    if (ev == BTN_SHORT) {
        view_t v = (view_t)((mode_manager_view() + 1) % VIEW_COUNT);
        mode_manager_set_view(v);
        g_settings.view = (uint8_t)v;
        show_overlay(mode_manager_view_name(v));
    } else if (ev == BTN_LONG) {
        size_t i = 0;
        while (i < sizeof BRIGHT_LEVELS && BRIGHT_LEVELS[i] <= g_settings.brightness) i++;
        g_settings.brightness = BRIGHT_LEVELS[i < sizeof BRIGHT_LEVELS ? i : 0];
        snprintf(buf, sizeof buf, "BRIGHTNESS %d%%", bright_pct(g_settings.brightness));
        show_overlay(buf);
    } else if (ev == BTN_RESET) {
        /* Laikyta 5 s: ištrinami Wi-Fi nustatymai ir viskas pradedama iš naujo */
        ESP_LOGW(TAG, "Wi-Fi nustatymai istrinti - perkraunama");
        settings_clear_wifi();
        show_overlay("WI-FI RESET");
        vTaskDelay(pdMS_TO_TICKS(1200));
        settings_restart();
    } else {
        return;
    }
    settings_save();
}

/* Sisteminė užduotis: mygtukas (kas 20 ms) ir kas sekundę – lentelės valymas, ryškumas, Wi-Fi būsenos sprendimai */
static void system_task(void *arg)
{
    uint32_t last_1s = 0, last_log = 0;
    bool ip_shown = false, boot_ok = false, connected_once = false;
    bool portal_auto = false;      /* nustatymų tinklą įjungė programa (nepavyko prisijungti), o ne naudotojas */
    for (;;) {
        uint32_t now = sky_now_ms();
        on_button(button_poll(now));

        if (now - last_1s >= 1000) {
            last_1s = now;
            /* Lėktuvas pašalinamas, jei jo nebuvo paskutiniame sėkmingame atsakyme (su 60 s atsarga).
             * Kai užklausos retos (duomenų biudžetas), tarp atnaujinimų jis nešalinamas, bet ne ilgiau nei 10 min. */
            uint32_t since_ok = g_status.last_ok_ms ? now - g_status.last_ok_ms : 0;
            aircraft_table_prune(STALE_MS + (since_ok < 540000 ? since_ok : 540000));
            s_bright_req = effective_brightness(now);
            /* Sekamas skrydis nusileido prieš 2 min. ir užrašas „LANDED“ jau parodytas – sekimas baigiamas (kaip „Stop“) */
            if (track_finished()) {
                ESP_LOGI(TAG, "sekamas skrydis nusileido - sekimas baigtas");
                net_track_stop();
            }

            if (!boot_ok && now > BOOT_STABLE_MS) {
                boot_ok = true;
                settings_boot_ok();
                esp_ota_mark_app_valid_cancel_rollback();   /* nauja programa veikia – grįžti į senąją nebereikia */
                ESP_LOGI(TAG, "paleidimas stabilus");
            }

            /* Pirmą kartą prisijungus 8 s rodomas IP adresas (valdymo puslapiui, jei aerobox.local neveikia).
             * Po Wi-Fi nustatymo adresas rodomas kitur (screen_portal ir screen_welcome), todėl čia praleidžiamas. */
            if (g_status.portal_state == PORTAL_OK || g_settings.welcome) ip_shown = true;
            if (g_status.wifi && !ip_shown && g_status.ip[0]) {
                ip_shown = true;
                show_overlay_for(g_status.ip, 8000);
            }
            /* Įjungus per WIFI_BOOT_MS prisijungti prie išsaugoto Wi-Fi nepavyko (pvz., įrenginys perkeltas į kitą
             * vietą) – įjungiamas nustatymų tinklas, kaip pirmą kartą. Kol telefonas neprisijungęs, fone toliau
             * bandoma jungtis prie išsaugoto tinklo. Jei ryšys dingsta jau veikiant, ekrane rodomas tik
             * „NO WI-FI“, o prisijungti bandoma iš naujo. */
            if (g_status.wifi) connected_once = true;
            if (!connected_once && !g_status.portal && g_settings.wifi_ssid[0] && s_wifi_t0 &&
                now - s_wifi_t0 >= WIFI_BOOT_MS) {
                ESP_LOGW(TAG, "prie '%s' prisijungti nepavyko per %d s - ijungiamas nustatymu tinklas",
                         g_settings.wifi_ssid, WIFI_BOOT_MS / 1000);
                net_portal_start();
                portal_auto = true;
            }
            /* Ryšys grįžo, o telefonu niekas nekeičiama – nustatymų tinklo nebereikia */
            if (portal_auto && g_status.portal && g_status.wifi && !g_status.portal_client &&
                g_status.portal_state != PORTAL_TESTING && g_status.portal_state != PORTAL_OK) {
                ESP_LOGI(TAG, "prisijungta prie '%s' - nustatymu tinklas isjungiamas", g_settings.wifi_ssid);
                net_portal_stop();
                portal_auto = false;
            }
        }
        if (now - last_log >= 30000) {           /* kas 30 s – būsenos eilutė žurnale */
            last_log = now;
            ESP_LOGI(TAG, "wifi=%d duomenys=%d lektuvu=%d laisva RAM=%u (blokas %u) PSRAM=%u srove~%lu mA",
                     g_status.wifi, g_status.data_ok, g_status.seen_total,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned long)hub75_power_ma());
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* Perkrovimo priežastis žmogui suprantamais žodžiais (rodoma valdymo puslapyje) */
static const char *reset_reason_text(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_EXT:      return "RST button";
    case ESP_RST_SW:       return "restart";
    case ESP_RST_PANIC:    return "software error";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "froze (watchdog)";
    case ESP_RST_BROWNOUT: return "power dip";
    case ESP_RST_USB:      return "USB";
    case ESP_RST_DEEPSLEEP: return "wake up";
    default:               return "unknown";
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(settings_init());              /* visi nustatymai iš NVS */

    /* Diagnostika: kodėl įrenginys persikrovė ir ar jis nepateko į perkrovimų ciklą */
    esp_reset_reason_t rr = esp_reset_reason();
    strlcpy(g_status.reset_reason, reset_reason_text(rr), sizeof g_status.reset_reason);
    /* Skaičiuojami tik strigimai ir maitinimo kritimai; įjungimas, RST mygtukas ir įkėlimas per USB – ne */
    const bool crash = rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT ||
                       rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT;
    if (crash) {
        g_status.boot_fails = settings_boot_begin();
    } else {
        settings_boot_ok();
    }
    if (g_status.boot_fails >= SAFE_MODE_FAILS) g_status.safe_mode = true;   /* ryškumą riboja effective_brightness() */
    ESP_LOGW(TAG, "paleidimas: %s (%d), nebaigtu paleidimu is eiles: %u%s", g_status.reset_reason, (int)rr,
             (unsigned)g_status.boot_fails, g_status.safe_mode ? " -> SAUGUS REZIMAS" : "");
    if (rr == ESP_RST_BROWNOUT)
        ESP_LOGE(TAG, "MAITINIMO KRITIMAS: paneles traukia per daug sroves arba ESP32 maitinamas is to paties silpno bloko");

    /* Panelių išdėstymas iš nustatymų; jei jis netinkamas – viena 64×32 panelė */
    if (!hub75_layout_get(g_settings.layout, g_settings.tiles, &s_layout))
        ESP_LOGW(TAG, "neteisingas isdestymas %u - naudojama 1x 64x32", (unsigned)g_settings.layout);
    g_status.width = s_layout.tile_w * s_layout.cols;
    g_status.height = s_layout.tile_h * s_layout.rows;
    ESP_LOGI(TAG, "ekranas %ux%u (%s)", g_status.width, g_status.height, hub75_layout_name(g_settings.layout));

    aircraft_table_init(g_settings.home_lat, g_settings.home_lon, g_settings.range_km);
    track_set_query(g_settings.track);
    mode_manager_set_view((view_t)g_settings.view);
    gfx_init(&s_gfx, g_status.width, g_status.height);
    s_bright_req = effective_brightness(sky_now_ms());
    button_init(BUTTON_PIN);

    /* Ekranas paleidžiamas prieš Wi-Fi: dideli DMA buferiai (iki 2 × 48 KB) išskiriami,
     * kol vidinė RAM dar nesuskaidyta */
    s_display_ready = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(render_task, "render", 6144, NULL, 5, NULL, 1);
    xSemaphoreTake(s_display_ready, pdMS_TO_TICKS(3000));

    net_init(g_settings.tz);
    /* Nustatymų tinklas įjungiamas, jei nėra išsaugoto Wi-Fi, įjungiant laikomas mygtukas arba veikia saugus režimas */
    const bool button = button_is_down();
    if (g_settings.wifi_ssid[0] && !button) {
        s_wifi_t0 = sky_now_ms() | 1;
        net_wifi_connect(g_settings.wifi_ssid, g_settings.wifi_pass);
    }
    if (!g_settings.wifi_ssid[0] || button || g_status.safe_mode) net_portal_start();
    net_webui_start();                             /* tas pats valdymo puslapis abiem atvejais */

    xTaskCreatePinnedToCore(net_task,    "net",   12288, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(system_task, "sys",    6144, NULL, 3, NULL, 0);
}
