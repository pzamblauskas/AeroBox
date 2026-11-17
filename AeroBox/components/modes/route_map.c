/* Maršruto žemėlapis bet kuriame stačiakampyje iki 128×64: sekamo skrydžio žemėlapis (TRACKING ciklas), artimiausių
 * skrydžių maršrutai (routes_view.c), padalinto ekrano langas, testas.
 * Viso pasaulio sausuma Merkatoriaus projekcija (kaip įprastuose žemėlapiuose); skrydžio kelias
 * išvykimas → lėktuvas → paskirtis didžiaisiais apskritimais (nuskrista dalis ryškesnė; tikri keliai apsuka uždarytą
 * oro erdvę, todėl linija visada eina per lėktuvą); oro uostai, namai ir lėktuvas. Maršrutas, kuris lėktuvui
 * netinka (route_plausible – tikriausiai kito reiso), nepiešiamas.
 * Vienas lėktuvas: vaizdas parenkamas pagal maršrutą; jei visas netelpa – pagal likusią dalį (lėktuvas, paskirtis,
 * namai), o tolimiems – aplink lėktuvą. Lėktuvui priartėjus prie krašto, vaizdas sklandžiai pasislenka, kad lėktuvas
 * būtų centre (mastelis nesikeičia). Veikia ir per 180-ąjį dienovidinį.
 * Keli lėktuvai: visi maršrutai savo spalvomis, vienas išskirtas; jei netelpa – be toliausių oro uostų.
 * Sausuma, ežerai ir valstybių sienos – Natural Earth 1:10M (land.c ir borders.c), ~1 km tikslumu.
 * Sausumos ir sienų kaukės perskaičiuojamos tik pasikeitus vaizdui. */
#include "mode.h"
#include "mode_util.h"
#include "track.h"
#include "app_status.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEG2RAD 0.017453292519943295f
#define MAP_W   128             /* didžiausias vaizdas */
#define MAP_H   64
#define PAN_EDGE 0.12f          /* lėktuvas arčiau krašto nei tokia vaizdo dalis – vaizdas slenkamas… */
#define PAN_MS   900            /* …per tiek ms… */
#define PAN_STEP 90             /* …kas tiek ms perskaičiuojant kaukę */

#define UNIT    0.01f           /* žemėlapio duomenys – 1/100 laipsnio vienetais */

/* Sausuma (land.c): žiedai (ilguma, platuma), LAND_END – kiekvieno pabaiga, LAND_BOX – ribos (vakarai, pietūs,
 * rytai, šiaurė). Užpildoma lyginio–nelyginio taisykle (ežerai – atskiri žiedai sausumos viduje) */
extern const uint8_t LAND_PTS_Z[];          /* taškai suspausti (žr. map_unpack) */
extern const uint32_t LAND_PTS_N;
extern const uint32_t LAND_END[];
extern const int16_t LAND_BOX[][4];
extern const int LAND_RINGS;
extern const int LAND_LAKE0;                /* nuo šio žiedo – ežerai */

/* Valstybių sienos (borders.c): linijos (ilguma, platuma), BORDER_END – kiekvienos pabaiga, BORDER_BOX – ribos */
extern const uint8_t BORDER_PTS_Z[];
extern const uint32_t BORDER_PTS_N;
extern const uint32_t BORDER_END[];
extern const int16_t BORDER_BOX[][4];
extern const int BORDER_LINES;

/* Išpakuoti taškai (PSRAM, ~0,9 MB). Programoje jie laikomi suspausti (~0,45 MB), todėl atnaujinimai mažesni.
 * Išpakuojama vieną kartą, pirmą kartą piešiant žemėlapį (~20 ms). */
static int16_t (*LAND_PTS)[2], (*BORDER_PTS)[2];

/* (dx, dy) int8 nuo ankstesnio taško arba 0x80 ir x, y int16 little endian (absoliučios reikšmės) */
static int16_t (*map_unpack(const uint8_t *z, uint32_t n))[2]
{
    int16_t (*p)[2] = heap_caps_malloc(n * sizeof *p, MALLOC_CAP_SPIRAM);
    if (!p) return NULL;
    int x = 0, y = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (z[0] == 0x80) {
            x = (int16_t)(z[1] | z[2] << 8);
            y = (int16_t)(z[3] | z[4] << 8);
            z += 5;
        } else {
            x += (int8_t)z[0];
            y += (int8_t)z[1];
            z += 2;
        }
        p[i][0] = (int16_t)x;
        p[i][1] = (int16_t)y;
    }
    return p;
}

/* true – sausumos taškai paruošti (sienos – jei pavyko, tada BORDER_PTS != NULL) */
static bool map_data(void)
{
    if (!LAND_PTS) LAND_PTS = map_unpack(LAND_PTS_Z, LAND_PTS_N);
    if (!BORDER_PTS) BORDER_PTS = map_unpack(BORDER_PTS_Z, BORDER_PTS_N);
    return LAND_PTS != NULL;
}

/* Kuriam vaizdui apskaičiuota kaukė: pasikeitus bent vienam laukui – skaičiuojama iš naujo. Keli lėktuvai
 * surikiuojami pagal ICAO adresą, kad pasikeitus jų eilei (pagal atstumą) vaizdas nebūtų skaičiuojamas iš naujo */
