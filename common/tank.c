/* tank.c — reflex layer. See tank.h. Faithful port of the browser prototype's
 * updateFish/targetForGoal/wall handling, with prototype px values scaled by
 * ~0.55 for the 448-wide tank. */
#include "tank.h"
#include "tank_internal.h"
#include "tank_events.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void veg_sync(tank_t *t);   /* veg_growth[] = per-bed mean of veg_h[][] */

const char *const GOAL_NAMES[GOAL_COUNT] = {
    "seek_food", "flee_shadow", "visit_bubbles", "follow_friend",
    "explore", "rest", "dart_play", "inspect_reef",
};

/* ---- the event bus (tank_events.h): one listener, synchronous ---- */
const char *const TANK_EVENT_NAMES[TEV_COUNT] = {
    "tap", "feed", "light_on", "light_off", "wipe", "snip", "eat", "spook", "investigate", "bubbles",
    "welcome", "wheel_tick", "confirm",
};
static tank_event_fn s_ev_fn; static void *s_ev_ud;
void tank_events_set(tank_event_fn fn, void *ud) { s_ev_fn = fn; s_ev_ud = ud; }
void tank_emit(int ev, int fish) { if (s_ev_fn) s_ev_fn(ev, fish, s_ev_ud); }

const char *const STAGE_NAMES[4] = { "fry", "juv", "adult", "elder" };
const char *const TRAINED_NAMES[N_TRAINED_NAMES] = { "mira", "bolt", "kelp", "nori" };

int tank_reflex_overrides = 0;   /* starvation-ignored episodes (diagnostic) */

/* deterministic xorshift32 */
uint32_t tank_xr(tank_t *t) {
    uint32_t x = t->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return t->rng = x;
}
float tank_randf(tank_t *t, float lo, float hi) {
    return lo + (hi - lo) * (float)(tank_xr(t) & 0xffffff) / 16777215.0f;
}

float tank_dist(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}

int tank_nearest_food(const tank_t *t, const fish_t *f, float *dist_out) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < MAX_FOOD; i++) {
        if (!t->food[i].alive) continue;
        float d = tank_dist(f->x, f->y, t->food[i].x, t->food[i].y);
        if (d < bd) { bd = d; best = i; }
    }
    if (dist_out) *dist_out = bd;
    return best;
}

int tank_nearest_friend(const tank_t *t, int fish_idx, float *dist_out) {
    const fish_t *f = &t->fish[fish_idx];
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < t->n_fish; i++) {
        if (i == fish_idx) continue;
        float d = tank_dist(f->x, f->y, t->fish[i].x, t->fish[i].y);
        if (d < bd) { bd = d; best = i; }
    }
    if (dist_out) *dist_out = bd;
    return best;
}

/* the coral's swatches (2026-09-23): the reference art's orange first, then
 * reef colours - pink, magenta, violet, blue, teal, green, gold */
const uint32_t CORAL_PAL[CORAL_N] = { 0xff7a1e, 0xf2698f, 0xd24bb4, 0x8a5be0, 0x3f8ff0, 0x2ec9b4, 0x7fc64a, 0xf2c23a };
/* the reef cluster's three looks (2026-09-24): REEF is the art's - an
 * orange coral, purple tubes, a cyan brain; LAGOON pink / blue / lime;
 * DUSK magenta / teal / gold */
const cluster_scheme_t CLUSTER_SCHEMES[CLUSTER_SCHEME_N] = {
    { "REEF",   0xff6a2a, 0x9b4fe0, 0x5fd8e8 },
    { "LAGOON", 0xf25c8a, 0x3f8ff0, 0xb9f04a },
    { "DUSK",   0xd24bb4, 0x2ec9b4, 0xf2c23a },
};


void tank_set_bubble_x(tank_t *t, float x) {
    /* clear of the grass corner's spot (the fish's other landmark) and of the glass;
       grass is fine - the beds cover most of the width and the default spot
       already rises through one */
    float lo = t->reef_x + 50, hi = TANK_FX1 - 30;
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    float dx = x - t->decor[DK_AIRSTONE].x;
    t->decor[DK_AIRSTONE].x = x;
    for (int i = 0; i < MAX_BUBBLE; i++) if (t->bubble[i].column) t->bubble[i].x += dx;   /* the column moves as one */
}


void tank_init(tank_t *t, uint32_t seed) {
    t->rng = seed ? seed : 0xC0FFEE;
    t->n_fish = 0;                 /* progression_boot restores or calls tank_new_population */
    for (int i = 0; i < MAX_FOOD; i++) t->food[i].alive = false;
    decor_init(t);
    t->decor[DK_AIRSTONE].x = BUBBLE_X_DEFAULT; t->bubble_y = TANK_BOT * 0.5f;   /* matches gen_traces.py; setup may move x */
    for (int i = 0; i < MAX_BUBBLE; i++) {
        bubble_t *b = &t->bubble[i];
        b->column = i < 10;
        b->x = b->column ? tank_bubble_x(t) + tank_randf(t, -10, 10) : tank_randf(t, TANK_FX0 + 12, TANK_FX1 - 12);
        b->y = tank_randf(t, 0, TANK_BOT);
        b->vy = tank_randf(t, 14, 30);
        b->wobble = tank_randf(t, 0, TAU);
    }
    t->reef_x   = TANK_FX0 + TANK_FW * 0.15f; t->reef_y   = TANK_BOT * 0.85f;
    t->clock = 0; t->night = false; t->idle_s = 0;
    t->light_idle_s = LIGHT_IDLE_S; t->light_auto = false; t->light_manual_off = false; t->light_tip_seen = false;
    t->orient_lock = false; t->autofeed_off = false;
    t->screen_turned = false; t->theme = 0;
    t->light_override = false; t->light_on = true;
    t->hold_active = false; t->hold_time = 0; t->hold_approached = false;
    t->tap_count = 0; t->tap_burst_t = 99; t->startled = false;
    t->startle_cooldown = 0;
    t->feed_spot_x = -1; t->player_feedings = 0; t->feed_open = false; t->hold_approaches = 0; t->greet_timer = 0;
    t->courting = false; t->court_a = t->court_b = -1;
    t->court_cool = 30; t->court_active = 0;
    t->spawning = false; t->spawn_danced = 0; t->ui_cover = false;
    t->ravenous = false; t->trickle_off = false;
    t->stage_fish = -1; t->hold_light = false;
    t->drag_active = false; t->drag_has_prev = false; t->drag_dist = 0;
    for (int b = 0; b < VEG_BEDS_MAX; b++) {
        /* a fresh tank's canopy has a natural profile: fronds within +-0.04
           of VEG_START, seeded per slot so it's the same tank every boot */
        for (int i = 0; i < VEG_FRONDS_MAX; i++) {
            uint32_t h = (uint32_t)((b * 17 + i + 1) * 2654435761u);
            t->veg_h[b][i] = VEG_START + ((int)(h >> 8 & 255) - 128) / 128.0f * 0.04f;
        }
    }
    veg_sync(t);
    t->slash_armed = t->slash_engaged = t->slash_cut = false;
    t->slash_h = t->slash_v = 0;
    t->tool = TOOL_HAND; t->tool_idle = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) t->algae[i] = 0;
    t->algae_acc = 0; t->trims = 0; t->cells_cleaned = 0;
    t->algae_colonies = 0; t->trim_px = 0;
    t->sd_balance = t->sd_earned = 0; t->sd_unlocks = 0;
    for (int i = 0; i < N_FISH_MAX; i++) t->sd_paid_fish[i] = 0;
    t->sd_colonies_paid = t->sd_inches_paid = 0;
    helper_init(t);
    t->tank_ms_bits = 0; t->tank_ms_seen = 0; t->ask_rr = 0; t->advisor_asks = 0;
    tank_scatter_food(t, 2);
}

#define TAP_WINDOW      0.5f    /* taps closer than this form a burst */
#define STARTLE_COOLDOWN 6.0f   /* calm seconds before the spook wears off */
/* (STARTLE_RADIUS, HOLD_RADIUS, HOLD_ATTRACT_S, HOLD_HUNGER_VETO: tank_internal.h) */
#define SPAWN_NEAR_PX   20.0f   /* past the courtship loop's reach: a parent this close is circling in the fronds */
#define HOLD_APPROACH_FROM 60.0f /* a hold-approach must START at least this far out:
                                 * a fish already under the finger earns nothing */
