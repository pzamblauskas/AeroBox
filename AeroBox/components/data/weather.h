/* Orai namų vietoje (Open-Meteo, kas 15 min). Į juos rašo net užduotis, o skaito ekranas ir valdymo puslapis. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     valid;
    float    temp_c;
    uint8_t  code;          /* WMO orų kodas (0 – giedra … 99 – perkūnija su kruša) */
    bool     is_day;
    uint32_t fetched_ms;
} weather_t;

/* Orų grupės ekrano piktogramai */
typedef enum { WX_CLEAR = 0, WX_PARTLY, WX_CLOUDY, WX_FOG, WX_DRIZZLE, WX_RAIN, WX_SNOW, WX_STORM } wx_kind_t;

void        weather_set(const weather_t *w);
/* false – orų dar nėra arba jie senesni nei 2 val. */
bool        weather_get(weather_t *out);
/* Pasikeitus vietai, ankstesni orai nebetinka */
void        weather_clear(void);
wx_kind_t   weather_kind(uint8_t code);
/* Trumpas tekstas ekranui: „CLEAR“, „PARTLY CLOUDY“, „RAIN“ … */
const char *weather_text(uint8_t code);
