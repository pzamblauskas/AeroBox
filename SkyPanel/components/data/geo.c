/* Geometrija: atstumai, kryptys ir maršruto tikrumo patikra */
#include "aircraft.h"
#include <math.h>

#define DEG2RAD 0.017453292519943295f

/* Plokščios Žemės aproksimacija: pakankamai tiksli iki ~50 km nuo namų */
void geo_flat_km(const home_t *h, float lat, float lon, float *north_km, float *east_km)
{
    *north_km = (lat - h->lat) * 111.32f;
    *east_km  = (lon - h->lon) * 111.32f * h->cos_lat;
}

/* Didžiojo apskritimo atstumas (haversino formulė) */
float geo_haversine_km(float lat1, float lon1, float lat2, float lon2)
{
    float dlat = (lat2 - lat1) * DEG2RAD;
    float dlon = (lon2 - lon1) * DEG2RAD;
    float a = sinf(dlat / 2) * sinf(dlat / 2) +
              cosf(lat1 * DEG2RAD) * cosf(lat2 * DEG2RAD) * sinf(dlon / 2) * sinf(dlon / 2);
    return 2.0f * 6371.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

float geo_bearing_deg(float lat1, float lon1, float lat2, float lon2)
{
    const float p1 = lat1 * DEG2RAD, p2 = lat2 * DEG2RAD, dl = (lon2 - lon1) * DEG2RAD;
    const float b = atan2f(sinf(dl) * cosf(p2), cosf(p1) * sinf(p2) - sinf(p1) * cosf(p2) * cosf(dl)) / DEG2RAD;
    return b < 0 ? b + 360.0f : b;
}

/* Tikri skrydžių keliai dažnai apsuka uždarytą oro erdvę (Baltarusija, Rusija, Ukraina), todėl leidžiamas gerokas
 * nuokrypis – bet ne toks, kaip skrydžio, kuriam duomenų bazė priskyrė kitą maršrutą (pvz., IST → STR virš Lietuvos) */
bool route_plausible(const route_t *r, const aircraft_t *a)
{
    if (!r->has_pos) return true;
    const float d  = geo_haversine_km(r->orig_lat, r->orig_lon, r->dest_lat, r->dest_lon);
    const float d1 = geo_haversine_km(r->orig_lat, r->orig_lon, a->lat, a->lon);
    const float d2 = geo_haversine_km(a->lat, a->lon, r->dest_lat, r->dest_lon);
    if (d1 + d2 > d * 1.3f + 200.0f) return false;
    if (a->on_ground || d1 < 150.0f || d2 < 150.0f) return true;        /* prie oro uostų kursas gali būti bet koks */
    float diff = fabsf(geo_bearing_deg(a->lat, a->lon, r->dest_lat, r->dest_lon) - (float)a->track_deg);
    if (diff > 180.0f) diff = 360.0f - diff;
    return diff <= 120.0f;
}

void geo_move(float *lat, float *lon, float track_deg, float km)
{
    const float tr = track_deg * DEG2RAD;
    *lat += km * cosf(tr) / 111.32f;
    *lon += km * sinf(tr) / (111.32f * cosf(*lat * DEG2RAD));
}