#define HOLD_APPROACH_AT   30.0f /* ... and come in this close */

/* ---- hunger economy (2026-09-01) ----
 * The prototype's per-second metabolism (0.15 + 0.12*bold: fed to starving
 * in under a minute) shipped unchanged into a tank the keeper tends for a
 * few minutes at a time - and the trickle that fed the prototype's single
 * fish was scaled per FRAME, so at the device's 25 fps it dropped 2.4x fewer
 * pellets than the 60 fps sim and could never keep up with four fish: the
 * school lived at hunger 9-10, begging under the surface (Strato: "ravenous
 * too often, hovering near the top, breaking the experience").
 * Now: a meal lasts ~5-6 minutes of awake time, and the tank's own trickle is
 * HUNGER-GATED and per-second - it drops a pellet only while somebody is
 * really hungry (a few seconds' wait), so an untended tank cycles between
 * "peckish" and "just fed" on its own and never reaches the ravenous
 * threshold (8.5, progression.c) awake. Ravenous begging is once again the
 * after-sleep / long-absence event it was designed to be (tank_tick_sleep's
 * 0.8/h), and the keeper's pellets are what make a fish FULL - play, bubbles,
 * the reef, a friend: the fed-fish repertoire. The advisor sees only banded
 * hunger, so its training distribution is untouched. */
/* (HUNGER_PER_S, HUNGER_BOLD_PER_S, HUNGER_DART_PER_S and the curiosity's spend: tank_internal.h) */
#define TRICKLE_HUNGER     7.5f     /* the tank feeds itself only past this (someone) */
#define TRICKLE_RATE       0.15f    /* pellets/s while the gate is open (~7 s wait) */

/* ---- upkeep: the vegetation keeps growing, algae films the glass ----
 * Vegetation is a comfort system, not scenery: fish swim slower inside the
 * canopy and their stress (already in the advisor's schema) answers to it -
 * cover calms (a canopy is a place to hide), a tank being smothered (two
 * beds past VEG_SMOTHER) presses hard, a completely scalped tank is a mild
 * unease that lifts as soon as one tuft regrows. Rates are per real second;
 * tank_tick runs them awake, tank_tick_sleep runs them faster (an untended
 * dark tank is where the garden gets away from you). */
#define VEG_GROW_AWAKE_S    162000.0f /* nubs -> the ceiling in ~45 h awake (was 30 h
                                       * until 2026-09-23: a few hours' sleep put
                                       * every bed at the surface, every time) */
#define VEG_GROW_SLEEP_S    72000.0f  /* ~20 h of sleep (was 10 h): one night takes a
                                       * trimmed bed from a third of the glass to
                                       * two thirds, the second reaches the ceilings */
#define VEG_SWORD_GROW      1.25f     /* the sword plant grows a bit faster than the
                                       * grass (~36 h awake / ~16 h asleep) */
#define ALGAE_STEP_AWAKE_S  240.0f    /* one film growth step per 4 min awake */
#define ALGAE_STEP_SLEEP_S  180.0f    /* (120 until 2026-09-23: a 7 h night filmed the
                                       * glass to the cap, so every morning looked the
                                       * same; now a night lands near ALGAE_DIRTY) */
#define ALGAE_COVER_CAP     0.30f     /* growth stops claiming new cells here */
#define WIPE_RADIUS      20.0f  /* squeegee half-width around the drag path */
#define WIPE_ENGAGE_PX   18.0f  /* stroke travel before a drag starts wiping
                                 * (a rolly fingertip tap stays under this) */
#define SLASH_PX         16.0f  /* sideways travel before a stroke is scissors (a
                                 * rolly fingertip tap stays under this; one
                                 * frond pitch is 12 px, so a flick takes 1-2) */
#define SLASH_RATIO      1.5f   /* ... and it must be this much more h than v */
#define SLASH_TOOL_PX    10.0f  /* the SCISSORS picked from the toolbox: the keeper
                                 * said "cut", so a shorter stroke, merely more
                                 * sideways than not, is enough */
#define SLASH_START_PX   10.0f  /* a slash must START this close to a FROND's tip
                                 * (upward) - not the bed's box: a cleaning scrub
                                 * begun mid-glass over a tall bed used to arm the
                                 * scissors (Strato, 2026-09-04) */
#define SLASH_START_SIDE_PX 16.0f /* ... and this close to a spine SIDEWAYS: the pad
                                 * of a fingertip is ~6 mm = 75 px on this glass, so
                                 * a landing a pitch beside a bed's outer frond has
                                 * that frond under the finger (a stroke begun at
                                 * the glass, heading in, used to arm nothing) */
#define SLASH_REACH_PX    8.0f  /* the fingertip's reach past its REPORTED point: a
                                 * stroke cuts the frond up to this far beyond where
                                 * it lands and where it lifts. The touch point is
                                 * the pad's centre; the outer frond of a bed stands
                                 * 24 px from the glass and the centre stops short of
                                 * it, so "the last blade never trips" (Strato,
                                 * 2026-09-14). Two thirds of the 12 px pitch: a flick
                                 * still takes only the fronds it visibly covers. */
#define SLASH_WALL_PX       48.0f /* a frond this close to a side wall is a WALL frond ... */
#define SLASH_WALL_SIDE_PX  96.0f /* ... and a stroke that starts on no frond of its bed,
                                 * but this close to it on its OPEN side (no higher
                                 * than its tip), is aimed at it: armed, and once it
                                 * is in among the wall fronds, heading for the wall,
                                 * it runs on into the wall. No finger can stand on a
                                 * frond 19 px from the glass, or come at it from the
                                 * wall: the pad meets the bezel and its center stops
                                 * 2-4 mm short. With its neighbors mown, a bed's
                                 * outer frond stood where no stroke could begin or
                                 * end (Strato's log, 2026-10-01: 73 tries at one,
                                 * starting 29..106 px from the wall, lifting at
                                 * 39..66). A stroke that starts ON a frond of that
                                 * bed keeps the precise reach above. */

