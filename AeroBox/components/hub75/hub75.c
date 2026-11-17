/* HUB75 tvarkyklė ESP32-S3 LCD_CAM periferijai.
 *
 * Kiekvienas 16 bitų žodis – vienas CLK (WR) periodas; bitai eina tiesiai į kontaktus:
 *   0 R1  1 G1  2 B1  3 R2  4 G2  5 B2  6 A  7 B  8 C  9 D  10 E  11 LAT  12 OE  13–15 laisvi
 *
 * Kadras = (height / 2) eilučių porų × BITS bitų plokštumų × width žodžių (BCM – binarinė kodinė moduliacija).
 * Kol į registrus stumiami bloko (eilutė r, bitas b) duomenys, rodomas ankstesnis užfiksuotas blokas:
 * adreso linijos rodo ankstesnę eilutę, o OE įjungtas tiek CLK periodų, kiek „sveria“ ankstesnis bitas.
 * Paskutiniame bloko žodyje LAT = 1 – registrų turinys perkeliamas į išėjimus.
 *
 * Atnaujinama nuolat: refresh_task vėl ir vėl siunčia tą patį buferį per DMA
 * (64×32: 12 KB, ~1,6 kHz esant 10 MHz CLK). Piešiama į antrą buferį, po to buferiai sukeičiami. */
#include "hub75.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/gpio_sig_map.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "hub75";

#define BITS         6           /* 64 ryškumo lygiai kiekvienam kanalui */
#define QUEUE_DEPTH  2           /* DMA perdavimų eilės ilgis */
#define ADDR_SHIFT   6           /* adreso linijos A–E prasideda 6-uoju bitu */
#define BIT_LAT      (1u << 11)
#define BIT_OE       (1u << 12)  /* loginis 1 – šviečia (kontaktas invertuojamas GPIO matricoje) */
#define OE_BUS_BIT   12

