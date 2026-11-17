/* Wi-Fi nustatymų tinklas (captive portal):
 *   1. Įrenginys sukuria tinklą SKY_AP_SSID su WPA2 slaptažodžiu SKY_AP_PASS (abu rodomi ekrane), adresas 192.168.4.1.
 *   2. DNS serveris į visas užklausas atsako 192.168.4.1, todėl telefonas pats atidaro valdymo puslapį.
 *   3. Puslapis (webui.c) tas pats kaip namų tinkle: Wi-Fi, vieta, ekranas, sistema.
 *      Naujas Wi-Fi pirmiausia išbandomas ir išsaugomas tik tada, kai prisijungti pavyko. */
#include "net.h"
#include "net_priv.h"
#include "app_status.h"
#include "settings.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "portal";
#define PORTAL_URL "http://192.168.4.1/"

static volatile bool s_dns_run;
static int           s_dns_sock = -1;

/* ---------- DNS: į bet kurią A užklausą atsako 192.168.4.1 ---------- */
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    int one = 1;
    if (sock >= 0) setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);   /* kad po išjungimo iškart būtų galima vėl įjungti */
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
        ESP_LOGE(TAG, "DNS socket klaida");
        if (sock >= 0) close(sock);
        vTaskDelete(NULL);
        return;
    }
    struct timeval tv = { .tv_sec = 1 };                 /* kas sekundę tikrinama, ar DNS dar reikia */
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    s_dns_sock = sock;
    static uint8_t buf[512];
    static const uint8_t answer[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1 };
    while (s_dns_run) {
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = recvfrom(sock, buf, sizeof buf - sizeof answer, 0, (struct sockaddr *)&from, &fl);
        if (n < 12 || (buf[2] & 0x80)) continue;        /* tik užklausos (ne atsakymai) */
        int p = 12;
        while (p < n && buf[p]) p += buf[p] + 1;        /* praleidžiamas klausimo vardas */
        p += 5;                                          /* 0 + QTYPE + QCLASS */
        if (p > n) continue;
        uint16_t qtype = (uint16_t)(buf[p - 4] << 8 | buf[p - 3]);
        buf[2] = 0x81;                                   /* žymima kaip atsakymas */
        buf[3] = 0x80;
        buf[6] = 0;
        buf[7] = qtype == 1 ? 1 : 0;                     /* atsakoma tik į A (IPv4) užklausas */
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        int len = p;
        if (qtype == 1) {
            memcpy(buf + p, answer, sizeof answer);
            len += sizeof answer;
        }
        sendto(sock, buf, len, 0, (struct sockaddr *)&from, fl);
    }
    close(sock);
    s_dns_sock = -1;
    vTaskDelete(NULL);
}

/* ---------- Bandomasis prisijungimas ---------- */
static struct {
    char ssid[33], pass[65];
    int  reason;
} s_try;

const char *net_portal_fail_text(void)
{
    int r = s_try.reason;
    if (r == 255)
        return "The router does not answer. Move AeroBox closer to the router and try again.";
    switch (sky_wifi_why((uint8_t)r)) {
    case WIFI_WHY_PASSWORD:
        return "Wrong password. Check upper and lower case letters and that there is no space at the end.";
    case WIFI_WHY_NOT_FOUND:
        return "Network not found. AeroBox sees only 2.4 GHz networks - check your router settings "
               "(on an iPhone hotspot turn on Maximize Compatibility).";
    default:
        return "Could not connect. Please try again.";
    }
}

static void test_task(void *arg)
{
    strlcpy(g_status.portal_target, s_try.ssid, sizeof g_status.portal_target);
    int r = net_wifi_test(s_try.ssid, s_try.pass);
    s_try.reason = r;
    if (r == 0) {
        /* Be perkrovimo: tinklas jau prisijungtas, todėl jis išsaugomas, ekrane 5 s rodoma „CONNECTED!“ ir IP
         * (kad telefonas spėtų parodyti adresą), tada nustatymų tinklas išjungiamas, o ekranas sklandžiai
         * pereina į įprastą vaizdą */
        settings_save_wifi(s_try.ssid, s_try.pass);
        net_wifi_keep();
        g_status.portal_state = PORTAL_OK;
        vTaskDelay(pdMS_TO_TICKS(5000));
        net_portal_stop();
        g_status.portal_state = PORTAL_IDLE;
        vTaskDelete(NULL);
        return;
    }
    g_status.portal_state = PORTAL_FAIL;
    net_wifi_restore();                         /* grįžtama prie senojo tinklo (jei jis buvo) */
    vTaskDelete(NULL);
}

bool net_portal_try_wifi(const char *ssid, const char *pass)
{
    if (g_status.portal_state == PORTAL_TESTING || g_status.portal_state == PORTAL_OK) return false;
    strlcpy(s_try.ssid, ssid, sizeof s_try.ssid);
    strlcpy(s_try.pass, pass, sizeof s_try.pass);
    g_status.portal_state = PORTAL_TESTING;
    if (xTaskCreate(test_task, "wifitest", 4096, NULL, 5, NULL) != pdPASS) {
        g_status.portal_state = PORTAL_IDLE;
        return false;
    }
    return true;
}

void net_portal_start(void)
{
    if (g_status.portal) return;
    net_wifi_set_portal(true);

    strlcpy(g_status.portal_ssid, SKY_AP_SSID, sizeof g_status.portal_ssid);
    strlcpy(g_status.portal_pass, SKY_AP_PASS, sizeof g_status.portal_pass);   /* rodomas ekrane */

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, SKY_AP_SSID, sizeof ap.ap.ssid);
    ap.ap.ssid_len = (uint8_t)strlen(SKY_AP_SSID);
    strlcpy((char *)ap.ap.password, g_status.portal_pass, sizeof ap.ap.password);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.capable = true;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();

    /* DHCP 114 parinktis (RFC 8910): naujesni telefonai puslapį atidaro iškart */
    esp_netif_t *apif = net_ap_netif();
    static char uri[] = PORTAL_URL;
    esp_netif_dhcps_stop(apif);
    esp_netif_dhcps_option(apif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri, strlen(uri));
    esp_netif_dhcps_start(apif);

    s_dns_run = true;
    xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL);

    g_status.portal_since_ms = sky_now_ms();
    g_status.portal = true;
    ESP_LOGI(TAG, "nustatymu tinklas: prisijunkite prie %s (slaptazodis - ekrane) ir atidarykite %s", SKY_AP_SSID,
             PORTAL_URL);
}

void net_portal_stop(void)
{
    if (!g_status.portal) return;
    s_dns_run = false;                          /* DNS užduotis baigsis per ≤ 1 s */
    g_status.portal = false;
    g_status.portal_client = false;
    net_wifi_set_portal(false);
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "nustatymu tinklas isjungtas");
}
