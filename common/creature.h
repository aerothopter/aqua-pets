/* The creatures' table (2026-10-23): the species, a row a kind (SPECIES[],
 * creature.c) - what it is, how it moves, its hooks. The classic fish is
 * SP_FISH, a row like the rest. tank.h includes this file where the goals
 * are known and before fish_t; the hooks take the fish and the tank by their
 * tags. Creatures keep the fish's names in code (fish_t, t->fish[]). */
#ifndef CREATURE_H
#define CREATURE_H

/* ---- species (2026-10-05, docs/species.md) ----
 * The classic fish and nine creatures that breed like it. The model picks
 * every creature's goal (schema 5 also hears its species); the species
 * decides how the goal is carried out (its locomotion, its own spots) and
 * how it looks (render.c). SP_FISH is 0, so every older save reads as fish. */
typedef enum { SP_FISH, SP_SEAHORSE, SP_OCTOPUS, SP_PUFFER, SP_ANGLER, SP_EEL, SP_SHARK, SP_SQUID,
               SP_CRAB, SP_LOBSTER,     /* the floor's two (2026-10-05, the same day) */
               SP_JELLYFISH,           /* appended: persisted species IDs never move */
               SP_SWORDFISH,           /* 2026-10-10 night: a fast open-water cruiser with a bill */
               SP_TURTLE,              /* 2026-10-11: a sea turtle - a slow paddler that naps on the sand and surfaces for a breath */
               SP_COUNT } species_t;
