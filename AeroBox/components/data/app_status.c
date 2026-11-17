#include "app_status.h"
#include "esp_timer.h"
#include <time.h>

app_status_t g_status;

uint32_t sky_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool sky_time_valid(void)
{
    return time(NULL) > 1700000000;   /* 2023-11: anksčiau – laikas dar nesinchronizuotas */
}

/* ESP-IDF wifi_err_reason_t kodų grupavimas į naudotojui suprantamas priežastis */
wifi_why_t sky_wifi_why(uint8_t reason)
{
    switch (reason) {
    case 0:
        return WIFI_WHY_NONE;
    case 14:  /* MIC_FAILURE */
    case 15:  /* 4WAY_HANDSHAKE_TIMEOUT */
    case 16:  /* GROUP_KEY_UPDATE_TIMEOUT */
    case 23:  /* 802_1X_AUTH_FAILED */
    case 202: /* AUTH_FAIL */
    case 204: /* HANDSHAKE_TIMEOUT */
        return WIFI_WHY_PASSWORD;
    case 201: /* NO_AP_FOUND */
    case 210: /* NO_AP_FOUND_W_COMPATIBLE_SECURITY */
    case 211: /* NO_AP_FOUND_IN_AUTHMODE_THRESHOLD */
    case 212: /* NO_AP_FOUND_IN_RSSI_THRESHOLD */
        return WIFI_WHY_NOT_FOUND;
    default:
        return WIFI_WHY_OTHER;
    }
}

const char *sky_wifi_lcd_text(uint8_t reason)
{
    switch (sky_wifi_why(reason)) {
    case WIFI_WHY_NONE:      return "CONNECTING...";
    case WIFI_WHY_PASSWORD:  return "WRONG PASSWORD";
    case WIFI_WHY_NOT_FOUND: return "WI-FI NOT FOUND";
    default:                 return "WI-FI FAILED";
    }
}