typedef struct {
    int      w, h, n;
    float    home_lat, home_lon;
    uint32_t icao[MAP_ROUTES];
    char     orig[MAP_ROUTES][5], dest[MAP_ROUTES][5];
} view_key_t;

static uint8_t s_mask[MAP_W * MAP_H / 8];   /* sausuma dabartiniame vaizde (s_w × s_h) */
static uint8_t s_bmask[MAP_W * MAP_H / 8];  /* valstybių sienos tame pačiame vaizde */
static view_key_t s_key;
static bool s_valid;                        /* s_mask atitinka s_key */
static struct {                             /* vaizdo slinkimas prie lėktuvo (map_draw_in) */
    bool     on;
    float    lon0, m0, lon1, m1;            /* iš kur į kur (centras: ilguma, Merkatoriaus platuma) */
    uint32_t t0, last;
} s_pan;
static int s_x0, s_y0, s_w, s_h;            /* vaizdo vieta ekrane */
static float s_lon0, s_lat0, s_cosk, s_ppd; /* vaizdo centras, cos(platumos), taškų platumos laipsniui centre */
static float s_m0;                          /* centro platuma Merkatoriaus projekcijoje */
static float s_min_span = 6.0f;            /* trumpesnė vaizdo pusė – ne mažiau kaip tiek laipsnių */

