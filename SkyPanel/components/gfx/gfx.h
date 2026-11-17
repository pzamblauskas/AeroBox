/* Paprasta 2D grafika RGB565 kadro buferyje: taškai, linijos, figūros ir tekstas keliais šriftais.
 * Nenaudoja FreeRTOS, todėl tą patį kodą galima kompiliuoti ir kompiuteryje (peržiūrai). */
#pragma once
#include <stdint.h>

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define C_BLACK   RGB565(0, 0, 0)
#define C_WHITE   RGB565(255, 255, 255)
#define C_AMBER   RGB565(255, 140, 0)
#define C_CYAN    RGB565(0, 190, 255)
#define C_GREEN   RGB565(0, 255, 60)
#define C_RED     RGB565(255, 20, 20)
#define C_BLUE    RGB565(40, 80, 255)
#define C_GREY    RGB565(90, 90, 90)
#define C_DIM     RGB565(40, 40, 40)

/* Laipsnio ženklas tekste: "13" GFX_DEG "C" */
#define GFX_DEG   "\177"

typedef struct {
    uint16_t w, h;
    uint16_t *buf;
} gfx_t;

void gfx_init(gfx_t *g, uint16_t w, uint16_t h);
void gfx_clear(gfx_t *g);
/* Piešti tik stačiakampyje (pvz., radaro kvadrate); gfx_clip_off() – vėl visur */
void gfx_clip(int x, int y, int w, int h);
void gfx_clip_off(void);
/* Išvykimų lentos efektas tekstui: elapsed_ms – laikas nuo pradžios, dur_ms – trukmė. Kol efektas veikia,
 * kiekvienas simbolis keičiasi atsitiktinėmis raidėmis ir sustoja (iš kairės į dešinę). gfx_flap_off() – įprastas tekstas */
void gfx_flap(uint32_t elapsed_ms, uint32_t dur_ms);
void gfx_flap_off(void);
void gfx_pixel(gfx_t *g, int x, int y, uint16_t c);
/* Taškas su permatomumu: a = 0 (nieko) … 15 (visiškai spalva c) – glotniems kraštams */
void gfx_blend(gfx_t *g, int x, int y, uint16_t c, uint8_t a);
void gfx_hline(gfx_t *g, int x, int y, int w, uint16_t c);
void gfx_vline(gfx_t *g, int x, int y, int h, uint16_t c);
void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, uint16_t c);
/* Glotnus užpildytas trikampis (taškas (x, y) dengia sritį [x, x+1) × [y, y+1)) */
void gfx_fill_triangle_aa(gfx_t *g, float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c);
void gfx_rect(gfx_t *g, int x, int y, int w, int h, uint16_t c);
void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, uint16_t c);
void gfx_circle(gfx_t *g, int cx, int cy, int r, uint16_t c);

/* Spalvos šviesumo mastelis: 0 – juoda, 255 – nepakitusi */
uint16_t gfx_scale(uint16_t c, uint8_t level);

/* Teksto šriftas (pasirenkamas valdymo puslapyje). Visos gfx_text* funkcijos naudoja dabartinį šriftą. */
typedef enum {
    GFX_FONT_SMALL = 0,    /* 3×5 taškinis, 4 taškai simboliui – telpa daugiausia teksto */
    GFX_FONT_WIDE,         /* 4×5 taškinis, 5 taškai – aiškesnės raidės, tas pats aukštis */
    GFX_FONT_SMOOTH,       /* glotnus proporcingas, aukštis 7 (128×64 ekranui) */
    GFX_FONT_CLASSIC,      /* 5×7 taškinis, aukštis 7 – ir dideli užrašai taškiniai */
    GFX_FONT_MONO,         /* glotnus vienodo pločio (JetBrains Mono), aukštis 7 – palyginimui su SMOOTH */
    GFX_FONT_COUNT
} gfx_font_id_t;

void gfx_set_font(int id);
int  gfx_font(void);
int  gfx_font_h(void);     /* didžiosios raidės aukštis: 5 arba 7 */

/* Tekstas dabartiniu šriftu (y – didžiųjų raidžių viršus). Grąžina x poziciją po teksto. */
int gfx_text(gfx_t *g, int x, int y, const char *s, uint16_t c);
/* 5×7 taškinis su mažosiomis raidėmis (FlightWall stilius), proporcingas; nuo dabartinio šrifto nepriklauso.
 * Mažųjų g, j, p, q, y nuleidimai užima 2 eilutes po teksto apačia (y + 7, y + 8) */
int gfx_text_lc(gfx_t *g, int x, int y, const char *s, uint16_t c);
int gfx_text_lc_width(const char *s);
int gfx_text_width(const char *s);
int gfx_text_right(gfx_t *g, int right_x, int y, const char *s, uint16_t c);
int gfx_text_center(gfx_t *g, int y, const char *s, uint16_t c);
/* Tiek simbolių, kiek telpa į max_w taškų (likusieji nukerpami) */
int gfx_text_max(gfx_t *g, int x, int y, const char *s, uint16_t c, int max_w);
/* sc kartų didesnis tekstas (aukštis = gfx_font_h() × sc). Antraštėms naudojamas glotnus šriftas
 * (DejaVu Bold 10 / 14, MONO – JetBrains Mono Bold 14), klasikiniam – taškinis, padidintas sc kartų. */
int gfx_text_s(gfx_t *g, int x, int y, const char *s, uint16_t c, int sc);
int gfx_text_width_s(const char *s, int sc);
int gfx_text_right_s(gfx_t *g, int right_x, int y, const char *s, uint16_t c, int sc);
int gfx_text_center_s(gfx_t *g, int y, const char *s, uint16_t c, int sc);
/* Centruotas tekstas, kuris visada telpa: didžiausias tinkamas mastelis iki max_sc, o jei netelpa ir esant 1 –
 * laikinai naudojamas mažasis 3×5 šriftas. Grąžina panaudotą mastelį. */
int gfx_text_center_fit(gfx_t *g, int y, const char *s, uint16_t c, int max_sc);

/* Laikrodžio skaitmenys: scale 2 – aukštis 14, 3 – aukštis 21 (glotnus DejaVu Bold;
 * klasikiniam šriftui – 5×7 taškinis, padidintas) */
int gfx_text_big(gfx_t *g, int x, int y, const char *s, uint16_t c, int scale);
int gfx_text_big_width(const char *s, int scale);
