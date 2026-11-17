/* HUB75 kontaktų žemėlapis: ESP32-S3-N16R8 → P2.5 64×32 panelė (1/16 skenavimas, ICN2037 ir TC7258 lustai).
 * Bet kurią reikšmę galima pakeisti nekeičiant šio failo: -D PANEL_PIN_xx=… (platformio.ini, build_flags). */
#pragma once

/* Panelių išdėstymas (1 × 64×32, 4 × 64×32 = 128×64 arba 1 × 128×64) saugomas NVS – žr.
 * components/hub75/hub75_layout.c. Numatytasis – CONFIG_SKY_PANEL_LAYOUT; puslapyje jis nerodomas,
 * prireikus keičiamas per /api/settings (layout=…, tiles=…, co=…, psu=…). */

/* Vienos 64×32 P2.5 panelės srovė (mA), jei visi taškai šviestų baltai 100 % laiko; naudojama srovei riboti.
 * Tvarkyklė šviečia ~1/3 šio laiko, todėl iš tikrųjų visiškai balta panelė ima ~1,2 A. */
#ifndef PANEL_FULL_WHITE_MA
#define PANEL_FULL_WHITE_MA 3600
#endif

#ifndef PANEL_PIN_R1
#define PANEL_PIN_R1  4
#endif
#ifndef PANEL_PIN_G1
#define PANEL_PIN_G1  5
#endif
#ifndef PANEL_PIN_B1
#define PANEL_PIN_B1  6
#endif
#ifndef PANEL_PIN_R2
#define PANEL_PIN_R2  7
#endif
#ifndef PANEL_PIN_G2
#define PANEL_PIN_G2  8
#endif
#ifndef PANEL_PIN_B2
#define PANEL_PIN_B2  9
#endif
#ifndef PANEL_PIN_A
#define PANEL_PIN_A   1
#endif
#ifndef PANEL_PIN_B
#define PANEL_PIN_B   2
#endif
#ifndef PANEL_PIN_C
#define PANEL_PIN_C   41
#endif
#ifndef PANEL_PIN_D
#define PANEL_PIN_D   42
#endif
#ifndef PANEL_PIN_E
#define PANEL_PIN_E   10      /* reikalingas tik 128×64 (1/32 skenavimo) panelei; 64×32 panelei nejungiamas */
#endif
#ifndef PANEL_PIN_LAT
#define PANEL_PIN_LAT 40
#endif
#ifndef PANEL_PIN_OE
#define PANEL_PIN_OE  39
#endif
#ifndef PANEL_PIN_CLK
#define PANEL_PIN_CLK 14
#endif

/* Laisvas GPIO, prie nieko NEJUNGIAMAS: LCD_CAM periferijai būtina DC linija ir 16 duomenų bitų */
#ifndef PANEL_PIN_SPARE
#define PANEL_PIN_SPARE 47
#endif

/* CLK dažnis. Jei laidai ilgi arba vaizdas mirga, bandykite 8000000 arba 5000000 */
#ifndef PANEL_PCLK_HZ
#define PANEL_PCLK_HZ 10000000
#endif

/* Jei vaizdas pasislinkęs per vieną tašką arba „dvigubas“, bandykite 1 */
#ifndef PANEL_CLK_FALLING
#define PANEL_CLK_FALLING 0
#endif

/* Mygtukas jungiamas tarp GPIO ir GND (naudojamas vidinis pull-up rezistorius). -1 – mygtuko nėra */
#ifndef BUTTON_PIN
#define BUTTON_PIN    21
#endif