static int popcount32u(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

/* canopy geometry: one source of truth for render, the trim hit test and the
 * in-canopy slowdown. Bed 0 is the reef bed (milestone lushness widens its
 * base, as ever); growth adds fronds (out, wider) and segments (up, toward
 * the surface). Height IS growth (2026-09-04): every bed's tallest frond
 * stands g of the way from the floor to the surface - at growth 1 it
 * touches the tank ceiling, whichever bed it is - so "85% grown" and "85%
 * of the tank's height" are the same thing for the comfort band below. At
 * VEG_NUB the fronds are ~5 segment green stubble. */
static int veg_segs(float h) { return 2 + (int)(h * (VEG_SEGS_FULL - 2)); }
/* frond pitch per bed: grass at 12 px; the sword plant's broad leaves at 14 */
static int veg_pitch(int b) { return b == 3 ? 14 : 12; }
/* beds 1 and 2 on this board's floor: the first frond's x and the base frond
 * count (the device header's DEVICE_BEDS; the reef bed follows the reef, the
 * sword plant its own place) */
static const struct { float x0; int n; } VEG_BED[2] = DEVICE_BEDS;
static void veg_bed_base(const tank_t *t, int b, float *bx0, int *n) {
    int lush = popcount32u(t->tank_ms_bits); if (lush > 4) lush = 4;
    int base_n;
    if (b == 0)      { *bx0 = t->reef_x - 24 - lush * 6; base_n = 5 + lush; }
    else if (b <= 2) { *bx0 = VEG_BED[b - 1].x0; base_n = VEG_BED[b - 1].n; }
    else             { *bx0 = tank_decor_x(t, 0) - PLANT_HALF_W; *n = 4; return; }   /* the sword plant
                                                              (2026-09-15): four broad leaves, by default on
                                                              the open floor between the reef bed's widest
                                                              reach (~199) and bed 2 (272) - the keeper's
                                                              to move (tank_decor_set) */
    /* the frond count is fixed per bed now (it used to widen with growth):
       with fronds cut one at a time, a bed's outer fronds can't be allowed
       to vanish because its MEAN height dropped */
    int nn = base_n + 6;
    if (b == 0) nn -= DEVICE_REEF_BED_LESS;   /* a short floor's reef bed gives up fronds */
    if (nn > VEG_FRONDS_MAX) nn = VEG_FRONDS_MAX;
    while (nn > 1 && *bx0 + nn * 12 > TANK_FX1 - 8) nn--;   /* beds stop at the glass */
    *n = nn;
}
int tank_veg_beds(const tank_t *t) { return (t->sd_unlocks & SD_ITEM_PLANT) ? VEG_BEDS_MAX : VEG_BEDS; }
veg_kind_t tank_veg_kind(const tank_t *t, int b) { (void)t; return b == 3 ? VEG_KIND_SWORD : VEG_KIND_GRASS; }
void tank_veg_bed(const tank_t *t, int b, float *x0, float *x1, float *top_y, int *fronds) {
    float bx0; int n;
    veg_bed_base(t, b, &bx0, &n);
    float hmax = 0;
    for (int i = 0; i < n; i++) if (t->veg_h[b][i] > hmax) hmax = t->veg_h[b][i];
    if (x0) *x0 = bx0 - 6;
    if (x1) *x1 = bx0 + n * veg_pitch(b) + 6;
    if (top_y) *top_y = TANK_BOT - 16 - veg_segs(hmax) * VEG_SEG_PX - 4;
    if (fronds) *fronds = n;
}
int tank_nursery_bed(const tank_t *t) {
    int best = -1;
    for (int b = 0; b < tank_veg_beds(t); b++)
        if (t->veg_growth[b] >= VEG_NURSERY && (best < 0 || t->veg_growth[b] > t->veg_growth[best])) best = b;
    return best;
}
/* where the pair circles: low inside the nursery bed, a flat loop that weaves
 * the fronds (body centre ~14 px off the floor line) */
void court_site(const tank_t *t, float *cx, float *cy, float *rx) {
    int b = tank_nursery_bed(t);
    if (b < 0) { *cx = t->reef_x + 26; *cy = TANK_BOT - 78; *rx = 26; return; }   /* legacy spot */
    float x0, x1; tank_veg_bed(t, b, &x0, &x1, NULL, NULL);
    *cx = (x0 + x1) * 0.5f; *cy = TANK_BOT - 16 - 14;
    float half = (x1 - x0) * 0.5f - 8; *rx = half < 16 ? 16 : half > 30 ? 30 : half;
}
void tank_court_puff(tank_t *t, int n) {
    float sx, sy, sr; court_site(t, &sx, &sy, &sr);
    for (int i = 0; i < MAX_BUBBLE && n > 0; i++) {
        if (t->bubble[i].column) continue;
        t->bubble[i].x = sx + tank_randf(t, -10, 10);
        t->bubble[i].y = sy - 6;
        n--;
    }
}
int tank_veg_frond(const tank_t *t, int b, int i, float *x) {
    float bx0; int n;
    veg_bed_base(t, b, &bx0, &n);
    if (x) *x = bx0 + i * veg_pitch(b);
    return veg_segs(t->veg_h[b][i]);
}
/* veg_growth[b] is the bed's mean frond height - the comfort band, the save
 * and the firmware log read it; recomputed after anything moves a frond */
static void veg_sync(tank_t *t) {
    for (int b = 0; b < VEG_BEDS_MAX; b++) {
        float bx0; int n; veg_bed_base(t, b, &bx0, &n);
        float sum = 0;
        for (int i = 0; i < n; i++) sum += t->veg_h[b][i];
        t->veg_growth[b] = sum / n;
    }
}
/* frond i of bed b grows toward its own ceiling: a hash of the slot spread
 * over VEG_CAP_LO..VEG_CAP_HI (a different hash than the fresh tank's start
 * profile, so a tall start does not mean a tall ceiling) */
float tank_veg_cap(int b, int i) {
    uint32_t h = (uint32_t)((b * 29 + i + 3) * 2246822519u);
    float cap = VEG_CAP_LO + (float)(h >> 8 & 1023) / 1023.0f * (VEG_CAP_HI - VEG_CAP_LO);
#if GLASS_CURVED                                           /* the bowl closes in over the outer fronds: a tip stops 12 px under the glass
                                                              (the reef bed taken at its widest, the sword plant wherever it stands);
                                                              the watch's upper corners do the same to the beds by its walls */
    if (b < 3) {
        float x = (b == 0 ? TANK_FX0 + TANK_FW * 0.15f - 48 : VEG_BED[b - 1].x0) + i * 12;
        float room = (TANK_BOT - 16 - tank_glass_top(x) - VEG_GLASS_GAP) / (VEG_SEGS_FULL * VEG_SEG_PX);
        if (cap > room) cap = room;
    }
#endif
    return cap;
}
/* a bed's mean ceiling: what "full" means for it (the smother band) */
float veg_cap_mean(const tank_t *t, int b) {
    float bx0; int n; veg_bed_base(t, b, &bx0, &n);
    float sum = 0;
    for (int i = 0; i < n; i++) sum += tank_veg_cap(b, i);
    return sum / n;
}
static void veg_grow(tank_t *t, float dg) {
    for (int b = 0; b < tank_veg_beds(t); b++) {
        float dgb = tank_veg_kind(t, b) == VEG_KIND_SWORD ? dg * VEG_SWORD_GROW : dg;
        for (int i = 0; i < VEG_FRONDS_MAX; i++) {
            float h = t->veg_h[b][i], cap = tank_veg_cap(b, i);
            if (h >= cap) continue;                   /* at (or staged above) its ceiling */
            float room = cap - h;                     /* the last stretch comes in slowly */
            float ease = room < VEG_CAP_TAPER ? fmaxf(0.3f, room / VEG_CAP_TAPER) : 1.0f;
            t->veg_h[b][i] = fminf(cap, h + dgb * ease);
        }
    }
    veg_sync(t);
}
void tank_veg_sync(tank_t *t) { veg_sync(t); }
void tank_veg_set(tank_t *t, int b, float g) {
    for (int i = 0; i < VEG_FRONDS_MAX; i++) t->veg_h[b][i] = g;
    veg_sync(t);
}

/* is (x,y) inside bed b's canopy? */
/* is (x,y) within `side` px of some frond of bed b sideways, and no more than
 * `up` px above its tip? The slash arms only from here. */
static bool veg_near_frond(const tank_t *t, int b, float x, float y, float side, float up) {
    float x0, x1; int n;
    tank_veg_bed(t, b, &x0, &x1, NULL, &n);
    if (x < x0 - side || x > x1 + side) return false;
    for (int i = 0; i < n; i++) {
        float fx; int segs = tank_veg_frond(t, b, i, &fx);
        float tip = TANK_BOT - 16 - segs * VEG_SEG_PX;
        if (fabsf(x - fx) <= side && y >= tip - up) return true;
    }
    return false;
}
/* does (x,y) lie on the OPEN side of one of bed b's wall fronds, within
 * SLASH_WALL_SIDE_PX of it and no more than `up` px above its tip? -1 = a
 * frond by the left wall, +1 = by the right, 0 = no. */
static int veg_wall_frond(const tank_t *t, int b, float x, float y, float up) {
    int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
    for (int i = 0; i < n; i++) {
        float fx; int segs = tank_veg_frond(t, b, i, &fx);
        if (y < TANK_BOT - 16 - segs * VEG_SEG_PX - up) continue;
        float d = x - fx;
        if (fx <= TANK_FX0 + SLASH_WALL_PX && d > 0 && d <= SLASH_WALL_SIDE_PX) return -1;
        if (fx >= TANK_FX1 - SLASH_WALL_PX && d < 0 && -d <= SLASH_WALL_SIDE_PX) return 1;
    }
    return 0;
}
bool veg_inside(const tank_t *t, int b, float x, float y) {
    float x0, x1, ty;
    tank_veg_bed(t, b, &x0, &x1, &ty, NULL);
    return x >= x0 && x <= x1 && y >= ty;
}
bool tank_in_grass(const tank_t *t, float x, float y) {
    for (int b = 0; b < tank_veg_beds(t); b++)
        if (tank_veg_kind(t, b) == VEG_KIND_GRASS && t->veg_growth[b] >= VEG_BARE && veg_inside(t, b, x, y)) return true;
    return false;
}
void tank_reef_spot(const tank_t *t, float *x, float *y) {
    if (!decor_reef(t, x, y)) { *x = t->reef_x; *y = t->reef_y; }   /* the reef cluster once there is one (DF_REEF) */
}

/* the scissors: cut every frond whose spine the stroke segment (x0,y0)-(x1,y1)
 * crosses, to the height where it crosses (only ever DOWN, never below nubs).
 * The segment reaches SLASH_REACH_PX past its far end - the finger is a pad,
 * not a point, and the frond just ahead of the reported point at lift is
 * under it (mid-stroke the next segment crosses that frond anyway, at the
 * same height); `landing` (the retroactive first segment of a stroke) reaches
 * the same distance back past its start, for the frond under the finger as
 * it touched down. A frond in the reach is cut at the height of that end.
 * Returns the number of fronds cut. */
static int veg_cut(tank_t *t, float x0, float y0, float x1, float y1, bool landing, int wall) {
    int cuts = 0;
    float lo = x0 < x1 ? x0 : x1, hi = x0 < x1 ? x1 : x0;
    if (x1 >= x0) hi += SLASH_REACH_PX; else lo -= SLASH_REACH_PX;
    if (landing) { if (x1 >= x0) lo -= SLASH_REACH_PX; else hi += SLASH_REACH_PX; }
    if (wall < 0 && x1 < x0 && x1 <= TANK_FX0 + SLASH_WALL_PX) lo = TANK_FX0;                  /* aimed at a wall frond (SLASH_WALL_SIDE_PX) */
    if (wall > 0 && x1 > x0 && x1 >= TANK_FX1 - SLASH_WALL_PX) hi = TANK_FX1;
    float dx = x1 - x0;
    for (int b = 0; b < tank_veg_beds(t); b++) {
        float bx0; int n; veg_bed_base(t, b, &bx0, &n);
        for (int i = 0; i < n; i++) {
            float fx = bx0 + i * veg_pitch(b);
            if (fx < lo || fx > hi) continue;
            float u = fabsf(dx) > 0.001f ? clampf((fx - x0) / dx, 0, 1) : 0;
            float cy = y0 + (y1 - y0) * u;
            float hf = (TANK_BOT - 16 - cy) / ((VEG_SEGS_FULL - 1) * VEG_SEG_PX);
            if (hf < VEG_NUB) hf = VEG_NUB;
            if (hf >= t->veg_h[b][i]) continue;    /* the stroke passed above the tip */
            t->trim_px += (t->veg_h[b][i] - hf) * (VEG_SEGS_FULL - 1) * VEG_PAY_PX;   /* the inches (sand dollars) */
            t->veg_h[b][i] = hf;
            cuts++;
            int puffs = 2;                         /* cut leaves drift up */
            for (int k = 0; k < MAX_BUBBLE && puffs > 0; k++) {
                if (t->bubble[k].column) continue;
                t->bubble[k].x = fx + tank_randf(t, -4, 4);
                t->bubble[k].y = cy - tank_randf(t, 0, 6);
                puffs--;
            }
        }
    }
    if (cuts) veg_sync(t);
    return cuts;
}

int tank_algae_cells(void) {
    static int n;
    if (!n) for (int i = 0; i < ALGAE_CELLS; i++) n += glass_holds_cell(i % ALGAE_COLS, i / ALGAE_COLS);
    return n;
}
float tank_algae_cover(const tank_t *t) {
    int covered = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) covered += t->algae[i] > 0;
    return (float)covered / tank_algae_cells();
}

