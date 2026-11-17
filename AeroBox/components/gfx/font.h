#pragma once
#include <stdint.h>

/* Taškiniai šriftai: ASCII 32…95 ir laipsnio ženklas (64-asis simbolis, C eilutėse "\177").
 * 3×5: bitai 4 – kairė, 2 – vidurys, 1 – dešinė; 4×5 (bitas 0x8 – kairė) ir 5×7 (bitas 0x10 – kairė) generuoja fontgen.py */
extern const uint8_t g_font3x5[65][5];
extern const uint8_t g_font4x5[65][5];
extern const uint8_t g_font5x7a[65][7];
/* 5×7 mažosios a…z su nuleidimais: 9 eilutės (7 kaip didžiųjų ir 2 po eilute raidėms g, j, p, q, y) – FlightWall variantui */
extern const uint8_t g_font5x7l[26][9];

/* Glotnūs (anti-aliased) proporcingi šriftai iš DejaVu Sans – generuoja fontgen_aa.py.
 * Tie patys 65 simboliai; 4 bitai taškui, eilutė užima (w + 1) / 2 baitų. */
typedef struct {
    uint16_t off;      /* pradžia data[] masyve */
    uint8_t  w;        /* piešinio plotis */
    int8_t   x;        /* piešinio poslinkis nuo rašymo vietos */
    uint8_t  adv;      /* žingsnis iki kito simbolio */
} aa_glyph_t;

typedef struct {
    uint8_t           cap;    /* didžiosios raidės aukštis */
    uint8_t           top;    /* kiek eilučių piešinys prasideda virš didžiosios raidės */
    uint8_t           rows;   /* piešinio eilučių skaičius (top + cap + po linija) */
    const aa_glyph_t *glyphs; /* 65 */
    const uint8_t    *data;
} aa_font_t;

extern const aa_font_t g_aa7;   /* DejaVu Sans, didžiosios raidės aukštis 7 */
extern const aa_font_t g_aa10;  /* DejaVu Sans Bold, 10 */
extern const aa_font_t g_aa14;  /* DejaVu Sans Bold, 14 */
extern const aa_font_t g_aa21;  /* DejaVu Sans Bold, 21 – tik skaitmenys, „:“, „-“, „.“ ir laipsnis */
extern const aa_font_t g_jb7;   /* JetBrains Mono SemiBold, 7 – 6 taškai simboliui (pritaikyta prie tinklelio) */
extern const aa_font_t g_jb14;  /* JetBrains Mono Bold, 14 */
extern const aa_font_t g_jb21;  /* JetBrains Mono Bold, 21 – tik skaitmenys */