/* Taškas vaizde (nuo jo kairiojo viršutinio kampo) */
static float wrap180(float d)
{
    while (d >= 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

/* Merkatorius: platuma → „laipsniai“ vertikaliai (ties pusiauju 1:1) ir atgal */
static float merc(float lat)
{
    lat = fminf(fmaxf(lat, -89.0f), 89.0f);
    return logf(tanf((45.0f + lat * 0.5f) * DEG2RAD)) / DEG2RAD;
}

static float imerc(float m)
{
    return 2.0f * atanf(expf(m * DEG2RAD)) / DEG2RAD - 90.0f;
}

/* Taškų vienam ilgumos (ir Merkatoriaus) laipsniui: centre platumos laipsnis lygiai s_ppd taškų */
static float px_per_deg(void)
{
    return s_cosk * s_ppd;
}

static void to_px(float lon, float lat, float *x, float *y)
{
    *x = (float)s_w * 0.5f + wrap180(lon - s_lon0) * px_per_deg();
    *y = (float)s_h * 0.5f - (merc(lat) - s_m0) * px_per_deg();
}

/* Ar lėktuvas per arti vaizdo krašto (ar už jo) */
static bool near_edge(float x, float y)
{
    const float mx = fmaxf(3.0f, PAN_EDGE * (float)s_w), my = fmaxf(3.0f, PAN_EDGE * (float)s_h);
    return x < mx || y < my || x > (float)s_w - 1.0f - mx || y > (float)s_h - 1.0f - my;
}

/* Sausumos kaukė: kiekvienai eilutei – kur ją kerta matomų žiedų krantai (lyginio–nelyginio taisyklė). Kiekviena
 * kranto atkarpa peržiūrima vieną kartą: jos susikirtimai su eilučių centrais įrašomi į tų eilučių sąrašus (x × 16;
 * už vaizdo – prie pat krašto, kad lyginumas išliktų). Žiedas imamas su tuo 360 laipsnių poslinkiu, kuriuo jis
 * patenka į vaizdą (prie 180-ojo dienovidinio – su abiem) */
#define XS_ROW 2048
static int16_t *s_xs;                       /* PSRAM: MAP_H eilučių po XS_ROW */
static uint16_t s_xn[MAP_H];
static float s_rlat[MAP_H];                 /* eilučių centrų platuma (mažėja) */

static int cmp_i16(const void *a, const void *b)
{
    return *(const int16_t *)a - *(const int16_t *)b;
}

static void ring_cross(int r, int off, int iv_s, int iv_n)
{
    const int i0 = r ? (int)LAND_END[r - 1] : 0, i1 = (int)LAND_END[r];
    const float k = px_per_deg(), sh = 360.0f * (float)off - s_lon0, cx = (float)s_w * 0.5f;
    for (int i = i0; i < i1; i++) {
        const int j = i + 1 < i1 ? i + 1 : i0;
        const int a1 = LAND_PTS[i][1], a2 = LAND_PTS[j][1];
        if (a1 == a2 || (a1 < iv_s && a2 < iv_s) || (a1 > iv_n && a2 > iv_n)) continue;
        const float la1 = (float)a1 * UNIT, la2 = (float)a2 * UNIT;
        const float lmin = fminf(la1, la2), lmax = fmaxf(la1, la2);
        int lo = 0, hi = s_h;                         /* pirma eilutė, kurios platuma < lmax */
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (s_rlat[mid] < lmax) hi = mid;
            else lo = mid + 1;
        }
        const float lo1 = (float)LAND_PTS[i][0] * UNIT, dlo = ((float)LAND_PTS[j][0] - (float)LAND_PTS[i][0]) * UNIT;
        for (int row = lo; row < s_h && s_rlat[row] >= lmin; row++) {
            float x = cx + (lo1 + (s_rlat[row] - la1) * dlo / (la2 - la1) + sh) * k;
            x = fminf(fmaxf(x, -2.0f), (float)s_w + 2.0f);
            if (s_xn[row] < XS_ROW) s_xs[row * XS_ROW + s_xn[row]++] = (int16_t)lrintf(x * 16.0f);
        }
    }
}

static void build_mask(void)
{
    memset(s_mask, 0, sizeof s_mask);
    const float k = px_per_deg();
    for (int row = 0; row < s_h; row++) s_rlat[row] = imerc(s_m0 - ((float)row + 0.5f - (float)s_h * 0.5f) / k);
    if (!s_xs) s_xs = heap_caps_malloc(MAP_H * XS_ROW * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_xs || !map_data()) return;
    memset(s_xn, 0, sizeof s_xn);
    const float hlo = (float)s_w * 0.5f / k + 0.5f, v_w = s_lon0 - hlo, v_e = s_lon0 + hlo;
    const int iv_s = (int)floorf(s_rlat[s_h - 1] / UNIT) - 1, iv_n = (int)ceilf(s_rlat[0] / UNIT) + 1;
    const float small = 3.0f / (k * UNIT), dot = 1.0f / (k * UNIT);   /* 3 ir 1 taškas, duomenų vienetais */
    for (int r = 0; r < LAND_RINGS; r++) {
        const int16_t *b = LAND_BOX[r];
        if (b[3] < iv_s || b[1] > iv_n) continue;
        if (b[2] - b[0] < dot && b[3] - b[1] < dot) continue;                         /* mažesnis už tašką */
        if (r >= LAND_LAKE0 && b[2] - b[0] < small && b[3] - b[1] < small) continue;   /* ežeras – vos taškas */
        for (int off = -1; off <= 1; off++)
            if ((float)b[0] * UNIT + 360.0f * (float)off <= v_e && (float)b[2] * UNIT + 360.0f * (float)off >= v_w)
                ring_cross(r, off, iv_s, iv_n);
    }
    for (int row = 0; row < s_h; row++) {
        int16_t *xs = s_xs + row * XS_ROW;
        const int n = s_xn[row];
        qsort(xs, (size_t)n, sizeof xs[0], cmp_i16);
        for (int q = 0; q + 1 < n; q += 2) {          /* taško centras col + 0,5 – tarp xs[q] ir xs[q + 1] */
            int c0 = (int)ceilf((float)xs[q] / 16.0f - 0.5f), c1 = (int)ceilf((float)xs[q + 1] / 16.0f - 0.5f);
            if (c0 < 0) c0 = 0;
            if (c1 > s_w) c1 = s_w;
            for (int col = c0; col < c1; col++) {
                const int i = row * s_w + col;
                s_mask[i >> 3] |= (uint8_t)(1 << (i & 7));
            }
        }
    }
}

static bool land(int x, int y)
{
    if (x < 0 || y < 0 || x >= s_w || y >= s_h) return true;   /* už krašto – tęsiasi, tai ne krantas */
    const int i = y * s_w + x;
    return s_mask[i >> 3] & (1 << (i & 7));
}

/* Vaizdas pagal taškus (ilguma, platuma): visi telpa su paraštėmis, trumpesnė pusė ne mažiau s_min_span laipsnių.
 * false – netelpa į 40 laipsnių ilgesnę pusę (vaizdas nepakeistas) */
static bool fit(const float (*p)[2], int n)
{
    float lo0 = p[0][0], lo1 = lo0, la0 = p[0][1], la1 = la0;
    for (int i = 1; i < n; i++) {
        const float lo = p[0][0] + wrap180(p[i][0] - p[0][0]);      /* per 180-ąjį dienovidinį – trumpesnė pusė */
        lo0 = fminf(lo0, lo), lo1 = fmaxf(lo1, lo);
        la0 = fminf(la0, p[i][1]), la1 = fmaxf(la1, p[i][1]);
    }
    const float m0 = merc(la0), m1 = merc(la1), lat0 = imerc((m0 + m1) * 0.5f), cosk = cosf(lat0 * DEG2RAD);
    const float sx = (lo1 - lo0) * cosk * 1.4f, sy = (m1 - m0) * cosk * 1.4f;    /* reikia platumos laipsnių centre */
    float ppd = (float)(s_w < s_h ? s_w : s_h) / s_min_span;
    if (sx > 0.0f) ppd = fminf(ppd, (float)s_w / sx);
    if (sy > 0.0f) ppd = fminf(ppd, (float)s_h / sy);
    if (ppd < (float)(s_w > s_h ? s_w : s_h) / 40.0f) return false;
    s_lat0 = lat0, s_lon0 = wrap180((lo0 + lo1) * 0.5f), s_ppd = ppd;
    return true;
}

/* Kiek vaizdas nutolęs: 0 – arti (trumpesnė pusė 6 laipsniai), 1 – toliausiai (ilgesnė 40 laipsnių, pvz., į Ameriką) */
static float zoom_out(void)
{
    const float near = (float)(s_w < s_h ? s_w : s_h) / 6.0f, far = (float)(s_w > s_h ? s_w : s_h) / 40.0f;
    return near > far ? fminf(fmaxf((near - s_ppd) / (near - far), 0.0f), 1.0f) : 0.0f;
}

/* Vaizdas neišeina už ašigalių (ten žemėlapis nebeturi prasmės); nustato s_cosk ir s_m0 */
static void clamp_view(void)
{
    for (int k = 0; k < 2; k++) {                     /* cos(platumos) priklauso nuo platumos – antrą kartą tiksliau */
        s_cosk = cosf(s_lat0 * DEG2RAD);
        const float hm = (float)s_h * 0.5f / px_per_deg();
        s_lat0 = imerc(fminf(fmaxf(merc(s_lat0), merc(-80.0f) + hm), merc(84.0f) - hm));
    }
    s_cosk = cosf(s_lat0 * DEG2RAD);
    s_m0 = merc(s_lat0);
    s_lon0 = wrap180(s_lon0);
}

/* Vienas lėktuvas: visas maršrutas su namais ir lėktuvu; netelpa – be išvykimo; vis tiek netelpa – 40 laipsnių
 * aplink lėktuvą */
static void choose_view(const aircraft_t *a, const route_t *r)
{
    const home_t *h = aircraft_table_home();
    float p[4][2] = { { a->lon, a->lat }, { h->lon, h->lat } };
    int n = 2;
    if (r) {
        p[n][0] = r->dest_lon, p[n++][1] = r->dest_lat;
        p[n][0] = r->orig_lon, p[n++][1] = r->orig_lat;
    }
    if (!fit((const float(*)[2])p, n) && !(r && fit((const float(*)[2])p, n - 1))) {
        s_lat0 = a->lat, s_lon0 = a->lon;
        s_ppd = (float)(s_w > s_h ? s_w : s_h) / 40.0f;
    }
    clamp_view();
}

/* Keli lėktuvai: namai, lėktuvai ir jų oro uostai; netelpa – be toliausių nuo namų oro uostų (jų linijos išeina
 * už krašto) */
static void choose_view_multi(const aircraft_t *const *ac, const route_t *const *r, int n)
{
    const home_t *h = aircraft_table_home();
    float p[1 + 3 * MAP_ROUTES][2], d[1 + 3 * MAP_ROUTES];
    int k = 0;
    /* Be maršrutų – arčiau, kad lėktuvai nesusilietų: radaro spindulys – ketvirtadalis aukščio
     * (bet ne arčiau 1,5 laipsnio – kranto linija per grubi) */
    bool any = false;
    for (int i = 0; i < n; i++) any |= r[i] != NULL;
    s_min_span = any ? 6.0f : fminf(fmaxf(h->range_km * 4.0f / 111.0f, 1.5f), 6.0f);
    p[k][0] = h->lon, p[k][1] = h->lat, d[k++] = 0.0f;
    for (int i = 0; i < n; i++) {
        p[k][0] = ac[i]->lon, p[k][1] = ac[i]->lat, d[k++] = 0.0f;     /* lėktuvai – visada */
        if (!r[i]) continue;
        p[k][0] = r[i]->orig_lon, p[k][1] = r[i]->orig_lat;
        d[k++] = geo_haversine_km(h->lat, h->lon, r[i]->orig_lat, r[i]->orig_lon);
        p[k][0] = r[i]->dest_lon, p[k][1] = r[i]->dest_lat;
        d[k++] = geo_haversine_km(h->lat, h->lon, r[i]->dest_lat, r[i]->dest_lon);
    }
    for (int i = 1; i < k; i++)                       /* artimiausi – pirmi (rikiuojama įterpimu) */
        for (int j = i; j > 0 && d[j] < d[j - 1]; j--) {
            const float t = d[j], x = p[j][0], y = p[j][1];
            d[j] = d[j - 1], p[j][0] = p[j - 1][0], p[j][1] = p[j - 1][1];
            d[j - 1] = t, p[j - 1][0] = x, p[j - 1][1] = y;
        }
    while (k > 1 && !fit((const float(*)[2])p, k)) k--;
    if (k == 1) fit((const float(*)[2])p, 1);         /* vien namai visada telpa */
    s_min_span = 6.0f;
    clamp_view();
}

/* Ar kaukė tinka šiam vaizdui; jei ne – nauja (vaizdas parenkamas po šio iškvietimo, kaukė – view_ready) */
static bool view_same(const view_key_t *key)
{
    return s_valid && memcmp(key, &s_key, sizeof *key) == 0;
}

static void view_new(const view_key_t *key)
{
    s_pan.on = false;
    memcpy(&s_key, key, sizeof *key);
    s_w = key->w, s_h = key->h;
}

static void build_borders(void);

static void view_ready(void)
{
    build_mask();
    build_borders();
    s_valid = true;
}

/* Taškas didžiajame apskritime tarp (lo1, la1) ir (lo2, la2): t 0…1 */
static void great_circle(float lo1, float la1, float lo2, float la2, float t, float *lo, float *la)
{
    const float p1 = la1 * DEG2RAD, l1 = lo1 * DEG2RAD, p2 = la2 * DEG2RAD, l2 = lo2 * DEG2RAD;
    const float x1 = cosf(p1) * cosf(l1), y1 = cosf(p1) * sinf(l1), z1 = sinf(p1);
    const float x2 = cosf(p2) * cosf(l2), y2 = cosf(p2) * sinf(l2), z2 = sinf(p2);
    const float d = acosf(fminf(1.0f, x1 * x2 + y1 * y2 + z1 * z2));
    float a = 1.0f - t, b = t;
    if (d > 1e-4f) {
        a = sinf((1.0f - t) * d) / sinf(d);
        b = sinf(t * d) / sinf(d);
    }
    const float x = a * x1 + b * x2, y = a * y1 + b * y2, z = a * z1 + b * z2;
    *la = atan2f(z, sqrtf(x * x + y * y)) / DEG2RAD;
    *lo = atan2f(y, x) / DEG2RAD;
}

/* Ko negali uždengti oro uosto kodas (vaizdo taškais): lėktuvas ir namai */
typedef struct { int x0, y0, x1, y1; } box_t;

static bool hits(int lx, int ly, int w, const box_t *b, int nb)
{
    for (int i = 0; i < nb; i++)
        if (lx - 1 <= b[i].x1 && lx + w >= b[i].x0 && ly - 1 <= b[i].y1 && ly + 5 >= b[i].y0) return true;
    return false;
}

/* Oro uostas: taškas ir kodas šalia (3×5 šriftu, neišeinant už vaizdo, neuždengiant lėktuvo ir namų); code NULL –
 * tik taškas (dot spalvos, 1 taško). Už vaizdo – nieko */
static void airport(gfx_t *g, float lon, float lat, const char *code, uint16_t dot, const box_t *avoid, int nb)
{
    float fx, fy;
    to_px(lon, lat, &fx, &fy);
    if (fx < -1.0f || fy < -1.0f || fx > (float)s_w || fy > (float)s_h) return;
    const int x = (int)fx, y = (int)fy;
    if (!code) {
        gfx_pixel(g, s_x0 + x, s_y0 + y, dot);
        return;
    }
    gfx_fill_rect(g, s_x0 + x - 1, s_y0 + y - 1, 2, 2, dot);
    const int keep = gfx_font();
    gfx_set_font(GFX_FONT_SMALL);
    const int w = gfx_text_width(code);
    /* Kodo vietos iš eilės: į vaizdo vidų, kita pusė, tas pats toliau, virš, po; visos užimtos – pirmoji */
    const bool r = x < s_w / 2;
    const int cx[6] = { r ? x + 3 : x - w - 2, r ? x - w - 2 : x + 3, r ? x + 7 : x - w - 6, r ? x - w - 6 : x + 7,
                        x - w / 2, x - w / 2 };
    const int cy[6] = { y - 2, y - 2, y - 2, y - 2, y - 8, y + 4 };
    int lx[6], ly[6], k = 0;
    for (int i = 0; i < 6; i++) {
        lx[i] = cx[i] > s_w - w ? s_w - w : cx[i] < 0 ? 0 : cx[i];
        ly[i] = cy[i] > s_h - 5 ? s_h - 5 : cy[i] < 0 ? 0 : cy[i];
    }
    while (k < 6 && hits(lx[k], ly[k], w, avoid, nb)) k++;
    if (k == 6) k = 0;
    gfx_text(g, s_x0 + lx[k], s_y0 + ly[k], code, C_GREY);
    gfx_set_font(keep);
}

/* Žemėlapio fonas: vanduo ir sausuma savo spalvomis (Colours → Map water / Map land; temose – automatiškai),
 * krantas – radaro spalva; „tik krantai“ – be užpildymo */
static void draw_land(gfx_t *g)
{
    const bool fill = g_mode_prefs.map_style != MAP_STYLE_LINES;
    const uint16_t water = fill ? COL(COL_MAP_WATER) : 0, ground = fill ? COL(COL_MAP_LAND) : 0;
    const uint16_t coast = gfx_scale(COL(COL_RADAR), 85);
    for (int py = 0; py < s_h; py++)
        for (int px = 0; px < s_w; px++) {
            uint16_t c;
            if (!land(px, py)) c = water;
            else if (land(px - 1, py) && land(px + 1, py) && land(px, py - 1) && land(px, py + 1)) c = ground;
            else c = coast;
            if (c) gfx_pixel(g, s_x0 + px, s_y0 + py, c);
        }
}

/* Sienos atkarpa vaizdo taškais į s_bmask: tik per sausumą (kad ties supaprastintu krantu neišlįstų į jūrą) */
static void border_seg(int x0, int y0, int x1, int y1)
{
    const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < s_w && y0 < s_h && land(x0, y0)) {
            const int i = y0 * s_w + x0;
            s_bmask[i >> 3] |= (uint8_t)(1 << (i & 7));
        }
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) err += dy, x0 += sx;
        if (e2 <= dx) err += dx, y0 += sy;
    }
}