/* one film step: thicken a covered cell, or claim a fresh one (preferring
 * cells next to existing film, then the glass edges - the way a real tank
 * fouls from the corners in) until the dapple cap is reached */
void tank_grow_algae(tank_t *t, int steps) {
    for (int s = 0; s < steps; s++) {
        int covered = 0;
        for (int i = 0; i < ALGAE_CELLS; i++) covered += t->algae[i] > 0;
        bool claim = covered < (int)(tank_algae_cells() * ALGAE_COVER_CAP);
        for (int try = 0; try < 8; try++) {
            int cx = (int)tank_randf(t, 0, ALGAE_COLS - 0.001f);
            int cy = (int)tank_randf(t, 0, ALGAE_ROWS - 0.001f);
            uint8_t *cell = &t->algae[cy * ALGAE_COLS + cx];
            if (!glass_holds_cell(cx, cy)) continue;
            if (*cell) { *cell = (uint8_t)(*cell > 210 ? 255 : *cell + 45); break; }
            if (!claim) continue;
            bool near = false;
            for (int dy = -1; dy <= 1 && !near; dy++)
                for (int dx = -1; dx <= 1 && !near; dx++) {
                    int nx = cx + dx, ny = cy + dy;
                    if (nx >= 0 && nx < ALGAE_COLS && ny >= 0 && ny < ALGAE_ROWS)
                        near = t->algae[ny * ALGAE_COLS + nx] > 0;
                }
            bool edge = cx == 0 || cy == 0 || cx == ALGAE_COLS - 1 || cy == ALGAE_ROWS - 1
                     || !glass_holds_cell(cx - 1, cy) || !glass_holds_cell(cx + 1, cy) || !glass_holds_cell(cx, cy - 1) || !glass_holds_cell(cx, cy + 1);
            float p = near ? 1.0f : edge ? 0.5f : 0.10f;
            if (tank_randf(t, 0, 1) < p) { *cell = 90; break; }
        }
    }
}

/* label the film's connected patches (8-neighbour): lab[i] = 1.. per cell
 * with film, 0 without; returns the patch count. A 28 x 23 grid, an
 * iterative flood fill - a few thousand steps, once per wiping frame. */
static int algae_label(const uint8_t *algae, uint8_t *lab) {
    int n = 0;
    int16_t stack[ALGAE_CELLS];
    for (int i = 0; i < ALGAE_CELLS; i++) lab[i] = 0;
    for (int i = 0; i < ALGAE_CELLS; i++) {
        if (!algae[i] || lab[i]) continue;
        if (n >= 255) break;                               /* the label is a byte; the cap is 30% cover anyway */
        n++; int sp = 0; stack[sp++] = (int16_t)i; lab[i] = (uint8_t)n;
        while (sp) {
            int c = stack[--sp], cx = c % ALGAE_COLS, cy = c / ALGAE_COLS;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = cx + dx, ny = cy + dy;
                    if (nx < 0 || nx >= ALGAE_COLS || ny < 0 || ny >= ALGAE_ROWS) continue;
                    int j = ny * ALGAE_COLS + nx;
                    if (algae[j] && !lab[j]) { lab[j] = (uint8_t)n; stack[sp++] = (int16_t)j; }
                }
        }
    }
    return n;
}
/* wipe the film in a WIPE_RADIUS band around the segment (x0,y0)-(x1,y1).
 * A patch whose last cell goes under the keeper's stroke is a COLONY removed
 * (tank_t.algae_colonies, the sand dollars' chore count, 2026-09-15). */
