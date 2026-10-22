/* The decor table and the one path every piece takes (see decor.h). */
#include "tank.h"
#include <math.h>
#include <string.h>

/* ---- what a piece does when it arrives, when it grows, what stands on it ---- */
static void buy_floor(tank_t *t, decor_t *d) { (void)t; d->x = 0; d->z = DECOR_Z_FRONT; }   /* the default spot, in front of the grass */
static void buy_plant(tank_t *t, decor_t *d) { (void)d; tank_veg_set(t, 3, VEG_START); }    /* a young plant; it grows from here */
static void buy_coral(tank_t *t, decor_t *d) {
    buy_floor(t, d);                                        /* the colour stays the keeper's */
    d->growth = CORAL_START;                                /* young: it grows from here */
}
static void buy_cluster(tank_t *t, decor_t *d) {
    buy_floor(t, d);                                        /* the look stays the keeper's */
    d->growth = CLUSTER_START + 1e-4f;                      /* > 0: growing (0 in a save = full, tank_cluster_growth) */
}
static void sell_plant(tank_t *t, decor_t *d) { (void)d; tank_veg_set(t, 3, VEG_START); }
static void grow_coral(tank_t *t, decor_t *d, float seconds) {
    (void)t;
    if (d->growth <= 0 || d->growth >= CORAL_FULL) return;
    d->growth = fminf(CORAL_FULL, d->growth + seconds / CORAL_GROW_S);
}
static void grow_cluster(tank_t *t, decor_t *d, float seconds) {
    (void)t;
    if (d->growth <= 0 || d->growth >= CLUSTER_FULL) return;
    d->growth = fminf(CLUSTER_FULL, d->growth + seconds / CLUSTER_GROW_S);
}
/* an octopus's camouflage: the renderer's own colours, mirrored */
static uint32_t camo_castle(const tank_t *t, const decor_t *d)  { (void)t; (void)d; return 0xa99b7b; }   /* the stone, CASTLE_RGB CT_MID */
static uint32_t camo_coral(const tank_t *t, const decor_t *d)   { (void)d; return tank_coral_rgb(t); }
static uint32_t camo_cluster(const tank_t *t, const decor_t *d) { (void)d; return CLUSTER_SCHEMES[tank_cluster_scheme(t)].coral; }
/* the reef cluster's rock: a low dome of stones with the brain coral's hump on
 * top (render.c CL_ROCKS and CL_BRAIN: ~20 px of rock over 128 px, the brain
 * 16 more over its middle 36, at the cluster's size). The castle and the coral
 * fan are walls and a fan - a walker goes round their feet, on the floor. */
static float ground_cluster(const tank_t *t, const decor_t *d, float x, float g) {
    (void)d;
    float gr = tank_cluster_growth(t), s = gr >= 1 ? 1 : CLUSTER_SIZE_MIN + (1 - CLUSTER_SIZE_MIN) * gr;
    float dx = fabsf(x - decor_x(t, DK_CLUSTER)), hw = 64 * s, bw = 18 * s;
    if (dx < hw) { float u = dx / hw; g -= 20 * s * sqrtf(1 - u * u); }
    if (dx < bw) { float v = dx / bw; g -= 16 * s * (1 - v * v); }
    return g;
}

#define BACK_FRONT (DZ_BIT(DECOR_Z_BACK) | DZ_BIT(DECOR_Z_FRONT))
#define ALL_Z      (DZ_BIT(DECOR_Z_BACK) | DZ_BIT(DECOR_Z_MIDDLE) | DZ_BIT(DECOR_Z_FRONT))
#define SEAGRASS(b) { .name = "SEAGRASS", .shop_item = -1, .where = DPLACE_AUTO, \
                      .z_mask = DZ_BIT(DECOR_Z_MIDDLE), .z_default = DECOR_Z_MIDDLE, .veg_bed = (b) }
/* the floor pieces' tap boxes: the castle stands on TANK_BOT - 16 and is 146 tall;
   the others sit DECOR_SINK into the pebbles at TANK_BOT - 14 */
