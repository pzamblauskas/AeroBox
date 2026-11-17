/* Bendra programos būsena (g_status): ją pildo įvairios užduotys, o skaito ekranas ir valdymo puslapis.
 * Čia nustatoma ir programos versija (SKY_FW_VERSION). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif
#ifdef CONFIG_SKY_FLIGHTWALL
#define SKY_FW_VERSION "1.2.4-fw"           /* FlightWall stiliaus variantas; serveris pagal „-fw“ siūlo tik šio varianto versijas */
#else
#define SKY_FW_VERSION "1.2.4"
#endif

/* Nustatymų tinklas (portal.c): vardas, WPA2 slaptažodis ir adresas – visiems ekranams vienodi; rodomi ekrane.
 * Slaptažodis turi būti 8–63 simbolių (WPA2 reikalavimas). */
#define SKY_AP_SSID "AeroBox"
#define SKY_AP_PASS "aerobox123"
#define SKY_AP_IP   "192.168.4.1"

/* Prisijungimo prie naudotojo Wi-Fi bandymo būsena nustatymų režime */
typedef enum { PORTAL_IDLE = 0, PORTAL_TESTING, PORTAL_OK, PORTAL_FAIL } portal_state_t;

/* Wi-Fi atsijungimo priežasčių grupės (naudotojui suprantamam pranešimui) */
typedef enum { WIFI_WHY_NONE = 0, WIFI_WHY_PASSWORD, WIFI_WHY_NOT_FOUND, WIFI_WHY_OTHER } wifi_why_t;

typedef struct {
    volatile bool     wifi;
    volatile uint8_t  wifi_reason;  /* paskutinė atsijungimo priežastis (0 – atsijungimo nebuvo) */
    volatile bool     data_ok;      /* paskutinė duomenų užklausa pavyko */
    volatile uint32_t last_ok_ms;
    volatile int      seen_total;   /* kiek lėktuvų grąžino paskutinis atsakymas */
    volatile uint16_t poll_ms;      /* dabartinis užklausų periodas */
    volatile uint16_t limited;      /* kiek kartų nuo paleidimo gautas 429 (per daug užklausų) */
    volatile uint32_t limited_until_ms; /* iki kada laukiama po 429 (0 – apribojimo nėra) */
    volatile float    budget;       /* adsb.lol užklausų rezervas (biudžeto režime) */
    volatile uint32_t next_req_ms;  /* kada bus kita užklausa */
    volatile int16_t  key_status;   /* paskutinis HTTP atsakymas su naudotojo API raktu (0 – dar nebuvo; 401/403 – raktas netinka) */
    volatile uint8_t  srv_down;     /* AeroBox serveris neatsako – duomenys laikinai imami tiesiai iš šaltinių */
    volatile int16_t  os_status;    /* paskutinis OpenSky HTTP atsakymas (0 – dar nebuvo; 401 – paskyra netinka) */
    volatile bool     portal;       /* įjungtas Wi-Fi nustatymų režimas */
    volatile bool     portal_client;/* prie AeroBox tinklo kas nors prisijungęs */
    volatile uint32_t portal_since_ms;
    volatile uint8_t  portal_state; /* portal_state_t */
    char              portal_ssid[24];
    char              portal_pass[64];   /* nustatymų tinklo slaptažodis (rodomas ekrane), iki 63 simbolių */
    char              portal_target[33]; /* tinklas, prie kurio bandoma prisijungti */
    char              ip[16];            /* IP adresas namų tinkle (valdymo puslapiui) */

    /* Diagnostika (rodoma valdymo puslapyje) */
    volatile bool     display_ok;        /* HUB75 inicializuotas */
    volatile bool     safe_mode;         /* keli nesėkmingi paleidimai iš eilės */
    uint8_t           boot_fails;        /* kiek paleidimų iš eilės nebaigta */
    char              reset_reason[24];  /* paskutinio perkrovimo priežastis */
    uint16_t          width, height;     /* ekrano dydis taškais */

    /* Testo vaizdas, paleistas iš valdymo puslapio */
    volatile uint32_t test_until_ms;
    volatile uint8_t  test_kind;         /* test_kind_t */
    volatile uint32_t test_start_ms;     /* kada testas pradėtas (animacijoms) */

    /* Ekrano peržiūra valdymo puslapyje: kadras kopijuojamas tik tol, kol puslapis jo prašo */
    uint16_t *volatile preview;          /* paskutinis kadras (PSRAM), w × h, RGB565 */
    volatile uint32_t preview_req_ms;    /* kada puslapis paskutinį kartą prašė kadro */

    /* Programos atnaujinimas iš AeroBox serverio (siūlomas valdymo puslapyje, diegiamas paspaudus „Update“) */
    char              upd_ver[24];       /* siūloma versija (tuščia – nėra) */
    char              upd_notes[128];
    char              upd_sha[65];       /* failo SHA-256 (šešioliktainiu pavidalu) */
    uint32_t          upd_size;
    volatile int8_t   upd_state;         /* 0 – nieko, 1 – diegiama, 2 – įdiegta (perkraunama), -1 – nepavyko */
    volatile uint8_t  upd_pct;
    const char *volatile upd_err;        /* nesėkmės priežastis (angliškai, rodoma puslapyje) */
} app_status_t;

/* Funkcijų demonstracijos (POST /api/test): kelias sekundes rodomos vietoj įprasto vaizdo */
typedef enum { TEST_NONE = 0, TEST_OVERHEAD, TEST_RARE, TEST_MAP, TEST_FLAP, TEST_TRACK, TEST_MULTI } test_kind_t;

extern app_status_t g_status;

/* Milisekundės nuo įjungimo (persivynioja po ~49 d., todėl lyginti tik skirtumais) */
uint32_t    sky_now_ms(void);
bool        sky_time_valid(void);          /* ar SNTP jau nustatė tikrą laiką */
wifi_why_t  sky_wifi_why(uint8_t reason);
/* Trumpas Wi-Fi klaidos tekstas ekranui (iki 16 simbolių) */
const char *sky_wifi_lcd_text(uint8_t reason);