static void wipe_algae(tank_t *t, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    uint8_t lab[ALGAE_CELLS]; int patches = -1;             /* labelled lazily: only a stroke that clears something pays the fill */
    int wiped = 0;
    for (int cy = 0; cy < ALGAE_ROWS; cy++)
        for (int cx = 0; cx < ALGAE_COLS; cx++) {
            uint8_t *cell = &t->algae[cy * ALGAE_COLS + cx];
            if (!*cell) continue;
            float px = cx * ALGAE_CELL + ALGAE_CELL * 0.5f;
            float py = cy * ALGAE_CELL + ALGAE_CELL * 0.5f;
            float u = len2 > 1 ? clampf(((px - x0) * dx + (py - y0) * dy) / len2, 0, 1) : 0;
            if (tank_dist(px, py, x0 + dx * u, y0 + dy * u) < WIPE_RADIUS) {
                if (patches < 0) patches = algae_label(t->algae, lab);
                *cell = 0;
                t->cells_cleaned++; wiped++;
            }
        }
    if (wiped && patches > 0) {
        bool alive[256] = { false };
        for (int i = 0; i < ALGAE_CELLS; i++) if (t->algae[i] && lab[i]) alive[lab[i]] = true;
        for (int p = 1; p <= patches; p++) {
            bool touched = false;                          /* only a patch this stroke reached can have gone */
            for (int i = 0; i < ALGAE_CELLS && !touched; i++) touched = lab[i] == p && !t->algae[i];
            if (touched && !alive[p]) t->algae_colonies++;
        }
    }
}

void tank_plant_place(tank_t *t)  { decor_place(t, DK_PLANT); }
void tank_castle_place(tank_t *t) { decor_place(t, DK_CASTLE); }
void tank_wreck_place(tank_t *t)   { decor_place(t, DK_WRECK); }
void tank_coral_place(tank_t *t)   { decor_place(t, DK_CORAL); }
void tank_cluster_place(tank_t *t) { decor_place(t, DK_CLUSTER); }
uint32_t tank_coral_rgb(const tank_t *t) { uint32_t c = t->decor[DK_CORAL].rgb; return c ? c : CORAL_PAL[0]; }
float    tank_coral_growth(const tank_t *t) { float g = t->decor[DK_CORAL].growth; return g <= 0 ? CORAL_FULL : g > CORAL_FULL ? CORAL_FULL : g; }
float tank_cluster_growth(const tank_t *t) { float g = t->decor[DK_CLUSTER].growth; return g <= 0 ? CLUSTER_FULL : g > CLUSTER_FULL ? CLUSTER_FULL : g; }
int   tank_cluster_scheme(const tank_t *t) { return t->decor[DK_CLUSTER].variant < CLUSTER_SCHEME_N ? t->decor[DK_CLUSTER].variant : 0; }
void  tank_cluster_set_scheme(tank_t *t, int i) { t->decor[DK_CLUSTER].variant = (uint8_t)(i < 0 ? 0 : i >= CLUSTER_SCHEME_N ? CLUSTER_SCHEME_N - 1 : i); }
void     tank_coral_set_rgb(tank_t *t, uint32_t rgb) { t->decor[DK_CORAL].rgb = rgb & 0xffffff; }
/* the decor by its shop index (the placement page, the shop, the platforms' hold): see decor.h */
bool  tank_decor_placeable(int item) { int k = decor_kind_of_item(item); return k >= 0 && (DECOR[k].flags & DF_MOVABLE); }
float tank_decor_half_w(int item) { int k = decor_kind_of_item(item); return k >= 0 ? DECOR[k].half_w : 0; }
int   tank_decor_z_count(int item) { return decor_z_count(decor_kind_of_item(item)); }
int   tank_decor_z_at(int item, int i) { return decor_z_at(decor_kind_of_item(item), i); }
int   tank_decor_z_index(int item, int z) { return decor_z_index(decor_kind_of_item(item), z); }
float tank_decor_x(const tank_t *t, int item) { return decor_x(t, decor_kind_of_item(item)); }
int   tank_decor_z(const tank_t *t, int item) { return decor_z(t, decor_kind_of_item(item)); }
int   tank_decor_hit(const tank_t *t, float x, float y) { int k = decor_hit(t, x, y); return k >= 0 ? DECOR[k].shop_item : -1; }
void  tank_decor_reset(tank_t *t, int item) { decor_reset(t, decor_kind_of_item(item)); }
void  tank_decor_set(tank_t *t, int item, float x, int z) { if (tank_decor_placeable(item)) decor_set(t, decor_kind_of_item(item), x, z); }

void tank_touch_hold(tank_t *t, float x, float y) {
    tank_handled(t);
    if (t->tool != TOOL_HAND) return;            /* a tool in hand: the glass takes its strokes and nothing else (2026-10-04) */
    t->hold_active = true; t->hold_x = x; t->hold_y = y;
}

void tank_touch_drag(tank_t *t, float x, float y) {
    tank_handled(t);
    t->drag_active = true;
    if (!t->drag_has_prev) {
        /* stroke start: a slash must BEGIN on a plant - within SLASH_START_PX
         * of an actual frond (beside its spine, no higher than its tip). A
         * cleaning scrub that starts mid-glass, even straight above a tall
         * bed, and then works down through the grass arms nothing: it keeps
         * wiping algae and the fronds stand. */
        t->slash_armed = t->tool == TOOL_SCISSORS;   /* the scissors: any stroke (below) */
        for (int b = 0; b < tank_veg_beds(t) && !t->slash_armed; b++)
            t->slash_armed = veg_near_frond(t, b, x, y, SLASH_START_SIDE_PX, SLASH_START_PX);
        /* ... or beside a wall frond, on no frond of that frond's own bed
         * (SLASH_WALL_SIDE_PX; another bed's canopy under the finger does
         * not make the stroke any less aimed at the wall) */
        t->slash_wall = 0;
        for (int b = 0; b < tank_veg_beds(t) && !t->slash_wall; b++)
            if (!veg_near_frond(t, b, x, y, SLASH_START_SIDE_PX, SLASH_START_PX))
                t->slash_wall = (int8_t)veg_wall_frond(t, b, x, y, SLASH_START_PX);
        if (t->slash_wall) t->slash_armed = true;
        if (t->tool == TOOL_SPONGE) { t->slash_armed = false; t->slash_wall = 0; }   /* the sponge never cuts */
        t->slash_engaged = t->slash_cut = false; t->wipe_sounded = false;
        t->slash_x0 = x; t->slash_y0 = y; t->slash_h = t->slash_v = 0;
    }
    if (t->drag_has_prev) {
        float sdx = x - t->drag_px, sdy = y - t->drag_py;
        t->drag_dist += tank_dist(x, y, t->drag_px, t->drag_py);
        if (t->tool != TOOL_SCISSORS && t->drag_dist >= WIPE_ENGAGE_PX) {   /* a real stroke, not a tap; the scissors never wipe */
            wipe_algae(t, t->drag_px, t->drag_py, x, y);
            if (!t->wipe_sounded) { t->wipe_sounded = true; tank_emit(TEV_WIPE, -1); }
        }
        /* the slash (2026-09-04, per frond): an armed stroke becomes scissors
         * once it has travelled SLASH_PX sideways, mostly sideways - deliberate
         * work, so a tap or a missed poke at a fish never shears the garden.
         * From then on every sideways segment cuts the fronds it crosses at
         * the height it crosses them (the travel before engagement is cut
         * retroactively as one straight segment from the stroke start), so
         * a flick takes one or two fronds at the finger's height and a sweep
         * along the floor mows the bed to nubs. */
        if (t->slash_armed) {
            t->slash_h += fabsf(sdx); t->slash_v += fabsf(sdy);
            int cuts = 0;
            if (!t->slash_engaged) {
                if (t->tool == TOOL_SCISSORS ? t->slash_h >= SLASH_TOOL_PX && t->slash_h > t->slash_v
                                             : t->slash_h >= SLASH_PX && t->slash_h > SLASH_RATIO * t->slash_v) {
                    t->slash_engaged = true;
                    cuts += veg_cut(t, t->slash_x0, t->slash_y0, x, y, true, t->slash_wall);
                }
            } else if (fabsf(sdx) >= fabsf(sdy))   /* only the sideways segments cut */
                cuts += veg_cut(t, t->drag_px, t->drag_py, x, y, false, t->slash_wall);
            if (cuts) tank_emit(TEV_SNIP, -1);
            if (cuts && !t->slash_cut) { t->slash_cut = true; t->trims++; }
        }
    }
    t->drag_px = x; t->drag_py = y; t->drag_has_prev = true;
    t->tool_idle = 0;
}

void tank_set_tool(tank_t *t, int tool) {
    t->tool = (uint8_t)(tool == TOOL_SPONGE || tool == TOOL_SCISSORS ? tool : TOOL_HAND);
    t->tool_idle = 0;
}

