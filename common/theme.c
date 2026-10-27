#include "theme.h"

static int active;
static const theme_palette_t palettes[THEME_COUNT] = {
    {"ORIGINAL", 0x031015,0x04141a,0xffffff,0x2a3f45,0x9fd8e2,0x031015,0x2a3f45,0xf2b65b,
     {0x0a3c46,0x08272f,0x031015}, {0x3f8b55,0x2e7d4f,0x8dbb48,0x6c9d38},
     {0x2e3b2c,0x22301f,0x1a2418,0x101a12},0},
};
int theme_valid(int id) { return id >= 0 && id < THEME_COUNT ? id : THEME_ORIGINAL; }
const theme_palette_t *theme_palette(int id) { return &palettes[theme_valid(id)]; }
int theme_active(void) { return active; }
void theme_activate(int id) { active = theme_valid(id); }
/* a theme's UI colours: the Original's, mapped to the palette's roles */
uint32_t theme_ui_color(uint32_t rgb) {
    if (!active) return rgb;
    const theme_palette_t *p = theme_palette(active);
    switch (rgb) {
    case 0x031015: return p->background;
    case 0x04141a: case 0x0e2229: case 0x1c2f36: case 0x0c2027: return p->panel;
    case 0xffffff: return p->text;
    case 0x9fd8e2: case 0x38dcc7: return p->accent;
    case 0x2a3f45: case 0x2c4a52: case 0x5f8a92: case 0x9fb4b8: return p->muted;
    case 0x155e58: return p->accent;
    case 0xf2b65b: return p->gold;
    default: return rgb;
    }
}
/* a theme's take on a creature's own colours (the hue the keeper chose stays):
   the Original draws them as they are */
uint32_t theme_creature_color(uint32_t rgb, int fin) {
    (void)fin;
    return rgb;
}
/* a theme's icons, the same size as the Original's; the shop thumbnails with
   generated looks per theme ask their own generator's table (icons.h) */
const icon_t *theme_icon(const icon_t *original) {
    if(original==&icon_shop_jellyfish)return jellyfish_theme_icon(active);
    if(original==&icon_shop_wreck)return wreck_theme_icon(active);
    if(original==&icon_shop_frogman)return frogman_theme_icon(active);
    if(original==&icon_shop_sub)return sub_theme_icon(active);
    return original;
}
