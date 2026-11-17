/* HUB75 tvarkyklė grynu C: ESP32-S3 LCD_CAM periferija (esp_lcd i80, 16 bitų) ir DMA.
 * Skirta paprastiems poslinkio registrų lustams (ICN2037, ICN2037BP ir pan.) ir 138 tipo eilučių dekoderiui.
 * Palaiko kelias paneles grandinėje, sudėliotas į vieną didesnį ekraną (pvz., 4 × 64×32 → 128×64). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define HUB75_MAX_CHAIN 8

/* Vienos panelės vieta dideliame ekrane: stulpelis, eilutė ir ar ji apversta 180° */
typedef struct {
    uint8_t col, row;
    bool    rot180;
} hub75_tile_t;

typedef struct {
    uint16_t width, height;        /* VIENOS panelės dydis, pvz., 64×32 (1/16 skenavimas) */
    uint8_t  chain;                /* kiek panelių grandinėje (1 – viena) */
    uint8_t  cols, rows;           /* ekranas sudarytas iš cols × rows panelių */
    hub75_tile_t tiles[HUB75_MAX_CHAIN]; /* [0] – arčiausiai ESP32 esanti panelė */
    int8_t r1, g1, b1, r2, g2, b2;
    int8_t a, b, c, d, e;          /* e = -1, jei linija nenaudojama */
    int8_t lat, oe, clk;
    int8_t spare;                  /* laisvas GPIO, prie nieko nejungiamas (LCD_CAM reikalauja DC ir 16 duomenų linijų) */
    uint32_t pclk_hz;              /* CLK dažnis, pvz., 10 MHz */
    bool clk_falling_edge;         /* true – duomenys nuskaitomi krintančiu CLK frontu */
    uint8_t brightness;            /* 0…255 */
} hub75_config_t;

bool hub75_init(const hub75_config_t *cfg);
void hub75_set_brightness(uint8_t b);
/* Viso ekrano (cols×width pločio, rows×height aukščio) RGB565 kadrą paskirsto panelėms ir parodo */
void hub75_present(const uint16_t *rgb565);

/* Spalvų tvarka: kuri spalva siunčiama į R, G ir B kontaktus (panelių partijos skiriasi).
 * 0 RGB, 1 RBG (žalia ↔ mėlyna), 2 GRB, 3 GBR, 4 BRG, 5 BGR. Įsigalioja kitame kadre. */
void     hub75_set_color_order(uint8_t order);

/* Srovės ribojimas: full_white_ma – VIENOS panelės srovė, kai visi taškai balti 100 % laiko;
 * limit_ma – maitinimo bloko riba (0 – neriboti). Viršijus ribą, ryškumas automatiškai sumažinamas. */
void     hub75_set_power_limit(uint32_t full_white_ma, uint32_t limit_ma);
/* Paskutinio kadro srovės įvertis (mA) ir ar ryškumas buvo apribotas */
uint32_t hub75_power_ma(void);
bool     hub75_power_limited(void);
