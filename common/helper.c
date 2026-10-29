/* The helpers: each one's rules, the table, and the one path every helper
 * takes (see helper.h). The rules moved here from tank.c unchanged
 * (2026-10-22); the tank's RNG is drawn in the same order. */
#include "tank.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- the snail (2026-09-15, the shop's algae control) ----
 * A rule-based creature on the GLASS: it crawls toward the nearest film cell
 * and grazes it thin, cell by cell; with the glass clean it ambles along the
 * edge. The model never sees it (schema v4 is frozen) - it reaches the fish
 * the way the film does, through cover and calm. Its grazing is not the
 * keeper's chore: cells_cleaned and the colonies never count it. */
#define SNAIL_PX_S        4.0f     /* crawl toward a patch (6 was "a bit fast" - Strato) */
#define SNAIL_AMBLE_PX_S  3.0f     /* nothing to eat: the edge walk */
#define SNAIL_GRAZE_PER_S 45.0f    /* film units per second on a cell (a fresh 90 cell in 2 s) */
#define SNAIL_SLEEP_FRAC_H 0.12f   /* the night shift (tank_tick_sleep): this share of the film's cells
                                    * an hour - the more film, the less it crawls between meals - so a
                                    * night ends cleaner than it would and a long absence settles under
                                    * the cap instead of on it, and there is still film to wipe (tuned
                                    * in selftest-sleep; 20 cells an hour flat was a spotless glass) */
