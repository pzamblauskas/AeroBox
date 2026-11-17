/* Vienas mygtukas: paspaudimų atpažinimas su atšokimo (debounce) filtru. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* BTN_SHORT – trumpas paspaudimas; BTN_LONG – laikyta 0,8 s; BTN_RESET – laikyta 5 s (Wi-Fi nustatymų ištrynimas) */
typedef enum { BTN_NONE = 0, BTN_SHORT, BTN_LONG, BTN_RESET } button_event_t;

/* Mygtukas jungiamas tarp GPIO ir GND. gpio = -1 – mygtuko nėra */
void button_init(int gpio);
/* Kviesti maždaug kas 20 ms; grąžina įvykį arba BTN_NONE */
button_event_t button_poll(uint32_t now_ms);
/* Ar mygtukas nuspaustas šiuo metu (pvz., įjungimo metu) */
bool button_is_down(void);
