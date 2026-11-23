/* Naudotojo nustatymai: laikomi atmintyje (g_settings) ir išsaugomi NVS (settings.c). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Panelių išdėstymas (layout) – žr. components/hub75/hub75_layout.c */
#define SETTINGS_TILES_LEN 48
#define SETTINGS_COLORS    14      /* turi sutapti su COL_COUNT (mode.h) */
#define SETTINGS_WINS      5       /* turi sutapti su WIN_COUNT (mode.h) */

/* Žemiau šios ribos LED vaizdas tampa blankus ir grubus (jauniausi BCM bitai nebešviečia). Puslapyje ši reikšmė
 * rodoma kaip 1 %, 255 – kaip 100 %. Nakties ryškumas gali būti ir 0: tada naktį ekranas nešviečia. */
#define SETTINGS_BRIGHT_MIN 128

#define SETTINGS_LABEL_LEN  21      /* sekamo skrydžio užrašas – iki 20 simbolių */
#define SETTINGS_KEY_LEN    72      /* naudotojo API raktas */
#define SETTINGS_OS_LEN     64      /* OpenSky API kliento ID ir slaptasis raktas */
#define SETTINGS_SRV_LEN    64      /* AeroBox serverio adresas (http(s)://…) */
#define SETTINGS_SRV_KEY_LEN 40     /* ekrano raktas, gautas iš serverio */

/* Numatytasis serverio adresas (tuščias – duomenys imami tiesiai iš nemokamų šaltinių).
 * Parduodamiems ekranams nustatomas kompiliuojant: -D SKY_SERVER_URL=\"https://…\" */
#ifndef SKY_SERVER_URL
#define SKY_SERVER_URL "https://aerobox.onrender.com"   /* bandomasis serveris (Render) */
#endif

/* Kuriuos langus rodyti sekant skrydį (track_show bitai) */
#define TRACK_SHOW_CARD  0x01
#define TRACK_SHOW_MAP   0x02
#define TRACK_SHOW_RADAR 0x04       /* kai sekamas skrydis yra radaro spinduliu */
#define TRACK_SHOW_PULSE 0x08       /* ne langas: pulsuojantis žiedas aplink sekamą lėktuvą */

/* Lėktuvų duomenų šaltinis: įtaisytas (AeroBox) arba naudotojo API raktas (ADS-B Exchange per RapidAPI) */
#define DATA_BUILTIN 0
#define DATA_OWN_KEY 1

/* Iš kur gauta namų vieta (loc_src). Kol ji ne LOC_USER, ekrane rodoma „CHOOSE YOUR LOCATION“ ir puslapio adresas.
 * LOC_AUTO – vieta, kurią senesnė programa nustatė pagal IP; dabar laikoma nepasirinkta. */
#define LOC_NONE 0
#define LOC_AUTO 1
#define LOC_USER 2

