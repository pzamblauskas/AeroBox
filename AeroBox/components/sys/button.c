#include "button.h"
#include "driver/gpio.h"

#define DEBOUNCE_MS  30      /* tiek laiko būsena turi nesikeisti, kad būtų laikoma tikra */
#define LONG_MS      800
#define RESET_MS     5000

static int      s_gpio = -1;
static bool     s_stable, s_last_raw, s_long_fired, s_reset_fired;
static uint32_t s_raw_since, s_press_ms;

void button_init(int gpio)
{
    s_gpio = gpio;
    if (gpio < 0) return;
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&c);
}

bool button_is_down(void)
{
    return s_gpio >= 0 && gpio_get_level(s_gpio) == 0;   /* nuspaustas mygtukas sujungia GPIO su GND */
}

/* Trumpas paspaudimas pranešamas atleidus mygtuką (jei dar nebuvo pranešta apie ilgą).
 * Ilgas ir RESET pranešami dar laikant: kiekvienas po vieną kartą per paspaudimą. */
button_event_t button_poll(uint32_t now)
{
    if (s_gpio < 0) return BTN_NONE;
    bool raw = gpio_get_level(s_gpio) == 0;
    if (raw != s_last_raw) {
        s_last_raw = raw;
        s_raw_since = now;
    }
    if (now - s_raw_since < DEBOUNCE_MS) return BTN_NONE;

    if (raw != s_stable) {
        s_stable = raw;
        if (raw) {
            s_press_ms = now;
            s_long_fired = false;
            s_reset_fired = false;
        } else if (!s_long_fired) {
            return BTN_SHORT;
        }
    } else if (raw && !s_long_fired && now - s_press_ms >= LONG_MS) {
        s_long_fired = true;
        return BTN_LONG;
    } else if (raw && !s_reset_fired && now - s_press_ms >= RESET_MS) {
        s_reset_fired = true;
        return BTN_RESET;
    }
    return BTN_NONE;
}
