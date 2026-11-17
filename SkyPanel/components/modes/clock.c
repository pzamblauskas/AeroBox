/* Laikrodis (laikas per SNTP): rodomas, kai danguje tuščia, arba pasirinkus jį rankiniu būdu.
 * 128×64: HH:MM, data, orai (piktograma, temperatūra, aprašymas), lėktuvų skaičius arba būsena.
 * 64×32:  HH:MM, data, apačioje paeiliui orai ir lėktuvų skaičius.
 * 64×64:  padalinto ekrano langas – HH:MM, data, orai ir lėktuvų skaičius vienas po kito. Spalvos – pagal temą. */
#include "mode.h"
#include "mode_util.h"
#include "app_status.h"
#include "weather.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SWAP_MS 4000                      /* 64×32: orai ir lėktuvai paeiliui */

static const aircraft_snapshot_t *s_snap;
static uint32_t s_ms;

static void clock_enter(void) { }

static void clock_update(const aircraft_snapshot_t *s, uint32_t ms)
{
    s_snap = s;
    s_ms = ms;
}

/* ---------- Orų piktogramos 9×7: didžioji raidė – visa spalva, mažoji – blankiai ---------- */
typedef struct {
    char     ch;
    uint16_t c;
} ink_t;

static const ink_t INKS[] = {
    { 'y', RGB565(255, 200, 40) }, { 'w', RGB565(200, 205, 215) }, { 'g', RGB565(120, 125, 135) },
    { 'b', RGB565(70, 150, 255) }, { 's', RGB565(235, 245, 255) }, { 'l', RGB565(255, 230, 60) },
    { 'm', RGB565(235, 225, 170) }, { 'f', RGB565(150, 155, 165) },
};

static const char *const ICONS[][7] = {
    [WX_CLEAR]   = { "....Y....", "..y...y..", "...YYY...", ".Y.YYY.Y.", "...YYY...", "..y...y..", "....Y...." },
    [WX_PARTLY]  = { ".y..Y....", "...YYY...", ".YYYYYww.", "..YYWWWWw", "..WWWWWWW", "..WWWWWWW", "...WWWWW." },
    [WX_CLOUDY]  = { ".........", "....ggg..", "..gGGGGg.", ".GGGGGGGg", "GGGGGGGGG", ".GGGGGGG.", "........." },
    [WX_FOG]     = { ".........", ".FFFFFFF.", ".........", "FFFFFFFFF", ".........", ".FFFFFFF.", "........." },
    [WX_DRIZZLE] = { "....ggg..", "..gGGGGg.", ".GGGGGGGg", "GGGGGGGGG", ".GGGGGGG.", "..B...B..", "....B...." },
    [WX_RAIN]    = { "....ggg..", "..gGGGGg.", ".GGGGGGGg", "GGGGGGGGG", ".GGGGGGG.", ".B..B..B.", "B..B..B.." },
    [WX_SNOW]    = { "....ggg..", "..gGGGGg.", ".GGGGGGGg", "GGGGGGGGG", ".GGGGGGG.", "S..S..S..", ".S..S..S." },
    [WX_STORM]   = { "....ggg..", "..gGGGGg.", ".GGGGGGGg", "GGGGGGGGG", ".GGGLGGG.", "...L.....", "..L......" },
};
static const char *const MOON[7] = { "....MMm..", "..MMm....", ".MMM.....", ".MMM.....", ".MMM.....", "..MMm....",
                                     "....MMm.." };

#define ICON_W 9
#define ICON_H 7

static void draw_icon(gfx_t *g, int x, int y, const weather_t *w)
{
    const wx_kind_t k = weather_kind(w->code);
    const char *const *rows = k == WX_CLEAR && !w->is_day ? MOON : ICONS[k];
    for (int r = 0; r < ICON_H; r++)
        for (int c = 0; c < ICON_W && rows[r][c]; c++) {
            const char ch = rows[r][c];
            if (ch == '.') continue;
            const char lo = (char)(ch | 0x20);
            for (size_t i = 0; i < sizeof INKS / sizeof INKS[0]; i++)
                if (INKS[i].ch == lo) gfx_blend(g, x + c, y + r, INKS[i].c, ch == lo ? 6 : 15);
        }
}

/* „13°C CLOUDY“ (arba trumpiau, jei netelpa); false – orų nėra */
static bool weather_line(char *out, size_t n, weather_t *w, int max_w, bool with_icon)
{
    if (!weather_get(w)) return false;
    char t[12];
    fmt_temp(t, sizeof t, w->temp_c);
    const int icon = with_icon ? ICON_W + 3 : 0;
    snprintf(out, n, "%s %s", t, weather_text(w->code));
    if (gfx_text_width(out) + icon > max_w) snprintf(out, n, "%s", t);
    return true;
}

