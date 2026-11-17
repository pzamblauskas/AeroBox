/* Ekrano režimai. Kiekvienas režimas – funkcijų rodyklių struktūra (display_mode_t); kurį rodyti, parenka
 * mode_manager.c. Čia taip pat aprašyti visų režimų nustatymai (mode_prefs_t), spalvų paskirtys ir pagalbinės
 * piešimo funkcijos, kurias naudoja keli režimai. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "aircraft.h"
#include "gfx.h"
#include "track.h"

typedef struct {
    const char *name;
    void (*enter)(void);                                        /* kviečiama įjungus režimą */
    void (*update)(const aircraft_snapshot_t *s, uint32_t ms);  /* logika (kiekvieną kadrą) */
    void (*draw)(gfx_t *g);                                     /* piešimas (kiekvieną kadrą) */
} display_mode_t;

extern const display_mode_t MODE_FLIGHT_CARD, MODE_RADAR, MODE_CLOCK, MODE_TRACK, MODE_TRACK_MAP, MODE_CUSTOM,
                            MODE_OVERHEAD, MODE_ROUTES;

/* Naudotojo pasirinktas vaizdas (keičiamas mygtuku arba puslapyje) */
typedef enum {
    VIEW_AUTO = 0,     /* laikrodis, kai tuščia; kitaip kortelės ir radaras paeiliui, taip pat sekamo skrydžio kortelė ir žemėlapis */
    VIEW_CARD,
    VIEW_RADAR,
    VIEW_CLOCK,
    VIEW_TRACK,        /* sekamas skrydis: kortelė → maršruto žemėlapis → radaras (jei spinduliu ir įjungta) */
    VIEW_CUSTOM,       /* padalintas ekranas: dvi pusės, kiekvienoje – naudotojo pasirinktas langas */
    VIEW_ROUTES,       /* maršrutų žemėlapis: artimiausių skrydžių maršrutai, išskiriami po vieną */
    VIEW_COUNT
} view_t;

/* Radaro animacija */
typedef enum {
    RADAR_CALM = 0,    /* be animacijos */
    RADAR_SONAR,       /* banga iš namų kas 3 s */
    RADAR_SWEEP,       /* klasikinis besisukantis spindulys */
    RADAR_STYLE_COUNT
} radar_style_t;

/* Kaip radare piešiamas lėktuvas */
typedef enum {
    MARK_ARROW = 0,    /* rodyklė skrydžio kryptimi */
    MARK_DOT,          /* apvalus taškas (krypties nerodo) */
    MARK_TRIANGLE,     /* glotnus trikampis tikslia skrydžio kryptimi */
    MARK_COUNT
} radar_mark_t;

/* Matavimo vienetai – kiekvienam dydžiui atskirai */
typedef enum { SPEED_KT = 0, SPEED_KMH, SPEED_MPH, SPEED_UNIT_COUNT } speed_unit_t;
typedef enum { DIST_KM = 0, DIST_MI, DIST_NM, DIST_UNIT_COUNT } dist_unit_t;
typedef enum { ALT_FL = 0, ALT_FT, ALT_M, ALT_UNIT_COUNT } alt_unit_t;     /* skrydžio lygis / pėdos / metrai */
typedef enum { VS_FPM = 0, VS_MS, VS_UNIT_COUNT } vs_unit_t;               /* vertikalusis greitis: ft/min / m/s */
typedef enum { TEMP_C = 0, TEMP_F, TEMP_UNIT_COUNT } temp_unit_t;

/* Ką rodo 128×64 skrydžio kortelė (bitai); skrydžio Nr. rodomas visada */
enum {
    CF_TYPE     = 1 << 0,
    CF_REG      = 1 << 1,
    CF_ROUTE    = 1 << 2,
    CF_ALT      = 1 << 3,
    CF_DIST     = 1 << 4,
    CF_SPEED    = 1 << 5,
    CF_HDG      = 1 << 6,
    CF_VS       = 1 << 7,
    CF_PROGRESS = 1 << 8,
    CF_COUNTER  = 1 << 9,      /* „1/5“ */
};
#define CF_ALL 0x3FF

/* Spalvos pagal paskirtį (valdymo puslapyje: tema arba kiekviena spalva atskirai) */
typedef enum {
    COL_FLIGHT = 0,    /* skrydžio Nr., laukimo ir ETA užrašai */
    COL_TYPE,          /* lėktuvo tipas */
    COL_ALT,           /* aukštis */
    COL_HDG,           /* kursas */
    COL_TEXT,          /* atstumas, greitis, maršrutas, orai */
    COL_UP,            /* kyla */
    COL_DOWN,          /* leidžiasi */
    COL_PROGRESS,      /* skrydžio eigos juosta */
    COL_CLOCK,         /* laikrodžio skaitmenys */
    COL_DATE,          /* data */
    COL_RADAR,         /* radaro žiedai, sonaras, spindulys; užrašas „spinduliu“ (žemėlapyje – kranto linija) */
    COL_MAP_LAND,      /* žemėlapio sausuma (temose – tamsiai ruda) */
    COL_MAP_WATER,     /* žemėlapio vanduo (temose – blanki radaro spalva) */
    COL_MAP_BORDER,    /* valstybių sienos žemėlapyje (temose – blankiai pilkos; juoda – nerodomos) */
    COL_COUNT
} color_role_t;

