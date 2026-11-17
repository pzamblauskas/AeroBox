/* Sekamas skrydis. Naudotojas puslapyje įveda šaukinį (BTI658), skrydžio numerį (BT658), registraciją (YL-ABW)
 * arba ICAO adresą (502D96). Kol lėktuvas toli, net užduotis jo ieško visame pasaulyje atskira užklausa;
 * patekęs į spindulį, jis atpažįstamas bendrame sąraše. Ekranui teikiama: duomenys apie lėktuvą, kiek liko iki
 * nusileidimo, išskyrimas radare. */
#pragma once
#include <stddef.h>
#include "aircraft.h"

typedef enum {
    TRACK_OFF = 0,     /* nieko nesekama */
    TRACK_SEARCHING,   /* ieškoma, atsakymo dar nėra */
    TRACK_LIVE,        /* yra nauja padėtis */
    TRACK_LOST,        /* buvo rastas, bet kelias minutes nebegaunamas (nusileido arba dingo iš aprėpties) */
    TRACK_NOT_FOUND,   /* šiuo metu ore nėra */
} track_state_t;

typedef struct {
    track_state_t state;
    char       query[12];     /* naudotojo įvestis (didžiosiomis raidėmis, be tarpų) */
    aircraft_t ac;            /* dabartinė padėtis (pastumta pagal greitį); dist_km – nuo namų; orig/dest/progress */
    uint32_t   age_s;         /* prieš kiek sekundžių gauta padėtis */
    bool       in_range;      /* yra radaro spinduliu */
    char       dest_city[16];
    char       airline[24];
    float      remain_km;     /* iki paskirties oro uosto; neigiama – nežinoma */
    int        eta_min;       /* minutės iki nusileidimo; neigiama – nežinoma */
    bool       landed;
    uint32_t   landed_s;      /* prieš kiek sekundžių nusileido (kai landed) */
} track_t;

/* Iš nustatymų arba puslapio; "" – nebesekti. Didžiosios raidės, be tarpų. */
void     track_set_query(const char *q);
void     track_query(char *out, size_t n);
/* Net užduočiai: šaukinys, pagal kurį ieškoma (BT658 → BTI658); iš pradžių – pati įvestis */
void     track_set_callsign(const char *cs);
void     track_callsign(char *out, size_t n);
/* Net užduočiai: ar atsakyme esantis lėktuvas yra sekamas (pagal šaukinį, registraciją ar ICAO adresą) */
bool     track_matches(const aircraft_t *a);
void     track_found(const aircraft_t *a);
void     track_missing(void);
/* Kada paskutinį kartą gauta padėtis (0 – dar niekada) */
uint32_t track_last_ms(void);
/* Ekranui ir puslapiui; false – nieko nesekama */
bool     track_get(track_t *out);
/* Ar šis lėktuvas (iš kopijos) yra sekamas */
bool     track_is(const aircraft_t *a);
/* Sekamas skrydis nusileido daugiau nei prieš 2 min. („LANDED“ jau parodytas) – sekti nebėra prasmės */
bool     track_finished(void);
