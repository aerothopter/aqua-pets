/* Presentation only. IDs are persisted: never reorder or reuse them.
 *
 * One theme for now (2026-10-26): the Original. Quiet Lagoon (1), Tidepool
 * Club (2) and Blackwater (3) were taken out while features are built, to
 * come back later; their palettes, art, tunes and generated assets are at
 * git tag multi-board-multi-theme. Their IDs stay reserved: a save that
 * holds one loads as the Original (theme_valid). The hooks stay too - a
 * theme is a palette row plus what it changes through theme_ui_color,
 * theme_creature_color and theme_icon, and the art asks theme_active(). */
#ifndef AQUA_THEME_H
#define AQUA_THEME_H
#include <stdint.h>
#include "icons.h"

typedef enum { THEME_ORIGINAL = 0, THEME_COUNT = 1 } theme_id_t;   /* 1-3: reserved (above) */
typedef struct {
    const char *name;
    uint32_t background, panel, text, muted, accent, on_accent, border, gold;
    uint32_t water[3], grass[4], sand[4];
    int radius;
} theme_palette_t;

int theme_valid(int id);
const theme_palette_t *theme_palette(int id);
int theme_active(void);
void theme_activate(int id); /* renderer owns activation and cache invalidation */
uint32_t theme_ui_color(uint32_t original);
uint32_t theme_creature_color(uint32_t original, int fin);
const icon_t *theme_icon(const icon_t *original);
#endif