typedef enum { THEME_DEFAULT = 0, THEME_AMBER, THEME_ICE, THEME_GREEN, THEME_WHITE, THEME_RED, THEME_COUNT } theme_t;
#define THEME_CUSTOM 0xFF
/* Temų spalvos (theme.c) */
void        theme_colors(int theme, int win, uint16_t out[COL_COUNT]);   /* win – win_t (žemėlapio langas turi savas) */
const char *theme_name(int theme);

/* Langai su atskiromis spalvomis (valdymo puslapio „Display“ skiltys): kiekvienas su savo tema arba naudotojo spalvomis */
typedef enum { WIN_CARD = 0, WIN_RADAR, WIN_MAP, WIN_CLOCK, WIN_TRACK, WIN_COUNT } win_t;

/* Žemėlapis: sausuma ir vanduo užpildyti (COL_MAP_LAND, COL_MAP_WATER) arba tik kranto linijos */
typedef enum { MAP_STYLE_FILL = 0, MAP_STYLE_LINES, MAP_STYLE_COUNT } map_style_t;

/* Padalinto ekrano (VIEW_CUSTOM) langai: kairė ir dešinė 64×64 pusės */
typedef enum { PANE_RADAR = 0, PANE_CARD, PANE_LIST, PANE_CLOCK, PANE_TRACK, PANE_MAP, PANE_COUNT } pane_t;

/* Ekrano nustatymai iš valdymo puslapio; render užduotis juos atnaujina kiekvienam kadrui (app_main.c) */
typedef struct {
    uint8_t  radar_style;  /* radar_style_t */
    uint8_t  radar_mark;   /* radar_mark_t */
    uint8_t  speed_unit;   /* speed_unit_t */
    uint8_t  dist_unit;    /* dist_unit_t */
    uint8_t  alt_unit;     /* alt_unit_t */
    uint8_t  vs_unit;      /* vs_unit_t */
    uint8_t  temp_unit;    /* temp_unit_t */
    bool     clock12;      /* 12 valandų laikrodis (AM/PM) */
    bool     track_card;   /* sekamo skrydžio langai AUTO ir TRACKING cikluose: kortelė, */
    bool     track_map;    /* maršruto žemėlapis, */
    bool     track_radar;  /* radaras (kai sekamas skrydis yra spinduliu) */
    bool     track_pulse;  /* pulsuojantis žiedas aplink sekamą lėktuvą (žemėlapyje ir radare) */
    char     track_label[21]; /* naudotojo užrašas sekamam skrydžiui (pvz., „MOM'S FLIGHT“); tuščias – nėra */
    uint16_t card_fields;  /* CF_* */
    uint8_t  pane[2];      /* pane_t: kairė, dešinė */
    uint8_t  overhead_km;  /* „lėktuvas virš galvos“: iki tiek km nuo namų (0 – išjungta) */
    bool     rare;         /* žymėti ypatingus lėktuvus (retus, karinius, sraigtasparnius, verslo) */
    bool     flap;         /* kortelės keičiasi kaip oro uosto išvykimų lentoje */
    uint8_t  card_s;       /* kiek sekundžių rodoma skrydžio kortelė (0 – numatyta) */
    uint8_t  map_style;    /* map_style_t: žemėlapio spalvos */
    bool     flightwall;   /* FlightWall stiliaus kortelė (atskiras variantas, Kconfig SKY_FLIGHTWALL) */
    uint16_t wcol[WIN_COUNT][COL_COUNT];   /* kiekvieno lango spalvos */
    uint16_t col[COL_COUNT];               /* šiuo metu piešiamo lango spalvos (nustato mode_win) */
} mode_prefs_t;

/* Ypatingo lėktuvo spalva – visose temose ta pati, kad išsiskirtų */
#define C_RARE RGB565(255, 50, 200)

extern mode_prefs_t g_mode_prefs;
#define COL(role) (g_mode_prefs.col[role])

/* Toliau piešiama lango w spalvomis (kviečiama režimų draw() ir padalinto ekrano langų pradžioje) */
static inline void mode_win(win_t w)
{
    memcpy(g_mode_prefs.col, g_mode_prefs.wcol[w], sizeof g_mode_prefs.col);
}

void        mode_manager_set_view(view_t v);
view_t      mode_manager_view(void);
const char *mode_manager_view_name(view_t v);
/* Parenka aktyvų režimą pagal vaizdą ir lėktuvų skaičių; persijungus iškviečia enter() */
const display_mode_t *mode_manager_select(const aircraft_snapshot_t *s, uint32_t ms);

/* Sekamo skrydžio kortelė rodoma ne trumpiau nei TRACK_CARD_MS (ilgiau, jei kortelėms nustatyta daugiau); jei yra
 * naudotojo užrašas, prieš ją TRACK_LABEL_MS rodomas užrašo langas. track_cycle_ms() – abiejų trukmė kartu
 * (MODE_TRACK ciklo dalis). */
