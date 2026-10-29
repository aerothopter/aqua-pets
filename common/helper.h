/* The helpers (2026-10-22): what moves by its own rules - the snail on the
 * glass, the urchin in the grass, the shrimp school, the frogman and the sub.
 * Each is a row of HELPER[] and its own state in tank_t (t->snail, t->urchin,
 * t->school, t->frogman, t->sub). A helper is bought as an upgrade; it arrives
 * and goes about its rules. It is never placed and never bred, and never one
 * of the creatures: t->fish[] is the model's population (and the encoder's
 * friend set), and no helper joins it. What stays put is decor (decor.h).
 *
 * tank.h includes this file ahead of tank_t for the state types; the table and
 * the functions take the tank by its tag. */
#ifndef HELPER_H
#define HELPER_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {                     /* order fixed: the ticks and the save's records run in it
                                    * (the tank's RNG is drawn in this order) */
    HK_SNAIL, HK_URCHIN, HK_SHRIMP, HK_FROGMAN, HK_SUB,
    HK_COUNT } helper_kind_t;

enum { HAB_GLASS, HAB_FLOOR, HAB_WATER };   /* where its rules keep it: the drifters in the water share it */

/* the shrimp school (2026-09-29, the shop's sixth item, SD_ITEM_SHRIMP): cherry
 * shrimp that swim as a loose school with a pull toward the grass, swarm the
 * pellets that sink below the grass's top (falling or on the floor), drift up
 * to the canopy now and then, and scatter when the keeper taps near them.
 * Bought as SHRIMP_START grown adults; every SHRIMP_PER_JOIN pellets the school
 * eats brings one more, up to SHRIMP_MAX, never two within SHRIMP_COOLDOWN_S
 * (the count stops at SHRIMP_PER_JOIN while it waits - no burst). Over
 * SHRIMP_REFUSE_COVER of the glass fouled they refuse food: they stay in the
 * grass and turn away from pellets, so the school stops growing (never shrinks).
 * Strato's words throughout (2026-09-29): "they should just swarm pellets that
 * hit the ground", "a user cant spam this", "when there is too much algae,
 * shrimp will refuse to eat". Drawn procedurally (render.c draw_shrimp). */
#define SHRIMP_START        4
#define SHRIMP_MAX          10
#define SHRIMP_PER_JOIN     10
#define SHRIMP_COOLDOWN_S   (15.0f * 60.0f)
#define SHRIMP_REFUSE_COVER 0.5f
#define SHRIMP_PECK_S       8.0f     /* shrimp-seconds of pecking to finish a pellet: a huddle of
                                      * 3-4 takes 2-3 s - long enough to see them swarm it */
typedef struct {
    float  x, y, vx, vy;             /* centre of the body; px/s */
    float  yaw;                      /* -1..1 as the fish's: the turn foreshortens it */
    float  behind;                   /* s its way has been behind it (a committed turn, as the fish) */
    float  ph;                       /* animation phase: legs, the peck, the bob */
    float  dart;                     /* s left of a tail-flick escape */
    int8_t facing;
    uint8_t pecking;                 /* at a floor pellet this frame */
} shrimp_t;

/* the snail (SD_ITEM_SNAIL): crawls the glass toward the nearest film and
 * grazes it. Its position is saved; heading / target are not. */
typedef struct {
    float    x, y, heading;
    int16_t  cell;                 /* the algae cell it is heading for, -1 = wandering */
    float    graze;                /* seconds on the current cell */
    bool     front;                /* walking the floor, it passes IN FRONT of a reef cluster or coral placed
                                    * IN FRONT (0.3.2, Strato: "it marches to the beat of its own drum" - the
                                    * big cluster hid it for most of its walk). Its own choice, made again at
                                    * each end of the floor; off the glass it lands in front, where it was.
                                    * Not saved. */
    int32_t  grazed;               /* algae cells it has grazed clean, lifetime (its card,
                                    * 2026-09-16; saved) */
    float    sleep_acc;            /* the night shift's part-cells: a nap's few minutes still count
                                    * (not saved; a boot lives the whole absence in one call) */
} snail_t;
/* the urchin (SD_ITEM_URCHIN, 2026-10-02): grazes the tall GRASS down on
 * the floor, as the snail grazes the glass. Its x is saved (the floor is
 * its y); the frond it is after, its appetite and its chewing are not. */
typedef struct {
    float    x;
    int8_t   bed, frond;           /* the frond it is after, -1 = none: it rests or ambles */
    float    appetite;             /* frond height it is due to eat (URCHIN_* in helper.c) */
    float    chew;                 /* seconds at the frond's foot so far */
    float    to;                   /* an amble's destination x, < 0 = resting */
    float    rest;                 /* seconds left of the rest */
    float    grazed_px;            /* grass it has eaten, lifetime, in the trim's px (VEG_PAY_PX:
                                    * the same length on every board; its card shows cm). Saved */
} urchin_t;
/* the shrimp school (see SHRIMP_*): how many (saved), the pellets eaten toward
 * the next one (0..SHRIMP_PER_JOIN, saved), the seconds before another may join
 * (saved), each shrimp's motion (not saved: a load places them in the grass),
 * and the school's current goal and its timer. */