void tank_feed(tank_t *t, float x, int n) {
    tank_handled(t);
    x = clampf(x, TANK_FX0 + 25, TANK_FX1 - 25);
    int dropped = 0;
    for (int i = 0; i < MAX_FOOD && n > 0; i++) {
        if (t->food[i].alive) continue;
        t->food[i].alive = true; t->food[i].from_player = true;
        t->food[i].x = clampf(x + tank_randf(t, -14, 14), TANK_FX0 + 20, TANK_FX1 - 20);
        t->food[i].y = tank_glass_top(t->food[i].x) + tank_randf(t, 6, 18);
        t->food[i].age = 0; t->food[i].floor_s = 0; t->food[i].nibbled = 0;
        n--; dropped++;
    }
    if (dropped) tank_emit(TEV_FEED, -1);        /* the plink is for pellets, not for the tap (a full tank drops none) */
    t->feed_spot_x = t->feed_spot_x < 0 ? x : t->feed_spot_x + (x - t->feed_spot_x) * 0.3f;
    t->feed_open = true;                         /* a meal once somebody eats from it */
}

/* the triple tap: fish near it bolt (stress, a little trust lost) and the
 * shrimp scatter - from the water or from the school alike */
static void tank_startle(tank_t *t, float x, float y) {
    tank_emit(TEV_SPOOK, -1);
    t->startled = true; t->startle_x = x; t->startle_y = y; t->startle_cooldown = STARTLE_COOLDOWN;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        if (tank_dist(f->x, f->y, x, y) < STARTLE_RADIUS) {
            f->stress = fminf(10, f->stress + 2.5f);
            f->trust = fmaxf(0, f->trust - 0.4f);
            sp_startle_hit(t, i, x, y);   /* a species' puff, ink, claws, flip */
        }
    }
    helper_startle_all(t, x, y, STARTLE_RADIUS);
}
int tank_shrimp_tap(tank_t *t, float x, float y) {
    tank_handled(t);
    if (t->school.tap_t > TAP_WINDOW) t->school.taps = 0;
    if (t->school.taps < 255) t->school.taps++;
    t->school.tap_t = 0;
    if (t->school.taps == 3) tank_startle(t, x, y);
    return t->school.taps;
}
void tank_touch_tap(tank_t *t, float x, float y) {
    tank_handled(t);
    if (t->tool != TOOL_HAND) return;            /* a tool in hand: no feed, no light, no startle - DONE first (2026-10-04) */
    if (y - tank_glass_top(x) < FEED_ZONE_Y) { tank_feed(t, x, 3); return; }     /* surface tap = feed, a portion of three; up to five portions fit (MAX_FOOD) */
    if (t->tap_burst_t > TAP_WINDOW) t->tap_count = 0;
    t->tap_count++; t->tap_burst_t = 0; t->tap_x = x; t->tap_y = y;
    tank_emit(TEV_TAP, -1);
    if (t->startled) {                              /* chasing: keep them spooked */
        t->startle_x = x; t->startle_y = y; t->startle_cooldown = STARTLE_COOLDOWN;
        for (int i = 0; i < t->n_fish; i++) {
            t->fish[i].stress = fminf(10, t->fish[i].stress + 0.6f);
            if (tank_dist(t->fish[i].x, t->fish[i].y, x, y) < STARTLE_RADIUS) sp_startle_hit(t, i, x, y);
        }
        helper_startle_all(t, x, y, STARTLE_RADIUS);
    } else if (t->tap_count >= 3) tank_startle(t, x, y);   /* aggressive: engage */
}

/* per-frame bookkeeping for the touch state machine */
static void touch_tick(tank_t *t, float dt) {
    t->tap_burst_t += dt;
    t->school.tap_t += dt;
    /* two taps then a pause: the light (MANUAL, the default; in settings'
       AUTO the idle rule owns it - 2026-09-15: the old always-on override
       rode in the save and froze a tank in permanent day) */
    if (!t->startled && t->tap_count == 2 && t->tap_burst_t > TAP_WINDOW) {
        if (!t->light_auto) {
            t->light_manual_off = !t->light_manual_off;
            if (t->light_manual_off) t->light_tip_seen = true;   /* the first time: the notice says what happened (2026-10-03) */
        }
        t->tap_count = 0;
    }
    if (t->tap_count >= 3 && t->tap_burst_t > TAP_WINDOW) t->tap_count = 0;
    if (t->startled) {
        t->startle_cooldown -= dt;
        if (t->startle_cooldown <= 0) { t->startled = false; t->tap_count = 0; }
    }
    if (t->hold_active) {                           /* calm presence earns trust */
        float was = t->hold_time;
        t->hold_time += dt;
        bool draw_begins = was < HOLD_ATTRACT_S && t->hold_time >= HOLD_ATTRACT_S;
        for (int i = 0; i < t->n_fish; i++) {
            fish_t *f = &t->fish[i];
            float d = tank_dist(f->x, f->y, t->hold_x, t->hold_y);
            if (d < HOLD_RADIUS) f->trust = fminf(10, f->trust + dt * 0.02f);
            /* a hold-approach = a fish that was out in the tank when the
             * settled hold began its draw and then came all the way in.
             * Per FISH (2026-09-15: it was gated on the per-hold flag below,
             * so only the first arrival - always the fastest, highest-trust
             * pair - could ever get it; the other two came in hold after
             * hold and were never credited), and only for a fish that
             * actually travelled (same day: a fish already sitting under the
             * finger used to be credited at 1.5 s without moving). The
             * tank's hold_approaches counter still counts the hold once. */
            if (draw_begins) f->hold_far = d >= HOLD_APPROACH_FROM;
            if (f->hold_far && d < HOLD_APPROACH_AT) {
                f->ms_bits |= MS_FIRST_HOLD_APPROACH;
                if (!t->hold_approached) { t->hold_approached = true; t->hold_approaches++; tank_emit(TEV_INVESTIGATE, i); }
            }
        }
    } else {
        t->hold_time = 0; t->hold_approached = false;
        for (int i = 0; i < t->n_fish; i++) t->fish[i].hold_far = false;
    }
    if (t->greet_timer > 0) t->greet_timer -= dt;
    /* courtship episodes: while progression says an arrival is close, the
     * pair circles the reef for a few seconds every minute or so - frequent
     * enough to notice across a couple of check-ins, rare enough to feel
     * like something glimpsed rather than an indicator */
    if (t->spawning && t->court_a >= 0 && t->court_b >= 0) {
        /* the spawning (2026-09-24): no episode clock - the pair circles in
         * the grass until the fry comes, lit or dark. The clock that counts
         * is the time BOTH spend in the fronds (a parent still swimming down,
         * or begging, holds it); the bubbles mark their arrival. */
        float sx, sy, sr; court_site(t, &sx, &sy, &sr);
        const fish_t *a = &t->fish[t->court_a], *b = &t->fish[t->court_b];
        /* (a big pair - the hammerheads' lanes, the eels - dances wider) */
        float near = sr + SPAWN_NEAR_PX + (a->species < SP_COUNT && SPECIES[a->species].big ? 90 : 0);
        if (tank_dist(a->x, a->y, sx, sy) < near && tank_dist(b->x, b->y, sx, sy) < near) {
            if (t->spawn_danced == 0) tank_court_puff(t, 2);
            t->spawn_danced += dt;
        }
        t->court_active = 0;
    } else if (t->courting && !t->night) {
        if (t->court_active > 0) t->court_active -= dt;
        else if ((t->court_cool -= dt) <= 0) {
            t->court_active = tank_randf(t, 6, 9);
            t->court_cool = tank_randf(t, 40, 90);
            tank_court_puff(t, 2);                  /* a flirt of bubbles, from the grass */
        }
    } else t->court_active = 0;
    /* a tool nobody has used for a while goes back in the box */
    if (t->tool && !t->drag_active && (t->tool_idle += dt) > TOOL_IDLE_S) t->tool = TOOL_HAND;
    /* a lifted finger ends the wipe/slash stroke (platform re-asserts while down) */
    if (!t->drag_active) {
        t->drag_has_prev = false; t->drag_dist = 0;
        t->slash_armed = t->slash_engaged = t->slash_cut = false;
        t->slash_h = t->slash_v = 0;
    }
    t->drag_active = false;
}