/* Sienų kaukė vaizdui: tik linijos, kurių ribos patenka į vaizdą */
static void build_borders(void)
{
    memset(s_bmask, 0, sizeof s_bmask);
    if (px_per_deg() < 1.0f || !map_data() || !BORDER_PTS) return;   /* viso pasaulio vaizde sienos susilietų */
    const float k = px_per_deg(), hlo = (float)s_w * 0.5f / k + 0.5f;
    const float v_s = s_rlat[s_h - 1] - 0.5f, v_n = s_rlat[0] + 0.5f;
    for (int l = 0, i0 = 0; l < BORDER_LINES; i0 = (int)BORDER_END[l++]) {
        const int16_t *b = BORDER_BOX[l];
        if ((float)b[3] * UNIT < v_s || (float)b[1] * UNIT > v_n) continue;
        bool vis = false;
        for (int off = -1; off <= 1 && !vis; off++)
            vis = (float)b[0] * UNIT + 360.0f * (float)off <= s_lon0 + hlo &&
                  (float)b[2] * UNIT + 360.0f * (float)off >= s_lon0 - hlo;
        if (!vis) continue;
        float xa, ya;
        to_px((float)BORDER_PTS[i0][0] * UNIT, (float)BORDER_PTS[i0][1] * UNIT, &xa, &ya);
        for (int i = i0 + 1; i < (int)BORDER_END[l]; i++) {
            float xb, yb;
            to_px((float)BORDER_PTS[i][0] * UNIT, (float)BORDER_PTS[i][1] * UNIT, &xb, &yb);
            const bool off = (xa < 0 && xb < 0) || (ya < 0 && yb < 0) || (xa >= s_w && xb >= s_w) ||
                             (ya >= s_h && yb >= s_h) || fabsf(xa) > 4000 || fabsf(xb) > 4000 || fabsf(ya) > 4000 ||
                             fabsf(yb) > 4000 || fabsf(xb - xa) > (float)(2 * s_w);   /* per vaizdo priešingą pusę */
            if (!off) border_seg((int)floorf(xa), (int)floorf(ya), (int)floorf(xb), (int)floorf(yb));
            xa = xb, ya = yb;
        }
    }
}

