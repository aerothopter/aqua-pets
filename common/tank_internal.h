/* What tank.c and creature.c share inside the reflex layer (2026-10-23, the
 * creatures' code moved to creature.c): the small maths, the tank's RNG core,
 * the constants both sides read, and the few calls across. Not for the
 * platforms - their API is tank.h. */
#ifndef TANK_INTERNAL_H
#define TANK_INTERNAL_H
#include "tank.h"
#include <math.h>

#define TAU 6.2831853f

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
static inline float norm_ang(float a) {
    while (a >  3.14159265f) a -= TAU;
    while (a < -3.14159265f) a += TAU;
    return a;
}
static inline float lerpf(float a, float b, float p) { return a + (b - a) * p; }

/* the tank's RNG core (deterministic xorshift32; tank_randf draws through it) */
uint32_t tank_xr(tank_t *t);
/* the glass, every shape (glass.c) */
void  rand_water(tank_t *t, float mx, float y0, float y1, float *x, float *y);   /* a random point of open water: y in [y0, y1],
                                                                                     x at least mx off the side glass at that height */
bool  glass_holds_cell(int cx, int cy);                  /* a film cell on the glass */
void  glass_bounce(fish_t *f, float m, float settle);    /* a swimmer's hard bounds: m its room, settle the pause after a turn */
void  glass_clamp_target(float *x, float *y, float m);   /* a swim target brought inside the glass */
float glass_avoidance(float x, float y, bool *hit);      /* the way away from the glass near (x, y), if any (*hit) */

/* the grass: inside a bed's canopy; a bed's mean ceiling */
bool  veg_inside(const tank_t *t, int b, float x, float y);
float veg_cap_mean(const tank_t *t, int b);
/* where the courting pair circles: low inside the nursery bed */
void  court_site(const tank_t *t, float *cx, float *cy, float *rx);

#define HOLD_RADIUS     1000.0f /* a resting finger is seen from anywhere on the
                                 * glass (2026-09-04, was 160: a fish only came
                                 * when you held next to it, which hid the
                                 * trust tell) */
#define HOLD_ATTRACT_S  2.7f    /* hold_time before fish approach; platforms
                                 * report holds ~0.3 s in, so ≈3 s of contact */
#define HOLD_HUNGER_VETO 7.5f   /* this hungry, a fish ignores the finger */
/* the hunger economy (tank.c has its notes) */
#define HUNGER_PER_S       0.010f   /* fed -> starving in ~17 min (shy fish) */
#define HUNGER_BOLD_PER_S  0.008f   /* bold fish burn hotter: ~9 min */
#define HUNGER_DART_PER_S  0.012f   /* play costs extra */
#define CURIOSITY_SPEND_PER_S 0.25f /* spent at the bubbles / the reef: 9 -> 3 in ~24 s */
#define CURIOSITY_SPEND_RADIUS 60.0f
#define VEG_SLOW            0.60f     /* cruise speed factor inside a canopy */

/* creature.c, called by the tank: a creature's frame */
void  update_fish(tank_t *t, int idx, float dt);

#endif