void tank_handled(tank_t *t) { t->idle_s = 0; }

void tank_toggle_light(tank_t *t) {
    /* the first toggle takes over from the idle rule AND flips the light
       (the on/off cue comes from tank_tick, where the flip lands) */
    if (!t->light_override) { t->light_override = true; t->light_on = t->night; }
    else t->light_on = !t->light_on;
}

void tank_light_auto(tank_t *t) { t->light_override = false; }

/* the worn tank's way up (tank.h) */
bool tank_screen_turned(const tank_t *t) { return DEVICE_WORN && t->screen_turned; }
void tank_screen_set(tank_t *t, bool turned) { t->screen_turned = DEVICE_WORN && turned; }
bool tank_orient(tank_t *t, bool live_inverted) {
    if (!t->orient_lock) t->orient_inv = live_inverted;
    return t->orient_inv;
}
void tank_orient_lock(tank_t *t, bool lock) { t->orient_lock = lock; }

const int LIGHT_IDLE_CHOICES[LIGHT_IDLE_N] = { 5, 15, 30, 60, 180, 300, 600, 1800 };
int tank_light_choice(const tank_t *t) {
    if (!t->light_auto) return 0;
    int best = 0;
    for (int i = 1; i < LIGHT_IDLE_N; i++)
        if (abs(LIGHT_IDLE_CHOICES[i] - t->light_idle_s) < abs(LIGHT_IDLE_CHOICES[best] - t->light_idle_s)) best = i;
    return best + 1;
}
void tank_light_choice_set(tank_t *t, int choice) {
    if (choice < 0) choice = 0;
    if (choice > LIGHT_IDLE_N) choice = LIGHT_IDLE_N;
    t->light_auto = choice > 0;
    if (choice > 0) t->light_idle_s = LIGHT_IDLE_CHOICES[choice - 1];
    t->light_manual_off = false;
}

void tank_scatter_food(tank_t *t, int n) {
    for (int i = 0; i < MAX_FOOD && n > 0; i++) {
        if (t->food[i].alive) continue;
        t->food[i].alive = true; t->food[i].from_player = false;
        t->food[i].x = tank_randf(t, TANK_FX0 + 25, TANK_FX1 - 25);
        t->food[i].y = tank_glass_top(t->food[i].x) + tank_randf(t, 6, 20);
        t->food[i].age = 0; t->food[i].floor_s = 0; t->food[i].nibbled = 0;
        n--;
    }
}

/* see tank.h: dark-screen physiology only, rates per real hour of sleep */
#define SLEEP_HUNGER_PER_H 0.8f    /* fed -> ravenous over ~7 h of sleep */
#define SLEEP_ENERGY_PER_H 2.0f
#define SLEEP_STRESS_PER_H 2.0f
#define SLEEP_SLICE_S      300.0f  /* the night's growth and grazing interleave at this grain */
void tank_tick_sleep(tank_t *t, float seconds) {
    if (seconds <= 0) return;
    float h = seconds / 3600.0f;
    t->clock += seconds;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        f->hunger = clampf(f->hunger + SLEEP_HUNGER_PER_H * h, 0, 10);
        f->energy = clampf(f->energy + SLEEP_ENERGY_PER_H * h, 0, 10);
        f->stress = clampf(f->stress - SLEEP_STRESS_PER_H * h, 0, 10);
        f->speed = 0; f->target_speed = 0; f->bored = 0;  /* a night's sleep is a fresh start */
        {                                                   /* ... a species' reflexes too: no puff, ink or flip to wake to (the classic fish has none) */
            f->surface_s=0; f->puff = 0; f->ink = 0; f->spark = 0; f->sp_mode = SPM_NONE; f->sp_t = 0;
            if (f->air_s <= 0) f->air_s = 60;
        }
        f->goal_age += seconds; f->ask_age += seconds;  /* wake re-asks the advisor at once */
    }
    t->idle_s = 0;                                      /* the wake press is handling: lights up */
    for (int i = 0; i < MAX_FOOD; i++)                  /* overnight pellets go stale */
        if (t->food[i].alive && (t->food[i].age += seconds) > 45) t->food[i].alive = false;
    decor_grow_all(t, seconds);
    helper_sleep_all(t, seconds);                       /* the shrimp's cooldown */
    /* the garden grows fastest in a dark, untended tank: waking to a taller
     * canopy and film on the glass is the morning chore. The night is lived
     * a SLEEP_SLICE_S at a time - the grass and the film grow, then the
     * snail and the urchin eat into what grew (2026-10-02: one pass for the
     * whole night let the snail eat only the film from before it, and the
     * night's growth all landed after) - the same whether the span comes in
     * one call (a boot after a power-off) or many (the naps). */
    for (float left = seconds; left > 0; left -= SLEEP_SLICE_S) {
        float dt = left < SLEEP_SLICE_S ? left : SLEEP_SLICE_S;
        veg_grow(t, dt / VEG_GROW_SLEEP_S);
        /* film steps go through the same accumulator the awake tick uses:
         * (int)(30 / 120) is 0 - the truncation that had quietly stopped every
         * bit of algae from forming overnight (2026-09-04). Nothing is banked
         * past the slice: the awake tick once paid a long sleep's overflow out
         * a step per frame, film refilling under the keeper's wipe (2026-09-29). */
        t->algae_acc += dt;
        int steps = (int)(t->algae_acc / ALGAE_STEP_SLEEP_S);
        t->algae_acc -= steps * ALGAE_STEP_SLEEP_S;
        tank_grow_algae(t, steps);
        helper_sleep_slice_all(t, dt);                      /* the snail and the urchin */
    }
}

void tank_shake(tank_t *t, float strength) {
    tank_handled(t);
    strength = clampf(strength, 0.3f, 1.5f);
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        float a = tank_randf(t, 0, 6.2831853f), v = tank_randf(t, 170, 300) * strength;
        f->toss = clampf(TOSS_S * (0.7f + 0.3f * strength) + tank_randf(t, -0.4f, 0.4f), 1.5f, 4.5f);
        f->toss_vx = cosf(a) * v; f->toss_vy = sinf(a) * v; f->toss_spin = tank_randf(t, -6, 6);
        f->stress = fminf(10, f->stress + 2.5f * strength); f->trust = fmaxf(0, f->trust - 0.3f);
        sp_startle_hit(t, i, f->x + 20, f->y);    /* a species' puff, ink, claws, flip */
    }
    helper_shake_all(t, strength);                          /* the shrimp scatter, the frogman and the sub tumble */
    tank_emit(TEV_SPOOK, -1);
}

/* Coarse state signature (the browser prototype's "blunt" gate): banded
 * drives plus bucketed sightings. Excludes clock bearings and exact drive
 * digits, which only steer - a change here is a reason to re-decide. */
static int band3(float v) { return v < 3.5f ? 0 : v < 7 ? 1 : 2; }
static uint32_t state_signature(const tank_t *t, int idx) {
    const fish_t *f = &t->fish[idx];
    /* 2026-09-11 (the battery pass): the device's LLM core was saturated by
     * this gate, not by the idle ceiling - measured in the sim at 25 fps,
     * 2 fish: food distance flipped 6.3x per fish-minute (every bucket a
     * sinking pellet crossed), the (since removed) roaming shadow's distance
     * 5.8x, the wall flag 4x, against 0.4 for hunger. So the twitchy fields
     * are now events:
     *  food   - only for a fish that could want it (not full): none / in the
     *           tank / within reach. A pellet appearing, or coming close, is a
     *           reason to re-decide; its every metre of sinking is not.
     *  wall   - dropped: it is steering, and the state line still says
     *           near/clear whenever a decision is made for another reason.
     * The model sees the exact distances in its state line either way; this
     * only decides WHEN it is asked. */
    float fd = 1e9f; int fi = tank_nearest_food(t, f, &fd);
    int food = (fi < 0 || band3(f->hunger) == 0) ? 0 : fd < 70 ? 2 : 1;
    return (uint32_t)band3(f->hunger) | (uint32_t)band3(f->energy) << 2 | (uint32_t)band3(f->stress) << 4
         | (uint32_t)food << 6
         | (uint32_t)t->night << 10 | (uint32_t)band3(f->curiosity) << 11
         | (uint32_t)band3(f->bored) << 13;      /* a fish going stale is asked again (2026-09-14) */
}

