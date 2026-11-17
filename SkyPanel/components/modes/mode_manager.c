/* Režimų tvarkyklė: pagal naudotojo vaizdą (view_t), lėktuvus ir sekamą skrydį parenka, kurį režimą rodyti dabar,
 * ir valdo AUTO bei TRACKING ciklus (kiekvienas elementas rodomas savo laiką). Bet kuriame vaizde gali įsiterpti
 * „LOOK UP!“ (overhead.c), kai lėktuvas skrenda virš namų. */
#include "mode.h"
#include "track.h"

#define CARD_MAX_AC     5               /* kortelės – po card_ms() kiekvienam lėktuvui */
#define RADAR_MS        10000
#define CLOCK_MS        10000
#define TRACK_MAP_MS    8000            /* sekamo skrydžio maršruto žemėlapis (kortelė – TRACK_CARD_MS, mode.h) */
#define OVERHEAD_MS     8000            /* „LOOK UP!“ trukmė */
#define OVERHEAD_GAP_MS 60000           /* ne dažniau nei kas minutę (prie oro uosto lėktuvų daug) */
#define OVERHEAD_SEEN   8               /* tas pats lėktuvas – tik vieną kartą (prisimenami paskutiniai) */

/* Tikros reikšmės (ir spalvos) nukopijuojamos iš nustatymų kiekvienam kadrui (app_main.c) */
mode_prefs_t g_mode_prefs = { .radar_style = RADAR_SONAR, .radar_mark = MARK_ARROW, .track_card = true,
                              .track_map = true, .track_radar = true, .card_fields = CF_ALL,
                              .pane = { PANE_RADAR, PANE_CARD } };

static volatile view_t s_view = VIEW_AUTO;
static view_t s_seg_view;              /* kuriam vaizdui skaičiuojamas ciklas */
static const display_mode_t *s_cur;
static const display_mode_t *s_seg;    /* AUTO ir TRACKING: dabartinis ciklo režimas */
static uint32_t s_seg_until;
static uint32_t s_ovh_until, s_ovh_last;   /* „lėktuvas virš galvos“: iki kada rodoma ir kada rodyta paskutinį kartą */
static uint32_t s_ovh_seen[OVERHEAD_SEEN];
static int s_ovh_pos;
static bool s_ovh_shown;               /* ciklas sustojo „LOOK UP!“ metu */

void mode_manager_set_view(view_t v)
{
    s_view = v < VIEW_COUNT ? v : VIEW_AUTO;
}

view_t mode_manager_view(void)
{
    return s_view;
}

const char *mode_manager_view_name(view_t v)
{
    static const char *names[VIEW_COUNT] = { "AUTO", "FLIGHTS", "RADAR", "CLOCK", "TRACKING", "CUSTOM", "ROUTE MAP" };
    return v < VIEW_COUNT ? names[v] : "?";
}

/* Sekamo skrydžio padėtis žinoma (ne „ieškoma“ ir ne „šiuo metu ore nėra“) – yra ką rodyti žemėlapyje */
static bool track_has_pos(void)
{
    track_t t;
    return track_get(&t) && (t.state == TRACK_LIVE || t.state == TRACK_LOST);
}

/* Sekamas skrydis yra ore radaro spinduliu */
static bool tracked_on_radar(const aircraft_snapshot_t *s)
{
    for (int i = 0; i < s->count; i++)
        if (s->ac[i].tracked) return true;
    return false;
}

static uint32_t seg_ms(const display_mode_t *m, const aircraft_snapshot_t *s)
{
    if (m == &MODE_TRACK) return track_cycle_ms();
    if (m == &MODE_TRACK_MAP) return TRACK_MAP_MS;
    if (m == &MODE_CLOCK) return CLOCK_MS;
    if (m == &MODE_ROUTES) return (uint32_t)routes_count(s) * ROUTES_HI_MS;
    if (m == &MODE_FLIGHT_CARD) return (uint32_t)(s->count < CARD_MAX_AC ? s->count : CARD_MAX_AC) * card_ms();
    return RADAR_MS;
}

/* Ciklas: list[] elementai paeiliui, kiekvienas savo laiką. Jei dabartinio nebeliko sąraše (pvz., sekamas išskrido
 * iš spindulio) – pradedama nuo pradžios. */
static const display_mode_t *rotate(const display_mode_t *const *list, int n, const aircraft_snapshot_t *s,
                                    uint32_t ms)
{
    int pos = -1;
    for (int i = 0; i < n; i++)
        if (list[i] == s_seg) pos = i;
    if (pos < 0 || (int32_t)(ms - s_seg_until) >= 0) {
        s_seg = pos < 0 ? list[0] : list[(pos + 1) % n];
        s_seg_until = ms + seg_ms(s_seg, s);
    }
    return s_seg;
}

