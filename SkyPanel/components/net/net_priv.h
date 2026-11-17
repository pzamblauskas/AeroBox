/* Vidinės tinklo modulio funkcijos (wifi.c ↔ portal.c ↔ webui.c); kitiems moduliams neskirtos. */
#pragma once
#include <stdbool.h>
#include "esp_netif.h"

esp_netif_t *net_ap_netif(void);
/* Nustatymų režime STA nesijungia, kai telefonas prisijungęs – kitaip pasikeistų AP kanalas ir telefonas atsijungtų */
void net_wifi_set_portal(bool on);
/* Bandomasis prisijungimas: 0 – pavyko, kitaip priežasties kodas (255 – neatsako) */
int  net_wifi_test(const char *ssid, const char *pass);
/* Patikrintas tinklas (net_wifi_test pavyko) tampa nuolatiniu – be perkrovimo */
void net_wifi_keep(void);
/* Po nepavykusio bandymo – vėl jungtis prie išsaugoto tinklo */
void net_wifi_restore(void);

/* Bandyti naują Wi-Fi fone: pavykus jis išsaugomas be perkrovimo, nepavykus grįžtama prie senojo.
 * Eiga – g_status.portal_state. false – bandymas jau vyksta. */
bool net_portal_try_wifi(const char *ssid, const char *pass);
/* Paskutinio nepavykusio bandymo paaiškinimas naudotojui (angliškai) */
const char *net_portal_fail_text(void);