static esp_lcd_i80_bus_handle_t  s_bus;
static esp_lcd_panel_io_handle_t s_io;
static uint16_t                 *s_buf[2];
static size_t                    s_bytes;
static int                       s_back = 1;
static uint16_t                  s_w, s_rows;
static volatile uint8_t          s_brightness;
static uint16_t *volatile        s_pending;          /* naujas buferis, kurį refresh_task turi pradėti siųsti */
static SemaphoreHandle_t         s_old_free;         /* atiduodamas, kai senasis buferis nebesiunčiamas */
static uint8_t                   s_lut5[32], s_lut6[64];   /* gama korekcija 5 ir 6 bitų kanalams → 0…63 */
static uint8_t                  *s_levels;           /* 6 kanalai × width vienai eilučių porai */
static hub75_config_t            s_cfg;
static uint16_t                 *s_phys;             /* grandinės kadras (chain×width × height), jei panelių daugiau nei viena */
static uint32_t                  s_full_ma, s_limit_ma;
static volatile uint8_t          s_order;            /* spalvų tvarka, žr. hub75_set_color_order */
static const uint8_t             PERM[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 },
                                                { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
static volatile uint32_t         s_power_ma;
static volatile bool             s_power_limited;

/* Gama 2,0 lentelės. Ne juoda spalva niekada netampa juoda – kitaip naktį blankus tekstas išnyktų. */
static void build_gamma(void)
{
    for (int i = 0; i < 32; i++) s_lut5[i] = (uint8_t)fmaxf(i ? 1.0f : 0.0f, lroundf(powf(i / 31.0f, 2.0f) * 63.0f));
    for (int i = 0; i < 64; i++) s_lut6[i] = (uint8_t)fmaxf(i ? 1.0f : 0.0f, lroundf(powf(i / 63.0f, 2.0f) * 63.0f));
}

/* Kadro srovės įvertis ir, jei reikia, sumažintas ryškumas.
 * Kanalo šviesumo dalis = lygis × on[0] / (BITS × w), kur on[0] ≈ (w − 2) × br / (32 × 255). */
static uint32_t limit_brightness(const uint16_t *fb, uint32_t br)
{
    const int npix = s_w * s_rows * 2;
    if (!s_full_ma || !npix) return br;
    uint32_t sum = 0;
    for (int i = 0; i < npix; i++) {
        uint16_t p = fb[i];
        sum += s_lut5[p >> 11] + s_lut6[(p >> 5) & 63] + s_lut5[p & 31];
    }
    /* mA = full × panelių × (sum / (npix × 3 × 63)) × 63 × (w − 2) × br / (32 × 255 × BITS × w) */
    const uint64_t num = (uint64_t)s_full_ma * s_cfg.chain * sum * (uint64_t)(s_w - 2) * br;
    const uint64_t den = (uint64_t)npix * 3 * 32 * 255 * BITS * s_w;
    uint32_t ma = (uint32_t)(num / den);
    s_power_limited = false;
    if (s_limit_ma && ma > s_limit_ma) {
        br = br * s_limit_ma / ma;
        if (br < 1) br = 1;
        ma = s_limit_ma;
        s_power_limited = true;
    }
    s_power_ma = ma;
    return br;
}

/* RGB565 kadrą paverčia DMA buferiu (BCM bitų plokštumomis), žr. failo pradžią */
static void build_frame(uint16_t *dst, const uint16_t *fb)
{
    const int w = s_w, rows = s_rows;
    const uint32_t br = limit_brightness(fb, s_brightness);

    /* OE trukmė (CLK periodais) kiekvienam bitui; LAT žodžiui ir adreso pakeitimui paliekama vietos */
    int on[BITS];
    /* Vyriausias bitas šviečia beveik visą bloką (w − 2 CLK), kiekvienas jaunesnis – perpus trumpiau.
     * Todėl ilga grandinė (4 panelės = 256 taškų eilutė) nėra tamsesnė už vieną panelę. */
    for (int b = 0; b < BITS; b++) {
        int t = (int)(((uint32_t)(w - 2) * (1u << b) * br + (32u * 255u) / 2) / (32u * 255u));
        on[b] = t > w - 2 ? w - 2 : t;
    }

    const uint8_t *pm = PERM[s_order < 6 ? s_order : 0];
    int prev_row = rows - 1, prev_bit = BITS - 1;    /* rodomas ankstesnis blokas (pirmam – paskutinis) */
    for (int r = 0; r < rows; r++) {
        const uint16_t *top = fb + r * w, *bot = fb + (r + rows) * w;
        uint8_t *lv = s_levels;
        for (int x = 0; x < w; x++, lv += 6) {
            uint16_t p = top[x], q = bot[x];
            const uint8_t t[3] = { s_lut5[p >> 11], s_lut6[(p >> 5) & 63], s_lut5[p & 31] };
            const uint8_t u[3] = { s_lut5[q >> 11], s_lut6[(q >> 5) & 63], s_lut5[q & 31] };
            lv[0] = t[pm[0]]; lv[1] = t[pm[1]]; lv[2] = t[pm[2]];
            lv[3] = u[pm[0]]; lv[4] = u[pm[1]]; lv[5] = u[pm[2]];
        }
        for (int b = 0; b < BITS; b++) {
            const uint16_t addr = (uint16_t)(prev_row << ADDR_SHIFT);
            const int on_t = on[prev_bit];
            const uint8_t *l = s_levels;
            for (int x = 0; x < w; x++, l += 6) {
                uint16_t word = addr
                    | ((l[0] >> b) & 1)        | (((l[1] >> b) & 1) << 1) | (((l[2] >> b) & 1) << 2)
                    | (((l[3] >> b) & 1) << 3) | (((l[4] >> b) & 1) << 4) | (((l[5] >> b) & 1) << 5);
                if (x >= 1 && x <= on_t) word |= BIT_OE;
                if (x == w - 1) word |= BIT_LAT;
                *dst++ = word;
            }
            prev_row = r;
            prev_bit = b;
        }
    }
}

/* Nuolat siunčia aktyvų buferį per DMA. Gavusi naują buferį, perjungia į jį ir, kai senasis išeina iš
 * DMA eilės, praneša piešėjui (s_old_free), kad senąjį jau galima perrašyti. */
static void refresh_task(void *arg)
{
    uint16_t *cur = s_buf[0];
    int sent = 0;
    bool swapped = false;
    for (;;) {
        uint16_t *p = s_pending;
        if (p) {
            s_pending = NULL;
            cur = p;
            sent = 0;
            swapped = true;
        }
        /* lcd_cmd = -1: be komandos fazės, tik duomenys. Blokuoja, kol eilėje atsilaisvina vieta. */
        esp_lcd_panel_io_tx_color(s_io, -1, cur, s_bytes);
        /* Kai eilėje lieka tik naujasis buferis, senąjį galima vėl naudoti piešimui */
        if (swapped && ++sent == QUEUE_DEPTH) {
            swapped = false;
            xSemaphoreGive(s_old_free);
        }
    }
}

bool hub75_init(const hub75_config_t *c)
{
    s_cfg = *c;
    if (s_cfg.chain < 1) s_cfg.chain = 1;
    if (s_cfg.chain > HUB75_MAX_CHAIN) s_cfg.chain = HUB75_MAX_CHAIN;
    if (s_cfg.chain == 1 && s_cfg.cols == 0) {
        s_cfg.cols = s_cfg.rows = 1;
        s_cfg.tiles[0] = (hub75_tile_t){ 0, 0, false };
    }
    s_w = c->width * s_cfg.chain;               /* grandinė elgiasi kaip viena ilga eilutė */
    s_rows = c->height / 2;
    s_brightness = c->brightness;
    s_bytes = (size_t)s_rows * BITS * s_w * sizeof(uint16_t);
    build_gamma();

    if (s_cfg.chain > 1) {
        /* Šį buferį naudoja tik CPU, todėl jis gali būti PSRAM; vidinė RAM paliekama DMA ir Wi-Fi */
        s_phys = heap_caps_calloc((size_t)s_w * c->height, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!s_phys) s_phys = heap_caps_calloc((size_t)s_w * c->height, sizeof(uint16_t), MALLOC_CAP_INTERNAL);
        if (!s_phys) {
            ESP_LOGE(TAG, "truksta atminties grandines kadrui");
            return false;
        }
    }
    s_levels = malloc((size_t)s_w * 6);
    s_buf[0] = heap_caps_calloc(1, s_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_buf[1] = heap_caps_calloc(1, s_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_old_free = xSemaphoreCreateBinary();
    if (!s_levels || !s_buf[0] || !s_buf[1] || !s_old_free) {
        ESP_LOGE(TAG, "truksta atminties: reikia 2x %u B DMA, didziausias laisvas blokas %u B", (unsigned)s_bytes,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        return false;
    }

    const int spare = c->spare;
    esp_lcd_i80_bus_config_t bc = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = spare,
        .wr_gpio_num = c->clk,
        .data_gpio_nums = {
            c->r1, c->g1, c->b1, c->r2, c->g2, c->b2,
            c->a, c->b, c->c, c->d, c->e >= 0 ? c->e : spare,
            c->lat, c->oe, spare, spare, spare,
        },
        .bus_width = 16,
        .max_transfer_bytes = s_bytes,
    };
    esp_err_t err = esp_lcd_new_i80_bus(&bc, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_i80_bus: %s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_panel_io_i80_config_t ic = {
        .cs_gpio_num = -1,
        .pclk_hz = c->pclk_hz,
        .trans_queue_depth = QUEUE_DEPTH,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 16,
        .dc_levels = { .dc_idle_level = 0, .dc_cmd_level = 0, .dc_dummy_level = 0, .dc_data_level = 1 },
        .flags = { .pclk_active_neg = c->clk_falling_edge },
    };
    err = esp_lcd_new_panel_io_i80(s_bus, &ic, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_i80: %s", esp_err_to_name(err));
        return false;
    }

    /* OE aktyvus žemu lygiu, todėl signalas invertuojamas GPIO matricoje: buferyje 1 reiškia „šviečia“,
     * o tarp DMA perdavimų (visos linijos 0) ekranas užgesęs – ryškių eilučių nematyti. */
    esp_rom_gpio_connect_out_signal(c->oe, LCD_DATA_OUT0_IDX + OE_BUS_BIT, true, false);

    xTaskCreatePinnedToCore(refresh_task, "hub75", 3072, NULL, configMAX_PRIORITIES - 2, NULL, 1);
    ESP_LOGI(TAG, "HUB75 %ux%u, %u B kadras, CLK %lu Hz", s_w, (unsigned)(s_rows * 2),
             (unsigned)s_bytes, (unsigned long)c->pclk_hz);
    return true;
}

void hub75_set_brightness(uint8_t b)
{
    s_brightness = b;     /* įsigalioja kitame kadre */
}

void hub75_set_color_order(uint8_t order)
{
    s_order = order < 6 ? order : 0;
}

void hub75_set_power_limit(uint32_t full_white_ma, uint32_t limit_ma)
{
    s_full_ma = full_white_ma;
    s_limit_ma = limit_ma;
}

uint32_t hub75_power_ma(void)
{
    return s_power_ma;
}

bool hub75_power_limited(void)
{
    return s_power_limited;
}

/* Viso ekrano kadras → grandinės kadras. Pirmas išstumtas taškas nukeliauja iki tolimiausios panelės,
 * todėl panelė [i] (i-oji nuo ESP32) grandinėje užima vietą nuo (chain − 1 − i) × width. */
static void map_tiles(const uint16_t *virt)
{
    const int pw = s_cfg.width, ph = s_cfg.height, vw = pw * s_cfg.cols;
    for (int i = 0; i < s_cfg.chain; i++) {
        const hub75_tile_t t = s_cfg.tiles[i];
        const int seg = (s_cfg.chain - 1 - i) * pw;
        for (int ly = 0; ly < ph; ly++) {
            const uint16_t *src = virt + (size_t)(t.row * ph + ly) * vw + t.col * pw;
            if (!t.rot180) {
                memcpy(s_phys + (size_t)ly * s_w + seg, src, (size_t)pw * sizeof(uint16_t));
            } else {
                uint16_t *dst = s_phys + (size_t)(ph - 1 - ly) * s_w + seg + pw - 1;
                for (int lx = 0; lx < pw; lx++) *dst-- = src[lx];
            }
        }
    }
}

void hub75_present(const uint16_t *rgb565)
{
    if (s_cfg.chain > 1) {
        map_tiles(rgb565);
        rgb565 = s_phys;
    }
    uint16_t *back = s_buf[s_back];
    build_frame(back, rgb565);
    s_pending = back;
    /* Palaukiame, kol DMA eilėje neliks senojo buferio (~1–2 ms) */
    xSemaphoreTake(s_old_free, pdMS_TO_TICKS(100));
    s_back ^= 1;
}