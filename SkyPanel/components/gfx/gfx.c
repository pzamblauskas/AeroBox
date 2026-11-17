/* 2D grafika ir tekstas RGB565 kadro buferyje (aprašas – gfx.h). */
#include "gfx.h"
#include "font.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void gfx_init(gfx_t *g, uint16_t w, uint16_t h)
{
    g->w = w;
    g->h = h;
    g->buf = calloc((size_t)w * h, sizeof(uint16_t));
}

void gfx_clear(gfx_t *g)
{
    memset(g->buf, 0, (size_t)g->w * g->h * sizeof(uint16_t));
}

static int s_clip_x0, s_clip_y0, s_clip_x1 = 0x7FFF, s_clip_y1 = 0x7FFF;

void gfx_clip(int x, int y, int w, int h)
{
    s_clip_x0 = x;
    s_clip_y0 = y;
    s_clip_x1 = x + w - 1;
    s_clip_y1 = y + h - 1;
}

void gfx_clip_off(void)
{
    s_clip_x0 = s_clip_y0 = 0;
    s_clip_x1 = s_clip_y1 = 0x7FFF;
}

static inline bool visible(const gfx_t *g, int x, int y)
{
    return (unsigned)x < (unsigned)g->w && (unsigned)y < (unsigned)g->h && x >= s_clip_x0 && x <= s_clip_x1 &&
           y >= s_clip_y0 && y <= s_clip_y1;
}

void gfx_pixel(gfx_t *g, int x, int y, uint16_t c)
{
    if (!visible(g, x, y)) return;
    g->buf[y * g->w + x] = c;
}

void gfx_blend(gfx_t *g, int x, int y, uint16_t c, uint8_t a)
{
    if (!a || !visible(g, x, y)) return;
    uint16_t *p = &g->buf[y * g->w + x];
    if (a >= 15) {
        *p = c;
        return;
    }
    const int dr = *p >> 11, dg = (*p >> 5) & 63, db = *p & 31;
    const int r = dr + ((int)(c >> 11) - dr) * a / 15;
    const int gg = dg + ((int)((c >> 5) & 63) - dg) * a / 15;
    const int b = db + ((int)(c & 31) - db) * a / 15;
    *p = (uint16_t)(r << 11 | gg << 5 | b);
}

void gfx_hline(gfx_t *g, int x, int y, int w, uint16_t c)
{
    for (int i = 0; i < w; i++) gfx_pixel(g, x + i, y, c);
}

void gfx_vline(gfx_t *g, int x, int y, int h, uint16_t c)
{
    for (int i = 0; i < h; i++) gfx_pixel(g, x, y + i, c);
}

void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(g, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Kiekvienam taškui imama 4 × 4 bandinių; kokia jų dalis trikampyje, tokia ir spalvos dalis */
void gfx_fill_triangle_aa(gfx_t *g, float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c)
{
    const float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (fabsf(area) < 0.01f) return;
    const float s = area > 0 ? 1.0f : -1.0f;
    const int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
    const int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    for (int py = miny; py <= maxy; py++)
        for (int px = minx; px <= maxx; px++) {
            int n = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    const float x = px + (sx + 0.5f) * 0.25f, y = py + (sy + 0.5f) * 0.25f;
                    if (((x1 - x0) * (y - y0) - (y1 - y0) * (x - x0)) * s >= 0 &&
                        ((x2 - x1) * (y - y1) - (y2 - y1) * (x - x1)) * s >= 0 &&
                        ((x0 - x2) * (y - y2) - (y0 - y2) * (x - x2)) * s >= 0)
                        n++;
                }
            const int a = (n * 15 + 8) / 16;
            if (a >= 3) gfx_blend(g, px, py, c, (uint8_t)a);   /* labai blankus kraštas nepiešiamas (kitaip aplink silpnai šviestų) */
        }
}

