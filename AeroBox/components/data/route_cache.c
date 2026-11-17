/* Maršrutų talpykla: ROUTE_MAX paskutinių skrydžių maršrutai pagal šaukinį (pilna – keičiamas seniausias). */
#include "aircraft.h"
#include "app_status.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>

#define ROUTE_MAX       48
#define ROUTE_RETRY_MS  (60u * 60u * 1000u)   /* nerastas: bandyti vėl po valandos */
#define ROUTE_TTL_MS    (6u * 3600u * 1000u)  /* rastas: galioja 6 val. */

static route_t          *s_routes;            /* PSRAM: vidinė RAM paliekama HUB75 DMA buferiams */
static bool              s_used[ROUTE_MAX];
static SemaphoreHandle_t s_mtx;

void route_cache_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    s_routes = heap_caps_calloc(ROUTE_MAX, sizeof(route_t), MALLOC_CAP_SPIRAM);
    if (!s_routes) s_routes = calloc(ROUTE_MAX, sizeof(route_t));
}

static int find(const char *cs)
{
    for (int i = 0; i < ROUTE_MAX; i++)
        if (s_used[i] && strcmp(s_routes[i].callsign, cs) == 0) return i;
    return -1;
}

bool route_cache_get(const char *callsign, route_t *out)
{
    if (!s_routes || !callsign[0]) return false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int i = find(callsign);
    if (i >= 0) *out = s_routes[i];
    xSemaphoreGive(s_mtx);
    return i >= 0;
}

void route_cache_put(const route_t *r)
{
    if (!s_routes) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int i = find(r->callsign);
    if (i < 0) {
        int oldest = 0;
        for (i = 0; i < ROUTE_MAX; i++) {
            if (!s_used[i]) break;
            if (s_routes[i].fetched_ms < s_routes[oldest].fetched_ms) oldest = i;
        }
        if (i == ROUTE_MAX) i = oldest;
    }
    s_routes[i] = *r;
    s_used[i] = true;
    xSemaphoreGive(s_mtx);
}

bool route_cache_needs_lookup(const char *callsign, uint32_t now_ms)
{
    route_t r;
    if (!callsign[0]) return false;
    if (!route_cache_get(callsign, &r)) return true;
    uint32_t age = now_ms - r.fetched_ms;
    return r.known ? age > ROUTE_TTL_MS : age > ROUTE_RETRY_MS;
}