/* Valstybių sienos – plona linija savo spalva (Colours → Map borders; temose blankiau už maršrutus; juoda – nerodomos) */
static void draw_borders(gfx_t *g)
{
    const uint16_t c = gfx_scale(COL(COL_MAP_BORDER), (uint8_t)(255 - 90 * zoom_out()));   /* nutolusiame vaizde sienos tankios – blankiau */
    if (!COL(COL_MAP_BORDER)) return;
    for (int i = 0, n = s_w * s_h; i < n; i++)
        if (s_bmask[i >> 3] & (1 << (i & 7))) gfx_pixel(g, s_x0 + i % s_w, s_y0 + i / s_w, c);
}

/* Didžiojo apskritimo atkarpa nuo (lo1, la1) iki (lo2, la2) */
static void draw_arc(gfx_t *g, float lo1, float la1, float lo2, float la2, uint16_t c)
{
    float px = 0, py = 0, x, y;
    for (int i = 0; i <= 24; i++) {
        float lo, la;
        great_circle(lo1, la1, lo2, la2, (float)i / 24.0f, &lo, &la);
        to_px(lo, la, &x, &y);
        if (i && fabsf(x - px) < (float)(4 * s_w))      /* ne per vaizdo priešingą pusę */
            gfx_line(g, s_x0 + (int)px, s_y0 + (int)py, s_x0 + (int)x, s_y0 + (int)y, c);
        px = x, py = y;
    }
}

