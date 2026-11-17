/* Wi-Fi: prisijungimas prie namų tinklo su pakartotiniais bandymais, AP + STA režimas nustatymų tinklui, SNTP. */
#include "net.h"
#include "net_priv.h"
#include "app_status.h"
#include "settings.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "wifi";

#define WIFI_OK_BIT   BIT0
#define TEST_OK_BIT   BIT1
#define TEST_FAIL_BIT BIT2

static EventGroupHandle_t s_ev;
static esp_timer_handle_t s_retry_timer;
static esp_netif_t       *s_ap_netif;
static int                s_attempt;
static volatile bool      s_sta_enabled, s_portal, s_testing;

/* Pakartotinio prisijungimo atidėjimas: 1, 2, 4 … 64 s ir atsitiktinis priedas (jitter) */
static void net_wifi_portal_client_joined(void);

static uint32_t backoff_ms(int attempt)
{
    uint32_t base = 1000u << (attempt < 6 ? attempt : 6);
    return base / 2 + esp_random() % base;
}

static void retry_cb(void *arg)
{
    /* Nustatymų režime bandoma fone, bet ne tada, kai prisijungęs telefonas (kad nepasikeistų AP kanalas) */
    if (!s_sta_enabled || s_testing || (s_portal && g_status.portal_client)) return;
    esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_sta_enabled) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        g_status.wifi = false;
        g_status.wifi_reason = d->reason;
        xEventGroupClearBits(s_ev, WIFI_OK_BIT);
        if (s_testing) {
            xEventGroupSetBits(s_ev, TEST_FAIL_BIT);
            return;
        }
        if (!s_sta_enabled) return;
        if (s_portal && g_status.portal_client) return;
        uint32_t ms = backoff_ms(s_attempt++);
        ESP_LOGW(TAG, "atsijungta (priezastis %d: %s), bandysiu po %lu ms", d->reason,
                 sky_wifi_lcd_text(d->reason), (unsigned long)ms);
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t)ms * 1000);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        g_status.portal_client = true;
        ESP_LOGI(TAG, "prie AeroBox tinklo prisijunge telefonas");
        net_wifi_portal_client_joined();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        /* Telefonas atsijungė ir daugiau niekas neprisijungęs – vėl bandoma jungtis prie išsaugoto tinklo */
        wifi_sta_list_t list;
        if (esp_wifi_ap_get_sta_list(&list) == ESP_OK && list.num == 0) {
            g_status.portal_client = false;
            if (s_sta_enabled && !s_testing && !g_status.wifi &&
                g_status.portal_state != PORTAL_TESTING && g_status.portal_state != PORTAL_OK) {
                esp_timer_stop(s_retry_timer);
                esp_timer_start_once(s_retry_timer, 3000 * 1000ULL);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "prisijungta, IP " IPSTR, IP2STR(&e->ip_info.ip));
        snprintf(g_status.ip, sizeof g_status.ip, IPSTR, IP2STR(&e->ip_info.ip));
        s_attempt = 0;
        g_status.wifi = true;
        g_status.wifi_reason = 0;
        xEventGroupSetBits(s_ev, WIFI_OK_BIT);
        if (s_testing) xEventGroupSetBits(s_ev, TEST_OK_BIT);
    }
}

void net_init(const char *tz)
{
    s_ev = xEventGroupCreate();
    const esp_timer_create_args_t targs = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_retry_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta, "aerobox");             /* maršruto parinktuvo įrenginių sąraše – ne „espressif“ */
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&icfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   /* nustatymai saugomi mūsų pačių NVS srityje (settings.c) */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    /* Laikas per SNTP (laikrodžio režimui ir nakties grafikui) */
    setenv("TZ", tz, 1);
    tzset();
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sc);
}

static void sta_config(wifi_config_t *wc, const char *ssid, const char *pass)
{
    memset(wc, 0, sizeof *wc);
    strlcpy((char *)wc->sta.ssid, ssid, sizeof wc->sta.ssid);
    strlcpy((char *)wc->sta.password, pass, sizeof wc->sta.password);
    wc->sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;  /* WPA, WPA2, WPA3 */
    wc->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;                                     /* WPA3 */
    wc->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;          /* jei keli prieigos taškai turi tą patį vardą, */
    wc->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;      /* jungiamasi prie stipriausio */
}

void net_wifi_connect(const char *ssid, const char *pass)
{
    wifi_config_t wc;
    sta_config(&wc, ssid, pass);
    s_sta_enabled = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);   /* įrenginys maitinamas iš tinklo – svarbiau maža delsa nei taupymas */
    ESP_LOGI(TAG, "jungiamasi prie %s", ssid);
}

int net_wifi_test(const char *ssid, const char *pass)
{
    wifi_config_t wc;
    sta_config(&wc, ssid, pass);
    s_testing = true;
    esp_timer_stop(s_retry_timer);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));                     /* senojo ryšio atsijungimo įvykis neturi būti palaikytas klaida */
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    int result = 255;                                   /* 255 – neatsako */
    for (int attempt = 0; attempt < 2; attempt++) {     /* WPA „rankos paspaudimo“ klaidos kartais būna atsitiktinės */
        xEventGroupClearBits(s_ev, TEST_OK_BIT | TEST_FAIL_BIT);
        g_status.wifi_reason = 0;
        esp_wifi_connect();
        EventBits_t b = xEventGroupWaitBits(s_ev, TEST_OK_BIT | TEST_FAIL_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(15000));
        if (b & TEST_OK_BIT) {
            result = 0;
            break;
        }
        result = (b & TEST_FAIL_BIT) ? g_status.wifi_reason : 255;
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    s_testing = false;
    ESP_LOGI(TAG, "tikrinimas '%s': %s (%d)", ssid, result == 0 ? "pavyko" : sky_wifi_lcd_text((uint8_t)result), result);
    return result;
}

void net_wifi_keep(void)
{
    s_sta_enabled = true;                               /* ir pirmą kartą (be išsaugoto tinklo STA nebuvo naudojamas) */
    s_attempt = 0;
    esp_wifi_set_ps(WIFI_PS_NONE);
}

void net_wifi_restore(void)
{
    if (!g_settings.wifi_ssid[0]) return;
    wifi_config_t wc;
    sta_config(&wc, g_settings.wifi_ssid, g_settings.wifi_pass);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    s_sta_enabled = true;
    s_attempt = 0;
    if (!(s_portal && g_status.portal_client)) esp_wifi_connect();
    ESP_LOGI(TAG, "vel jungiamasi prie issaugoto '%s'", g_settings.wifi_ssid);
}

bool net_wifi_wait(uint32_t timeout_ms)
{
    EventBits_t b = xEventGroupWaitBits(s_ev, WIFI_OK_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (b & WIFI_OK_BIT) != 0;
}

esp_netif_t *net_ap_netif(void)
{
    return s_ap_netif;
}

void net_wifi_set_portal(bool on)
{
    s_portal = on;
    if (on && g_status.portal_client) esp_timer_stop(s_retry_timer);
}

static void net_wifi_portal_client_joined(void)
{
    /* Telefonas prisijungė prie AeroBox tinklo – fone nebesijungiama */
    esp_timer_stop(s_retry_timer);
    if (s_sta_enabled && !s_testing && !g_status.wifi) esp_wifi_disconnect();
}