void gfx_rect(gfx_t *g, int x, int y, int w, int h, uint16_t c)
{
    gfx_hline(g, x, y, w, c);
    gfx_hline(g, x, y + h - 1, w, c);
    gfx_vline(g, x, y, h, c);
    gfx_vline(g, x + w - 1, y, h, c);
}

void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, uint16_t c)
{
    for (int j = 0; j < h; j++) gfx_hline(g, x, y + j, w, c);
}

void gfx_circle(gfx_t *g, int cx, int cy, int r, uint16_t c)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_pixel(g, cx + x, cy + y, c); gfx_pixel(g, cx - x, cy + y, c);
        gfx_pixel(g, cx + x, cy - y, c); gfx_pixel(g, cx - x, cy - y, c);
        gfx_pixel(g, cx + y, cy + x, c); gfx_pixel(g, cx - y, cy + x, c);
        gfx_pixel(g, cx + y, cy - x, c); gfx_pixel(g, cx - y, cy - x, c);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

uint16_t gfx_scale(uint16_t c, uint8_t level)
{
    uint32_t r = (c >> 11) & 0x1F, gg = (c >> 5) & 0x3F, b = c & 0x1F;
    r = r * level / 255;
    gg = gg * level / 255;
    b = b * level / 255;
    return (uint16_t)((r << 11) | (gg << 5) | b);
}

/* ---------- Tekstas ---------- */

typedef struct {
    uint8_t        w, h, adv;      /* simbolio plotis, aukštis ir žingsnis */
    const uint8_t *rows;           /* 65 simboliai po h eilučių */
} font_desc_t;

/* Taškiniai šriftai; glotniajam – 5×7, kai tokio dydžio glotnaus nėra */
static const font_desc_t FONTS[GFX_FONT_COUNT] = {
    { 3, 5, 4, &g_font3x5[0][0] },
    { 4, 5, 5, &g_font4x5[0][0] },
    { 5, 7, 6, &g_font5x7a[0][0] },
    { 5, 7, 6, &g_font5x7a[0][0] },
    { 5, 7, 6, &g_font5x7a[0][0] },
};
static int s_font = GFX_FONT_SMOOTH;

void gfx_set_font(int id)
{
    s_font = id >= 0 && id < GFX_FONT_COUNT ? id : GFX_FONT_SMALL;
}

int gfx_font(void)
{
    return s_font;
}

int gfx_font_h(void)
{
    return FONTS[s_font].h;
}

/* Mažosios raidės rodomos didžiosiomis; "\177" – laipsnio ženklas (64-asis simbolis) */
static int glyph_index(char ch)
{
    unsigned char u = (unsigned char)ch;
    if (u >= 'a' && u <= 'z') u = (unsigned char)(u - 'a' + 'A');
    if (u == 0x7F) return 64;
    if (u < 32 || u > 95) u = '?';
    return u - 32;
}

/* Glotnus šriftas sc kartų didesniam tekstui; NULL – piešiama taškiniu */
static const aa_font_t *aa_for(int sc)
{
    if (s_font == GFX_FONT_CLASSIC) return NULL;
    if (s_font == GFX_FONT_MONO) return sc <= 1 ? &g_jb7 : sc == 2 ? &g_jb14 : NULL;
    if (sc <= 1) return s_font == GFX_FONT_SMOOTH ? &g_aa7 : NULL;
    const int cap = FONTS[s_font].h * sc;
    if (cap == 10) return &g_aa10;
    if (cap == 14) return &g_aa14;
    return NULL;
}

/* Išvykimų lentos efektas: kiekviena raidė iš pradžių keičiasi (kas 70 ms kita) ir sustoja savo laiku –
 * esančios kairiau sustoja anksčiau. Tarpai lieka tarpais. */
static uint32_t s_flap_t, s_flap_dur;

void gfx_flap(uint32_t elapsed_ms, uint32_t dur_ms)
{
    s_flap_t = elapsed_ms;
    s_flap_dur = dur_ms;
}

void gfx_flap_off(void)
{
    s_flap_dur = 0;
}

