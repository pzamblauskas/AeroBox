/* Spalvų temos: kiekvienai paskirčiai (color_role_t) – po spalvą. Valdymo puslapyje temos spalvas galima keisti po vieną. */
#include "mode.h"
#include <string.h>

static const uint16_t THEMES[THEME_COUNT][COL_COUNT] = {
    /*                 skrydis               tipas                 aukštis               kursas
     *                 tekstas               kyla                  leidžiasi             eiga
     *                 laikrodis             data                  radaras (žemėlapio spalvas nustato theme_colors) */
    [THEME_DEFAULT] = { RGB565(255, 140, 0),   RGB565(0, 190, 255),   RGB565(0, 255, 60),    RGB565(0, 190, 255),
                        RGB565(255, 255, 255), RGB565(0, 255, 60),    RGB565(255, 20, 20),   RGB565(40, 80, 255),
                        RGB565(255, 140, 0),   RGB565(0, 120, 160),   RGB565(0, 255, 105) },
    /* Gintarinė – kaip seni aviacijos ekranai */
    [THEME_AMBER]   = { RGB565(255, 140, 0),   RGB565(255, 175, 70),  RGB565(255, 200, 90),  RGB565(255, 175, 70),
                        RGB565(255, 215, 160), RGB565(255, 225, 0),   RGB565(255, 70, 0),    RGB565(255, 110, 0),
                        RGB565(255, 140, 0),   RGB565(190, 95, 0),    RGB565(255, 130, 0) },
    /* Ledinė – melsva ir balta */
    [THEME_ICE]     = { RGB565(90, 200, 255),  RGB565(160, 175, 255), RGB565(130, 225, 255), RGB565(160, 175, 255),
                        RGB565(230, 240, 255), RGB565(0, 235, 200),   RGB565(255, 90, 150),  RGB565(60, 120, 255),
                        RGB565(90, 200, 255),  RGB565(70, 120, 210),  RGB565(0, 150, 255) },
    /* Žalia – radaro terminalas */
    [THEME_GREEN]   = { RGB565(80, 255, 80),   RGB565(0, 210, 130),   RGB565(160, 255, 160), RGB565(0, 210, 130),
                        RGB565(210, 255, 210), RGB565(170, 255, 0),   RGB565(255, 150, 0),   RGB565(0, 190, 70),
                        RGB565(80, 255, 80),   RGB565(0, 150, 70),    RGB565(0, 255, 90) },
    /* Balta – didžiausias kontrastas */
    [THEME_WHITE]   = { RGB565(255, 255, 255), RGB565(175, 175, 180), RGB565(255, 255, 255), RGB565(175, 175, 180),
                        RGB565(255, 255, 255), RGB565(255, 255, 255), RGB565(255, 255, 255), RGB565(160, 160, 165),
                        RGB565(255, 255, 255), RGB565(140, 140, 145), RGB565(150, 150, 155) },
    /* Raudona naktinė – neakina tamsoje */
    [THEME_RED]     = { RGB565(255, 30, 0),    RGB565(210, 25, 0),    RGB565(255, 70, 40),   RGB565(210, 25, 0),
                        RGB565(255, 90, 70),   RGB565(255, 110, 50),  RGB565(170, 0, 0),     RGB565(160, 0, 0),
                        RGB565(255, 30, 0),    RGB565(150, 10, 0),    RGB565(200, 0, 0) },
};

void theme_colors(int theme, int win, uint16_t out[COL_COUNT])
{
    const int t = theme >= 0 && theme < THEME_COUNT ? theme : THEME_DEFAULT;
    memcpy(out, THEMES[t], sizeof THEMES[0]);
    out[COL_MAP_LAND] = gfx_scale(out[COL_RADAR], 22);    /* žemėlapis: sausuma – blanki radaro spalva, */
    out[COL_MAP_WATER] = 0;                               /* vanduo nešviečia */
    out[COL_MAP_BORDER] = t == THEME_RED ? RGB565(70, 12, 8) : RGB565(50, 50, 58);   /* naktinėje – be baltos */
    if (win == WIN_MAP && t == THEME_DEFAULT) {           /* maršrutų žemėlapio numatytosios spalvos */
        out[COL_MAP_LAND] = RGB565(7, 70, 32);
        out[COL_MAP_BORDER] = RGB565(48, 162, 16);
        out[COL_MAP_WATER] = RGB565(6, 41, 71);
        out[COL_RADAR] = RGB565(22, 18, 253);             /* žemėlapio lange COL_RADAR – kranto linija */
    }
}

const char *theme_name(int theme)
{
    static const char *const names[THEME_COUNT] = { "default", "amber", "ice", "green", "white", "red" };
    return theme >= 0 && theme < THEME_COUNT ? names[theme] : "custom";
}