/* Kelias per lėktuvą: išvykimas → lėktuvas (nuskrista, done) → paskirtis (likusi, left). Tikri keliai dažnai apsuka
 * uždarytą oro erdvę, todėl tiesi linija tarp oro uostų nueitų toli nuo lėktuvo – taip ji visada eina per jį */
static void draw_route(gfx_t *g, const route_t *r, const aircraft_t *a, uint16_t done, uint16_t left)
{
    draw_arc(g, r->orig_lon, r->orig_lat, a->lon, a->lat, done);
    draw_arc(g, a->lon, a->lat, r->dest_lon, r->dest_lat, left);
}

static void draw_home(gfx_t *g, float hx, float hy)
{
    gfx_hline(g, s_x0 + (int)hx - 1, s_y0 + (int)hy, 3, C_WHITE);
    gfx_vline(g, s_x0 + (int)hx, s_y0 + (int)hy - 1, 3, C_WHITE);
}

/* Lėktuvas; pulse ≥ 0 – su žiedu kaip radare. Nutolusiame vaizde (pvz., skrydis į Ameriką) – iki 40 % didesnis ir su
 * tamsiu kontūru, kad nesusilietų su krantais, sienomis ir maršruto linija */
static void draw_plane(gfx_t *g, float x, float y, const aircraft_t *a, uint16_t c, float k, int pulse, bool big)
{
    const int px = s_x0 + (int)x, py = s_y0 + (int)y;
    const float z = zoom_out();
    k *= 1.0f + 0.4f * z;
    if (pulse >= 0)
        gfx_circle(g, px, py, (big ? 3 : 2) + (int)(2.0f * z) + pulse * (big ? 5 : 3) / MAP_PULSE_MS,
                   gfx_scale(C_WHITE, (uint8_t)(200 - 170 * pulse / MAP_PULSE_MS)));
    if (z > 0.1f) draw_kite(g, px, py, a->track_deg, C_BLACK, k * 1.35f);
    draw_kite(g, px, py, a->track_deg, c, k);
}

/* Slinkimo pradžia: tikslas – lėktuvas centre (kiek leidžia clamp_view); jei jau beveik ten – neslenkama */
static void pan_start(const aircraft_t *a, uint32_t now)
{
    const float lon0 = s_lon0, lat0 = s_lat0, m0 = s_m0;
    s_lon0 = a->lon, s_lat0 = a->lat;
    clamp_view();
    const float lon1 = s_lon0, m1 = s_m0;
    s_lon0 = lon0, s_lat0 = lat0;
    clamp_view();
    const float k = px_per_deg();
    if (fabsf(wrap180(lon1 - lon0)) * k < 2.0f && fabsf(m1 - m0) * k < 2.0f) return;
    s_pan.on = true;
    s_pan.lon0 = lon0, s_pan.m0 = m0, s_pan.lon1 = lon1, s_pan.m1 = m1;
    s_pan.t0 = now, s_pan.last = 0;
}