static char flap_char(char ch, int x, int y, const gfx_t *g)
{
    if (!s_flap_dur || s_flap_t >= s_flap_dur || ch == ' ') return ch;
    const int px = x < 0 ? 0 : x >= g->w ? g->w - 1 : x;
    const uint32_t settle = s_flap_dur * (uint32_t)(35 + 60 * px / g->w) / 100 + (uint32_t)(y > 0 ? y : 0);
    if (s_flap_t >= settle) return ch;
    static const char SET[] = "ABCDEFGHIJKLMNOPRSTUVWXYZ0123456789";
    const uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (s_flap_t / 70 + 1) * 83492791u;
    return SET[(h >> 7) % (sizeof SET - 1)];
}

static int aa_draw(gfx_t *g, int x, int y, const char *s, uint16_t c, const aa_font_t *f)
{
    for (; *s; s++) {
        const int adv = f->glyphs[glyph_index(*s)].adv;      /* vieta skaičiuojama pagal tikrą raidę, kad tekstas nesislinktų */
        const aa_glyph_t *gl = &f->glyphs[glyph_index(flap_char(*s, x, y, g))];
        const uint8_t *p = f->data + gl->off;
        const int bpr = (gl->w + 1) / 2;
        for (int r = 0; r < f->rows; r++, p += bpr)
            for (int i = 0; i < gl->w; i++) {
                const uint8_t a = i & 1 ? p[i >> 1] & 0x0F : p[i >> 1] >> 4;
                if (a) gfx_blend(g, x + gl->x + i, y - f->top + r, c, a);
            }
        x += adv;
    }
    return x;
}

/* Plotis be tarpo po paskutinio simbolio */
static int aa_width(const char *s, const aa_font_t *f)
{
    int w = 0, tail = 0;
    for (; *s; s++) {
        const aa_glyph_t *gl = &f->glyphs[glyph_index(*s)];
        w += gl->adv;
        tail = gl->adv - (gl->x + gl->w);
    }
    return w - tail;
}

static int pixel_draw(gfx_t *g, int x, int y, const char *s, uint16_t c, const font_desc_t *f, int sc)
{
    for (; *s; s++) {
        const uint8_t *gl = f->rows + glyph_index(flap_char(*s, x, y, g)) * f->h;
        for (int r = 0; r < f->h; r++)
            for (int col = 0; col < f->w; col++)
                if (gl[r] & (1u << (f->w - 1 - col))) gfx_fill_rect(g, x + col * sc, y + r * sc, sc, sc, c);
        x += f->adv * sc;
    }
    return x;
}

static int pixel_width(const char *s, const font_desc_t *f, int sc)
{
    int n = (int)strlen(s);
    return n ? (n * f->adv - (f->adv - f->w)) * sc : 0;
}

/* 5×7 su mažosiomis: didžiosios, skaitmenys ir ženklai – iš g_font5x7a, mažosios – iš g_font5x7l (9 eilučių).
 * Tušti stulpeliai nukerpami („i“ ir „:“ siauresni), tarp simbolių 1 taškas, tarpas – 3 taškai */
static const uint8_t *lc_glyph(char ch, int *h, int *lo, int *hi)
{
    const uint8_t *gl;
    if (ch >= 'a' && ch <= 'z') {
        gl = g_font5x7l[ch - 'a'];
        *h = 9;
    } else {
        gl = g_font5x7a[glyph_index(ch)];
        *h = 7;
    }
    uint8_t m = 0;
    for (int r = 0; r < *h; r++) m |= gl[r];
    *lo = 0;
    *hi = -1;
    if (m) {
        while (!(m & (0x10 >> *lo))) (*lo)++;
        *hi = 4;
        while (!(m & (0x10 >> *hi))) (*hi)--;
    }
    return gl;
}

