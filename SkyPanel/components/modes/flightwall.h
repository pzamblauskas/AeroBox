/* FlightWall stiliaus variantas (Kconfig SKY_FLIGHTWALL – atskiras programos failas): skrydžio kortelė su
 * aviakompanijos logotipu (fw_card.c); logotipai – logos.c */
#pragma once
#include <stdint.h>
#include "aircraft.h"
#include "gfx.h"

#define FW_LOGO 32                    /* logotipo kraštinė taškais */

typedef struct {
    char            icao[4];          /* šaukinio pradžia, pvz., RYR */
    const char     *name;             /* trumpas pavadinimas, kaip FlightWall: „Ryanair“, „United“ */
    const uint16_t *logo;             /* FW_LOGO × FW_LOGO RGB565 juodame fone (0 – fonas); NULL – logotipo nėra */
} fw_airline_t;

extern const fw_airline_t FW_AIRLINES[];   /* surikiuota pagal icao (dvejetainei paieškai) */
extern const int          FW_AIRLINES_N;

/* Aviakompanija pagal šaukinį (3 raidės ir skaitmuo: RYR2LD); NULL – nežinoma arba tai registracija */
const fw_airline_t *fw_airline(const char *callsign);
/* 128×64 kortelė; shown_ms – kiek laiko rodomas šis lėktuvas (apatinės eilutės keičiasi) */
void fw_card_draw(gfx_t *g, const aircraft_t *a, uint32_t shown_ms);
