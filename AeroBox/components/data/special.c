/* Ypatingi lėktuvai, kuriuos verta pažymėti ekrane: retas, karinis, sraigtasparnis, verslo lėktuvas. */
#include "aircraft.h"
#include <string.h>

/* ICAO tipų kodai */
static const char *const RARE[] = {
    "A388", "B741", "B742", "B743", "B744", "B748", "B74S", "BLCF", "A3ST", "A337", "A124", "A225", "AN22",
    "AN12", "IL76", "IL62", "IL96", "MD11", "DC10", "L101", "CONC", "A342", "A343", "A345", "A346",
};
static const char *const MILITARY[] = {
    "C17", "C5M", "C130", "C30J", "A400", "K35R", "E3TF", "E3CF", "P8", "B52", "F16", "F35", "EUFI", "RFAL",
    "TOR", "A10",
};
static const char *const HELI[] = {
    "EC20", "EC30", "EC35", "EC45", "EC55", "EC75", "AS50", "AS55", "AS65", "H160", "R22", "R44", "R66", "B06",
    "B407", "B412", "A109", "A119", "A139", "A169", "A189", "S76", "S92", "NH90", "MI8",
};
static const char *const BIZJET[] = {
    "GLF4", "GLF5", "GLF6", "GL5T", "GLEX", "GL7T", "G280", "F2TH", "F900", "FA7X", "FA8X", "FA50", "C25A", "C25B",
    "C25C", "C525", "C550", "C560", "C56X", "C680", "C68A", "C700", "C750", "CL30", "CL35", "CL60", "LJ35", "LJ40",
    "LJ45", "LJ60", "LJ75", "E35L", "E545", "E550", "E50P", "E55P", "PC24", "H25B", "BE40", "PRM1", "HDJT", "SF50",
    "EA50",
};

static bool in(const char *type, const char *const *list, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (strcmp(type, list[i]) == 0) return true;
    return false;
}

#define IN(t, list) in((t), (list), sizeof(list) / sizeof((list)[0]))

/* Tikrinimo tvarka svarbi: karinis svarbiau už retą, retas – už sraigtasparnį, šis – už verslo lėktuvą */
uint8_t aircraft_special(const char *type, const char *category, int db_flags)
{
    if (db_flags & 1 || IN(type, MILITARY)) return SP_MILITARY;
    if (IN(type, RARE) || db_flags & 2) return SP_RARE;
    if ((category && strcmp(category, "A7") == 0) || IN(type, HELI)) return SP_HELI;
    if (IN(type, BIZJET)) return SP_BIZJET;
    return SP_NONE;
}

const char *special_label(uint8_t sp)
{
    static const char *const L[SP_COUNT] = { "", "RARE AIRCRAFT", "MILITARY", "HELICOPTER", "BUSINESS JET" };
    return sp < SP_COUNT ? L[sp] : "";
}

const char *special_short(uint8_t sp)
{
    static const char *const L[SP_COUNT] = { "", "RARE", "MIL", "HELI", "BIZ" };
    return sp < SP_COUNT ? L[sp] : "";
}