#define SNAIL_MARGIN      30.0f    /* it keeps inside the visible window: the panel's corners are
                                    * rounded and the bezel curve hides the outer ~24 px (Strato,
                                    * 2026-09-15: "stuck in the bottom left corner, I can barely
                                    * see it" - the film seeds from the corners and it went there) */
#define SNAIL_REACH       16.0f    /* it grazes a cell from this close (one cell): a corner cell
                                    * under the bezel is eaten from the visible edge */
/* where the snail may stand: SNAIL_MARGIN inside the rectangle's frame; on the
 * bowl, whose glass hides nothing, 16 px inside the circle */
static void snail_clamp(float *x, float *y, float ymax) {
#if GLASS_CURVED
    tank_glass_clamp(x, y, 16);
    if (*y > ymax) *y = ymax;
#else
    *x = clampf(*x, SNAIL_MARGIN, TANK_W - SNAIL_MARGIN); *y = clampf(*y, SNAIL_MARGIN, ymax);
#endif
}
static int snail_nearest_cell(const tank_t *t) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < ALGAE_CELLS; i++) {
        if (!t->algae[i]) continue;
        float cx = (i % ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f, cy = (i / ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f;
        float d = tank_dist(t->snail.x, t->snail.y, cx, cy);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
bool tank_snail_upright(const tank_t *t) { return t->snail.cell < 0 && t->snail.y >= SNAIL_FLOOR_Y - 1; }
#define SNAIL_TAP_RADIUS 48.0f     /* generous round the 32 px sprite: a fingertip on this 322 ppi
                                    * panel covers ~60 px, and the snail is small and low on the glass
                                    * (Strato, 2026-09-16: "challenging to tap the little guy" at 30;
                                    * the fish take 38 and are tested first) */
bool tank_snail_hit(const tank_t *t, float x, float y) {
    if (!(t->sd_unlocks & SD_ITEM_SNAIL) || t->snail.x < 0) return false;
    return tank_dist(t->snail.x, t->snail.y, x, y) <= SNAIL_TAP_RADIUS;
}
static void snail_tick(tank_t *t, float dt) {
    if (!(t->sd_unlocks & SD_ITEM_SNAIL)) return;
    if (t->snail.x < 0) tank_snail_place(t);
    if (t->snail.cell < 0 || !t->algae[t->snail.cell]) { t->snail.cell = (int16_t)snail_nearest_cell(t); t->snail.graze = 0; }
    if (t->snail.cell >= 0) {
        float cx = (t->snail.cell % ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f;
        float cy = (t->snail.cell / ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f;
        float tx = cx, ty = cy; snail_clamp(&tx, &ty, TANK_BOT - SNAIL_MARGIN);
        float d = tank_dist(t->snail.x, t->snail.y, tx, ty);      /* to the nearest point it may stand on */
        if (d > 3 && tank_dist(t->snail.x, t->snail.y, cx, cy) > SNAIL_REACH) {
            t->snail.heading = atan2f(ty - t->snail.y, tx - t->snail.x);
            float step = fminf(d, SNAIL_PX_S * dt);
            t->snail.x += cosf(t->snail.heading) * step; t->snail.y += sinf(t->snail.heading) * step;
        } else {                                            /* grazing: the film thins under it */
            t->snail.graze += dt;
            int v = t->algae[t->snail.cell] - (int)(SNAIL_GRAZE_PER_S * dt + 0.5f);
            t->algae[t->snail.cell] = (uint8_t)(v < 0 ? 0 : v);
            if (v <= 0) t->snail.grazed++;                  /* a cell eaten clean: its card's tally */
        }
    } else if (t->snail.y < SNAIL_FLOOR_Y - 1) {            /* clean glass: down to the floor, flat on the glass, head down.
                                                               A hair off straight down keeps the sideways facing it had
                                                               (cos's sign) for the floor walk it lands in. */
        t->snail.heading = 1.5708f + (cosf(t->snail.heading) < 0 ? 0.001f : -0.001f);
        t->snail.front = true;                              /* off the glass it stands where it was: in front of everything */
        t->snail.y = fminf(SNAIL_FLOOR_Y, t->snail.y + SNAIL_PX_S * dt);
    } else {                                                /* the floor walk, upright: along the bottom, a rest now and
                                                               then, a turn at each end (the facing lives in snail.heading) */
        t->snail.y = SNAIL_FLOOR_Y;
        bool resting = fmodf(t->clock, 24.0f) < 5.0f;
        if (!resting) {
            float dir = cosf(t->snail.heading) < 0 ? -1.0f : 1.0f;
            t->snail.x += dir * SNAIL_AMBLE_PX_S * dt;
            bool turned = false;
            if (t->snail.x <= TANK_FX0 + SNAIL_MARGIN) { t->snail.x = TANK_FX0 + SNAIL_MARGIN; t->snail.heading = 0; turned = true; }
            if (t->snail.x >= TANK_FX1 - SNAIL_MARGIN) { t->snail.x = TANK_FX1 - SNAIL_MARGIN; t->snail.heading = 3.14159f; turned = true; }
            /* at each end it picks its lane for the walk back: in front of the IN FRONT pieces, or behind
               them (from the clock's bits, not the tank's RNG: a seeded run is the same run) */
            if (turned) t->snail.front = (((uint32_t)(t->clock * 997.0f) * 2654435761u) >> 20 & 1) != 0;
        }
    }
    snail_clamp(&t->snail.x, &t->snail.y, SNAIL_FLOOR_Y);
}
/* asleep: the snail keeps working, coarsely - the nearest cells go, one by
 * one. tank_tick_sleep hands it the night a slice at a time, AFTER the
 * slice's film has grown, so it eats the night's film as it comes; the
 * part-cell carries over, so a nap's few minutes count too (2026-10-02: it
 * used to run once, before the night's film, on the film the keeper had
 * already left - from a clean glass a snail's tank woke exactly as dirty as
 * one without, and (int) of a short nap was no cells at all). */
static void snail_sleep(tank_t *t, float seconds) {
    if (!(t->sd_unlocks & SD_ITEM_SNAIL)) return;
    if (t->snail.x < 0) tank_snail_place(t);
    int covered = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) covered += t->algae[i] > 0;
    t->snail.sleep_acc += covered * (1.0f - expf(-SNAIL_SLEEP_FRAC_H * seconds / 3600.0f));
    int cells = (int)t->snail.sleep_acc;
    t->snail.sleep_acc -= cells;
    for (int k = 0; k < cells; k++) {
        int c = snail_nearest_cell(t);
        if (c < 0) break;
        t->algae[c] = 0; t->snail.grazed++;
        t->snail.x = (c % ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f;
        t->snail.y = (c / ALGAE_COLS) * ALGAE_CELL + ALGAE_CELL * 0.5f;
        snail_clamp(&t->snail.x, &t->snail.y, TANK_BOT - SNAIL_MARGIN);
    }
    t->snail.cell = -1;
}
void tank_snail_place(tank_t *t) {
    t->snail.x = TANK_FX0 + SNAIL_MARGIN + 10; t->snail.y = SNAIL_FLOOR_Y;   /* on the floor, bottom left, facing right */
    t->snail.heading = 0; t->snail.cell = -1; t->snail.graze = 0; t->snail.front = true;
}

/* ---- the urchin (2026-10-02, SD_ITEM_URCHIN; helper.h has the rules) ----
 * The grass's snail. Awake its appetite fills slowly (URCHIN_AWAKE_PER_H of
 * frond height an hour, for the whole tank: the grass grows ~0.75 an hour
 * awake across its ~30 fronds, so it takes the edge off and the scissors
 * stay the keeper's); with a bite's worth it crawls to the frond standing
 * tallest over URCHIN_KEEP (a near one wins a close call), chews at its foot
 * for URCHIN_CHEW_S while the frond comes down a bite and bits of leaf drift
 * up from the tip, then rests and ambles the sand. Asleep it grazes the
 * grass's height over the keep line in proportion to how much there is
 * (URCHIN_SLEEP_FRAC_H of it an hour), every frond its share, so the canopy
 * keeps its shape, only lower; a tall tank feeds it faster, so a long
 * absence settles below the ceilings instead of on them. */
#define URCHIN_PX_S          1.6f     /* tube feet: slower than the snail */
#define URCHIN_AWAKE_PER_H   0.30f
#define URCHIN_SLEEP_FRAC_H  0.20f    /* of the height over the keep line, an hour asleep (tuned in
                                       * selftest-sleep: a 7 h night from a trimmed 0.35 lands ~0.6
                                       * where it lands 0.70 alone; days away settle ~0.65, not 0.83) */
#define URCHIN_BITE          0.05f    /* one sitting: ~17 px off a frond on the 1.8 */
#define URCHIN_CHEW_S        14.0f
#define URCHIN_TAP_RADIUS    40.0f    /* a fingertip round a ~32 px urchin */
#define URCHIN_HALF_W        16.0f
static bool urchin_frond_ok(const tank_t *t, int b, int i) {
    if (b < 0 || b >= tank_veg_beds(t) || tank_veg_kind(t, b) != VEG_KIND_GRASS) return false;
    int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
    return i >= 0 && i < n;
}
static float urchin_lo(void) { return TANK_FX0 + DECOR_MARGIN + URCHIN_HALF_W; }
static float urchin_hi(void) { return TANK_FX1 - DECOR_MARGIN - URCHIN_HALF_W; }
static float urchin_frond_x(const tank_t *t, int b, int i) {
    float x; tank_veg_frond(t, b, i, &x);
    return clampf(x, urchin_lo(), urchin_hi());
}
/* the frond standing tallest over the keep line, a near one winning a close
 * call; -1 when no frond is worth a sitting */
static bool urchin_pick(tank_t *t) {
    float best = 0; int bb = -1, bi = -1;
    for (int b = 0; b < tank_veg_beds(t); b++) {
        if (tank_veg_kind(t, b) != VEG_KIND_GRASS) continue;
        int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
        for (int i = 0; i < n; i++) {
            float over = t->veg_h[b][i] - URCHIN_KEEP;
            if (over < URCHIN_BITE * 0.5f) continue;
            float score = over - fabsf(urchin_frond_x(t, b, i) - t->urchin.x) * 0.0004f;
            if (bb < 0 || score > best) { best = score; bb = b; bi = i; }
        }
    }
    t->urchin.bed = (int8_t)bb; t->urchin.frond = (int8_t)bi; t->urchin.chew = 0;
    return bb >= 0;
}
bool tank_urchin_chewing(const tank_t *t) { return t->urchin.frond >= 0 && t->urchin.chew > 0; }
bool tank_urchin_hit(const tank_t *t, float x, float y) {
    if (!(t->sd_unlocks & SD_ITEM_URCHIN) || t->urchin.x < 0) return false;
    return tank_dist(t->urchin.x, URCHIN_FLOOR_Y, x, y) <= URCHIN_TAP_RADIUS;
}
static void urchin_tick(tank_t *t, float dt) {
    if (!(t->sd_unlocks & SD_ITEM_URCHIN)) return;
    if (t->urchin.x < 0) tank_urchin_place(t);
    t->urchin.appetite = fminf(t->urchin.appetite + URCHIN_AWAKE_PER_H / 3600.0f * dt, URCHIN_BITE * 2);
    if (t->urchin.frond >= 0 && (!urchin_frond_ok(t, t->urchin.bed, t->urchin.frond)
                                 || t->veg_h[t->urchin.bed][t->urchin.frond] <= URCHIN_KEEP + 0.005f)) {
        t->urchin.bed = t->urchin.frond = -1; t->urchin.chew = 0;      /* the keeper's scissors got there first */
    }
    if (t->urchin.frond < 0 && t->urchin.appetite >= URCHIN_BITE) urchin_pick(t);
    if (t->urchin.frond >= 0) {
        int b = t->urchin.bed, i = t->urchin.frond;
        float fx = urchin_frond_x(t, b, i), d = fx - t->urchin.x;
        if (fabsf(d) > 1.5f) {                                         /* on its way */
            t->urchin.x += (d > 0 ? 1 : -1) * fminf(fabsf(d), URCHIN_PX_S * dt);
            t->urchin.chew = 0;
            return;
        }
        float before = t->veg_h[b][i];                                 /* chewing at its foot */
        float h = fmaxf(URCHIN_KEEP, before - URCHIN_BITE / URCHIN_CHEW_S * dt);
        float eat = fminf(before - h, fmaxf(t->urchin.appetite, 0));
        t->veg_h[b][i] = before - eat;
        t->urchin.appetite -= eat;
        t->urchin.grazed_px += eat * (VEG_SEGS_FULL - 1) * VEG_PAY_PX;
        float c0 = t->urchin.chew; t->urchin.chew += dt;
        if (fmodf(c0, 1.8f) > fmodf(t->urchin.chew, 1.8f)) {           /* a bit of leaf drifts up from the tip */
            float x; int segs = tank_veg_frond(t, b, i, &x);
            for (int k = 0; k < MAX_BUBBLE; k++) {
                if (t->bubble[k].column) continue;
                t->bubble[k].x = x + tank_randf(t, -3, 3);
                t->bubble[k].y = TANK_BOT - 16 - segs * VEG_SEG_PX;
                break;
            }
        }
        tank_veg_sync(t);
        if (t->urchin.appetite <= 0.0005f || t->veg_h[b][i] <= URCHIN_KEEP + 0.0005f) {   /* full, or down to the keep line */
            t->urchin.bed = t->urchin.frond = -1; t->urchin.chew = 0;
            t->urchin.to = -1; t->urchin.rest = tank_randf(t, 30, 70);
        }
        return;
    }
    if (t->urchin.rest > 0) { t->urchin.rest -= dt; return; }      /* resting, its spines waving */
    if (t->urchin.to < 0) t->urchin.to = tank_randf(t, urchin_lo(), urchin_hi());
    float d = t->urchin.to - t->urchin.x;
    t->urchin.x += (d > 0 ? 1 : -1) * fminf(fabsf(d), URCHIN_PX_S * 0.7f * dt);
    if (fabsf(d) < 0.5f) { t->urchin.to = -1; t->urchin.rest = tank_randf(t, 40, 120); }
}
/* asleep: a share of the grass over the keep line, every frond in proportion
 * (see above); it wakes where the grass stands tallest */
static void urchin_sleep(tank_t *t, float seconds) {
    if (!(t->sd_unlocks & SD_ITEM_URCHIN)) return;
    if (t->urchin.x < 0) tank_urchin_place(t);
    float k = 1.0f - expf(-URCHIN_SLEEP_FRAC_H * seconds / 3600.0f);
    float eaten = 0, most = 0; int mb = -1, mi = -1;
    for (int b = 0; b < tank_veg_beds(t); b++) {
        if (tank_veg_kind(t, b) != VEG_KIND_GRASS) continue;
        int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
        for (int i = 0; i < n; i++) {
            float over = t->veg_h[b][i] - URCHIN_KEEP;
            if (over <= 0) continue;
            t->veg_h[b][i] -= over * k; eaten += over * k;
            if (over > most) { most = over; mb = b; mi = i; }
        }
    }
    if (eaten <= 0) return;
    t->urchin.grazed_px += eaten * (VEG_SEGS_FULL - 1) * VEG_PAY_PX;
    if (mb >= 0) t->urchin.x = urchin_frond_x(t, mb, mi);
    t->urchin.bed = t->urchin.frond = -1; t->urchin.chew = 0; t->urchin.to = -1; t->urchin.rest = 0;
    tank_veg_sync(t);
}
void tank_urchin_place(tank_t *t) {
    float x0, x1; tank_veg_bed(t, 0, &x0, &x1, NULL, NULL);       /* on the sand just right of the reef bed */
    t->urchin.x = clampf(x1 + 18, urchin_lo(), urchin_hi());
    t->urchin.bed = t->urchin.frond = -1; t->urchin.chew = 0; t->urchin.appetite = 0;
    t->urchin.to = -1; t->urchin.rest = 20;
}
/* ---- the shrimp school (SD_ITEM_SHRIMP; helper.h has the rules) ----
 * A loose school steering to one goal - a spot at the foot of the grass most
 * of the time, now and then out on the sand or up in open water - each shrimp
 * at its own offset from it, keeping about a body length from its neighbours
 * (Strato: "space them out a bit more so they overlap a tad less"). A pellet
 * resting on the floor becomes the goal and they crowd in to peck it. They
 * turn like the fish: a committed turn, then a settle. Tuned against the
 * 2026-09-29 mockup Strato approved. */
#define SHRIMP_CRUISE    12.0f        /* px/s */
#define SHRIMP_DART      58.0f        /* px/s, the tail-flick escape */
#define SHRIMP_SEP_X     18.0f        /* personal space, side to side (a body and a bit) ... */
#define SHRIMP_SEP_Y     11.0f        /* ... and up and down (7 stacked them in towers on the glass) */
#define SHRIMP_TAP_R     30.0f        /* a fingertip on a shrimp: its card */
#define SHRIMP_HEAD      7.0f         /* the head's offset from the body's centre, along the facing */
static float shrimp_hash(int i, int k) {  /* a fixed 0..1 per shrimp: its place in the school (no RNG draw) */
    uint32_t h = (uint32_t)i * 2654435761u ^ (uint32_t)k * 40503u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (h & 0xffff) / 65535.0f;
}
bool tank_shrimp_refusing(const tank_t *t) { return tank_algae_cover(t) > SHRIMP_REFUSE_COVER; }
static void shrimp_spawn(tank_t *t, int i, float x) {
    shrimp_t *q = &t->school.q[i];
    memset(q, 0, sizeof *q);
    q->x = x; q->y = FOOD_FLOOR_Y - 6 - shrimp_hash(i, 3) * 14;
    q->facing = shrimp_hash(i, 4) < 0.5f ? -1 : 1; q->yaw = q->facing;
    q->ph = shrimp_hash(i, 5) * 6.28f;
}
void tank_shrimp_place(tank_t *t, int n) {
    if (n < 0) n = 0;
    if (n > SHRIMP_MAX) n = SHRIMP_MAX;
    int wide = 0; float wbest = -1, x0 = 0, x1 = 0;           /* at the foot of the widest bed, clear of the glass */
    for (int b = 0; b < tank_veg_beds(t); b++) { float a, z; tank_veg_bed(t, b, &a, &z, NULL, NULL); if (z - a > wbest) { wbest = z - a; wide = b; } }
    tank_veg_bed(t, wide, &x0, &x1, NULL, NULL);
    float cx = clampf((x0 + x1) * 0.5f, TANK_FX0 + 60, TANK_FX1 - 60);
    for (int i = 0; i < n; i++) shrimp_spawn(t, i, cx + (shrimp_hash(i, 1) - 0.5f) * (40 + n * 9));
    t->school.n = (uint8_t)n;
    t->school.tx = cx; t->school.ty = FOOD_FLOOR_Y - 12; t->school.tt = 4;
}
/* the school's next goal. Mostly the foot of the grass; now and then out on
 * the sand; and now and then a slow DRIFT UP a bed to the top of its canopy
 * (or at least SHRIMP_RISE_MIN above the floor when the grass is cut short),
 * a while up there, then back down - Strato, 2026-09-29: "you dont get to see
 * them much ... i dont want them swimming all over the tank all the time but a
 * chance for them to randomly drift up to at least the top of the sea grass".
 * About a quarter of their time goes on these trips. Never while refusing food. */
#define SHRIMP_RISE_P    0.06f         /* of the goals picked (0.10: up off the bottom ~45% of the time) */
#define SHRIMP_RISE_MIN  60.0f         /* px above the floor, at the least */
static void shrimp_goal(tank_t *t, bool refusing) {
    int beds = tank_veg_beds(t);
    float r = tank_randf(t, 0, 1);
    if (!refusing && r < SHRIMP_RISE_P && beds > 0) {              /* up the grass to the canopy */
        int b = (int)tank_randf(t, 0, beds - 0.001f);
        float x0, x1, top; tank_veg_bed(t, b, &x0, &x1, &top, NULL);
        t->school.tx = x1 - x0 > 16 ? tank_randf(t, x0 + 8, x1 - 8) : (x0 + x1) * 0.5f;
        t->school.ty = fminf(top + tank_randf(t, -8, 10), FOOD_FLOOR_Y - SHRIMP_RISE_MIN);
        t->school.tt = tank_randf(t, 22, 32);                           /* the drift up is slow; then a while at the top */
        return;
    }
    if ((refusing || r < SHRIMP_RISE_P + 0.62f) && beds > 0) {     /* the grass: the school's cover */
        int b = (int)tank_randf(t, 0, beds - 0.001f);
        float x0, x1; tank_veg_bed(t, b, &x0, &x1, NULL, NULL);
        t->school.tx = x1 - x0 > 16 ? tank_randf(t, x0 + 8, x1 - 8) : (x0 + x1) * 0.5f;
    } else t->school.tx = tank_randf(t, TANK_FX0 + 40, TANK_FX1 - 40);         /* out on the sand */
    t->school.ty = refusing || tank_randf(t, 0, 1) < 0.8f ? FOOD_FLOOR_Y - tank_randf(t, 12, 26)
                                                          : FOOD_FLOOR_Y - tank_randf(t, 30, 64);   /* a swim up into open water */
    t->school.tt = tank_randf(t, 6, 11);
}
/* the grass's top over x: the bed under it, or the beds' average over open
 * sand. A pellet that has sunk below it is the shrimp's to take, still falling
 * or resting on the floor (Strato, 2026-09-29: "if food falls below the
 * seagrass canopy its fair game for shrimp even as its still floating downward") */
static float shrimp_canopy_y(const tank_t *t, float x) {
    float sum = 0; int nb = tank_veg_beds(t);
    for (int b = 0; b < nb; b++) {
        float x0, x1, top; tank_veg_bed(t, b, &x0, &x1, &top, NULL);
        if (x >= x0 && x <= x1) return top;
        sum += top;
    }
    return nb ? sum / nb : FOOD_FLOOR_Y;
}
static bool shrimp_food_ok(const tank_t *t, const food_t *p) {
    return p->alive && (p->floor_s > 0 || p->y >= shrimp_canopy_y(t, p->x));
}
static void shrimp_tick(tank_t *t, float dt) {
    if (!(t->sd_unlocks & SD_ITEM_SHRIMP)) return;
    if (t->school.n < SHRIMP_START) tank_shrimp_place(t, SHRIMP_START);
    bool refusing = tank_shrimp_refusing(t);
    if (t->school.cool > 0) t->school.cool -= dt;
    int n = t->school.n;
    float cx = 0, cy = 0;                                          /* the school's middle */
    for (int i = 0; i < n; i++) { cx += t->school.q[i].x; cy += t->school.q[i].y; }
    cx /= n; cy /= n;
    /* a newcomer: ten pellets eaten and the cooldown out - it steps out of the grass by the school */
    if (t->school.food >= SHRIMP_PER_JOIN && t->school.cool <= 0 && n < SHRIMP_MAX) {
        shrimp_spawn(t, n, cx + (shrimp_hash(n, 1) - 0.5f) * 20);
        t->school.n = (uint8_t)++n;
        t->school.food = 0; t->school.cool = SHRIMP_COOLDOWN_S;
    }
    t->school.tt -= dt;
    if (t->school.tt <= 0) shrimp_goal(t, refusing);
    int fp = -1; float best = 1e9f;                                /* the pellet in reach nearest the school */
    if (!refusing)
        for (int k = 0; k < MAX_FOOD; k++) {
            const food_t *p = &t->food[k];
            if (!shrimp_food_ok(t, p)) continue;
            float d = fabsf(p->x - cx) + fabsf(p->y - cy) * 0.5f;
            if (d < best) { best = d; fp = k; }
        }
    float gx = fp >= 0 ? t->food[fp].x : t->school.tx;
    float gy = fp < 0 ? t->school.ty : t->food[fp].floor_s > 0 ? FOOD_FLOOR_Y - 2 : t->food[fp].y;   /* on the floor, or still sinking */
    float spread = 40 + n * 9;                                    /* the school's width: 76 px for 4, 130 for 10 */
    /* the whole school fits between the side glass: a goal by the wall (a bed
       against it, a pellet in the corner) keeps them spread, not piled up on
       the glass; the ones nearest a corner pellet still reach it */
    float half = (fp >= 0 ? spread * 0.45f : spread) * 0.5f + 16;
    gx = clampf(gx, TANK_FX0 + half, TANK_FX1 - half);
    for (int i = 0; i < n; i++) {
        shrimp_t *q = &t->school.q[i];
        q->ph += dt; q->pecking = 0;
        if (q->dart > 0) {
            q->dart -= dt;
            float k = powf(0.2f, dt); q->vx *= k; q->vy *= k;
        } else {
            float ox = (shrimp_hash(i, 1) - 0.5f) * spread, oy = (shrimp_hash(i, 2) - 0.5f) * 24;
            float tx = gx + (fp >= 0 ? ox * 0.45f : ox), ty = gy + (fp >= 0 ? 0 : oy);
            float dx = tx - q->x, dy = ty - q->y, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
            bool chase = fp >= 0;                                       /* food in reach: they hurry, up or down alike */
            float sp = fminf(SHRIMP_CRUISE * (chase ? 1.6f : 1.0f), d * 0.9f);
            float wx = dx / d * sp, wy = dy / d * sp * (chase ? 1.0f : 0.7f);
            float sx = fp >= 0 ? SHRIMP_SEP_X * 0.6f : SHRIMP_SEP_X, sy = fp >= 0 ? SHRIMP_SEP_Y * 0.6f : SHRIMP_SEP_Y;
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                float nx = (q->x - t->school.q[j].x) / sx, ny = (q->y - t->school.q[j].y) / sy, e = sqrtf(nx * nx + ny * ny);
                if (e < 1 && e > 0) { wx += nx / e * (1 - e) * 22; wy += ny / e * (1 - e) * 9; }
            }
            float a = fminf(1, dt * 2.5f);
            q->vx += (wx - q->vx) * a; q->vy += (wy - q->vy) * a;
            float hx = q->x + SHRIMP_HEAD * q->facing;
            for (int k = 0; k < MAX_FOOD; k++) {                       /* a pellet at its head */
                food_t *p = &t->food[k];
                if (!shrimp_food_ok(t, p) || fabsf(p->x - hx) > 8 || fabsf(p->y - q->y) > 9) continue;
                if (refusing) {                                        /* too much algae: it turns its back on it */
                    q->facing = (int8_t)(q->x > p->x ? 1 : -1); q->behind = -0.6f;
                    q->vx = q->facing * 6.0f;
                } else if (k == fp) {
                    q->pecking = 1; q->vx *= 0.5f;
                    q->vy = p->floor_s > 0 ? q->vy * 0.5f : 8.0f;           /* a falling pellet: it sinks with it */
                    p->nibbled += dt;
                }
                break;
            }
        }
        q->x += q->vx * dt;
        q->y += q->vy * dt + sinf(q->ph * 2.1f) * dt * 1.2f;
        q->x = clampf(q->x, TANK_FX0 + 12, TANK_FX1 - 12);
        q->y = clampf(q->y, 40, FOOD_FLOOR_Y - 1);
#if GLASS_CURVED
        tank_glass_clamp(&q->x, &q->y, 10);          /* up a tall frond by the wall, the bowl closes in */
#endif
        if (q->dart <= 0) {                                             /* the committed turn */
            int8_t want = q->vx > 1.5f ? 1 : q->vx < -1.5f ? -1 : q->facing;
            if (want != q->facing) { q->behind += dt; if (q->behind > 0.35f) { q->facing = want; q->behind = -0.6f; } }
            else q->behind = fminf(0, q->behind + dt);
        }
        q->yaw += clampf(q->facing - q->yaw, -5 * dt, 5 * dt);
    }
    if (fp >= 0 && t->food[fp].nibbled >= SHRIMP_PECK_S) {             /* eaten: one toward the next shrimp */
        t->food[fp].alive = false;
        if (t->school.food < SHRIMP_PER_JOIN) t->school.food++;       /* stops at ten while the cooldown runs */
        t->school.eaten++;
    }
}
/* asleep: nothing is eaten, so nothing joins; the cooldown runs on lived time */
static void shrimp_sleep(tank_t *t, float seconds) {
    if (!(t->sd_unlocks & SD_ITEM_SHRIMP)) return;
    t->school.cool = fmaxf(0, t->school.cool - seconds);
}
static void shrimp_scatter(tank_t *t, float x, float y, float radius) {
    if (!(t->sd_unlocks & SD_ITEM_SHRIMP)) return;
    bool any = false;
    for (int i = 0; i < t->school.n; i++) {
        shrimp_t *q = &t->school.q[i];
        float dx = q->x - x, dy = q->y - y, d = sqrtf(dx * dx + dy * dy);
        if (d >= radius) continue;
        if (d < 1) { dx = q->facing ? -q->facing : 1; dy = 0; d = 1; }
        q->dart = 0.55f; q->vx = dx / d * SHRIMP_DART; q->vy = dy / d * SHRIMP_DART * 0.7f;
        q->facing = (int8_t)(q->vx > 0 ? -1 : 1); q->yaw = q->facing;   /* tail first: they flick backward */
        q->behind = -0.8f; any = true;
    }
    if (any) t->school.tt = fminf(t->school.tt, 3.0f);                /* and regroup somewhere new */
}
void tank_frogman_place(tank_t *t) {
    t->frogman.x = TANK_W / 2; t->frogman.y = t->frogman.lane = TANK_H * 0.36f; t->frogman.yaw = 1; t->frogman.dir = 1; t->frogman.lane_t = 20;
}
void tank_sub_place(tank_t *t) {
    t->sub.x = TANK_W / 2; t->sub.y = t->sub.lane = TANK_H * 0.26f; t->sub.yaw = -1; t->sub.dir = -1; t->sub.lane_t = 15;
    t->sub.v = SUB_SPEED; t->sub.stop = 0; t->sub.next = 18; t->sub.peri = 0; t->sub.look = 0;
}
bool tank_sub_hit(const tank_t *t, float x, float y) {
    if (!(t->sd_unlocks & SD_ITEM_SUB)) return false;
    float dx = x - t->sub.x, dy = y - t->sub.y;
    return dx * dx / (30.0f * 30.0f) + dy * dy / (16.0f * 16.0f) < 1;
}
/* the drifters share the water (2026-10-10 night, the frogman and the sub): a lane picked within
 * PASS_DY of another's is pushed PASS_DY + 6 away (the side with room), and when two close within
 * PASS_DX nearly level, each eases off to the far side of the other - they pass, never collide */
#define PASS_DX 110.0f
#define PASS_DY 34.0f
static float lane_clear_of(float lane, float other, float lo, float hi) {
    if (fabsf(lane - other) >= PASS_DY) return lane;
    float up = other - (PASS_DY + 6), down = other + (PASS_DY + 6);
    if (lane < other) return up >= lo ? up : down;
    return down <= hi ? down : up;
}
/* ... over every other drifter in the water: the lane clear of each one's, and the one
   coming close enough to pass (none = NULL) */
static float lane_clear(tank_t *t, int self, float lane, float lo, float hi) {
    for (int k = 0; k < HK_COUNT; k++)
        if (k != self && HELPER[k].habitat == HAB_WATER && helper_owned(t, k)) lane = lane_clear_of(lane, HELPER[k].drift(t)->lane, lo, hi);
    return lane;
}
static const drift_t *drift_near(tank_t *t, int self) {
    const drift_t *me = HELPER[self].drift(t);
    for (int k = 0; k < HK_COUNT; k++) {
        if (k == self || HELPER[k].habitat != HAB_WATER || !helper_owned(t, k)) continue;
        const drift_t *o = HELPER[k].drift(t);
        if (fabsf(o->x - me->x) < PASS_DX && fabsf(o->y - me->y) < PASS_DY) return o;
    }
    return NULL;
}
/* the submarine's cruise (2026-10-10 night): the frogman's drift at SUB_SPEED, easing to a halt for
 * a stop every SUB_CRUISE_LO..HI s - the periscope rises over the first second, scans (the renderer
 * turns it on sub.look), and sinks over the last - then on again; a new lane now and then */
static void sub_tick(tank_t *t, float dt) {
    if (!(t->sd_unlocks & SD_ITEM_SUB)) return;
    float lo = tank_glass_x0(t->sub.y) + SUB_MARGIN, hi = tank_glass_x1(t->sub.y) - SUB_MARGIN;   /* the glass at its height: the bowl narrows toward the top and the sand */
    if (t->sub.stop > 0) {
        t->sub.stop -= dt;
        float into = SUB_STOP_S - t->sub.stop, want = into < 1 ? into : t->sub.stop < 1 ? t->sub.stop : 1;
        want = clampf(want, 0, 1);
        t->sub.peri += (want - t->sub.peri) * fminf(1, 5 * dt);
        t->sub.look += dt;
        t->sub.v = fmaxf(0, t->sub.v - 24 * dt);
        if (t->sub.stop <= 0) { t->sub.stop = 0; t->sub.peri = 0; t->sub.look = 0; t->sub.next = tank_randf(t, SUB_CRUISE_LO, SUB_CRUISE_HI); }
    } else {
        t->sub.next -= dt;
        if (t->sub.next <= 0) { t->sub.stop = SUB_STOP_S; t->sub.look = 0; }
        t->sub.v = fminf(SUB_SPEED, t->sub.v + 10 * dt);
    }
    if (t->sub.dir >= 0 && t->sub.x >= hi) t->sub.dir = -1;
    if (t->sub.dir < 0 && t->sub.x <= lo) t->sub.dir = 1;
    float want = (float)t->sub.dir, d = want - t->sub.yaw, step = 1.6f * dt;
    t->sub.yaw += d > step ? step : d < -step ? -step : d;
    t->sub.x += t->sub.yaw * t->sub.v * dt;
    t->sub.x = t->sub.x < lo - 2 ? lo - 2 : t->sub.x > hi + 2 ? hi + 2 : t->sub.x;
    t->sub.lane_t -= dt;
    if (t->sub.lane_t <= 0) {
        t->sub.lane_t = tank_randf(t, 15, 35);
        t->sub.lane = tank_randf(t, fmaxf(SUB_LANE_LO, tank_glass_top(t->sub.x) + 30), SUB_LANE_HI);
        t->sub.lane = lane_clear(t, HK_SUB, t->sub.lane, SUB_LANE_LO, SUB_LANE_HI);
    }
    const drift_t *o = drift_near(t, HK_SUB); bool dodge = o != NULL;   /* the frogman ahead: pass him (frogman_tick) */
    if (dodge) t->sub.lane = clampf(o->y + (t->sub.y >= o->y ? PASS_DY + 6 : -(PASS_DY + 6)), SUB_LANE_LO, SUB_LANE_HI);
    t->sub.y += (t->sub.lane - t->sub.y) * fminf(1, (dodge ? 0.9f : 0.12f) * dt);
}
bool tank_frogman_hit(const tank_t *t, float x, float y) {
    if (!(t->sd_unlocks & SD_ITEM_FROGMAN)) return false;
    float dx = x - t->frogman.x, dy = y - t->frogman.y;
    return dx * dx / (22.0f * 22.0f) + dy * dy / (14.0f * 14.0f) < 1;
}
/* the frogman's drift (2026-10-10): sideways across the water at FROG_SPEED, a turn at
 * either margin (his yaw eases over ~0.8 s, so he thins and comes back facing the other
 * way), a slow ease to a new lane every 20-40 s. The bob and the wave are the renderer's,
 * off the clock. */
static void frogman_tick(tank_t *t, float dt) {
    if (!(t->sd_unlocks & SD_ITEM_FROGMAN)) return;
    float lo = tank_glass_x0(t->frogman.y) + FROG_MARGIN, hi = tank_glass_x1(t->frogman.y) - FROG_MARGIN;   /* the glass at his height (the whole column is his since 2026-10-10 night) */
    if (t->frogman.dir >= 0 && t->frogman.x >= hi) t->frogman.dir = -1;
    if (t->frogman.dir < 0 && t->frogman.x <= lo) t->frogman.dir = 1;
    float want = (float)t->frogman.dir, d = want - t->frogman.yaw, step = 2.5f * dt;
    t->frogman.yaw += d > step ? step : d < -step ? -step : d;
    t->frogman.x += t->frogman.yaw * FROG_SPEED * dt;            /* slows through the turn, as a fish does */
    t->frogman.x = t->frogman.x < lo - 2 ? lo - 2 : t->frogman.x > hi + 2 ? hi + 2 : t->frogman.x;
    t->frogman.lane_t -= dt;
    if (t->frogman.lane_t <= 0) {
        t->frogman.lane_t = tank_randf(t, 20, 40);
        t->frogman.lane = tank_randf(t, fmaxf(FROG_LANE_LO, tank_glass_top(t->frogman.x) + 26), FROG_LANE_HI);
        t->frogman.lane = lane_clear(t, HK_FROGMAN, t->frogman.lane, FROG_LANE_LO, FROG_LANE_HI);
    }
    /* the sub coming (2026-10-10 night, Alvin: "make sure the submarine and frogman don't crash into
       each other's path"): within PASS_DX of it and nearly level, he eases off to a lane PASS_DY
       clear of it - quickly - and the sub does the same the other way, so they pass */
    const drift_t *o = drift_near(t, HK_FROGMAN); bool dodge = o != NULL;
    if (dodge) t->frogman.lane = clampf(o->y + (t->frogman.y >= o->y ? PASS_DY + 6 : -(PASS_DY + 6)), FROG_LANE_LO, FROG_LANE_HI);
    t->frogman.y += (t->frogman.lane - t->frogman.y) * fminf(1, (dodge ? 0.9f : 0.15f) * dt);
}
bool tank_shrimp_hit(const tank_t *t, float x, float y) {
    if (!(t->sd_unlocks & SD_ITEM_SHRIMP)) return false;
    for (int i = 0; i < t->school.n; i++)
        if (tank_dist(t->school.q[i].x, t->school.q[i].y, x, y) <= SHRIMP_TAP_R) return true;
    return false;
}

/* ---- what each one does at tank_init, on arrival and at a startle,
 * and what it keeps across a boot ---- */
static void snail_init(tank_t *t) {
    t->snail.x = -1; t->snail.y = -1; t->snail.heading = 0; t->snail.cell = -1; t->snail.graze = 0;
    t->snail.grazed = 0; t->snail.sleep_acc = 0;
}
/* its spot (x, y) and the cells it grazed (count), owned or not */
static void snail_save(const tank_t *t, helper_rec_t *r) {
    r->x = t->snail.x > 0 ? t->snail.x : 0; r->y = t->snail.y > 0 ? t->snail.y : 0; r->count = t->snail.grazed;
}
static void snail_load(tank_t *t, const helper_rec_t *r) {
    if (r->x > 0) { t->snail.x = r->x; t->snail.y = r->y; }
    t->snail.grazed = r->count;
}
static void urchin_init(tank_t *t) {
    t->urchin.x = -1; t->urchin.bed = t->urchin.frond = -1; t->urchin.appetite = 0; t->urchin.chew = 0;
    t->urchin.to = -1; t->urchin.rest = 0; t->urchin.grazed_px = 0;
}
/* its x on the floor and the grass it ate (amount, px) */
static void urchin_save(const tank_t *t, helper_rec_t *r) {
    r->x = helper_owned(t, HK_URCHIN) && t->urchin.x > 0 ? t->urchin.x : 0;
    r->amount = t->urchin.grazed_px > 0 ? t->urchin.grazed_px : 0;
}
static void urchin_load(tank_t *t, const helper_rec_t *r) {
    if (helper_owned(t, HK_URCHIN)) {                  /* back where it was on the floor, its tally */
        tank_urchin_place(t);
        if (r->x > 0) t->urchin.x = r->x;
        t->urchin.grazed_px = r->amount > 0 ? r->amount : 0;
    } else { t->urchin.x = -1; t->urchin.grazed_px = 0; }
}
static void shrimp_init(tank_t *t) { memset(&t->school, 0, sizeof t->school); }   /* (2026-10-22: tank_init left the last tank's school, its tally on its card) */
static void shrimp_arrive(tank_t *t) { tank_shrimp_place(t, SHRIMP_START); t->school.food = 0; t->school.cool = 0; }
static int  shrimp_tap(tank_t *t, float x, float y) {
    int taps = tank_shrimp_tap(t, x, y);
    return taps == 1 ? 1 : taps >= 3 ? -1 : 0;         /* the third quick tap scares them: the card goes */
}
/* their number (n), the pellets toward the next one (m), the cooldown (amount)
   and the pellets eaten (count) */
static void shrimp_save(const tank_t *t, helper_rec_t *r) {
    r->n = helper_owned(t, HK_SHRIMP) ? t->school.n : 0; r->m = t->school.food;
    r->amount = t->school.cool > 0 ? t->school.cool : 0; r->count = t->school.eaten;
}
static void shrimp_load(tank_t *t, const helper_rec_t *r) {
    if (helper_owned(t, HK_SHRIMP)) {                  /* the school back in the grass, its count and its progress */
        tank_shrimp_place(t, r->n >= SHRIMP_START ? r->n : SHRIMP_START);
        t->school.food = r->m <= SHRIMP_PER_JOIN ? r->m : SHRIMP_PER_JOIN;
        t->school.cool = r->amount > 0 && r->amount <= SHRIMP_COOLDOWN_S ? r->amount : 0;
        t->school.eaten = r->count > 0 ? r->count : 0;
    } else { t->school.n = 0; t->school.eaten = 0; }
}
static drift_t *frogman_drift(tank_t *t) { return &t->frogman; }
static drift_t *sub_drift(tank_t *t)     { return &t->sub; }
static bool snail_body(const tank_t *t, int i, float *x, float *y)   { *x = t->snail.x; *y = t->snail.y; return i == 0 && t->snail.x >= 0; }
static bool urchin_body(const tank_t *t, int i, float *x, float *y)  { *x = t->urchin.x; *y = URCHIN_FLOOR_Y; return i == 0 && t->urchin.x >= 0; }
static bool shrimp_body(const tank_t *t, int i, float *x, float *y)  {
    if (i < 0 || i >= t->school.n) return false;
    *x = t->school.q[i].x; *y = t->school.q[i].y; return true;
}
static bool frogman_body(const tank_t *t, int i, float *x, float *y) { *x = t->frogman.x; *y = t->frogman.y; return i == 0; }
static bool sub_body(const tank_t *t, int i, float *x, float *y)     { *x = t->sub.x; *y = t->sub.y; return i == 0; }

const helper_def_t HELPER[HK_COUNT] = {
    [HK_SNAIL]   = { .name = "SNAIL", .shop_bit = SD_ITEM_SNAIL, .shop_item = 1, .habitat = HAB_GLASS,
                     .init = snail_init, .arrive = tank_snail_place, .tick = snail_tick, .sleep_slice = snail_sleep,
                     .hit = tank_snail_hit, .body = snail_body, .save = snail_save, .load = snail_load },
    [HK_URCHIN]  = { .name = "SEA URCHIN", .shop_bit = SD_ITEM_URCHIN, .shop_item = 6, .habitat = HAB_FLOOR,
                     .init = urchin_init, .arrive = tank_urchin_place, .tick = urchin_tick, .sleep_slice = urchin_sleep,
                     .hit = tank_urchin_hit, .body = urchin_body, .save = urchin_save, .load = urchin_load },
    [HK_SHRIMP]  = { .name = "SHRIMP", .shop_bit = SD_ITEM_SHRIMP, .shop_item = 5, .habitat = HAB_FLOOR,
                     .init = shrimp_init, .arrive = shrimp_arrive, .tick = shrimp_tick, .sleep = shrimp_sleep,
                     .startle = shrimp_scatter, .hit = tank_shrimp_hit, .tap = shrimp_tap, .body = shrimp_body,
                     .save = shrimp_save, .load = shrimp_load },
    /* the drifters: no card yet - a tap on one goes through to the water */
    [HK_FROGMAN] = { .name = "FROGMAN", .shop_bit = SD_ITEM_FROGMAN, .shop_item = SD_ITEM_FROGMAN_IDX, .habitat = HAB_WATER,
                     .init = tank_frogman_place, .arrive = tank_frogman_place, .tick = frogman_tick,
                     .body = frogman_body, .drift = frogman_drift },
    [HK_SUB]     = { .name = "SUBMARINE", .shop_bit = SD_ITEM_SUB, .shop_item = SD_ITEM_SUB_IDX, .habitat = HAB_WATER,
                     .init = tank_sub_place, .arrive = tank_sub_place, .tick = sub_tick,
                     .body = sub_body, .drift = sub_drift },
};

int helper_kind_of_item(int item) {
    if (item < 0) return -1;
    for (int k = 0; k < HK_COUNT; k++) if (HELPER[k].shop_item == item) return k;
    return -1;
}
bool helper_owned(const tank_t *t, int k) {
    return k >= 0 && k < HK_COUNT && (t->sd_unlocks & HELPER[k].shop_bit);
}
int helper_hit(const tank_t *t, float x, float y) {
    for (int k = 0; k < HK_COUNT; k++)
        if (HELPER[k].hit && HELPER[k].hit(t, x, y)) return k;
    return -1;
}
void helper_init(tank_t *t) {
    for (int k = 0; k < HK_COUNT; k++) if (HELPER[k].init) HELPER[k].init(t);
}
void helper_arrive(tank_t *t, int k) {
    if (k >= 0 && k < HK_COUNT && HELPER[k].arrive) HELPER[k].arrive(t);
}
void helper_tick_all(tank_t *t, float dt) {
    for (int k = 0; k < HK_COUNT; k++) if (HELPER[k].tick && helper_owned(t, k)) HELPER[k].tick(t, dt);
}
void helper_sleep_all(tank_t *t, float seconds) {
    for (int k = 0; k < HK_COUNT; k++) if (HELPER[k].sleep && helper_owned(t, k)) HELPER[k].sleep(t, seconds);
}
void helper_sleep_slice_all(tank_t *t, float seconds) {
    for (int k = 0; k < HK_COUNT; k++) if (HELPER[k].sleep_slice && helper_owned(t, k)) HELPER[k].sleep_slice(t, seconds);
}
void helper_startle_all(tank_t *t, float x, float y, float radius) {
    for (int k = 0; k < HK_COUNT; k++) if (HELPER[k].startle && helper_owned(t, k)) HELPER[k].startle(t, x, y, radius);
}
void helper_leave(tank_t *t, int k) {
    if (k >= 0 && k < HK_COUNT && HELPER[k].init) HELPER[k].init(t);
}
