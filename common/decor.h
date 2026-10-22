/* The decor (2026-10-21): every static piece the tank holds - the free seagrass
 * beds and the airstone setup places, and the shop's sword plant, castle,
 * coral, reef cluster and wreck - is a row of DECOR[] and an instance in
 * tank_t.decor[]. Decor never moves: it may grow and sway in place, the keeper
 * may place it and pick its depth. What moves by its own rules (the snail, the
 * frogman, ...) is a helper; what the model decides for is a creature.
 *
 * A depth is BACK (all of it behind the creatures), FRONT (all of it over
 * them) or MIDDLE: half of it behind and half in front, each piece saying
 * which half is which (the grass's alternate stalks). A piece offers only
 * the depths in its z_mask.
 *
 * tank.h includes this file ahead of tank_t for the instance type; the table
 * and the functions take the tank by its tag. */
#ifndef DECOR_H
#define DECOR_H
#include <stdbool.h>
#include <stdint.h>

enum { DECOR_Z_BACK = 0, DECOR_Z_MIDDLE = 1, DECOR_Z_FRONT = 2, DECOR_Z_N = 3 };
#define DZ_BIT(z) (1u << (z))

typedef enum {                     /* order fixed: it indexes tank_t.decor[] */
    DK_SEAGRASS0, DK_SEAGRASS1, DK_SEAGRASS2,   /* vegetation beds 0..2: the reef bed and the two decor beds */
    DK_PLANT,                                   /* bed 3, the sword plant */
    DK_CASTLE, DK_CORAL, DK_CLUSTER, DK_WRECK,
    DK_AIRSTONE,                                /* the bubble column's stones */
    DK_COUNT } decor_kind_t;

enum { DPLACE_FLOOR, DPLACE_AUTO };   /* the keeper drags it along the floor / setup puts it where it goes */
enum { DF_SELLABLE = 1, DF_MOVABLE = 2, DF_REEF = 4 };

/* one piece in the tank. x <= 0 = its default spot (DECOR[].x_default) */
typedef struct {
    float    x;
    uint8_t  z;                    /* DECOR_Z_* */
    uint8_t  variant;              /* the reef cluster's look (CLUSTER_SCHEMES) */
    uint32_t rgb;                  /* the coral's colour, 0xRRGGBB (0 = its default) */
    float    growth;               /* the coral's / the cluster's (0 = full: see their accessors) */
    float    acc;                  /* awake seconds not yet grown: it lands by the minute */
} decor_t;

struct tank;
typedef struct tank tank_t;

typedef struct {
    const char *name;
    uint32_t shop_bit;             /* SD_ITEM_*; 0 = the tank's own */
    int8_t   shop_item;            /* its shop index, -1 = not sold */
    uint8_t  where;                /* DPLACE_* */
    uint8_t  z_mask, z_default;    /* the depths it offers; the one it arrives at */
    uint16_t flags;                /* DF_* */
    int8_t   veg_bed;              /* the vegetation bed whose fronds it is, -1 = none */
    float    x_default;            /* its centre when unplaced */
    float    half_w;               /* centre to the footprint's edge: the clamp, the tap box */
    float    top_h;                /* its tap box's height above TANK_BOT (0 = its bed's canopy) */
    float    reef_h;               /* DF_REEF: the reef spot's height above TANK_BOT */
    int8_t   hit_rank;             /* who wins a tap where pieces overlap: 1 first, 0 = never tapped */
    int8_t   den_rank;             /* where a den is dug: the first owned in rank order (0 = none) */
    float    den_dx;               /* ... its offset from the centre, to either side */
    int8_t   camo_rank;            /* what an octopus over it takes its colour from, in rank order (0 = none) */
    void     (*on_buy)(tank_t *t, decor_t *d);             /* bought: it arrives */
    void     (*on_sell)(tank_t *t, decor_t *d);            /* sold: after the instance is back at its factory state */
    void     (*grow)(tank_t *t, decor_t *d, float seconds);   /* NULL = it does not grow */
    uint32_t (*camo_rgb)(const tank_t *t, const decor_t *d);
    float    (*ground)(const tank_t *t, const decor_t *d, float x, float g);   /* the ground at x with it there, from g without it */
} decor_def_t;
extern const decor_def_t DECOR[DK_COUNT];

int   decor_kind_of_item(int item);            /* a shop index -> its kind, -1 = not decor */
bool  decor_owned(const tank_t *t, int k);
float decor_x(const tank_t *t, int k);         /* its centre, the default when unplaced */
int   decor_z(const tank_t *t, int k);
void  decor_set(tank_t *t, int k, float x, int z);   /* x clamped to the floor, z to what it offers */
void  decor_restore(tank_t *t, int k, float x, int z);   /* a load: x > 0 is its spot (this board's clamp), z its depth */
int   decor_z_count(int k);
int   decor_z_at(int k, int i);
int   decor_z_index(int k, int z);
float decor_top(const tank_t *t, int k);       /* its highest pixel, for the tap box */
int   decor_hit(const tank_t *t, float x, float y);   /* the owned, movable piece under (x, y), -1 = none */
void  decor_init(tank_t *t);                   /* tank_init: every piece at its factory state */
void  decor_place(tank_t *t, int k);           /* bought */
void  decor_reset(tank_t *t, int k);           /* sold */
void  decor_tick(tank_t *t, float dt);         /* awake: the growth, by the minute */
void  decor_grow_all(tank_t *t, float seconds);   /* asleep: the same pace */
/* the floor spots the creatures read off the decor */
bool  decor_den_x(const tank_t *t, float side, float *x);    /* false = no piece digs a den */
bool  decor_camo(const tank_t *t, float x, uint32_t *rgb);   /* false = no piece under x */
float decor_ground(const tank_t *t, float x, float g);       /* the floor g at x, raised by what stands there */
bool  decor_reef(const tank_t *t, float *x, float *y);

#endif
