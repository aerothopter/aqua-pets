/* glass.c - the glass, whatever its shape (glass.h; refactor phase 4b,
 * 2026-10-23). Every piece of the tank that depends on the glass's shape is
 * here, one body a shape, picked by DEVICE_GLASS: the glass's edges, a point
 * brought inside it, open water, the film's cells, and the swimmers' glass
 * (the bounce, the push away, a target's room). One shape for now
 * (2026-10-26): the rectangle. The bowl's and the watch's bodies are at git
 * tag multi-board-multi-theme; a board brought back brings its shape here. */
#include "glass.h"
#include "tank.h"
#include "tank_internal.h"
#include <math.h>

#if DEVICE_GLASS != GLASS_RECT
#error "glass.c has no body for this glass's shape (the bowl's and the watch's: git tag multi-board-multi-theme)"
#endif

/* ---- the glass's edges: the rectangle's is its frame ---- */
float tank_glass_x0(float y) { (void)y; return 0; }
float tank_glass_x1(float y) { (void)y; return TANK_W; }
float tank_glass_top(float x) { (void)x; return 0; }
void tank_glass_clamp(float *x, float *y, float m) {
    *x = clampf(*x, m, TANK_W - m); *y = clampf(*y, m, TANK_H - m);
}

/* a random point of open water: y in [y0, y1], x at least mx off the side glass at that height */
void rand_water(tank_t *t, float mx, float y0, float y1, float *x, float *y) {
    *x = tank_randf(t, mx, TANK_W - mx); *y = tank_randf(t, y0, y1);
}

/* a film cell that is on the glass: every one in the rectangle */
bool glass_holds_cell(int cx, int cy) {
    (void)cx; (void)cy; return true;
}

/* ---- the swimmers' glass (creature.c) ---- */

/* hard bounds. The side glass mirrors a heading only while it points
 * INTO the glass: the facing lags the mirror, and a second bounce on the
 * next frame would send the fish back into the glass. m is the body's room,
 * settle the pause after a turn the bounce makes. */
void glass_bounce(fish_t *f, float m, float settle) {
    if (f->x < m)          { f->x = m;          if (cosf(f->heading) < 0) f->heading = norm_ang(3.14159f - f->heading);
                                                if (f->facing < 0) { f->facing = 1; f->behind = -settle; } }
    if (f->x > TANK_W - m) { f->x = TANK_W - m; if (cosf(f->heading) > 0) f->heading = norm_ang(3.14159f - f->heading);
                                                if (f->facing > 0) { f->facing = -1; f->behind = -settle; } }
    if (f->y < m)          { f->y = m;          f->heading = -f->heading; }
    if (f->y > TANK_H - m) { f->y = TANK_H - m; f->heading = -f->heading; }
}

/* a swim target brought inside: m off the frame (m + 9 under the top) */
void glass_clamp_target(float *x, float *y, float m) {
    *x = clampf(*x, m, TANK_W - m);
    *y = clampf(*y, m + 9, TANK_H - m);
}

/* the push away from the glass near it: the way to steer (an angle), and
 * whether there is one (*hit) */
float glass_avoidance(float x, float y, bool *hit) {
    const float m = 34;
    float vx = 0, vy = 0;
    if (x < m)          vx += 1 - x / m;
    if (x > TANK_W - m) vx -= 1 - (TANK_W - x) / m;
    if (y < m + 6)      vy += 1 - (y - 6) / m;
    if (y > TANK_H - m) vy -= 1 - (TANK_H - y) / m;
    *hit = (vx != 0 || vy != 0);
    return *hit ? atan2f(vy, vx) : 0;
}