void tank_tick(tank_t *t, float dt, advisor_fn advise) {
    t->clock += dt;
    /* the light: on while the device is handled, off LIGHT_IDLE_S after the
     * last touch or movement (tank_handled) - a tank left on the desk goes
     * dark and the fish sleep. A setup page or a prompt holds it on; the
     * director's / sim's manual override wins over both. */
    t->idle_s += dt;
    bool was_night = t->night;
    t->night = t->light_override ? !t->light_on
             : t->hold_light ? false
             : !t->light_auto ? t->light_manual_off              /* MANUAL (default): the double-tap's state */
             : t->idle_s > (float)t->light_idle_s;                      /* AUTO: the idle rule */
    if (t->night != was_night) tank_emit(t->night ? TEV_LIGHT_OFF : TEV_LIGHT_ON, -1);

    touch_tick(t, dt);

    /* food sinks, settles, decays */
    int live_food = 0;
    for (int i = 0; i < MAX_FOOD; i++) {
        food_t *p = &t->food[i];
        if (!p->alive) continue;
        live_food++;
        p->age += dt;
        if (p->y < FOOD_FLOOR_Y) {
            p->y += 8 * dt;
            p->x += sinf(p->age * 1.5f) * dt * 2;
        } else p->floor_s += dt;                        /* resting on the floor (FOOD_FLOOR_S) */
        if (p->floor_s > 0 ? p->floor_s > FOOD_FLOOR_S : p->age > FOOD_LIFE_S) p->alive = false;
    }
    /* the tank's own trickle keeps fish alive when nobody is home (never
     * ruined by absence); the keeper's pellets are what progression counts.
     * Hunger-gated (see the hunger economy notes above): a pellet only when
     * somebody is really hungry, at a per-SECOND rate, so it's the same tank
     * at 25 fps and 60 fps and it never over-feeds a school that isn't asking.
     * While the tank is ravenous-begging it holds off, so the keeper's first
     * pellets are the event that ends the wait (progression re-arms it) -
     * but once the keeper HAS fed this episode it helps again: a quick fish
     * can gobble every pellet, and the slow one shouldn't beg out the whole
     * give-up valve for it. */
    float hungriest = 0;
    for (int i = 0; i < t->n_fish; i++)
        if (t->fish[i].hunger > hungriest) hungriest = t->fish[i].hunger;
    bool hold = (t->ravenous && !t->ravenous_fed) || t->trickle_off || t->autofeed_off;   /* (AUTO FEED off: the keeper's alone, 0.3.2) */
    if (!hold && live_food < 2 && hungriest >= TRICKLE_HUNGER &&
        tank_randf(t, 0, 1) < TRICKLE_RATE * dt)
        tank_scatter_food(t, 1);

    /* upkeep: the garden gets away from an idle keeper, slowly */
    veg_grow(t, dt / VEG_GROW_AWAKE_S);
    decor_tick(t, dt);                                      /* the coral's and the cluster's growth, by the minute */
    t->algae_acc += dt;
    if (t->algae_acc >= ALGAE_STEP_AWAKE_S) {
        t->algae_acc = 0;                                   /* one step per 4 min, never a backlog */
        tank_grow_algae(t, 1);
    }
    helper_tick_all(t, dt);                                 /* the snail, the urchin, the shrimp, the frogman, the sub (helper.c) */

    /* bubbles rise */
    for (int i = 0; i < MAX_BUBBLE; i++) {
        bubble_t *b = &t->bubble[i];
        b->wobble += dt * 2.5f;
        b->y -= b->vy * dt;
        b->x += sinf(b->wobble) * dt * (b->column ? 9 : 4);
        if (b->y < -6) {
            b->y = TANK_BOT + tank_randf(t, 4, 24);
            b->x = b->column ? tank_bubble_x(t) + tank_randf(t, -10, 10)
                             : tank_randf(t, TANK_FX0 + 12, TANK_FX1 - 12);
        }
    }

    /* advisor: polled every frame (async decisions land the moment they're
     * ready). A (re)decision is REQUESTED need-based, not on a fixed cadence:
     * the coarse signature changed and ADVISOR_MIN_INTERVAL passed, or the
     * idle ceiling hit, or something urgent (starving with food in view).
     * Effective cadence therefore scales with how much is
     * happening, not with how many fish live here. The start index rotates
     * so no slot is structurally favoured when several fish ask at once. */
    if (advise && t->n_fish > 0) {
        for (int k = 0; k < t->n_fish; k++) {
            int i = (t->ask_rr + k) % t->n_fish;
            fish_t *f = &t->fish[i];
            uint32_t sig = state_signature(t, i);
            /* prototype's urgent path: starving with food in view gets
             * asked NOW instead of waiting its turn */
            bool urgent = f->hunger > 8.0f && f->goal.id != GOAL_SEEK_FOOD &&
                          f->goal_age > 0.8f && tank_nearest_food(t, f, 0) >= 0;
            bool changed = sig != f->sig && f->ask_age > ADVISOR_MIN_INTERVAL;
            bool idle = f->ask_age > ADVISOR_IDLE_CEILING;
            bool want = changed || idle || (urgent && f->ask_age > 0.5f);
            goal_t g = advise(t, i, want);
            if (want) {            /* async advisors queue it; rules answer now */
                f->sig = sig; f->ask_age = 0; t->advisor_asks++;
                t->ask_rr = (i + 1) % t->n_fish;
            }
            if (g.id < GOAL_COUNT && g.id != f->goal.id) {
                /* a genuinely new pastime relieves boredom; bouncing back to
                 * the one just left (bubbles -> friend -> bubbles) does not */
                if (g.id != f->goal_prev) f->bored = clampf(f->bored - BORED_NEW_GOAL, 0, 10);
                f->goal_prev = f->goal.id;
                if (g.id == GOAL_EXPLORE) f->explore_set = false;   /* pick a fresh destination */
                if (f->species < SP_COUNT && (SPECIES[f->species].flags & SPF_HOME)) f->sp_x = f->x;        /* the ambusher's home is where it is now */
                f->goal = g;
                f->goal_age = 0;
                /* visible deliberation, scaled by how torn the advisor was */
                bool calm = g.id != GOAL_FLEE_SHADOW && g.urgency < 8 && g.confidence < 0.6f;
                f->hesitate = calm ? (0.6f - g.confidence) * 2.5f : 0;
            } else if (g.id == f->goal.id) {
                f->goal.urgency = g.urgency;
                f->goal.confidence = g.confidence; f->goal.runner_up = g.runner_up;
            }
        }
    }

    /* Starvation instrument (was a TEMPORARY survival-reflex override; retired
     * 2026-08-21 after the retrained model chose seek_food on 92% of starving
     * states with defensible misses - per Strato: the LLM owns decisions).
     * Counts moments a starving fish ignores available food for >4s. Pure
     * diagnostic, never changes a goal. */
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        if (f->hunger > 8.5f && f->goal.id != GOAL_SEEK_FOOD &&
            f->goal.id != GOAL_FLEE_SHADOW && f->goal_age > 4.0f &&
            !f->starve_flagged && tank_nearest_food(t, f, 0) >= 0) {
            f->starve_flagged = true;          /* count once per episode */
            tank_reflex_overrides++;
        }
        if (f->goal.id == GOAL_SEEK_FOOD || f->hunger < 8.0f) f->starve_flagged = false;
    }

    for (int i = 0; i < t->n_fish; i++) update_fish(t, i, dt);

    /* consume the hold AFTER the fish have seen it (the platform re-asserts
     * every frame the finger stays down). Clearing it at the top of the tick
     * was the old bug that kept the drift-to-finger reflex from ever firing. */
    t->hold_active = false;
}