#define SUNK(h) (14 - DECOR_SINK + (h))
const decor_def_t DECOR[DK_COUNT] = {
    [DK_SEAGRASS0] = SEAGRASS(0), [DK_SEAGRASS1] = SEAGRASS(1), [DK_SEAGRASS2] = SEAGRASS(2),
    /* only the plant weaves AMONG the fish; the castle and both corals are
       BEHIND or IN FRONT (Strato, 2026-09-24: "remove the among option for
       corals, it doesn't really make as much sense") */
    [DK_PLANT]   = { .name = "SWORD PLANT", .shop_bit = SD_ITEM_PLANT, .shop_item = 0, .where = DPLACE_FLOOR,
                     .z_mask = ALL_Z, .z_default = DECOR_Z_MIDDLE, .flags = DF_SELLABLE | DF_MOVABLE, .veg_bed = 3,
                     .x_default = PLANT_X_DEFAULT, .half_w = PLANT_HALF_W, .hit_rank = 2,
                     .on_buy = buy_plant, .on_sell = sell_plant },
    [DK_CASTLE]  = { .name = "CASTLE", .shop_bit = SD_ITEM_CASTLE, .shop_item = 2, .where = DPLACE_FLOOR,
                     .z_mask = BACK_FRONT, .z_default = DECOR_Z_FRONT, .flags = DF_SELLABLE | DF_MOVABLE, .veg_bed = -1,
                     .x_default = CASTLE_X_DEFAULT, .half_w = CASTLE_HALF_W, .top_h = 16 + 146, .hit_rank = 3,
                     .den_rank = 2, .den_dx = CASTLE_HALF_W - 24, .camo_rank = 2,
                     .on_buy = buy_floor, .camo_rgb = camo_castle },
    [DK_CORAL]   = { .name = "CORAL", .shop_bit = SD_ITEM_CORAL, .shop_item = 3, .where = DPLACE_FLOOR,
                     .z_mask = BACK_FRONT, .z_default = DECOR_Z_FRONT, .flags = DF_SELLABLE | DF_MOVABLE, .veg_bed = -1,
                     .x_default = CORAL_X_DEFAULT, .half_w = CORAL_HALF_W, .top_h = SUNK(92), .hit_rank = 1,
                     .den_rank = 4, .den_dx = 14, .camo_rank = 3,
                     .on_buy = buy_coral, .grow = grow_coral, .camo_rgb = camo_coral },
    [DK_CLUSTER] = { .name = "REEF CLUSTER", .shop_bit = SD_ITEM_CLUSTER, .shop_item = 4, .where = DPLACE_FLOOR,
                     .z_mask = BACK_FRONT, .z_default = DECOR_Z_FRONT, .flags = DF_SELLABLE | DF_MOVABLE | DF_REEF, .veg_bed = -1,
                     .x_default = CLUSTER_X_DEFAULT, .half_w = CLUSTER_HALF_W, .top_h = SUNK(120),
                     .reef_h = 50,   /* it stands ~75 px off the floor (more with its bloom): the orbit, REEF_ORBIT_UP above this, rides round its crown */
                     .hit_rank = 4, .den_rank = 1, .den_dx = CLUSTER_HALF_W - 16, .camo_rank = 1,
                     .on_buy = buy_cluster, .grow = grow_cluster, .camo_rgb = camo_cluster, .ground = ground_cluster },
    [DK_WRECK]   = { .name = "WRECK", .shop_bit = SD_ITEM_WRECK, .shop_item = SD_ITEM_WRECK_IDX, .where = DPLACE_FLOOR,
                     .z_mask = BACK_FRONT, .z_default = DECOR_Z_FRONT, .flags = DF_SELLABLE | DF_MOVABLE, .veg_bed = -1,
                     .x_default = WRECK_X_DEFAULT, .half_w = WRECK_HALF_W, .top_h = SUNK(84), .hit_rank = 5,
                     .den_rank = 3, .den_dx = WRECK_HALF_W - 20,   /* under the hull's holes */
                     .on_buy = buy_floor },
    [DK_AIRSTONE] = { .name = "AIRSTONE", .shop_item = -1, .where = DPLACE_AUTO,
                      .z_mask = DZ_BIT(DECOR_Z_BACK), .z_default = DECOR_Z_BACK, .veg_bed = -1,
                      .x_default = BUBBLE_X_DEFAULT },
};

int decor_kind_of_item(int item) {
    if (item < 0) return -1;
    for (int k = 0; k < DK_COUNT; k++) if (DECOR[k].shop_item == item) return k;
    return -1;
}
bool decor_owned(const tank_t *t, int k) {
    if (k < 0 || k >= DK_COUNT) return false;
    return !DECOR[k].shop_bit || (t->sd_unlocks & DECOR[k].shop_bit);
}
float decor_x(const tank_t *t, int k) {
    if (k < 0 || k >= DK_COUNT) return 0;
    if (t->decor[k].x > 0) return t->decor[k].x;
    if (DECOR[k].x_default > 0 || DECOR[k].veg_bed < 0) return DECOR[k].x_default;
    float x0, x1; tank_veg_bed(t, DECOR[k].veg_bed, &x0, &x1, NULL, NULL);   /* a bed setup lays out: its fronds' middle */
    return (x0 + x1) * 0.5f;
}
int decor_z(const tank_t *t, int k) { return k < 0 || k >= DK_COUNT ? DECOR_Z_MIDDLE : t->decor[k].z; }

/* the depths a piece offers, in BACK..FRONT order: the placement bar's segments */
static unsigned z_mask(int k) { return k < 0 || k >= DK_COUNT ? BACK_FRONT : DECOR[k].z_mask; }
int decor_z_count(int k) {
    int n = 0;
    for (int z = 0; z < DECOR_Z_N; z++) n += (z_mask(k) >> z) & 1;
    return n;
}
int decor_z_at(int k, int i) {
    int n = decor_z_count(k), last = DECOR_Z_FRONT;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    for (int z = 0; z < DECOR_Z_N; z++)
        if ((z_mask(k) >> z) & 1) { if (i-- == 0) return z; last = z; }
    return last;
}
int decor_z_index(int k, int z) {
    int i = 0;
    for (int b = 0; b < z && b < DECOR_Z_N; b++) i += (z_mask(k) >> b) & 1;
    return i < decor_z_count(k) ? i : decor_z_count(k) - 1;
}
/* a depth the piece does not offer goes to the nearest it does, ties in front
   (an AMONG asked of the castle is IN FRONT) */