#define SP_VARIANTS 4            /* designs a species (fish_t.variant) */
#define SP_MUTATE_P 0.04f        /* a classic fish's fry hatching as a new species */
/* how a species moves (docs/species.md "How they move") */
typedef enum {
    LOCO_FIN,        /* the classic fish: committed U-turns, a tail beat */
    LOCO_UPRIGHT,    /* seahorse: upright, slow, vertical, anchors its tail */
    LOCO_JET,        /* octopus / squid: pulsed jets, mantle first; the octopus crawls the floor */
    LOCO_HOVER,      /* pufferfish: sculls, stops, turns on the spot, backs up */
    LOCO_AMBUSH,     /* anglerfish: still and low, short fast lunges */
    LOCO_UNDULATE,   /* electric eel: a travelling wave, swims backward, breathes air */
    LOCO_CRUISE,     /* hammerhead: never stops, wide turns, the head sweeps */
    LOCO_SIDEWALK,   /* crab: walks the floor SIDEWAYS on its legs, climbs rock */
    LOCO_WALK        /* lobster: walks the floor head first, a backward tail-flip to escape */
} loco_t;
typedef struct { const char *name; uint32_t color, fin, accent; } sp_variant_t;
/* how a species turns and carries its body (tank.c's species motion) */
typedef struct {
    float yaw_rate;   /* how fast the head swings round, yaw units/s (the fish: turn_rate x 1.2) */
    float brake;      /* how much the swing slows it: ~1 = it stops to turn, a turn on the spot */
    float rev_px;     /* a goal this close BEHIND it is backed up to, not turned for (0 = never backs) */
    float resp;       /* speed response, 1/s (the fish: 2.6) */
    float len_k;      /* body length over a fish's of its size (the bounds, tank_body_half_len) */
    float head_k;     /* the mouth along the body axis, x the half length (0 = mid-body) */
    float walk_off;   /* a floor walker's middle above the ground, x size */
} sp_motion_t;
/* what a species is (SPF_*): each one a way it acts that some kinds share */
enum {
    SPF_UPRIGHT  = 1 << 0,   /* upright (the seahorse): the snout up top, the snick, faces its partner in the dance, never slowed by grass */
    SPF_PULSES   = 1 << 1,   /* a bell (the jellyfish): it moves by its pulse, which bobs it */
    SPF_SIDEWAYS = 1 << 2,   /* walks sideways (the crab): face-on, its width its length */
    SPF_BACKS    = 1 << 3,   /* drawn backing up when its way is behind it (not the fish, the crab, the shark) */
    SPF_JETS     = 1 << 4,   /* jets mantle first (the octopus, the squid) */
    SPF_BREATHES = 1 << 5,   /* breathes air: a trip up for a gulp, and it rests flat on the floor (the eel, the turtle) */
    SPF_PUFFS    = 1 << 6,   /* puffs into a ball (the pufferfish) */
    SPF_INKS     = 1 << 7,   /* squirts ink, idle (the octopus, the squid) */
    SPF_SPARKS   = 1 << 8,   /* sparks, idle (the eel) */
    SPF_VISITS   = 1 << 9,   /* idle, a trip to the surface and back */
    SPF_HOME     = 1 << 10,  /* an ambusher: a new goal makes where it is its home (the angler) */
    SPF_LEVEL    = 1 << 11,  /* off the top or the floor, its climb is not turned back (the shark) */
    SPF_ROSTER   = 1 << 12,  /* its names, look, size and temperament are the ROSTER's, its fry the family's (the classic fish) */
    SPF_ROAMS    = 1 << 13,  /* it holds no station: by the glass the wall still steers it, and a big one's berth only turns it (the classic fish) */
};
typedef struct { float w0, w1, p0, p1, k; } sp_gait_t;   /* stop-and-go: steps of w0..w1 s, pauses of p0..p1 s, k x the pace while stepping */
typedef struct target { float x, y, speed; bool valid; } target_t;   /* a goal's point and cruise speed */
typedef struct { float low, food_x, food_y, hx, hy; } sp_aim_t;      /* what sp_target worked out: the low water, the pellet, the mouth */
struct tank; struct fish;
typedef struct {
    const char  *name;            /* the card's / shop's word, upper case */
    const char  *token;           /* schema-5 word: fish seahorse octopus puffer angler eel shark squid crab lobster */
    const char  *names[4];        /* a newborn's default names (<= FISH_NAME_MAX) */
    sp_variant_t var[SP_VARIANTS];
    float size_lo, size_hi;       /* base size (1.0 = the classic fish's ~42 px) */
    float bold_lo, bold_hi, soc_lo, soc_hi;
    float curiosity, lazy, turn_rate;
    loco_t loco;
    float speed_k;                /* cruise speed x (the goal's speed) */
    float burst_k;                /* dart / flight speed x */
    float vert_k;                 /* climb preference: the classic fish's 0.72 flattens climbs; > 1 favours them */
    float min_speed;              /* never slower (the hammerhead's ram breathing); 0 = can stop */
    float hit_r;                  /* tap radius at size 1, px */
    bool  big;                    /* the small ones keep their distance (SP_AVOID_R) */
    /* how it moves (tank.c, the species' motion; 2026-10-23: the rows took over
     * the species switches - a new species is a row and its hooks) */
    sp_motion_t mo;
    uint32_t flags;               /* SPF_* */
    float fin_hz, fin_div;        /* the free swim's beat a second: fin_hz, or fin_hz + speed / fin_div (fin_div > 0) */
    float bob_amp, bob_hz;        /* the free swim's bob: amp x sin(clock x hz) px/s (0 = none) */
    float clamp_th;               /* the body's thickness inside the glass, x size (0 = 8) */
    float den_dx;                 /* its den beside the others', x the side (the crab right, the lobster left) */
    sp_gait_t gait;               /* a walker's stop-and-go (k = 0: none) */
    float walk_step;              /* a walker's stride, px a beat of its legs (0 = the legs don't beat) */
    float walk_idle;              /* ... and their beat standing, a second */
    float mutate_p;               /* a fry of two of its kind hatching as another species instead */
    /* the hooks: NULL = it has none / the species' way (the classic fish's row brings its own dance and swim) */
    void  (*poke)(struct tank *t, int idx, struct fish *f, float x, float y);         /* tank_poke: its trick (the finger at x, y) */
    void  (*state)(struct tank *t, int idx, struct fish *f, float dt);                /* every frame: its own state */
    void  (*flee)(const struct tank *t, const struct fish *f, float *away, float *w, float *v);   /* a poke's getaway: how it goes (fish_t.flee_t) */
    void  (*court)(const struct tank *t, const struct fish *f, bool b, float cx, float cy, float rx, target_t *tg);   /* its part of the dance */
    void  (*target)(struct tank *t, int idx, struct fish *f, goal_id_t goal, bool glance, target_t *tg, const sp_aim_t *aim);   /* a goal, its way */
    bool  (*walking)(const struct tank *t, const struct fish *f);                     /* on the floor this frame */
    bool  (*move)(struct tank *t, struct fish *f, float desired, float want, bool override, float dt);   /* its own way of moving; false = the walk or the swim */
    void  (*dance)(const struct tank *t, struct fish *f, int idx, float vk, target_t *tg, float *desired, float *speed);   /* the courtship's steering */
    void  (*swim)(struct tank *t, int idx, struct fish *f, const target_t *tg, float desired, float touch_speed, float hes_speed,
                  bool sp_units, bool agile, float dt);                                /* the frame's move, once steered (it eats after) */
} species_def_t;
extern const species_def_t SPECIES[SP_COUNT];
#define SP_AVOID_R 70.0f          /* px at size 1: a small creature's berth around a big one */

#endif