/* Slinkimo žingsnis: ne dažniau kaip kas PAN_STEP (kaukė perskaičiuojama), gale sulėtėjant */
static void pan_step(uint32_t now)
{
    float t = (float)(now - s_pan.t0) / (float)PAN_MS;
    if (t >= 1.0f) t = 1.0f, s_pan.on = false;
    else if (s_pan.last && now - s_pan.last < PAN_STEP) return;
    const float e = t * t * (3.0f - 2.0f * t);
    s_lon0 = s_pan.lon0 + wrap180(s_pan.lon1 - s_pan.lon0) * e;
    s_lat0 = imerc(s_pan.m0 + (s_pan.m1 - s_pan.m0) * e);
    clamp_view();
    view_ready();
    s_pan.last = now | 1;
}

void map_draw_in(gfx_t *g, int x0, int y0, int w, int h, const aircraft_t *a, const route_t *r, int pulse)
{
    const home_t *hm = aircraft_table_home();
    if (r && (!r->has_pos || !a->has_route)) r = NULL;     /* netinkamas šiam lėktuvui – laikomas nežinomu */
    if (w > MAP_W) w = MAP_W;
    if (h > MAP_H) h = MAP_H;
    s_x0 = x0, s_y0 = y0;

    /* Kaukė perskaičiuojama tik pasikeitus vaizdui (dydžiui, namams, lėktuvui, maršrutui) arba jį slenkant */
    view_key_t key;
    memset(&key, 0, sizeof key);
    key.w = w, key.h = h, key.n = 1, key.home_lat = hm->lat, key.home_lon = hm->lon, key.icao[0] = a->icao24;
    if (r) {
        memcpy(key.orig[0], r->orig, sizeof key.orig[0]);
        memcpy(key.dest[0], r->dest, sizeof key.dest[0]);
    }
    if (!view_same(&key)) {
        view_new(&key);
        choose_view(a, r);
        view_ready();
        s_pan.on = false;
    }
    /* Lėktuvas prie krašto – vaizdas slenka, kol jis atsiduria centre */
    const uint32_t now = sky_now_ms();
    float x, y;
    to_px(a->lon, a->lat, &x, &y);
    if (!s_pan.on && near_edge(x, y)) pan_start(a, now);
    if (s_pan.on) pan_step(now);

    gfx_clip(x0, y0, w, h);
    draw_land(g);
    draw_borders(g);

    /* Lėktuvo ir namų vietos – kad oro uostų kodai jų neuždengtų */
    const bool big = h >= 48 && w >= 96;
    float ax, ay, hx, hy;
    to_px(a->lon, a->lat, &ax, &ay);
    to_px(hm->lon, hm->lat, &hx, &hy);
    const int kr = big ? 5 : 4;
    const box_t avoid[2] = { { (int)ax - kr, (int)ay - kr, (int)ax + kr, (int)ay + kr },
                             { (int)hx - 2, (int)hy - 2, (int)hx + 2, (int)hy + 2 } };

    if (r) {
        draw_route(g, r, a, COL(COL_PROGRESS), gfx_scale(COL(COL_PROGRESS), 100));   /* likusi dalis – blankiau */
        airport(g, r->orig_lon, r->orig_lat, r->orig, C_WHITE, avoid, 2);
        airport(g, r->dest_lon, r->dest_lat, r->dest, C_WHITE, avoid, 2);
    }                                                /* be maršruto – „NO DATA“ viršuje, šalia skrydžio */
    draw_home(g, hx, hy);
    draw_plane(g, ax, ay, a, COL(COL_FLIGHT), big ? 0.9f : 0.75f, pulse, big);
    gfx_clip_off();
}

void map_draw_world(gfx_t *g, int x0, int y0, int w, int h)
{
    const home_t *hm = aircraft_table_home();
    if (w > MAP_W) w = MAP_W;
    if (h > MAP_H) h = MAP_H;
    s_x0 = x0, s_y0 = y0;
    view_key_t key;
    memset(&key, 0, sizeof key);
    key.w = w, key.h = h, key.n = -1, key.home_lat = hm->lat, key.home_lon = hm->lon;
    if (!view_same(&key)) {
        view_new(&key);
        /* 360 laipsnių per plotį, namai horizontaliai viduryje; vertikalus centras – 25° Š, kad matytųsi visi žemynai */
        s_lon0 = hm->lon, s_lat0 = 25.0f;
        for (int i = 0; i < 2; i++) {
            s_cosk = cosf(s_lat0 * DEG2RAD);
            s_ppd = (float)w / 360.0f / s_cosk;
            clamp_view();
        }
        s_ppd = (float)w / 360.0f / s_cosk;
        view_ready();
    }
    gfx_clip(x0, y0, w, h);
    draw_land(g);
    float hx, hy;
    to_px(hm->lon, hm->lat, &hx, &hy);
    draw_home(g, hx, hy);
    gfx_clip_off();
}

void map_draw(gfx_t *g, const aircraft_t *a, const route_t *r)
{
    map_draw_in(g, 0, 0, g->w, g->h, a, r, -1);
}