/* Sekamo skrydžio langai pagal nustatymus (skiltis „Track“). Kol padėtis nežinoma (ieškoma, šiuo metu ore nėra) –
 * tik kortelė su užrašu, net jei ji nepasirinkta (žemėlapyje nebūtų ką rodyti). Grąžina, kiek langų pridėta. */
static int track_windows(const display_mode_t **list, const aircraft_snapshot_t *s, bool with_radar)
{
    const bool pos = track_has_pos();
    int n = 0;
    if (g_mode_prefs.track_card || !pos || !g_mode_prefs.track_map) list[n++] = &MODE_TRACK;
    if (g_mode_prefs.track_map && pos) list[n++] = &MODE_TRACK_MAP;
    if (with_radar && g_mode_prefs.track_radar && pos && tracked_on_radar(s)) list[n++] = &MODE_RADAR;
    return n;
}

/* AUTO: [sekamo kortelė + žemėlapis] + (kortelės, radaras, [maršrutų žemėlapis] arba laikrodis, kai tuščia), paeiliui.
 * Maršrutų žemėlapis – kai žinomas bent vieno artimo skrydžio maršrutas. Spinduliu esantis sekamas skrydis matomas
 * radare (baltas, su žiedu) ir kortelėse. */
static const display_mode_t *auto_pick(const aircraft_snapshot_t *s, uint32_t ms)
{
    char q[12];
    track_query(q, sizeof q);
    const display_mode_t *list[5];
    int n = 0;
    if (q[0]) n += track_windows(list, s, false);
    if (s->count > 0) {
        list[n++] = &MODE_FLIGHT_CARD;
        list[n++] = &MODE_RADAR;
        if (routes_count(s) > 0) list[n++] = &MODE_ROUTES;
    } else {
        list[n++] = &MODE_CLOCK;
    }
    return rotate(list, n, s, ms);
}

/* TRACKING: kortelė → maršruto žemėlapis → radaras (kai sekamas yra spinduliu) → kortelė … – tik pasirinkti langai.
 * Kol padėtis nežinoma (ieškoma, šiuo metu ore nėra, niekas nesekama) – tik kortelė su užrašu. */
static const display_mode_t *track_pick(const aircraft_snapshot_t *s, uint32_t ms)
{
    const display_mode_t *list[3];
    const int n = track_windows(list, s, true);
    return rotate(list, n, s, ms);
}

/* Lėktuvas arčiau nei overhead_km (ir dar nerodytas) – „LOOK UP!“ bet kuriame vaizde */
static bool overhead_now(const aircraft_snapshot_t *s, uint32_t ms)
{
    if (s_ovh_until) {
        if ((int32_t)(ms - s_ovh_until) < 0) return true;
        s_ovh_until = 0;
    }
    if (!g_mode_prefs.overhead_km || (s_ovh_last && ms - s_ovh_last < OVERHEAD_GAP_MS)) return false;
    for (int i = 0; i < s->count; i++) {                 /* surikiuota pagal atstumą, todėl pirmas – artimiausias */
        const aircraft_t *a = &s->ac[i];
        if (a->dist_km > g_mode_prefs.overhead_km) break;
        bool seen = a->on_ground;
        for (int k = 0; k < OVERHEAD_SEEN && !seen; k++) seen = s_ovh_seen[k] == a->icao24;
        if (seen) continue;
        s_ovh_seen[s_ovh_pos] = a->icao24;
        s_ovh_pos = (s_ovh_pos + 1) % OVERHEAD_SEEN;
        overhead_start(a, ms);
        s_ovh_until = (ms + OVERHEAD_MS) | 1;
        s_ovh_last = ms | 1;
        return true;
    }
    return false;
}

const display_mode_t *mode_manager_select(const aircraft_snapshot_t *s, uint32_t ms)
{
    const display_mode_t *m;
    const view_t v = s_view;
    if (v != s_seg_view) {                        /* kitas vaizdas – jo ciklas nuo pradžios */
        s_seg_view = v;
        s_seg = NULL;
    }
    if (overhead_now(s, ms)) {
        m = &MODE_OVERHEAD;
        s_ovh_shown = true;
    } else {
        if (s_ovh_shown) {                        /* ciklas buvo sustojęs – dabartinis rodomas likusį laiką */
            s_ovh_shown = false;
            s_seg_until += OVERHEAD_MS;
        }
        switch (v) {
        case VIEW_CARD:   m = &MODE_FLIGHT_CARD; break;
        case VIEW_RADAR:  m = &MODE_RADAR; break;
        case VIEW_CLOCK:  m = &MODE_CLOCK; break;
        case VIEW_CUSTOM: m = &MODE_CUSTOM; break;
        case VIEW_ROUTES: m = &MODE_ROUTES; break;
        case VIEW_TRACK:  m = track_pick(s, ms); break;
        default:          m = auto_pick(s, ms); break;
        }
    }
    if (m != s_cur) {
        s_cur = m;
        if (m->enter) m->enter();
    }
    return m;
}