#define TRACK_CARD_MS  8000
#define TRACK_LABEL_MS 4000
uint32_t track_cycle_ms(void);

/* Padalinto ekrano 64×64 langai (režimų update() jau iškviesta) */
void     radar_pane(gfx_t *g, uint32_t focus_icao, bool with_list);   /* focus – kortelėje rodomas lėktuvas */
void     radar_list_pane(gfx_t *g);
void     card_pane(gfx_t *g);
uint32_t card_ms(void);            /* kiek laiko rodoma viena skrydžio kortelė (nustatymas) */
uint32_t card_pane_icao(void);     /* kurio lėktuvo kortelė rodoma dabar (0 – jokio) */
void     clock_pane(gfx_t *g);
void     track_pane(gfx_t *g);
/* Maršruto žemėlapis: focus – kurio lėktuvo (kortelės šalia). 0 – sekamo; jei nesekama – artimiausių
 * skrydžių maršrutai paeiliui (routes_view.c); ms – animacijai */
void     map_pane(gfx_t *g, const aircraft_snapshot_t *s, uint32_t focus_icao, uint32_t ms);
/* Langui: išvalytas 64×64 buferis (NULL – nepavyko išskirti) ir jo kopijavimas į kairę (0) ar dešinę (1) pusę */
gfx_t   *pane_begin(void);
void     pane_end(gfx_t *g, gfx_t *pane, int side);

/* Konkretaus lėktuvo maršruto žemėlapis per visą g (r – NULL, jei maršruto koordinačių nėra) */
void map_draw(gfx_t *g, const aircraft_t *a, const route_t *r);
/* Tas pats stačiakampyje (x0, y0, w, h), iki 128×64; pulse ≥ 0 – žiedas aplink lėktuvą (0…MAP_PULSE_MS) */
#define MAP_PULSE_MS 1200
#define MAP_ROUTES   5         /* kelių lėktuvų žemėlapyje – iki tiek (artimiausi; 4 – kaip radaro sąraše) */
#define MAP_NO_ROUTE "NO DATA" /* maršrutas nežinomas – rodoma žemėlapio viršuje šalia skrydžio */
void map_draw_in(gfx_t *g, int x0, int y0, int w, int h, const aircraft_t *a, const route_t *r, int pulse);
/* Visas pasaulis per visą plotį (namai – viduryje, pažymėti) */
void map_draw_world(gfx_t *g, int x0, int y0, int w, int h);
/* Keli lėktuvai ir jų maršrutai (r[i] – NULL, jei nežinomas) savomis spalvomis col[i]; hi – išskirtasis (ryškus,
 * su oro uostų kodais ir žiedu; -1 – nėra) */
void map_draw_multi(gfx_t *g, int x0, int y0, int w, int h, const aircraft_t *const *ac, const route_t *const *r,
                    const uint16_t *col, int n, int hi, int pulse);
/* Maršrutų žemėlapis (MODE_ROUTES): kiek lėktuvų rodytų (iki MAP_ROUTES; 0 – nė vieno žinomo maršruto). Kiekvienas
 * išskiriamas ROUTES_HI_MS – nuo to priklauso AUTO ciklo trukmė */
#define ROUTES_HI_MS 4000
int routes_count(const aircraft_snapshot_t *s);
/* Tas pats žemėlapis testui: iš kopijos arba iš pateiktų skrydžių; t – laikas, pagal kurį nustatomas išskirtasis */
void routes_draw_snap(gfx_t *g, const aircraft_snapshot_t *s, uint32_t t);
void routes_show(gfx_t *g, const aircraft_t *const *ac, const route_t *const *r, int n, uint32_t t);
/* Ypatingas lėktuvas (jei žymima): rožinė juosta su užrašu (pvz., „MILITARY“) 128 taškų pločio kortelėje; y – teksto viršus */
void special_banner(gfx_t *g, int y, const aircraft_t *a);
/* Sekamo skrydžio kortelė ir maršruto žemėlapis konkrečiam skrydžiui (ir demonstracijoms);
 * r – maršrutas žemėlapiui (NULL – iš talpyklos pagal skrydžio Nr.), ms – lėktuvo žiedo animacijai */
void track_card_show(gfx_t *g, const track_t *t);
void track_map_show(gfx_t *g, const track_t *t, const route_t *r, uint32_t ms);
/* Vieno lėktuvo kortelė pagal g dydį (128×64 / 64×64 / 64×32) – demonstracijoms */
void card_show(gfx_t *g, const aircraft_t *a);
/* „Lėktuvas virš galvos“: pradėti (mode_manager) ir piešti nurodytą lėktuvą (elapsed – nuo pradžios) */
void overhead_start(const aircraft_t *a, uint32_t ms);
void overhead_draw_for(gfx_t *g, const aircraft_t *a, uint32_t elapsed_ms);
/* Valdymo puslapio testai („Test“): kind – test_kind_t, real – tikri lėktuvai (jei jų nėra – pavyzdiniai) */
void demo_draw(gfx_t *g, int kind, const aircraft_snapshot_t *real, uint32_t elapsed_ms);