/* Lėktuvų eilutė: Wi-Fi ar duomenų būsena arba kiek lėktuvų yra spinduliu. true – būsenos problema */
static bool flights_line(char *out, size_t n, uint16_t *col, bool short_text, int max_w)
{
    char rg[12];
    fmt_range(rg, sizeof rg);
    const int cnt = s_snap ? s_snap->count : 0;
    if (!g_status.wifi) {
        uint8_t r = g_status.wifi_reason;
        snprintf(out, n, "%s", sky_wifi_lcd_text(r));
        *col = r ? C_RED : C_GREY;
        return true;
    }
    if (g_status.limited_until_ms && (int32_t)(g_status.limited_until_ms - sky_now_ms()) > 0) {
        snprintf(out, n, "DATA SOURCE BUSY");    /* per daug užklausų – laukiama tiek, kiek prašė serveris */
        *col = C_AMBER;
        return true;
    }
    if (!g_status.data_ok) {
        snprintf(out, n, "NO FLIGHT DATA");
        *col = C_AMBER;
        return true;
    }
    /* Rodomas ilgiausias telpantis variantas: „3 FLIGHTS WITHIN 20KM“ → „3 FLIGHTS IN 20KM“ → „3 FLIGHTS“ */
    char num[12];
    if (cnt > 0) snprintf(num, sizeof num, "%d", cnt);
    else strcpy(num, "NO");
    const char *s = cnt == 1 ? "" : "S";
    snprintf(out, n, "%s FLIGHT%s WITHIN %s", num, s, rg);
    if (short_text || gfx_text_width(out) > max_w) snprintf(out, n, "%s FLIGHT%s IN %s", num, s, rg);
    if (short_text || gfx_text_width(out) > max_w) snprintf(out, n, "%s FLIGHT%s", num, s);
    *col = cnt > 0 ? COL(COL_RADAR) : RGB565(130, 130, 135);
    return false;
}

/* Valandos ir minutės; 12 valandų formatu – dar ir AM/PM */
static bool time_text(char *hh, char *mm, const char **ampm, struct tm *tm)
{
    *ampm = NULL;
    if (!sky_time_valid()) {
        strcpy(hh, "--");
        strcpy(mm, "--");
        return false;
    }
    time_t t = time(NULL);
    localtime_r(&t, tm);
    int h = tm->tm_hour;
    if (g_mode_prefs.clock12) {
        *ampm = h < 12 ? "AM" : "PM";
        h = h % 12 ? h % 12 : 12;
        sprintf(hh, "%d", h);
    } else {
        sprintf(hh, "%02d", h);
    }
    sprintf(mm, "%02d", tm->tm_min);
    return true;
}