typedef struct {
    float   home_lat, home_lon;
    float   range_km;
    uint8_t brightness;    /* naudotojo ryškumas 0…255 */
    uint8_t view;          /* view_t (mode.h) */
    uint8_t view_prev;     /* vaizdas prieš pradedant sekti skrydį; „Stop“ jį grąžina (ir po perkrovimo) */
    uint16_t night_start;  /* nakties režimo pradžia ir pabaiga minutėmis nuo vidurnakčio; lygios – režimas išjungtas */
    uint16_t night_end;
    uint8_t night_bright;  /* 0 – naktį ekranas nešviečia; kitaip SETTINGS_BRIGHT_MIN…255 */
    uint8_t layout;        /* panelių išdėstymo numeris (hub75_layout.h) */
    uint8_t flip;          /* 1 – visas vaizdas pasuktas 180° */
    uint8_t psu_amps;      /* maitinimo bloko srovė, A (0 – neriboti) */
    uint8_t color_order;   /* spalvų tvarka: 0 RGB, 1 RBG, 2 GRB, 3 GBR, 4 BRG, 5 BGR */
    uint8_t radar_style;   /* radar_style_t (mode.h) */
    uint8_t radar_mark;    /* radar_mark_t (mode.h): rodyklė, taškas arba glotnus trikampis */
    uint8_t font;          /* šriftas: gfx_font_id_t (gfx.h) */
    uint8_t poll_s;        /* 0 – pagal šaltinio taisykles; 5…15 – kas tiek sekundžių (bandymams) */
    uint8_t data_src;      /* įtaisytas šaltinis: 0 adsb.lol, 1 adsb.fi (puslapyje nerodomas) */
    uint8_t data_mode;     /* DATA_BUILTIN arba DATA_OWN_KEY */
    uint16_t key_poll_s;   /* su naudotojo raktu – užklausa kas tiek sekundžių (pagal naudotojo planą) */
    char    api_key[SETTINGS_KEY_LEN];
    char    server_url[SETTINGS_SRV_LEN];   /* AeroBox serveris; tuščias – nenaudojamas */
    char    server_key[SETTINGS_SRV_KEY_LEN]; /* gaunamas automatiškai (POST /v1/register); tuščias – dar negautas */
    char    os_id[SETTINGS_OS_LEN];     /* papildomi lėktuvai iš OpenSky (nemokama paskyra); tuščias – nenaudojama */
    char    os_secret[SETTINGS_OS_LEN];
    uint8_t clock12;       /* 1 – 12 valandų laikrodis (AM/PM) */
    uint8_t speed_unit;    /* greitis: 0 mazgai, 1 km/h, 2 mph (speed_unit_t, mode.h) */
    uint8_t dist_unit;     /* atstumas: 0 km, 1 mylios, 2 jūrmylės (dist_unit_t) */
    uint8_t alt_unit;      /* aukštis: 0 skrydžio lygis, 1 pėdos, 2 metrai (alt_unit_t) */
    uint8_t vs_unit;       /* vertikalusis greitis: 0 ft/min, 1 m/s (vs_unit_t) */
    uint8_t temp_unit;     /* temperatūra: 0 Celsijus, 1 Farenheitas (temp_unit_t) */
    uint16_t card_fields;  /* ką rodo skrydžio kortelė: CF_* (mode.h) */
    uint8_t theme[SETTINGS_WINS];     /* kiekvieno lango (win_t) spalvų tema (theme_t); 0xFF – naudotojo spalvos iš colors[] */
    uint16_t colors[SETTINGS_WINS][SETTINGS_COLORS]; /* naudotojo spalvos (RGB565) pagal paskirtį (color_role_t) */
    uint8_t pane[2];       /* padalintas ekranas: kairės ir dešinės pusės langai (pane_t) */
    uint8_t overhead_km;   /* „lėktuvas virš galvos“ – iki tiek km (0 – išjungta) */
    uint8_t rare;          /* 1 – žymėti ypatingus lėktuvus */
    uint8_t flap;          /* 1 – kortelės keičiasi kaip išvykimų lentoje */
    uint8_t card_s;        /* kiek sekundžių rodoma skrydžio kortelė (3…30) */
    uint8_t map_style;     /* žemėlapis: 0 – sausuma ir vanduo užpildyti, 1 – tik kranto linijos (map_style_t) */
    uint8_t track_show;    /* TRACK_SHOW_*: sekamo skrydžio langai (AUTO ir TRACKING cikluose) */
    char    track[12];     /* sekamas skrydis (šaukinys, skrydžio Nr., registracija ar ICAO adresas); tuščias – nesekama */
    char    track_label[SETTINGS_LABEL_LEN]; /* naudotojo užrašas sekamam skrydžiui (pvz., „MOM'S FLIGHT“); tuščias – nėra */
    char    tiles[SETTINGS_TILES_LEN]; /* naudotojo išdėstymas, pvz. „1,0,1;1,1,0;0,1,0;0,0,1“ */
    char    tz[48];        /* POSIX laiko juosta */
    uint8_t tz_auto;       /* 1 – laiko juosta pagal valdymo puslapį atidariusio įrenginio laikrodį */
    uint8_t loc_src;       /* namų vietos šaltinis: LOC_NONE (nepasirinkta), LOC_AUTO (senesnės programos), LOC_USER */
    char    wifi_ssid[33]; /* tuščias – Wi-Fi dar nenustatytas */
    char    wifi_pass[65];
} settings_t;

extern settings_t g_settings;

/* Inicializuoja NVS ir nuskaito nustatymus (numatytosios reikšmės – iš menuconfig) */
esp_err_t settings_init(void);
/* Išsaugo visus nustatymus, išskyrus Wi-Fi */
void      settings_save(void);
/* Išsaugo Wi-Fi tinklą (po sėkmingo prisijungimo) */
void      settings_save_wifi(const char *ssid, const char *pass);
/* Išsaugo serverio adresą ir ekrano raktą (tuščias raktas – ekranas užsiregistruos iš naujo) */
void      settings_save_server(const char *url, const char *key);
/* Iš valdymo puslapio: namų vieta ir spindulys (vieta tampa naudotojo pasirinkta – LOC_USER) */
void      settings_save_location(float lat, float lon, float range_km);
/* Ištrina Wi-Fi nustatymus; po perkrovimo vėl įjungiamas nustatymų tinklas */
void      settings_clear_wifi(void);
/* Ištrina visus nustatymus (grąžina gamyklinius) */
void      settings_factory_reset(void);

/* Nesėkmingų paleidimų skaitiklis (saugiam režimui).
 * settings_boot_begin() grąžina, kiek paleidimų iš eilės nepasiekė „stabilios“ būsenos. */
uint8_t   settings_boot_begin(void);
void      settings_boot_ok(void);
/* adsb.lol užklausų biudžeto likutis ir išsaugojimo laikas (Unix s), kad perkrovimas nepridėtų užklausų */
void      settings_budget_save(float tokens, int64_t epoch_s);
bool      settings_budget_load(float *tokens, int64_t *epoch_s);

/* Tyčinis perkrovimas (nustatymai, Wi-Fi, atnaujinimas); neskaičiuojamas kaip nesėkmingas paleidimas */
void      settings_restart(void);

/* Ar toks naudotojo rakto užklausų periodas (s) leidžiamas */
bool      settings_key_poll_ok(unsigned s);

