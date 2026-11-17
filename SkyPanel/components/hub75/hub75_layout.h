/* Paruošti panelių išdėstymai, kad ta pati programa tiktų vienai 64×32 panelei, keturioms (2 × 2 = 128×64)
 * ir vienai didelei 128×64 panelei (1/32 skenavimas). Išdėstymas nustatomas per /api/settings. */
#pragma once
#include <stdbool.h>
#include "hub75.h"

typedef enum {
    LAYOUT_1X_64X32 = 0,   /* viena 64×32 panelė */
    LAYOUT_4X_S,           /* 4 × 64×32: ESP32 → viršuje dešinėje → apačioje dešinėje → apačioje kairėje → viršuje kairėje */
    LAYOUT_4X_ROWS,        /* 4 × 64×32: visos vienodai, eilutėmis iš kairės į dešinę, iš viršaus į apačią */
    LAYOUT_1X_128X64,      /* viena 128×64 panelė (1/32 skenavimas, reikia E linijos) */
    LAYOUT_CUSTOM,         /* 4 × 64×32 su naudotojo išdėstymu (settings.tiles) */
    LAYOUT_COUNT
} hub75_layout_id_t;

typedef struct {
    uint16_t     tile_w, tile_h;
    uint8_t      chain, cols, rows;
    hub75_tile_t tiles[4];
} hub75_layout_t;

/* Užpildo išdėstymą pagal numerį; naudotojo išdėstymas imamas iš teksto „c,r,f;c,r,f;c,r,f;c,r,f“
 * (stulpelis, eilutė, apversta). Netinkamas numeris ar tekstas → grąžina false ir užpildo LAYOUT_1X_64X32. */
bool hub75_layout_get(int id, const char *custom, hub75_layout_t *out);
/* Tikrina naudotojo išdėstymo tekstą: 4 panelės, stulpelis ir eilutė 0…1, kiekviena vieta tik vieną kartą */
bool hub75_layout_parse(const char *s, hub75_tile_t tiles[4]);
const char *hub75_layout_name(int id);
