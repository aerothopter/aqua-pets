/* glass.c - the glass, whatever its shape (glass.h; refactor phase 4b,
 * 2026-10-23). Every piece of the tank that depends on the glass's shape is
 * here, one body a shape, picked by DEVICE_GLASS: the glass's edges, a point
 * brought inside it, open water, the film's cells, and the swimmers' glass
 * (the bounce, the push away, a target's room). */
#include "glass.h"
#include "tank.h"
#include "tank_internal.h"
#include <math.h>

/* ---- the glass's edges. The rectangle's is its frame; the bowl's is the
 * circle, with the flat bottom at TANK_BOT. ---- */
#if DEVICE_GLASS == GLASS_BOWL
static float glass_half(float d) {                 /* half the chord, d px off the centre line */
    float r2 = TANK_RAD * TANK_RAD - d * d;
    return r2 > 0 ? sqrtf(r2) : 0;
}
float tank_glass_x0(float y) { return TANK_RAD - glass_half(y - TANK_RAD); }
float tank_glass_x1(float y) { return TANK_RAD + glass_half(y - TANK_RAD); }
float tank_glass_top(float x) { return TANK_RAD - glass_half(x - TANK_RAD); }
void tank_glass_clamp(float *x, float *y, float m) {
    if (*y > TANK_BOT - m) *y = TANK_BOT - m;
    float dx = *x - TANK_RAD, dy = *y - TANK_RAD, d = sqrtf(dx * dx + dy * dy), lim = TANK_RAD - m;
    if (d > lim && d > 0) {
        *x = TANK_RAD + dx * lim / d; *y = TANK_RAD + dy * lim / d;
        if (*y > TANK_BOT - m) {                   /* the corner where the glass meets the floor: along the floor to the glass */
            *y = TANK_BOT - m;
            float h = glass_half(*y - TANK_RAD) - m; if (h < 0) h = 0;
            *x = dx < 0 ? TANK_RAD - h : TANK_RAD + h;
        }
    }
}
#elif DEVICE_GLASS == GLASS_CORNERS
/* the watch: the rectangle's frame, and in each corner's R x R square the
 * arc. corner_in says how far a point stands off its corner's centre (0 = it
 * is in no corner's square: the straight glass is the limit there). */
static float corner_in(float x, float y, float *cx, float *cy) {
    const float R = TANK_CORNER_R;
    if (x < R) *cx = R; else if (x > TANK_W - R) *cx = TANK_W - R; else return 0;
    if (y < R) *cy = R; else if (y > TANK_H - R) *cy = TANK_H - R; else return 0;
    return sqrtf((x - *cx) * (x - *cx) + (y - *cy) * (y - *cy));
}
static float corner_inset(float v, float len) {    /* how far the glass stands in from the frame, v px along a side of length len */
    const float R = TANK_CORNER_R;
    float d = v < R ? R - v : v > len - R ? v - (len - R) : 0;
    if (d <= 0) return 0;
    return d >= R ? R : R - sqrtf(R * R - d * d);
}
float tank_glass_x0(float y) { return corner_inset(y, TANK_H); }
float tank_glass_x1(float y) { return TANK_W - corner_inset(y, TANK_H); }
float tank_glass_top(float x) { return corner_inset(x, TANK_W); }
static void corner_clamp(float *x, float *y, float m) {   /* out of a corner: back along its radius to m inside the arc */
    float cx, cy, d = corner_in(*x, *y, &cx, &cy), lim = TANK_CORNER_R - m;
    if (d > lim && d > 0) { *x = cx + (*x - cx) * lim / d; *y = cy + (*y - cy) * lim / d; }
}
void tank_glass_clamp(float *x, float *y, float m) {
    *x = clampf(*x, m, TANK_W - m); *y = clampf(*y, m, TANK_H - m);
    corner_clamp(x, y, m);
}
#else
float tank_glass_x0(float y) { (void)y; return 0; }
float tank_glass_x1(float y) { (void)y; return TANK_W; }
float tank_glass_top(float x) { (void)x; return 0; }
void tank_glass_clamp(float *x, float *y, float m) {
    *x = clampf(*x, m, TANK_W - m); *y = clampf(*y, m, TANK_H - m);
}
#endif

/* a random point of open water: y in [y0, y1], x at least mx off the side glass at that height */
void rand_water(tank_t *t, float mx, float y0, float y1, float *x, float *y) {
#if GLASS_CURVED
    *y = tank_randf(t, y0, y1);
    float lo = tank_glass_x0(*y) + mx, hi = tank_glass_x1(*y) - mx;
    *x = lo < hi ? tank_randf(t, lo, hi) : TANK_W * 0.5f;
#else
    *x = tank_randf(t, mx, TANK_W - mx); *y = tank_randf(t, y0, y1);
#endif
}

/* a film cell that is on the glass: every one in the rectangle; in the bowl,
 * the ones whose centre the circle holds (the grid is the square around it) */
bool glass_holds_cell(int cx, int cy) {
#if DEVICE_GLASS == GLASS_BOWL
    if (cx < 0 || cx >= ALGAE_COLS || cy < 0 || cy >= ALGAE_ROWS) return false;
    float dx = cx * ALGAE_CELL + ALGAE_CELL * 0.5f - TANK_RAD, dy = cy * ALGAE_CELL + ALGAE_CELL * 0.5f - TANK_RAD;
    return dx * dx + dy * dy <= (TANK_RAD - 6) * (TANK_RAD - 6);
#elif DEVICE_GLASS == GLASS_CORNERS                         /* the watch: not the cells its round corners hide (the last column and
                                                              row hang off the frame: their centres are still on the glass) */
    float px = cx * ALGAE_CELL + ALGAE_CELL * 0.5f, py = cy * ALGAE_CELL + ALGAE_CELL * 0.5f, ccx, ccy;
    if (px > TANK_W - 1) px = TANK_W - 1;
    if (py > TANK_H - 1) py = TANK_H - 1;
    return corner_in(px, py, &ccx, &ccy) <= TANK_CORNER_R - 4;