static int z_snap(int k, int z) {
    if (z < 0) z = 0;
    if (z >= DECOR_Z_N) z = DECOR_Z_N - 1;
    unsigned m = z_mask(k);
    if ((m >> z) & 1) return z;
    for (int d = 1; d < DECOR_Z_N; d++) {
        if (z + d < DECOR_Z_N && ((m >> (z + d)) & 1)) return z + d;
        if (z - d >= 0 && ((m >> (z - d)) & 1)) return z - d;
    }
    return DECOR[k].z_default;
}
void decor_set(tank_t *t, int k, float x, int z) {
    if (k < 0 || k >= DK_COUNT) return;
    const decor_def_t *d = &DECOR[k];
    if (d->where == DPLACE_FLOOR) {
        float lo = TANK_FX0 + DECOR_MARGIN + d->half_w, hi = TANK_FX1 - DECOR_MARGIN - d->half_w;
        if (x < lo) x = lo;
        if (x > hi) x = hi;
        t->decor[k].x = x;
    }
    t->decor[k].z = (uint8_t)z_snap(k, z);
}
float decor_top(const tank_t *t, int k) {
    if (DECOR[k].top_h <= 0 && DECOR[k].veg_bed >= 0) { float top; tank_veg_bed(t, DECOR[k].veg_bed, NULL, NULL, &top, NULL); return top; }
    return TANK_BOT - DECOR[k].top_h;
}
int decor_hit(const tank_t *t, float x, float y) {
    for (int rank = 1; rank <= DK_COUNT; rank++)            /* the smaller pieces win a tie */
        for (int k = 0; k < DK_COUNT; k++) {
            if (DECOR[k].hit_rank != rank || !(DECOR[k].flags & DF_MOVABLE) || !decor_owned(t, k)) continue;
            float cx = decor_x(t, k), half = DECOR[k].half_w + 8;
            if (x >= cx - half && x <= cx + half && y >= decor_top(t, k) - 8 && y <= TANK_BOT) return k;
        }
    return -1;
}
static void factory(tank_t *t, int k) {
    memset(&t->decor[k], 0, sizeof t->decor[k]);
    t->decor[k].z = DECOR[k].z_default;
}
void decor_init(tank_t *t) { for (int k = 0; k < DK_COUNT; k++) factory(t, k); }
void decor_place(tank_t *t, int k) {
    if (k >= 0 && k < DK_COUNT && DECOR[k].on_buy) DECOR[k].on_buy(t, &t->decor[k]);
}
void decor_reset(tank_t *t, int k) {
    if (k < 0 || k >= DK_COUNT) return;
    factory(t, k);
    if (DECOR[k].on_sell) DECOR[k].on_sell(t, &t->decor[k]);
}
void decor_tick(tank_t *t, float dt) {
    for (int k = 0; k < DK_COUNT; k++) {
        if (!DECOR[k].grow) continue;
        decor_t *d = &t->decor[k];
        d->acc += dt;                                       /* by the minute: a frame's 1/60 s over 30 days is under float precision next to the growth */
        if (d->acc >= 60) { if (decor_owned(t, k)) DECOR[k].grow(t, d, d->acc); d->acc = 0; }
    }
}
void decor_grow_all(tank_t *t, float seconds) {
    for (int k = 0; k < DK_COUNT; k++)
        if (DECOR[k].grow && decor_owned(t, k)) DECOR[k].grow(t, &t->decor[k], seconds);
}

bool decor_den_x(const tank_t *t, float side, float *x) {
    for (int rank = 1; rank <= DK_COUNT; rank++)
        for (int k = 0; k < DK_COUNT; k++)
            if (DECOR[k].den_rank == rank && decor_owned(t, k)) { *x = decor_x(t, k) + side * DECOR[k].den_dx; return true; }
    return false;
}
bool decor_camo(const tank_t *t, float x, uint32_t *rgb) {
    for (int rank = 1; rank <= DK_COUNT; rank++)
        for (int k = 0; k < DK_COUNT; k++)
            if (DECOR[k].camo_rank == rank && decor_owned(t, k) && fabsf(x - decor_x(t, k)) < DECOR[k].half_w) {
                *rgb = DECOR[k].camo_rgb(t, &t->decor[k]); return true;
            }
    return false;
}
float decor_ground(const tank_t *t, float x, float g) {
    for (int k = 0; k < DK_COUNT; k++)
        if (DECOR[k].ground && decor_owned(t, k)) g = DECOR[k].ground(t, &t->decor[k], x, g);
    return g;
}
bool decor_reef(const tank_t *t, float *x, float *y) {
    for (int k = 0; k < DK_COUNT; k++)
        if ((DECOR[k].flags & DF_REEF) && decor_owned(t, k)) { *x = decor_x(t, k); *y = TANK_BOT - DECOR[k].reef_h; return true; }
    return false;
}