int gfx_text_lc(gfx_t *g, int x, int y, const char *s, uint16_t c)
{
    for (; *s; s++) {
        int h, lo, hi;
        const uint8_t *gl = lc_glyph(*s, &h, &lo, &hi);
        if (hi < lo) {
            x += 3;
            continue;
        }
        for (int r = 0; r < h; r++)
            for (int col = lo; col <= hi; col++)
                if (gl[r] & (0x10 >> col)) gfx_pixel(g, x + col - lo, y + r, c);
        x += hi - lo + 2;
    }
    return x;
}

int gfx_text_lc_width(const char *s)
{
    int w = 0;
    for (; *s; s++) {
        int h, lo, hi;
        lc_glyph(*s, &h, &lo, &hi);
        w += hi < lo ? 3 : hi - lo + 2;
    }
    return w ? w - 1 : 0;
}

int gfx_text_s(gfx_t *g, int x, int y, const char *s, uint16_t c, int sc)
{
    const aa_font_t *aa = aa_for(sc);
    return aa ? aa_draw(g, x, y, s, c, aa) : pixel_draw(g, x, y, s, c, &FONTS[s_font], sc);
}

int gfx_text_width_s(const char *s, int sc)
{
    const aa_font_t *aa = aa_for(sc);
    return aa ? aa_width(s, aa) : pixel_width(s, &FONTS[s_font], sc);
}

int gfx_text_center_fit(gfx_t *g, int y, const char *s, uint16_t c, int max_sc)
{
    for (int sc = max_sc; sc >= 1; sc--) {
        if (gfx_text_width_s(s, sc) <= g->w - 2) {
            gfx_text_center_s(g, y, s, c, sc);
            return sc;
        }
    }
    const int keep = s_font;
    s_font = GFX_FONT_SMALL;
    gfx_text_center_s(g, y, s, c, 1);
    s_font = keep;
    return 1;
}

int gfx_text_right_s(gfx_t *g, int right_x, int y, const char *s, uint16_t c, int sc)
{
    return gfx_text_s(g, right_x - gfx_text_width_s(s, sc) + 1, y, s, c, sc);
}

int gfx_text_center_s(gfx_t *g, int y, const char *s, uint16_t c, int sc)
{
    return gfx_text_s(g, (g->w - gfx_text_width_s(s, sc)) / 2, y, s, c, sc);
}

int gfx_text(gfx_t *g, int x, int y, const char *s, uint16_t c)
{
    return gfx_text_s(g, x, y, s, c, 1);
}

int gfx_text_max(gfx_t *g, int x, int y, const char *s, uint16_t c, int max_w)
{
    char buf[40];
    strlcpy(buf, s, sizeof buf);
    size_t n = strlen(buf);
    while (n && gfx_text_width(buf) > max_w) buf[--n] = 0;
    return gfx_text(g, x, y, buf, c);
}

int gfx_text_width(const char *s)
{
    return gfx_text_width_s(s, 1);
}

int gfx_text_right(gfx_t *g, int right_x, int y, const char *s, uint16_t c)
{
    return gfx_text(g, right_x - gfx_text_width(s) + 1, y, s, c);
}

int gfx_text_center(gfx_t *g, int y, const char *s, uint16_t c)
{
    return gfx_text(g, (g->w - gfx_text_width(s)) / 2, y, s, c);
}

static const aa_font_t *big_font(int scale)
{
    if (s_font == GFX_FONT_CLASSIC) return NULL;
    if (s_font == GFX_FONT_MONO) return scale >= 3 ? &g_jb21 : &g_jb14;
    return scale >= 3 ? &g_aa21 : &g_aa14;
}

int gfx_text_big(gfx_t *g, int x, int y, const char *s, uint16_t c, int scale)
{
    const aa_font_t *f = big_font(scale);
    return f ? aa_draw(g, x, y, s, c, f) : pixel_draw(g, x, y, s, c, &FONTS[GFX_FONT_CLASSIC], scale);
}

int gfx_text_big_width(const char *s, int scale)
{
    const aa_font_t *f = big_font(scale);
    return f ? aa_width(s, f) : pixel_width(s, &FONTS[GFX_FONT_CLASSIC], scale);
}