typedef struct {
    uint8_t  n;
    uint8_t  food;
    float    cool;
    int32_t  eaten;                /* pellets the school has eaten, lifetime (its card; saved) */
    uint8_t  taps;                 /* quick taps on the school in a row (the third scares them; not saved) */
    float    tap_t;                /* seconds since the last one */
    shrimp_t q[SHRIMP_MAX];
    float    tx, ty, tt;
} school_t;
/* a drifter in open water, glass to glass: the frogman (2026-10-10, item 8; he
 * hung from the wreck's stern before), a little diver who floats SIDEWAYS,
 * turning at each end, easing to a new lane now and then, waving at times; and
 * the submarine (2026-10-10 night, item 9), which cruises the same way,
 * propeller turning, bubbles streaming from the stern - and every
 * SUB_CRUISE_LO..HI s it stops, a periscope rises, scans, and sinks before it
 * goes on. The suit and the hull are the theme's (render.c). Only their bits
 * are saved: a load puts them mid-tank. */
typedef struct {
    float    x, y;
    float    yaw;                  /* -1 facing left .. 1 facing right; thin through a turn */
    int8_t   dir;                  /* the way it is going */
    float    lane;                 /* the y it eases toward */
    float    lane_t;               /* seconds until it picks another lane */
    /* the sub's cruise */
    float    v;                    /* px/s along its heading: eases to SUB_SPEED, or to 0 for a stop */
    float    stop;                 /* seconds left of a stop (0 = cruising) */
    float    next;                 /* seconds until the next stop */
    float    peri;                 /* the periscope: 0 down .. 1 up */
    float    look;                 /* seconds the periscope has been up (it scans on this) */
} drift_t;

struct tank;
typedef struct tank tank_t;

/* what a helper keeps across a boot (progression.c holds it in the save, a
 * record a kind); each row says what its fields mean */
typedef struct {
    uint8_t  n, m;
    float    x, y, amount;
    int32_t  count;
} helper_rec_t;

typedef struct {
    const char *name;
    uint32_t shop_bit;             /* SD_ITEM_* */
    int8_t   shop_item;            /* its shop index */
    uint8_t  habitat;              /* HAB_* */
    void     (*init)(tank_t *t);                   /* its factory state: tank_init, and a sale (helper_leave) */
    void     (*arrive)(tank_t *t);                 /* bought (or loaded with nothing saved): it appears and starts its rules */
    void     (*tick)(tank_t *t, float dt);         /* awake: its rules */
    void     (*sleep)(tank_t *t, float seconds);   /* asleep: the whole span at once, before the night is lived */
    void     (*sleep_slice)(tank_t *t, float seconds);   /* asleep: a slice at a time, after the slice's grass and film grow */
    void     (*startle)(tank_t *t, float x, float y, float radius);   /* a startle at (x, y) */
    bool     (*hit)(const tank_t *t, float x, float y);   /* a tap on it opens its card; NULL = a tap goes through to the water */
    int      (*tap)(tank_t *t, float x, float y);  /* a tap on it: 1 = its card up or down, 0 = nothing, -1 = the card down; NULL = 1 */
    bool     (*body)(const tank_t *t, int i, float *x, float *y);   /* its i-th body's centre (the school's shrimp, one by one); false past the last */
    drift_t *(*drift)(tank_t *t);                  /* HAB_WATER: its drift */
    void     (*save)(const tank_t *t, helper_rec_t *r);
    void     (*load)(tank_t *t, const helper_rec_t *r);   /* called for every kind with a save hook, owned or not;
                                                            * a kind without one arrives when it is owned */
} helper_def_t;
extern const helper_def_t HELPER[HK_COUNT];

int   helper_kind_of_item(int item);           /* a shop index -> its kind, -1 = not a helper */
bool  helper_owned(const tank_t *t, int k);
int   helper_hit(const tank_t *t, float x, float y);   /* the owned helper a tap on (x, y) reaches, in kind order; -1 = none */
void  helper_init(tank_t *t);                  /* tank_init */
void  helper_arrive(tank_t *t, int k);         /* bought */
void  helper_leave(tank_t *t, int k);          /* sold: it leaves the tank, back at its factory state (init) - a rebuy starts fresh */
void  helper_tick_all(tank_t *t, float dt);
void  helper_sleep_all(tank_t *t, float seconds);
void  helper_sleep_slice_all(tank_t *t, float seconds);
void  helper_startle_all(tank_t *t, float x, float y, float radius);

