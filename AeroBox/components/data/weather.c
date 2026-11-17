#include "weather.h"
#include "app_status.h"
#include "freertos/FreeRTOS.h"

#define MAX_AGE_MS (2u * 3600u * 1000u)   /* senesni orai nerodomi */

static weather_t    s_wx;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void weather_set(const weather_t *w)
{
    portENTER_CRITICAL(&s_lock);
    s_wx = *w;
    portEXIT_CRITICAL(&s_lock);
}

bool weather_get(weather_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_wx;
    portEXIT_CRITICAL(&s_lock);
    return out->valid && sky_now_ms() - out->fetched_ms < MAX_AGE_MS;
}

void weather_clear(void)
{
    portENTER_CRITICAL(&s_lock);
    s_wx.valid = false;
    portEXIT_CRITICAL(&s_lock);
}

/* WMO kodų grupavimas (https://open-meteo.com/en/docs, „WMO Weather interpretation codes“) */
wx_kind_t weather_kind(uint8_t code)
{
    if (code <= 1) return WX_CLEAR;
    if (code == 2) return WX_PARTLY;
    if (code == 3) return WX_CLOUDY;
    if (code == 45 || code == 48) return WX_FOG;
    if (code >= 51 && code <= 57) return WX_DRIZZLE;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WX_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WX_SNOW;
    if (code >= 95) return WX_STORM;
    return WX_CLOUDY;
}

const char *weather_text(uint8_t code)
{
    switch (code) {
    case 0:  return "CLEAR";
    case 1:  return "MOSTLY CLEAR";
    case 2:  return "PARTLY CLOUDY";
    case 3:  return "CLOUDY";
    case 45:
    case 48: return "FOG";
    case 51:
    case 53:
    case 55: return "DRIZZLE";
    case 56:
    case 57: return "FREEZING DRIZZLE";
    case 61: return "LIGHT RAIN";
    case 63: return "RAIN";
    case 65: return "HEAVY RAIN";
    case 66:
    case 67: return "FREEZING RAIN";
    case 71: return "LIGHT SNOW";
    case 73: return "SNOW";
    case 75: return "HEAVY SNOW";
    case 77: return "SNOW GRAINS";
    case 80:
    case 81: return "SHOWERS";
    case 82: return "HEAVY SHOWERS";
    case 85:
    case 86: return "SNOW SHOWERS";
    case 95: return "THUNDERSTORM";
    case 96:
    case 99: return "STORM, HAIL";
    default: return "";
    }
}
