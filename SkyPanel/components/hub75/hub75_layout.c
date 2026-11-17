/* Panelių išdėstymai: kur kiekviena grandinės panelė yra bendrame ekrane */
#include "hub75_layout.h"
#include <stdio.h>
#include <string.h>

static const char *const NAMES[LAYOUT_COUNT] = {   /* rodoma žurnale */
    "1x 64x32",
    "4x 64x32 (S grandine)",
    "4x 64x32 (eilutemis)",
    "1x 128x64 (1/32 scan)",
    "4x 64x32 (savas)",
};

const char *hub75_layout_name(int id)
{
    return id >= 0 && id < LAYOUT_COUNT ? NAMES[id] : "?";
}

bool hub75_layout_parse(const char *s, hub75_tile_t tiles[4])
{
    bool used[2][2] = { 0 };
    for (int i = 0; i < 4; i++) {
        int c, r, f, n = 0;
        if (!s || sscanf(s, " %d , %d , %d %n", &c, &r, &f, &n) != 3) return false;
        if (c < 0 || c > 1 || r < 0 || r > 1 || f < 0 || f > 1 || used[r][c]) return false;
        used[r][c] = true;
        tiles[i] = (hub75_tile_t){ (uint8_t)c, (uint8_t)r, f != 0 };
        s += n;
        if (i < 3) {
            if (*s != ';') return false;
            s++;
        }
    }
    while (*s == ' ' || *s == ';') s++;
    return *s == 0;
}

bool hub75_layout_get(int id, const char *custom, hub75_layout_t *out)
{
    memset(out, 0, sizeof *out);
    out->tile_w = 64;
    out->tile_h = 32;
    out->chain = 4;
    out->cols = out->rows = 2;
    switch (id) {
    case LAYOUT_4X_S:
        out->tiles[0] = (hub75_tile_t){ 1, 0, true };
        out->tiles[1] = (hub75_tile_t){ 1, 1, false };
        out->tiles[2] = (hub75_tile_t){ 0, 1, false };
        out->tiles[3] = (hub75_tile_t){ 0, 0, true };
        return true;
    case LAYOUT_4X_ROWS:
        out->tiles[0] = (hub75_tile_t){ 0, 0, false };
        out->tiles[1] = (hub75_tile_t){ 1, 0, false };
        out->tiles[2] = (hub75_tile_t){ 0, 1, false };
        out->tiles[3] = (hub75_tile_t){ 1, 1, false };
        return true;
    case LAYOUT_1X_128X64:
        out->tile_w = 128;
        out->tile_h = 64;
        out->chain = out->cols = out->rows = 1;
        return true;
    case LAYOUT_CUSTOM:
        if (hub75_layout_parse(custom, out->tiles)) return true;
        break;
    case LAYOUT_1X_64X32:
        out->chain = out->cols = out->rows = 1;
        return true;
    default:
        break;
    }
    out->chain = out->cols = out->rows = 1;
    memset(out->tiles, 0, sizeof out->tiles);
    return false;
}