/* ---- the snail ----
 * It has two poses (Strato's sprites, 2026-09-15): UPRIGHT, walking the tank
 * floor (nothing to graze: it comes down and ambles along the bottom, turning
 * at the ends), and flat ON THE GLASS (crawling to film and grazing it, its
 * underside to the viewer). snail.y is the sprite's centre; on the floor it is
 * SNAIL_FLOOR_Y, the sole (6 px under the centre) on the line the grass is
 * rooted at, TANK_BOT - 14: at - 24 it stood 4 px above the frond roots, and
 * the roots of the fronds BEHIND it showed under its foot - a snail afloat
 * (Strato, 2026-10-02). */
#define SNAIL_FLOOR_Y (TANK_BOT - 20.0f)
void  tank_snail_place(tank_t *t);             /* bought: on the floor, bottom left */
bool  tank_snail_upright(const tank_t *t);
/* a tap on the snail (its card, 2026-09-16): placed, and within a fingertip
 * of the sprite's centre. Platforms test the fish first. */
bool  tank_snail_hit(const tank_t *t, float x, float y);

/* ---- the urchin (2026-10-02, the episode 5 promise: "an urchin that keeps the
 * grass trimmed") ----
 * A rule-based grazer on the floor, drawn by code (render.c draw_urchin, from
 * Strato's sea-urchin-v1). Awake it gets hungry slowly, crawls to the grass
 * frond standing tallest over URCHIN_KEEP and chews it down a bite, then rests
 * or ambles the sand. Asleep it works the night shift: a share of every grass
 * frond's height over URCHIN_KEEP, so a morning's grass is lower than it would
 * be - never below the keep line, and never the sword plant. Like the snail it
 * makes the chores lighter, never gone, and its eating never counts as the
 * keeper's trimming. Its centre is URCHIN_FLOOR_Y, its base on the root line. */
#define URCHIN_FLOOR_Y (TANK_BOT - 21.0f)
#define URCHIN_KEEP     0.40f      /* it never takes a frond below this (a fresh tank's cover is 0.35) */
void  tank_urchin_place(tank_t *t);
bool  tank_urchin_hit(const tank_t *t, float x, float y);
bool  tank_urchin_chewing(const tank_t *t);   /* at a frond's foot, eating (render: the spines work) */

/* ---- the shrimp ----
 * The bought school (SHRIMP_START in the grass), or n of them (a load, the
 * sim's key); refusing = the glass is too fouled for them to eat. A tap on the
 * school (2026-09-29, Strato: "one tap should bring up a card ... it should be
 * a triple tap which scares them (same as for fish)"). Hit: within a fingertip
 * of any shrimp (the platforms test the fish and the snail first). Tap: counts
 * quick taps on the school - its own count, so a double tap there never
 * toggles the light - and returns the count: 1 = toggle its card, 2 = nothing,
 * 3+ = the startle, exactly the water's triple tap (the fish spooked, the
 * shrimp scatter): the platform drops the card. */
void  tank_shrimp_place(tank_t *t, int n);
bool  tank_shrimp_refusing(const tank_t *t);
bool  tank_shrimp_hit(const tank_t *t, float x, float y);
int   tank_shrimp_tap(tank_t *t, float x, float y);

/* ---- the drifters ---- */
void  tank_frogman_place(tank_t *t);         /* the frogman bought (or loaded): mid-tank, heading right (item 8) */
void  tank_sub_place(tank_t *t);             /* the submarine bought (or loaded): mid-tank, heading left (item 9) */
bool  tank_frogman_hit(const tank_t *t, float x, float y);   /* within a fingertip of him */
bool  tank_sub_hit(const tank_t *t, float x, float y);       /* within a fingertip of the sub */
#define FROG_SPEED     9.0f                  /* px/s across the tank */
#define FROG_MARGIN    (DECOR_MARGIN + 24)   /* he turns this far from the glass */
#define FROG_LANE_LO   (TANK_H * 0.07f)      /* the water he drifts in: all of it (2026-10-10 night, Alvin: "float freely */
#define FROG_LANE_HI   (TANK_BOT - 46)       /*   anywhere in the tank"; a mid-water band before), the sand and the surface kept */
#define SUB_SPEED      13.0f                 /* px/s, cruising */
#define SUB_MARGIN     (DECOR_MARGIN + 30)
#define SUB_LANE_LO    (TANK_H * 0.07f)      /* the whole water column, like the frogman's (2026-10-10 night) */
#define SUB_LANE_HI    (TANK_BOT - 50)
#define SUB_STOP_S     8.0f                  /* a stop: the periscope up for the middle six seconds */
#define SUB_CRUISE_LO  22.0f                 /* seconds of cruising between stops */
#define SUB_CRUISE_HI  40.0f

#endif