/* Lėktuvas už vaizdo (toli nuo kitų) – rodyklė prie krašto jo link */
static bool edge_arrow(gfx_t *g, float x, float y, uint16_t c)
{
    if (x >= 0 && y >= 0 && x < (float)s_w && y < (float)s_h) return false;
    const float cx = (float)s_w * 0.5f, cy = (float)s_h * 0.5f, dx = x - cx, dy = y - cy;
    const float k = fminf((cx - 4) / fmaxf(fabsf(dx), 0.001f), (cy - 4) / fmaxf(fabsf(dy), 0.001f));
    draw_kite(g, s_x0 + (int)(cx + dx * k), s_y0 + (int)(cy + dy * k), atan2f(dx, -dy) * 57.29578f, c, 0.8f);
    return true;
}

/* Maršruto linijos spalva (k – ryškumas): sekamo skrydžio – pasirinkta (Colours → Route line), kitų – jų spalva */
static uint16_t done_col(const aircraft_t *a, uint16_t own, int k)
{
    const uint16_t c = a->tracked ? COL(COL_PROGRESS) : own;
    return k >= 255 ? c : gfx_scale(c, (uint8_t)k);
}

void map_draw_multi(gfx_t *g, int x0, int y0, int w, int h, const aircraft_t *const *ac, const route_t *const *r,
                    const uint16_t *col, int n, int hi, int pulse)
{
    const home_t *hm = aircraft_table_home();
    if (n > MAP_ROUTES) n = MAP_ROUTES;
    if (w > MAP_W) w = MAP_W;
    if (h > MAP_H) h = MAP_H;
    s_x0 = x0, s_y0 = y0;
    const route_t *rr[MAP_ROUTES];
    for (int i = 0; i < n; i++) rr[i] = r[i] && r[i]->has_pos && ac[i]->has_route ? r[i] : NULL;

    /* Vaizdas parenkamas pagal lėktuvų ir maršrutų rinkinį (ne pagal jų eilę) */
    view_key_t key;
    memset(&key, 0, sizeof key);
    key.w = w, key.h = h, key.n = n, key.home_lat = hm->lat, key.home_lon = hm->lon;
    int ord[MAP_ROUTES];
    for (int i = 0; i < n; i++) ord[i] = i;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && ac[ord[j]]->icao24 < ac[ord[j - 1]]->icao24; j--) {
            const int t = ord[j];
            ord[j] = ord[j - 1], ord[j - 1] = t;
        }
    for (int i = 0; i < n; i++) {
        key.icao[i] = ac[ord[i]]->icao24;
        if (rr[ord[i]]) {
            memcpy(key.orig[i], rr[ord[i]]->orig, sizeof key.orig[i]);
            memcpy(key.dest[i], rr[ord[i]]->dest, sizeof key.dest[i]);
        }
    }
    if (!view_same(&key)) {
        view_new(&key);
        choose_view_multi(ac, rr, n);
        view_ready();
    }

    gfx_clip(x0, y0, w, h);
    draw_land(g);
    draw_borders(g);
    const bool big = h >= 48 && w >= 96;
    float hx, hy, ax = 0, ay = 0;
    to_px(hm->lon, hm->lat, &hx, &hy);
    if (hi >= n) hi = -1;
    if (hi >= 0) to_px(ac[hi]->lon, ac[hi]->lat, &ax, &ay);

    /* Kiti maršrutai – blankiau, oro uostai – tik taškais */
    for (int i = 0; i < n; i++) {
        if (i == hi || !rr[i]) continue;
        draw_route(g, rr[i], ac[i], done_col(ac[i], col[i], 110), done_col(ac[i], col[i], 45));
        airport(g, rr[i]->orig_lon, rr[i]->orig_lat, NULL, gfx_scale(col[i], 150), NULL, 0);
        airport(g, rr[i]->dest_lon, rr[i]->dest_lat, NULL, gfx_scale(col[i], 150), NULL, 0);
    }
    /* Išskirtasis – ryškiai, su oro uostų kodais (neuždengiant jo lėktuvo ir namų) */
    if (hi >= 0 && rr[hi]) {
        const int kr = big ? 5 : 4;
        const box_t avoid[2] = { { (int)ax - kr, (int)ay - kr, (int)ax + kr, (int)ay + kr },
                                 { (int)hx - 2, (int)hy - 2, (int)hx + 2, (int)hy + 2 } };
        draw_route(g, rr[hi], ac[hi], done_col(ac[hi], col[hi], 255), done_col(ac[hi], col[hi], 100));
        airport(g, rr[hi]->orig_lon, rr[hi]->orig_lat, rr[hi]->orig, C_WHITE, avoid, 2);
        airport(g, rr[hi]->dest_lon, rr[hi]->dest_lat, rr[hi]->dest, C_WHITE, avoid, 2);
    }
    /* Namai, kiti lėktuvai (mažesni ir blankesni nei išskirtasis), išskirtasis – viršuje su žiedu */
    draw_home(g, hx, hy);
    for (int i = 0; i < n; i++) {
        if (i == hi) continue;
        float x, y;
        to_px(ac[i]->lon, ac[i]->lat, &x, &y);
        const uint16_t c = hi >= 0 ? gfx_scale(col[i], 150) : col[i];
        if (!edge_arrow(g, x, y, c)) draw_plane(g, x, y, ac[i], c, hi >= 0 ? 0.6f : 0.75f, -1, big);
    }
    if (hi >= 0 && !edge_arrow(g, ax, ay, col[hi])) draw_plane(g, ax, ay, ac[hi], col[hi], big ? 0.9f : 0.75f, pulse, big);
    gfx_clip_off();
}