#else
    (void)cx; (void)cy; return true;
#endif
}

/* ---- the swimmers' glass (creature.c) ---- */

/* hard bounds. The side glass mirrors a heading only while it points
 * INTO the glass: the facing lags the mirror, and a second bounce on the
 * next frame would send the fish back into the glass. m is the body's room,
 * settle the pause after a turn the bounce makes. */
void glass_bounce(fish_t *f, float m, float settle) {
#if DEVICE_GLASS == GLASS_BOWL                             /* the bowl's glass: back onto the circle m inside it, the heading mirrored
                                                              in the glass while it points out, the body turned to swim back in */
    { float dx = f->x - TANK_RAD, dy = f->y - TANK_RAD, d = sqrtf(dx * dx + dy * dy), lim = TANK_RAD - m;
      if (d > lim && d > 1) {
          float nx = dx / d, ny = dy / d;
          f->x = TANK_RAD + nx * lim; f->y = TANK_RAD + ny * lim;
          float hx = cosf(f->heading), hy = sinf(f->heading), out = hx * nx + hy * ny;
          if (out > 0) {
              hx -= 2 * out * nx; hy -= 2 * out * ny; f->heading = atan2f(hy, hx);
              if (nx < -0.5f && f->facing < 0) { f->facing = 1; f->behind = -settle; }
              if (nx > 0.5f && f->facing > 0)  { f->facing = -1; f->behind = -settle; }
          }
      }
      if (f->y > TANK_BOT - m) { f->y = TANK_BOT - m; f->heading = -f->heading; } }
#else
    if (f->x < m)          { f->x = m;          if (cosf(f->heading) < 0) f->heading = norm_ang(3.14159f - f->heading);
                                                if (f->facing < 0) { f->facing = 1; f->behind = -settle; } }
    if (f->x > TANK_W - m) { f->x = TANK_W - m; if (cosf(f->heading) > 0) f->heading = norm_ang(3.14159f - f->heading);
                                                if (f->facing > 0) { f->facing = -1; f->behind = -settle; } }
    if (f->y < m)          { f->y = m;          f->heading = -f->heading; }
    if (f->y > TANK_H - m) { f->y = TANK_H - m; f->heading = -f->heading; }
#if DEVICE_GLASS == GLASS_CORNERS                          /* the watch's corners: back onto the arc m inside it, the heading mirrored
                                                              in the glass while it points out (the bowl's rule, a corner at a time) */
    { float cx, cy, d = corner_in(f->x, f->y, &cx, &cy), lim = TANK_CORNER_R - m;
      if (d > lim && d > 1) {
          float nx = (f->x - cx) / d, ny = (f->y - cy) / d;
          f->x = cx + nx * lim; f->y = cy + ny * lim;
          float hx = cosf(f->heading), hy = sinf(f->heading), out = hx * nx + hy * ny;
          if (out > 0) {
              hx -= 2 * out * nx; hy -= 2 * out * ny; f->heading = atan2f(hy, hx);
              if (nx < -0.5f && f->facing < 0) { f->facing = 1; f->behind = -settle; }
              if (nx > 0.5f && f->facing > 0)  { f->facing = -1; f->behind = -settle; }
          }
      } }
#endif
#endif
}

/* a swim target brought inside: m off the frame (m + 9 under the top),
 * m + 4 off the bowl's circle and the watch's corners */
void glass_clamp_target(float *x, float *y, float m) {
#if DEVICE_GLASS == GLASS_BOWL
    tank_glass_clamp(x, y, m + 4);
#else
    *x = clampf(*x, m, TANK_W - m);
    *y = clampf(*y, m + 9, TANK_H - m);
#if DEVICE_GLASS == GLASS_CORNERS
    corner_clamp(x, y, m + 4);
#endif
#endif
}

/* the push away from the glass near it: the way to steer (an angle), and
 * whether there is one (*hit) */
float glass_avoidance(float x, float y, bool *hit) {
    const float m = 34;
    float vx = 0, vy = 0;
#if DEVICE_GLASS == GLASS_BOWL                             /* the bowl: away from the glass along its radius, and up off the floor */
    float dx = x - TANK_RAD, dy = y - TANK_RAD, d = sqrtf(dx * dx + dy * dy), gap = TANK_RAD - d;
    if (gap < m && d > 1) { float p = 1 - (gap < 0 ? 0 : gap) / m; vx -= dx / d * p; vy -= dy / d * p; }
    if (y > TANK_BOT - m) vy -= 1 - (TANK_BOT - y) / m;
#else
    if (x < m)          vx += 1 - x / m;
    if (x > TANK_W - m) vx -= 1 - (TANK_W - x) / m;
    if (y < m + 6)      vy += 1 - (y - 6) / m;
    if (y > TANK_H - m) vy -= 1 - (TANK_H - y) / m;
#if DEVICE_GLASS == GLASS_CORNERS                          /* the watch: in a corner the arc is nearer than either straight wall */
    { float cx, cy, d = corner_in(x, y, &cx, &cy), gap = TANK_CORNER_R - d;
      if (d > 1 && gap < m) { float p = 1 - (gap < 0 ? 0 : gap) / m; vx -= (x - cx) / d * p; vy -= (y - cy) / d * p; } }
#endif
#endif
    *hit = (vx != 0 || vy != 0);
    return *hit ? atan2f(vy, vx) : 0;
}