static const char *const WDAY[7] = { "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" };
static const char *const MON[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                     "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

/* Rodoma ilgiausia telpanti data: „SUNDAY 25 OCT 2026“ → „SUN 25 OCT 2026“ → „SUN 25 OCT“ */
static void date_text(char *out, size_t n, const struct tm *tm, int max_w)
{
    char wd[4] = { 0 };
    memcpy(wd, WDAY[tm->tm_wday], 3);
    snprintf(out, n, "%s %d %s %d", WDAY[tm->tm_wday], tm->tm_mday, MON[tm->tm_mon], tm->tm_year + 1900);
    if (gfx_text_width(out) > max_w)
        snprintf(out, n, "%s %d %s %d", wd, tm->tm_mday, MON[tm->tm_mon], tm->tm_year + 1900);
    if (gfx_text_width(out) > max_w) snprintf(out, n, "%s %d %s", wd, tm->tm_mday, MON[tm->tm_mon]);
}

/* Dvitaškis: du skaitmenų linijos storio taškai (scale × scale), o ne šrifto dvigubi kvadratai. Per 2 s jis tolygiai
 * prigęsta ir vėl pašviesėja (nemirksi). Taškiniu šriftu – kvadratiniai kaip skaitmenų taškai, glotniu – apvalinti. */
static void draw_colon(gfx_t *g, int x, int y, int scale, uint16_t c)
{
    const int h = 7 * scale, d = scale, top = h * 2 / 7;
    const bool round = gfx_font() != GFX_FONT_CLASSIC && d >= 3;
    const float k = 0.65f + 0.35f * cosf((float)(s_ms % 2000) * (6.2831853f / 2000.0f));
    const uint16_t cc = gfx_scale(c, (uint8_t)(255.0f * k));
    for (int i = 0; i < 2; i++) {
        const int yy = y + (i ? h - top - d : top);
        for (int r = 0; r < d; r++)
            for (int q = 0; q < d; q++) {
                const bool corner = round && (r == 0 || r == d - 1) && (q == 0 || q == d - 1);
                gfx_blend(g, x + q, yy + r, cc, corner ? 6 : 15);
            }
    }
}

/* Trys eilutės (data, orai, lėktuvai) po laiku: tolygiai tarp top ir apačios h */
static void three_rows(int top, int h, int fh, int y[3])
{
    const float gp = (float)(h - top - 3 * fh) / 4.0f;
    for (int i = 0; i < 3; i++) y[i] = top + (int)(gp * (float)(i + 1) + (float)(fh * i) + 0.5f);
}

/* HH:MM per vidurį */
static void draw_time(gfx_t *g, int y, int scale, const char *hh, const char *mm, const char *ampm)
{
    const uint16_t c = COL(COL_CLOCK);
    const int gap = scale + 2;                       /* tarpas abipus dvitaškio */
    const int keep = gfx_font();
    gfx_set_font(GFX_FONT_SMALL);
    const int ap_w = ampm ? gfx_text_width(ampm) + 2 : 0;
    gfx_set_font(keep);
    const int hw = gfx_text_big_width(hh, scale), cw = scale;
    const int mw = gfx_text_big_width(mm, scale);
    int x = (g->w - (hw + gap + cw + gap + mw + ap_w)) / 2;
    gfx_text_big(g, x, y, hh, c, scale);
    x += hw + gap;
    draw_colon(g, x, y, scale, c);
    x += cw + gap;
    gfx_text_big(g, x, y, mm, c, scale);
    if (ampm) {
        gfx_set_font(GFX_FONT_SMALL);
        gfx_text(g, x + mw + 2, y + 1, ampm, gfx_scale(c, 160));
        gfx_set_font(keep);
    }
}

static void clock_draw(gfx_t *g)
{
    mode_win(WIN_CLOCK);
    const bool big = g->w >= 128 && g->h >= 64;
    char hh[4], mm[4], date[40] = "", st[32], wx[32];
    const char *ampm;
    struct tm tm;
    const bool valid = time_text(hh, mm, &ampm, &tm);
    const uint16_t date_col = COL(COL_DATE), wx_col = gfx_scale(COL(COL_TEXT), 210);
    uint16_t stc;
    weather_t w;

    if (big) {                                       /* laikas eilutėse 4…24, po juo tolygiai trys eilutės, kraštuose ≥ 4 taškai */
        int y[3];
        three_rows(25, g->h, gfx_font_h(), y);
        draw_time(g, 4, 3, hh, mm, ampm);
        if (valid) date_text(date, sizeof date, &tm, g->w - 8);
        gfx_text_center_fit(g, y[0], date[0] ? date : "SYNCING TIME", date[0] ? date_col : C_GREY, 1);
        if (weather_line(wx, sizeof wx, &w, g->w - 8, true)) {
            const int tw = gfx_text_width(wx), x = (g->w - tw - ICON_W - 3) / 2;
            draw_icon(g, x, y[1] - (ICON_H - gfx_font_h()) / 2, &w);
            gfx_text(g, x + ICON_W + 3, y[1], wx, wx_col);
        }
        flights_line(st, sizeof st, &stc, false, g->w - 8);
        gfx_text_center_fit(g, y[2], st, stc, 1);
        return;
    }

    draw_time(g, 1, 2, hh, mm, ampm);
    if (valid) date_text(date, sizeof date, &tm, g->w - 2);
    gfx_text_center_fit(g, 18, date[0] ? date : "SYNCING", date[0] ? date_col : C_GREY, 1);
    /* Apačioje: būsenos problema rodoma visada; kitaip orai ir lėktuvai paeiliui */
    const bool problem = flights_line(st, sizeof st, &stc, true, g->w - 2);
    if (!problem && (s_ms / SWAP_MS) % 2 == 0 && weather_line(wx, sizeof wx, &w, g->w - 2, true)) {
        const int tw = gfx_text_width(wx);
        if (tw + ICON_W + 2 <= g->w) {
            const int x = (g->w - tw - ICON_W - 2) / 2;
            draw_icon(g, x, 25, &w);
            gfx_text(g, x + ICON_W + 2, 26, wx, wx_col);
        } else {
            gfx_text_center_fit(g, 26, wx, wx_col, 1);
        }
        return;
    }
    gfx_text_center_fit(g, 26, st, stc, 1);
}

/* Padalinto ekrano 64×64 langas */
void clock_pane(gfx_t *g)
{
    mode_win(WIN_CLOCK);
    char hh[4], mm[4], date[40] = "", st[32], wx[32];
    const char *ampm;
    struct tm tm;
    const bool valid = time_text(hh, mm, &ampm, &tm);
    uint16_t stc;
    weather_t w;

    int y[3];
    three_rows(18, g->h, gfx_font_h(), y);
    draw_time(g, 4, 2, hh, mm, ampm);
    if (valid) date_text(date, sizeof date, &tm, g->w - 2);
    gfx_text_center_fit(g, y[0], date[0] ? date : "SYNCING", date[0] ? COL(COL_DATE) : C_GREY, 1);
    if (weather_line(wx, sizeof wx, &w, g->w - 2, true)) {
        const int tw = gfx_text_width(wx), x = (g->w - tw - ICON_W - 2) / 2;
        draw_icon(g, x, y[1] - (ICON_H - gfx_font_h()) / 2, &w);
        gfx_text(g, x + ICON_W + 2, y[1], wx, gfx_scale(COL(COL_TEXT), 210));
    }
    flights_line(st, sizeof st, &stc, true, g->w - 2);
    gfx_text_center_fit(g, y[2], st, stc, 1);
}

const display_mode_t MODE_CLOCK = {
    .name = "clock", .enter = clock_enter, .update = clock_update, .draw = clock_draw,
};
