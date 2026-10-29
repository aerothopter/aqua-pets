#include "progression.h"
#include "theme.h"
#include "tank_events.h"
#include "version.h"
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define RAVENOUS_GIVE_UP_S 150.0f  /* begging window before the fish give up */
/* AUTO FEED off (0.3.2): nobody dies, but a fish left starving (hunger at
 * STARVE_HUNGER or worse) in a lit, awake tank for longer than the grace
 * loses trust, slowly, down to a floor a few feedings and holds win back. */
#define STARVE_HUNGER      8.5f
#define STARVE_GRACE_S     300.0f
#define STARVE_TRUST_PER_S (1.0f / 1200.0f)   /* a point of trust per 20 min */
#define STARVE_TRUST_FLOOR 2.0f
#define SAVE_HEARTBEAT_S 600.0f
#define SAVE_MIN_GAP_S   30.0f

const char *const MS_NAMES[MS_FISH_COUNT] = {
    "arrived", "first meal from you", "first hold-approach", "first dart", "first bubbles",
    "(inspected)", "(retired)", "(retired)", "first follow",
    "reached juv", "reached adult", "reached elder",
    "first rest in the seagrass",
};
const char *const TMS_NAMES[TMS_COUNT] = {
    "a pair", "a trio", "a quartet", "a quintet", "a sextet",
    "first full night's sleep", "first play session", "the tank changed someone", "first feeding",
    "first trimming", "first glass cleaning", "a school of ten shrimp",
    "first visit to the reef",
};

/* ---- the save: layout 3 (2026-10-21, the decor refactor) ----
 * One blob, NVS tank/save (SAVE_NVS_NS / SAVE_NVS_KEY). Layout 3 replaced the
 * PTK2 layout that had grown tail by tail since 2026-08-30: a save from before
 * it is not loaded and the tank starts fresh - the keeper chose a clean layout
 * over carrying the old one (2026-10-21). Within layout 3 a field only ever
 * APPENDS (the lock below): a longer save, from a newer build, loads the part
 * this build knows; nothing shorter than SAVE_MIN_SIZE is ours. The decor and
 * the helpers are records keyed by kind, so a new kind is a new record, not a
 * new field. The film: ALGAE_SAVE cells, the most any board has (the watch's
 * 23 x 28); a board with fewer keeps its own in the front. */
#define SAVE_MAGIC 0x50544b33u   /* "PTK3"; a PTK1 / PTK2 save starts fresh */
#define ALGAE_SAVE 644
_Static_assert(ALGAE_CELLS <= ALGAE_SAVE, "the film grid outgrew the cells the save keeps");

/* a fish: everything it keeps (100 B) */
typedef struct {
    uint8_t  preset, stage, species, variant;
    float    size, trust, bold, sociable, bold0, sociable0, hunger, energy, stress, curiosity;
    float    age_s, rest_dx, rest_dy;
    int32_t  eaten, eaten_player;
    uint32_t ms_bits, ms_seen;
    uint32_t body, accent;                 /* the keeper's colours: 0 = the preset's */
    char     name[FISH_NAME_MAX + 1];      /* the keeper's name: empty = the default */
    uint8_t  parent_p1[2], pad[2];         /* its parents' slots + 1 (0 = none) */
    float    drift_acc;
    uint32_t sd_paid;                      /* SD_PAID_* already paid for it */
} fish_save_t;
/* a piece of decor (decor.h), keyed by its kind + 1 (0 = an empty record) */
typedef struct {
    uint8_t  kind1, z, variant, pad;
    float    x;                            /* its centre (0 = the default spot); the airstone's is the bubble column */
    float    growth;                       /* the coral's / the cluster's (0 = full) */
    uint32_t rgb;                          /* the coral's colour (0 = its default) */
} decor_save_t;
#define DECOR_SAVE_MAX 12
_Static_assert(DK_COUNT <= DECOR_SAVE_MAX, "a decor kind without a record in the save");
/* a helper, keyed by kind (SVH_*, 0 = an empty record). The snail keeps its
 * spot (x, y) and the cells it grazed (count); the urchin its x and the grass
 * it ate (amount, px); the shrimp their number (n), the pellets toward the next
 * one (m), the cooldown (amount) and the pellets eaten (count). Positions not
 * kept are their own rules' to pick again on a load. */
enum { SVH_NONE, SVH_SNAIL, SVH_URCHIN, SVH_SHRIMP };
typedef struct {
    uint8_t  kind, n, m, pad;
    float    x, y, amount;
    int32_t  count;
} helper_save_t;
#define HELPER_SAVE_MAX 8
/* each helper's record kind (helper.h's rows say what the fields hold); a
   helper with no save hook keeps nothing (the frogman, the sub) */
static const uint8_t SVH_OF[HK_COUNT] = { [HK_SNAIL] = SVH_SNAIL, [HK_URCHIN] = SVH_URCHIN, [HK_SHRIMP] = SVH_SHRIMP };
_Static_assert(HK_COUNT <= HELPER_SAVE_MAX, "a helper without a record in the save");

typedef struct {
    /* the header */
    uint32_t magic;
    uint32_t saved_release;                /* version.h PT_RELEASE_NUM of the build that wrote it */
    int64_t  saved_unix;                   /* its wall clock (0 = unknown) */
    float    clock;
    /* the keeper and the tank */
    uint8_t  n_fish, arrival_pending, setup_pending, newborn_p1;   /* newborn: the arrival still owed its welcome, slot + 1 */
    float    feed_spot_x;
    int32_t  player_feedings, hold_approaches;
    int32_t  sale_meals_need;              /* the meals a tank that sold a fish owes before its next fry (0 = none sold) */
    uint32_t tank_ms_bits, tank_ms_seen;
    uint16_t light_idle_s;                 /* 0 = the default */
    uint8_t  light_auto, light_manual_off, light_tip_seen;
    uint8_t  screen_turned, orient, autofeed_off;   /* orient: bit 0 = ROTATION locked, bit 1 = the locked way up is turned over */
    uint8_t  theme, pad_keeper[3];
    /* the sand dollars */
    int32_t  sd_balance, sd_earned;
    uint32_t sd_unlocks;
    int32_t  sd_colonies_paid, sd_inches_paid;
    /* the upkeep */
    int32_t  trims, cells_cleaned, algae_colonies;
    float    trim_px;
    float    veg_h[VEG_BEDS_MAX][VEG_FRONDS_MAX];   /* every frond; bed 3 (the plant) zeros until it is bought */
    uint8_t  algae[ALGAE_SAVE];
    /* the decor, the helpers, the fish */
    decor_save_t  decor[DECOR_SAVE_MAX];
    helper_save_t helper[HELPER_SAVE_MAX];
    fish_save_t   fish[N_FISH_MAX];
} save_t;

/* ---- SAVE LAYOUT LOCK (layout 3, 2026-10-21): a new field goes at the END
 * of save_t with its own assert here; never edit an existing line here. ----
 * Every saved tank is read back through these offsets after an update, so they
 * are frozen: a failing assert means the change just moved (scrambled) every
 * saved tank - put the field at the end instead. The array bounds are in here
 * too: growing one shifts everything after it, so a bigger bound needs a new
 * tail. The numbers hold on both builds: no pointers or longs, int64_t aligns
 * to 8 and bool is 1 byte on the device's xtensa gcc and on the 64-bit sim alike. */
#define SAVE_MIN_SIZE 3848                 /* layout 3's first length: anything shorter is not ours */
_Static_assert(SAVE_MAGIC == 0x50544b33u, "SAVE LAYOUT LOCK: a new magic = every saved tank starts fresh");
_Static_assert(sizeof SAVE_NVS_NS == 5 && sizeof SAVE_NVS_KEY == 5, "SAVE LAYOUT LOCK: the NVS namespace / key are \"tank\" / \"save\"");
#define SAVE_AT(f, off)   _Static_assert(offsetof(save_t, f) == (off), "SAVE LAYOUT LOCK: save_t." #f " moved")
#define FISH_AT(f, off)   _Static_assert(offsetof(fish_save_t, f) == (off), "SAVE LAYOUT LOCK: fish_save_t." #f " moved")
#define DECOR_AT(f, off)  _Static_assert(offsetof(decor_save_t, f) == (off), "SAVE LAYOUT LOCK: decor_save_t." #f " moved")
#define HELPER_AT(f, off) _Static_assert(offsetof(helper_save_t, f) == (off), "SAVE LAYOUT LOCK: helper_save_t." #f " moved")
FISH_AT(preset, 0); FISH_AT(stage, 1); FISH_AT(species, 2); FISH_AT(variant, 3); FISH_AT(size, 4); FISH_AT(trust, 8);
FISH_AT(bold, 12); FISH_AT(sociable, 16); FISH_AT(bold0, 20); FISH_AT(sociable0, 24);
FISH_AT(hunger, 28); FISH_AT(energy, 32); FISH_AT(stress, 36); FISH_AT(curiosity, 40);
FISH_AT(age_s, 44); FISH_AT(rest_dx, 48); FISH_AT(rest_dy, 52); FISH_AT(eaten, 56); FISH_AT(eaten_player, 60);
FISH_AT(ms_bits, 64); FISH_AT(ms_seen, 68); FISH_AT(body, 72); FISH_AT(accent, 76); FISH_AT(name, 80);
FISH_AT(parent_p1, 88); FISH_AT(drift_acc, 92); FISH_AT(sd_paid, 96);
_Static_assert(sizeof(fish_save_t) == 100, "SAVE LAYOUT LOCK: fish_save_t is frozen (a new per-fish field = a new tail array)");
DECOR_AT(kind1, 0); DECOR_AT(z, 1); DECOR_AT(variant, 2); DECOR_AT(x, 4); DECOR_AT(growth, 8); DECOR_AT(rgb, 12);
_Static_assert(sizeof(decor_save_t) == 16, "SAVE LAYOUT LOCK: decor_save_t is frozen");
HELPER_AT(kind, 0); HELPER_AT(n, 1); HELPER_AT(m, 2); HELPER_AT(x, 4); HELPER_AT(y, 8); HELPER_AT(amount, 12); HELPER_AT(count, 16);
_Static_assert(sizeof(helper_save_t) == 20, "SAVE LAYOUT LOCK: helper_save_t is frozen");
_Static_assert(SVH_SNAIL == 1 && SVH_URCHIN == 2 && SVH_SHRIMP == 3, "SAVE LAYOUT LOCK: the helpers' saved kinds never move");
_Static_assert(DK_SEAGRASS0 == 0 && DK_PLANT == 3 && DK_CASTLE == 4 && DK_CORAL == 5 && DK_CLUSTER == 6 && DK_WRECK == 7 && DK_AIRSTONE == 8,
               "SAVE LAYOUT LOCK: the decor's saved kinds never move (a new kind goes at the end of decor_kind_t)");
SAVE_AT(magic, 0); SAVE_AT(saved_release, 4); SAVE_AT(saved_unix, 8); SAVE_AT(clock, 16);
SAVE_AT(n_fish, 20); SAVE_AT(arrival_pending, 21); SAVE_AT(setup_pending, 22); SAVE_AT(newborn_p1, 23);
SAVE_AT(feed_spot_x, 24); SAVE_AT(player_feedings, 28); SAVE_AT(hold_approaches, 32); SAVE_AT(sale_meals_need, 36);
SAVE_AT(tank_ms_bits, 40); SAVE_AT(tank_ms_seen, 44);
SAVE_AT(light_idle_s, 48); SAVE_AT(light_auto, 50); SAVE_AT(light_manual_off, 51); SAVE_AT(light_tip_seen, 52);
SAVE_AT(screen_turned, 53); SAVE_AT(orient, 54); SAVE_AT(autofeed_off, 55); SAVE_AT(theme, 56);
SAVE_AT(sd_balance, 60); SAVE_AT(sd_earned, 64); SAVE_AT(sd_unlocks, 68); SAVE_AT(sd_colonies_paid, 72); SAVE_AT(sd_inches_paid, 76);
SAVE_AT(trims, 80); SAVE_AT(cells_cleaned, 84); SAVE_AT(algae_colonies, 88); SAVE_AT(trim_px, 92);
SAVE_AT(veg_h, 96);                                            /* 4 beds x 16 fronds */
SAVE_AT(algae, 352);
SAVE_AT(decor, 996);                                           /* 12 x 16 */
SAVE_AT(helper, 1188);                                         /* 8 x 20 */
SAVE_AT(fish, 1348);                                           /* 25 x 100 */
_Static_assert(VEG_BEDS_MAX == 4 && VEG_FRONDS_MAX == 16 && N_FISH_MAX == 25 && FISH_NAME_MAX == 7,
               "SAVE LAYOUT LOCK: the arrays' bounds are frozen");
_Static_assert(sizeof(save_t) == SAVE_MIN_SIZE, "SAVE LAYOUT LOCK: layout 3 ends at 3848 (a new field: a SAVE_AT here, and this becomes >=)");
/* NVS budget: the save is one blob in the nvs partition (0x9000, 0x6000 =
 * 6 pages of 4096 B; tools/make_installer.py pins the row). A page is 126
 * entries of 32 B, and NVS keeps one page free for its garbage collection:
 * 5 x 126 = 630 entries usable. A blob costs an entry per 32 B, a header per
 * chunk (a chunk stays on one page) and an index entry, and nvs_set_blob
 * writes the new copy before it erases the old - on top of the director's
 * parked "bk" copy, three at once at worst. At 4000 B (a page's worth) that
 * is 3 x ~128 = ~390 entries, plus ~10 for the settings, "bat"/"hist" and
 * the batlog's "bed": under 2/3 of the 630, so GC always has room. Past it,
 * saves can start failing for space, and any nvs_flash_init error makes
 * main.c ERASE the partition. 3848 B in layout 3 (~121 entries a copy,
 * 3 x 121 + 10 = ~373 of the 630): 152 B of headroom. */
#define SAVE_NVS_BUDGET 4000
_Static_assert(sizeof(save_t) <= SAVE_NVS_BUDGET, "the save outgrew its NVS budget - see the math above");

float progression_time_scale = 1.0f;

static float s_age[N_FISH_MAX];      /* seconds of tended life per fish */
static uint32_t s_loaded_release;    /* the release that wrote the save this boot loaded (0 = older / none) */
static int64_t  s_loaded_unix;       /* ... and its wall-clock stamp (0 = none) */
static float s_since_save, s_dirty_since;
static bool  s_dirty;
static bool  s_ravenous;             /* begging/frenzy active until everyone's fed / give-up */
static float s_ravenous_t;           /* seconds spent begging (dash time excluded) */
static int   s_rav_feedings0;        /* player_feedings when the episode began (tank.ravenous_fed) */
static bool  s_gave_up;              /* AUTO FEED off: they begged, nobody came, nothing fell - no more begging
                                        until the keeper feeds (s_gave_feedings0) or someone is no longer starving */
static int   s_gave_feedings0;
static float s_starve_s[N_FISH_MAX]; /* seconds each fish has been starving in a lit tank with AUTO FEED off */
static bool  s_arrival_pending;
static float s_spawn_in = -1;        /* seconds until the spawning starts (-1 = not counting) */
static bool  s_prev_night;
static bool  s_booted;
static bool  s_setup_pending;        /* the first-run flow still owed (setup.c) */
static int   s_newborn = -1;         /* the arrival still owed its birth flow (setup.c), or -1 */
static int   s_sd_pending;           /* dollars awarded and not yet shown (the toast) */
static int32_t s_sd_prev_feedings = -1;   /* player_feedings at the last tick (-1 = adopt at the next) */
static int32_t s_sale_need;               /* player_feedings the next fry waits for since a fish was sold (0 = none sold) */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static void mark_dirty(void) { if (!s_dirty) { s_dirty = true; s_dirty_since = 0; } }

float progression_age_s(const tank_t *t, int idx) { (void)t; return idx >= 0 && idx < N_FISH_MAX ? s_age[idx] : 0; }
bool  progression_arrival_pending(void) { return s_arrival_pending; }
bool  progression_setup_pending(void)   { return s_setup_pending; }
void  progression_setup_done(tank_t *t) { s_setup_pending = false; progression_save(t); }
int   progression_newborn(void)         { return s_newborn; }
uint32_t progression_loaded_release(void) { return s_loaded_release; }
int64_t  progression_loaded_unix(void) { return s_loaded_unix; }
void  progression_newborn_done(tank_t *t) { s_newborn = -1; progression_save(t); }

static void apply_growth(fish_t *f);
static void set_tms(tank_t *t, uint32_t bit);
static const uint32_t POP_TMS[N_FISH_MAX + 1];
static void set_ms(fish_t *f, uint32_t bit) { if (!(f->ms_bits & bit)) { f->ms_bits |= bit; mark_dirty(); } }
/* ---- sand dollars ---- */
const sd_item_t SD_ITEMS[SD_ITEM_COUNT] = {
    { SD_ITEM_PLANT, "SWORD PLANT", "BROAD, VERTICAL LEAVES", "MORE COVER FOR YOUR CRITTERS", SD_PRICE_PLANT },   /* Strato's words (2026-09-16); the second line is 28 chars, the shop modal is 352 wide for it */
    { SD_ITEM_SNAIL, "SNAIL",       "GRAZES THE GLASS CLEAN,",   "EVEN WHILE THE TANK SLEEPS",  SD_PRICE_SNAIL },
    { SD_ITEM_CASTLE, "CASTLE",     "STONE TOWERS AND AN ARCH",  "THE FISH SWIM THROUGH IT",    SD_PRICE_CASTLE },   /* 2026-09-16 */
    { SD_ITEM_CORAL,  "CORAL",      "A BRANCHING REEF CORAL,",   "GROWS FOR WEEKS, YOUR COLOR", SD_PRICE_CORAL },    /* 2026-09-23 */
    { SD_ITEM_CLUSTER, "REEF CLUSTER", "A MATURE REEF ON A ROCK,", "FILLS OUT, THEN IT BLOOMS",  SD_PRICE_CLUSTER },  /* 2026-09-24: the dearest; three looks on its page */
    { SD_ITEM_SHRIMP,  "SHRIMP",    "A SCHOOL OF CHERRY SHRIMP", "THEY EAT SCRAPS AND MULTIPLY", SD_PRICE_SHRIMP },
    { SD_ITEM_URCHIN,  "SEA URCHIN", "NIBBLES THE TALL GRASS,",  "EVEN WHILE THE TANK SLEEPS",  SD_PRICE_URCHIN },
    { SD_ITEM_WRECK,   "WRECK",      "A SUNKEN BOAT WITH HOLES", "TO SWIM THROUGH, AN ANCHOR",   SD_PRICE_WRECK },   /* "SHIPWRECK" until 2026-10-10 night */
    { SD_ITEM_FROGMAN, "FROGMAN",    "A DIVER WHO DRIFTS ACROSS", "THE TANK, TRAILING BUBBLES",  SD_PRICE_FROGMAN },
    { SD_ITEM_SUB,     "SUBMARINE",  "CRUISES THE TANK, BUBBLING;", "STOPS TO RAISE A PERISCOPE", SD_PRICE_SUB },   /* 2026-10-10 night */   /* 2026-10-10 (he came with the wreck that morning) */   /* 2026-10-10 */  /* 2026-10-02: the episode 5 promise, a resident like the snail */  /* 2026-09-29: a resident, like the snail; Strato: "should mention that they multiply" (28 chars, as the plant's) */
    /* the species (2026-10-05, docs/species.md; one a purchase since 2026-10-07), in
       species order; the words say what makes them them, and that two of a kind breed */
    { SD_ITEM_SP_SEAHORSE, "SEAHORSE",    "A YOUNG ONE. IT HOLDS THE", "GRASS BY ITS TAIL. 2 BREED",  SD_PRICE_SP_SEAHORSE },
    { SD_ITEM_SP_OCTOPUS,  "OCTOPUS",     "A YOUNG ONE. IT HIDES AND", "TAKES ITS DEN'S COLOR",       SD_PRICE_SP_OCTOPUS },
    { SD_ITEM_SP_PUFFER,   "PUFFERFISH",  "A YOUNG ONE. TAP IT TWICE", "AND IT PUFFS UP. TWO BREED",      SD_PRICE_SP_PUFFER },
    { SD_ITEM_SP_ANGLER,   "ANGLERFISH",  "A YOUNG ONE. ITS LURE",     "GLOWS AT NIGHT. TWO BREED",   SD_PRICE_SP_ANGLER },
    { SD_ITEM_SP_EEL,      "ELECTRIC EEL", "A YOUNG ONE. IT RISES FOR", "AIR AND SPARKS. TWO BREED",  SD_PRICE_SP_EEL },
    { SD_ITEM_SP_SHARK,    "HAMMERHEAD",  "A PUP THAT NEVER STOPS",    "SWIMMING. TWO BREED",         SD_PRICE_SP_SHARK },
    { SD_ITEM_SP_SQUID,    "SQUID",       "A YOUNG ONE. IT JETS AND",  "INKS. TWO OF THEM BREED",     SD_PRICE_SP_SQUID },
    { SD_ITEM_SP_CRAB,     "CRAB",        "A YOUNG ONE THAT WALKS",    "SIDEWAYS. TWO BREED",         SD_PRICE_SP_CRAB },
    { SD_ITEM_SP_LOBSTER,  "LOBSTER",     "A YOUNG FLOOR WALKER.",     "TWO OF THEM BREED",           SD_PRICE_SP_LOBSTER },
    { SD_ITEM_SP_JELLYFISH,"JELLYFISH",   "A YOUNG ONE. IT PULSES",    "AND DRIFTS. TWO BREED",       SD_PRICE_SP_JELLYFISH },
    { SD_ITEM_SP_SWORDFISH,"SWORDFISH",   "A YOUNG ONE. IT CRUISES",   "FAST, BILL FIRST. 2 BREED",   SD_PRICE_SP_SWORDFISH },   /* 2026-10-10 night */
    { SD_ITEM_SP_TURTLE,   "TURTLE",      "A YOUNG ONE. IT PADDLES,",  "NAPS, BREATHES. 2 BREED",    SD_PRICE_SP_TURTLE },      /* 2026-10-11 */
};
_Static_assert(SD_ITEM_SP_FIRST + SP_COUNT - 1 == SD_ITEM_COUNT, "a shop item per species, after the things");
_Static_assert(SD_ITEM_COUNT <= 32, "sd_unlocks is 32 bits");
static void sd_award(tank_t *t, int n) {
    if (n <= 0) return;
    t->sd_balance += n; t->sd_earned += n; s_sd_pending += n;
    mark_dirty();
}
int  progression_sd_take_award(void) { int n = s_sd_pending; s_sd_pending = 0; return n; }
void progression_sd_grant(tank_t *t, int n) {
    if (n >= 0) sd_award(t, n);
    else { t->sd_balance += n; if (t->sd_balance < 0) t->sd_balance = 0; mark_dirty(); }
}
/* the ledger against the tank: anything earned and not yet paid is paid now.
 * Runs every tick, so it is also the back pay for a save from before the
 * shop (paid bits all zero: the stages and the trust it already has, the
 * hundreds its counters already passed - once). MEALS are the exception: a
 * meal pays as it happens, so the count is adopted, not back-paid. */
static void sd_tick(tank_t *t) {
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i]; uint32_t *paid = &t->sd_paid_fish[i];
        if ((f->ms_bits & MS_REACHED_JUV)   && !(*paid & SD_PAID_JUV))   { *paid |= SD_PAID_JUV;   sd_award(t, SD_STAGE_JUV); }
        if ((f->ms_bits & MS_REACHED_ADULT) && !(*paid & SD_PAID_ADULT)) { *paid |= SD_PAID_ADULT; sd_award(t, SD_STAGE_ADULT); }
        if ((f->ms_bits & MS_REACHED_ELDER) && !(*paid & SD_PAID_ELDER)) { *paid |= SD_PAID_ELDER; sd_award(t, SD_STAGE_ELDER); }
        if (f->trust >= 10.0f && !(*paid & SD_PAID_TRUST))               { *paid |= SD_PAID_TRUST; sd_award(t, SD_TRUST); }
    }
    if (s_sd_prev_feedings < 0) s_sd_prev_feedings = t->player_feedings;
    if (t->player_feedings > s_sd_prev_feedings) sd_award(t, SD_MEAL * (t->player_feedings - s_sd_prev_feedings));
    s_sd_prev_feedings = t->player_feedings;
    int32_t hund = t->algae_colonies / SD_CHORE_EVERY;
    if (hund > t->sd_colonies_paid) { sd_award(t, SD_CHORE * (hund - t->sd_colonies_paid)); t->sd_colonies_paid = hund; }
    hund = (int32_t)(t->trim_px / (PX_PER_CM * SD_TRIM_CM));
    if (hund > t->sd_inches_paid) { sd_award(t, SD_CHORE * (hund - t->sd_inches_paid)); t->sd_inches_paid = hund; }
}
int progression_sell_value(int item) { return item < 0 || item >= SD_ITEM_COUNT ? 0 : SD_ITEMS[item].price * SD_SELL_PCT / 100; }
bool progression_sell(tank_t *t, int item) {
    int k = decor_kind_of_item(item), h = helper_kind_of_item(item);   /* a sellable piece, or a helper (2026-10-22: the snail too) */
    bool decor = k >= 0 && (DECOR[k].flags & DF_SELLABLE);
    if (item < 0 || item >= SD_ITEM_COUNT || (!decor && h < 0)) return false;
    const sd_item_t *it = &SD_ITEMS[item];
    if (!(t->sd_unlocks & it->bit)) return false;
    t->sd_unlocks &= ~it->bit;
    int back = progression_sell_value(item);
    t->sd_balance += back; s_sd_pending += back;
    if (decor) decor_reset(t, k);
    else helper_leave(t, h);                               /* it leaves the tank: a school's grown shrimp with it */
    mark_dirty(); progression_save(t);
    return true;
}
/* ---- a fish sold (2026-10-01) ---- */
int progression_fish_value(const tank_t *t, int fish) {
    static const int worth[4] = { SD_FISH_FRY, SD_FISH_JUV, SD_FISH_ADULT, SD_FISH_ELDER };
    if (fish < 0 || fish >= t->n_fish) return 0;
    int v = worth[t->fish[fish].stage & 3], sp = t->fish[fish].species;
    /* a species' creature sells back for its shop price at most (2026-10-07: at 10 a creature,
       a juvenile bought for 10 and sold for SD_FISH_JUV 15 would mint sand dollars) */
    if (sp > SP_FISH && sp < SP_COUNT) { int price = SD_ITEMS[SD_ITEM_SP_FIRST + sp - 1].price; if (v > price) v = price; }
    return v;
}
bool progression_fish_sellable(const tank_t *t, int fish) {
    return fish >= 0 && fish < t->n_fish && t->n_fish > FISH_KEEP_MIN && s_newborn < 0;
}
int progression_item_species(int item) {
    return item >= SD_ITEM_SP_FIRST && item < SD_ITEM_COUNT ? 1 + item - SD_ITEM_SP_FIRST : -1;
}
bool progression_has_room(const tank_t *t) { return t->n_fish + 2 <= POP_CAP && t->n_fish + 2 <= N_FISH_MAX; }
bool progression_has_room_one(const tank_t *t) { return t->n_fish + 1 <= POP_CAP && t->n_fish + 1 <= N_FISH_MAX; }
void progression_species_sync(tank_t *t) {
    uint32_t was = t->sd_unlocks;
    for (int sp = 1; sp < SP_COUNT; sp++) {
        uint32_t bit = SD_ITEMS[SD_ITEM_SP_FIRST + sp - 1].bit;
        if (tank_species_n(t, sp) > 0) t->sd_unlocks |= bit; else t->sd_unlocks &= ~bit;
    }
    if (t->sd_unlocks != was) mark_dirty();
}
bool progression_sell_fish(tank_t *t, int fish) {
    if (!progression_fish_sellable(t, fish)) return false;
    int worth = progression_fish_value(t, fish);
    if (!tank_remove_fish(t, fish)) return false;
    for (int i = fish; i < t->n_fish; i++) { s_age[i] = s_age[i + 1]; s_starve_s[i] = s_starve_s[i + 1]; }   /* the clocks move down with their fish */
    s_age[t->n_fish] = 0; s_starve_s[t->n_fish] = 0;
    /* the slot it leaves is earned again by care, not handed back: a fry
       already on its way is called off, and the next one waits for
       SELL_FRY_MEALS more meals (care_gates) - every other gate was met long
       ago in a grown tank, so without this the fry would be in the grass
       within the minute */
    s_arrival_pending = false; s_spawn_in = -1;
    s_sale_need = t->player_feedings + SELL_FRY_MEALS;
    sd_award(t, worth);
    progression_species_sync(t);                           /* the last of a species gone: its pair is for sale again */
    tank_emit(TEV_CONFIRM, -1);
    progression_save(t);                                   /* a sale sticks at once */
    return true;
}
/* a species' newcomers in the tank: n juveniles by the reef, their clocks
   where a juvenile's are, nothing owed for the stage they were bought at
   (the shop sells ONE at a time since 2026-10-07; the director's spawn stages a pair) */
static bool add_species(tank_t *t, int sp, int n) {
    if (sp <= SP_FISH || sp >= SP_COUNT || t->n_fish + n > POP_CAP) return false;
    int first = tank_add_species_n(t, sp, n);
    if (first < 0) return false;
    for (int i = first; i < t->n_fish; i++) {
        s_age[i] = STAGE_JUV_AGE; s_starve_s[i] = 0;
        t->fish[i].ms_bits = MS_ARRIVED | MS_REACHED_JUV;
        t->sd_paid_fish[i] = SD_PAID_JUV;                  /* bought grown that far: no stage pay for it */
        apply_growth(&t->fish[i]);
    }
    if (t->n_fish <= N_FISH_MAX) set_tms(t, POP_TMS[t->n_fish]);
    return true;
}
bool progression_spawn_pair(tank_t *t, int species) {
    if (!add_species(t, species, 2)) return false;
    progression_species_sync(t); progression_save(t);
    return true;
}
bool progression_buy(tank_t *t, int item) {
    if (item < 0 || item >= SD_ITEM_COUNT) return false;
    const sd_item_t *it = &SD_ITEMS[item];
    int sp = progression_item_species(item);
    /* a thing is bought once; a species' creature is bought as often as there is room
       (2026-10-07: one a purchase - two of a kind breed, so the keeper buys a second) */
    if ((sp <= 0 && (t->sd_unlocks & it->bit)) || t->sd_balance < it->price) return false;
    if (sp > 0 && !add_species(t, sp, 1)) return false;    /* one juvenile, by the reef - or no room */
    t->sd_balance -= it->price; t->sd_unlocks |= it->bit;
    decor_place(t, decor_kind_of_item(item));             /* a piece of decor arrives at its default spot */
    helper_arrive(t, helper_kind_of_item(item));          /* a helper appears and goes about its rules */
    progression_save(t);                                   /* a purchase sticks at once */
    return true;
}
const char *const *progression_sd_earn_lines(void) {
    static char lines[SD_EARN_LINES][30]; static const char *ptr[SD_EARN_LINES + 1]; static bool made;
    if (!made) {
        /* <= 25 chars each: the modal is 336 px wide at scale 2 */
        snprintf(lines[0], 30, "+%d  EVERY MEAL EATEN", SD_MEAL);
        snprintf(lines[1], 30, "+%d/%d/%d  A FISH GROWS UP", SD_STAGE_JUV, SD_STAGE_ADULT, SD_STAGE_ELDER);
        snprintf(lines[2], 30, "+%d  A NEW FRY IS BORN", SD_BIRTH);
        snprintf(lines[3], 30, "+%d  A FISH FULLY TRUSTS", SD_TRUST);
        snprintf(lines[4], 30, "+%d  %d ALGAE COLONIES", SD_CHORE, SD_CHORE_EVERY);
        snprintf(lines[5], 30, "+%d  %d CM OF GRASS CUT", SD_CHORE, SD_TRIM_CM);
        for (int i = 0; i < SD_EARN_LINES; i++) ptr[i] = lines[i];
        ptr[SD_EARN_LINES] = NULL; made = true;
    }
    return ptr;
}
static void set_tms(tank_t *t, uint32_t bit) { if (!(t->tank_ms_bits & bit)) { t->tank_ms_bits |= bit; mark_dirty(); } }

static void apply_stage(fish_t *f, float age) {
    stage_t st = age >= STAGE_ELDER_AGE ? STAGE_ELDER : age >= STAGE_ADULT_AGE ? STAGE_ADULT
               : age >= STAGE_JUV_AGE ? STAGE_JUV : STAGE_FRY;
    if (st != f->stage) { f->stage = st; mark_dirty(); }         /* silent surprise */
    if (st >= STAGE_JUV)   set_ms(f, MS_REACHED_JUV);
    if (st >= STAGE_ADULT) set_ms(f, MS_REACHED_ADULT);
    if (st >= STAGE_ELDER) set_ms(f, MS_REACHED_ELDER);
}

/* size: base by stage, plus a meal-fed bonus; never shrinks below stage base */
static void apply_growth(fish_t *f) {
    static const float stage_scale[4] = { 0.55f, 0.78f, 1.04f, 1.23f };  /* elder +14% (2026-08-30) */
    float fed = 1.0f + clampf(f->eaten / 60.0f, 0, 1) * 0.18f;
    f->size = f->base_size * stage_scale[f->stage] * fed;
}

static const uint32_t POP_TMS[N_FISH_MAX + 1] = { 0, 0, TMS_PAIR, TMS_TRIO, TMS_QUARTET, TMS_QUINTET, TMS_SEXTET };   /* (7..10: no badge of their own) */

/* the parents: the two most trusting grown fish - or, with fewer than two
 * grown (at two fish no stage is gated), the most trusting of the rest fill
 * in, so there is always a pair to court (2026-09-24: the spawning needs
 * two fish in the grass; tank_add_fish already fell back to fish 0 and 1).
 * Since the species (2026-10-05) a pair is two of ONE species: the ranking
 * runs within each species, and the tank's pair is the species' best pair
 * - the one whose weaker parent ranks highest (grown first, then trust),
 * the classic fish winning a tie. No species with two = no pair (-1, -1):
 * no courting, no arrival, until a pair is bought or one grows up. */
static float parent_rank(const fish_t *f) { return (f->stage >= STAGE_ADULT ? 100.0f : 0.0f) + f->trust; }
static void pick_parents(const tank_t *t, int *pa, int *pb) {
    *pa = *pb = -1;
    float best = -1;
    for (int sp = 0; sp < SP_COUNT; sp++) {
        int a = -1, b = -1;
        for (int i = 0; i < t->n_fish; i++) {
            if (t->fish[i].species != sp) continue;
            float r = parent_rank(&t->fish[i]);
            if (a < 0 || r > parent_rank(&t->fish[a])) { b = a; a = i; }
            else if (b < 0 || r > parent_rank(&t->fish[b])) b = i;
        }
        if (b < 0) continue;
        float r = parent_rank(&t->fish[b]);
        if (r > best) { best = r; *pa = a; *pb = b; }
    }
}
static bool have_pair(const tank_t *t) { int a, b; pick_parents(t, &a, &b); return a >= 0 && b >= 0; }

/* the arrival itself: a fry in the nursery grass, traits inherited from the
 * parents; the stage clock starts from zero */
static void do_arrival(tank_t *t) {
    int a, b;
    pick_parents(t, &a, &b);
    int slot = tank_add_fish(t, a, b);
    s_arrival_pending = false;
    s_spawn_in = -1; t->spawning = false; t->spawn_danced = 0;
    if (slot < 0) return;
    progression_species_sync(t);             /* a surprise species is in the tank now: its pair is off the shelf */
    int nb = tank_nursery_bed(t);            /* born in the grass it was courted in */
    if (nb >= 0) {
        float x0, x1; tank_veg_bed(t, nb, &x0, &x1, NULL, NULL);
        t->fish[slot].x = (x0 + x1) * 0.5f; t->fish[slot].y = TANK_BOT - 16 - 18;
    }
    s_age[slot] = 0;
    t->fish[slot].ms_bits = MS_ARRIVED;
    t->sd_paid_fish[slot] = 0;               /* a new ledger for the new fish */
    if (t->n_fish <= N_FISH_MAX) set_tms(t, POP_TMS[t->n_fish]);
    s_newborn = slot;                        /* owed its welcome: the birth flow (setup.c) */
    sd_award(t, SD_BIRTH);
    mark_dirty();
}

/* care gates (docs/progression-next.md, Act 2): never time alone.
 * Counted, not just checked, so the tank can TELL when it's close: one
 * condition shy of an arrival, the parents-to-be start courting - and the
 * milestones page can LIST them (progression_next_fry, 2026-09-14). One
 * table serves both: `have` / `need` are the numbers behind the words. */
typedef struct { int kind; float have, need, frac; bool met; } gate_t;
#define CARE_GATES_MAX 4                 /* the population's three + the glass */
static int care_gates(const tank_t *t, gate_t g[CARE_GATES_MAX]) {
    float min_trust = 10;
    for (int i = 0; i < t->n_fish; i++)
        if (t->fish[i].trust < min_trust) min_trust = t->fish[i].trust;
    int last = t->n_fish - 1;
    float age = last >= 0 ? s_age[last] : 0;
    int n = 0;
    /* the MEALS gate after a sale: the table's count, or the meals owed since
       the fish was sold, whichever is further off */
#define MEALS(tbl) ((float)((tbl) > s_sale_need ? (tbl) : s_sale_need))
#define GATE(k, h, nd, m) do { g[n].kind = (k); g[n].have = (h); g[n].need = (nd); g[n].met = (m); \
        g[n].frac = g[n].met ? 1.0f : (nd) > 0 ? (h) / (nd) : 0; if (g[n].frac > 1) g[n].frac = 1; n++; } while (0)
    switch (t->n_fish) {
    case 2:
        GATE(FRY_REQ_TRUST, min_trust, 6.0f, min_trust >= 6.0f);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, MEALS(12), t->player_feedings >= MEALS(12));
        GATE(FRY_REQ_HOLD, (float)t->hold_approaches, 1, t->hold_approaches >= 1);
        break;
    case 3:
        /* 2026-09-15 (Strato): MEALS and CHANGE were one "40 meals OR someone
         * drifted 0.11" row - and the parents had always drifted long before,
         * so it was dead weight. Now three rows, and CHANGE is the YOUNGEST
         * fish's accumulated drift pressure (tank.h drift_acc): the same
         * ~26 lit minutes fed and calm, and impossible to soft-lock for a fry
         * born on a trait clamp (inheritance puts them there). */
        GATE(FRY_REQ_GROW, age, (float)STAGE_JUV_AGE, t->fish[last].stage >= STAGE_JUV);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, MEALS(40), t->player_feedings >= MEALS(40));
        GATE(FRY_REQ_CHANGE, t->fish[last].drift_acc, DRIFT_CHANGE, t->fish[last].drift_acc >= DRIFT_CHANGE);
        break;
    case 4:
        GATE(FRY_REQ_GROW, age, (float)STAGE_ADULT_AGE, t->fish[last].stage >= STAGE_ADULT);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, MEALS(80), t->player_feedings >= MEALS(80));
        GATE(FRY_REQ_TRUST, min_trust, 7.0f, min_trust >= 7.0f);
        break;
    case 5:
        GATE(FRY_REQ_GROW, age, (float)STAGE_ADULT_AGE, t->fish[last].stage >= STAGE_ADULT);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, MEALS(140), t->player_feedings >= MEALS(140));
        GATE(FRY_REQ_TRUST, min_trust, 8.0f, min_trust >= 8.0f);
        break;
    default: {
        /* 6..9 (2026-10-05, the species' ten places): the five's gates, the
           meals climbing ~+70 a place and the trust to 9 from eight - a
           full tank is a lot of mouths, and every one of them must trust */
        static const int meals[N_FISH_MAX] = { 0, 0, 0, 0, 0, 140, 200, 270, 350, 440,
            /* 10..24 (2026-10-07, the 25 places): the same ladder on, ~+70 a place */
            510, 580, 650, 720, 790, 860, 930, 1000, 1070, 1140, 1210, 1280, 1350, 1420, 1490 };
        int nf = t->n_fish < N_FISH_MAX ? t->n_fish : N_FISH_MAX - 1;
        float trust = nf >= 7 ? 9.0f : 8.0f;
        GATE(FRY_REQ_GROW, age, (float)STAGE_ADULT_AGE, t->fish[last].stage >= STAGE_ADULT);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, MEALS(meals[nf]), t->player_feedings >= MEALS(meals[nf]));
        GATE(FRY_REQ_TRUST, min_trust, trust, min_trust >= trust);
        break; }
    }
    /* and a clean tank (Strato, 2026-09-16: "fish should not be able to
     * breed in a dirty tank. Some algae is OK but if a certain percentage of
     * glass crosses a threshold it will prevent new fries from spawning
     * unless cleaned"): film on no more than ALGAE_DIRTY of the glass. A
     * care gate like the others, so the parents court while only the glass
     * holds them back, and the checklist lists it. The bar reads the other
     * way from the rest - empty at the growth cap, full at the threshold -
     * so it fills as the keeper wipes. Note the staged fry is NOT held by
     * the glass at the light-on that brings it: a night's sleep films ~25%
     * of the glass, so a fry conceived clean would otherwise never land on
     * a morning (do_arrival waits for the nursery only). */
    {
        float cover = tank_algae_cover(t);
        GATE(FRY_REQ_GLASS, cover, ALGAE_DIRTY, cover <= ALGAE_DIRTY);
        if (!g[n - 1].met) {
            float frac = 1.0f - (cover - ALGAE_DIRTY) / ALGAE_DIRTY;
            g[n - 1].frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
        }
    }
#undef GATE
#undef MEALS
    return n;
}
static void arrival_conditions(const tank_t *t, int *met, int *total) {
    gate_t g[CARE_GATES_MAX];
    *total = care_gates(t, g); *met = 0;
    for (int i = 0; i < *total; i++) *met += g[i].met;
}

/* how each gate is moved - the words behind the HOW? button */
static const char *const TIP_TRUST[]  = { "REST A FINGER ON THE GLASS", "AND KEEP IT STILL. EVERY", "FISH EARNS TRUST WHILE IT", "RESTS THERE.", NULL };   /* (3 quick taps scared them until 2026-10-28) */
static const char *const TIP_FEED[]   = { "PICK FEED IN THE MENU AND", "TAP THE WATER TO DROP FOOD.", "A FEEDING COUNTS AS A MEAL", "ONCE A FISH EATS FROM IT.", NULL };   /* (2026-10-28: the FEED tool) */
static const char *const TIP_HOLD[]   = { "REST A FINGER ON THE GLASS", "FOR A FEW SECONDS. A FISH", "THAT TRUSTS YOU SWIMS OVER", "AND STAYS. FEED FIRST: A", "HUNGRY FISH WON'T COME.", NULL };
static const char *const TIP_GROW[]   = { "FISH GROW WITH TIME,", "SLOWER WHEN THE TANK IS", "IN SLEEP MODE.", NULL };
static const char *const TIP_CHANGE[] = { "FISH PERSONALITIES WILL", "NATURALLY DRIFT AS THEY", "INTERACT WITH THE WORLD.", NULL };   /* Strato: intentionally vague */
static const char *const TIP_GRASS[]  = { "GRASS REGROWS ON ITS OWN,", "FASTEST WHILE THE TANK", "SLEEPS.", NULL };
static const char *const TIP_GLASS[]  = { "PICK THE SPONGE IN THE MENU", "AND DRAG IT ACROSS THE GLASS", "TO WIPE IT CLEAN.", "UNLOCKABLE CRITTERS CAN", "HELP KEEP IT CLEAN.", NULL };   /* Strato's words, the SPONGE's since 2026-10-28 */
const char *const *progression_fry_tip(int kind) {
    switch (kind) {
    case FRY_REQ_TRUST:  return TIP_TRUST;
    case FRY_REQ_FEED:   return TIP_FEED;
    case FRY_REQ_HOLD:   return TIP_HOLD;
    case FRY_REQ_GROW:   return TIP_GROW;
    case FRY_REQ_CHANGE: return TIP_CHANGE;
    case FRY_REQ_GLASS:  return TIP_GLASS;
    default:             return TIP_GRASS;
    }
}

/* the checklist, in words. No %f: the device's printf may be the nano one. */
static const char *const STAGE_WORDS[4] = { "A FRY", "A JUVENILE", "AN ADULT", "AN ELDER" };
int progression_next_fry(const tank_t *t, fry_req_t out[FRY_REQ_MAX], bool *staged) {
    if (staged) *staged = s_arrival_pending;
    if (t->n_fish >= POP_CAP || t->n_fish >= N_FISH_MAX || t->n_fish < 2 || !have_pair(t)) return 0;   /* (no two of a kind: nothing to list) */
    gate_t g[CARE_GATES_MAX];
    int n = care_gates(t, g);
    for (int i = 0; i < n; i++) {
        fry_req_t *r = &out[i];
        memset(r, 0, sizeof *r);
        r->kind = g[i].kind; r->frac = g[i].frac; r->met = g[i].met;
        int have = (int)g[i].have, need = (int)g[i].need;
        switch (g[i].kind) {
        case FRY_REQ_TRUST:
            snprintf(r->title, sizeof r->title, "TRUST");
            snprintf(r->words, sizeof r->words, "ALL FISH MUST HAVE TRUST");
            snprintf(r->words2, sizeof r->words2, "OF AT LEAST %d OUT OF 10", need);
            if (r->met) snprintf(r->progress, sizeof r->progress, "EVERY FISH DOES");
            else { int tenths = (int)(g[i].have * 10 + 0.5f);
                   snprintf(r->progress, sizeof r->progress, "LOWEST NOW %d.%d", tenths / 10, tenths % 10); }
            break;
        case FRY_REQ_FEED:
            snprintf(r->title, sizeof r->title, "MEALS");
            snprintf(r->words, sizeof r->words, "FEED THE FISH AT LEAST");
            snprintf(r->words2, sizeof r->words2, "%d TIMES IN ALL", need);
            if (r->met) snprintf(r->progress, sizeof r->progress, "DONE");
            else snprintf(r->progress, sizeof r->progress, "%d OF %d SO FAR", have, need);
            break;
        case FRY_REQ_HOLD:
            snprintf(r->title, sizeof r->title, "HOLD");
            snprintf(r->words, sizeof r->words, "REST A FINGER ON THE GLASS");
            snprintf(r->words2, sizeof r->words2, "UNTIL A FISH SWIMS TO IT");
            snprintf(r->progress, sizeof r->progress, r->met ? "DONE" : "NOT YET");
            break;
        case FRY_REQ_GROW: {
            const fish_t *f = &t->fish[t->n_fish - 1];
            snprintf(r->title, sizeof r->title, "GROW");
            snprintf(r->words, sizeof r->words, "THE YOUNGEST FISH MUST");
            snprintf(r->words2, sizeof r->words2, need >= STAGE_ADULT_AGE ? "GROW INTO AN ADULT" : "GROW INTO A JUVENILE");
            snprintf(r->progress, sizeof r->progress, "%s IS %s", f->name, STAGE_WORDS[f->stage & 3]);
            break; }
        case FRY_REQ_CHANGE: {
            const fish_t *f = &t->fish[t->n_fish - 1];
            snprintf(r->title, sizeof r->title, "CHANGE");
            snprintf(r->words, sizeof r->words, "%s'S PERSONALITY", f->name);
            snprintf(r->words2, sizeof r->words2, "MUST START TO SHIFT");
            if (r->met) snprintf(r->progress, sizeof r->progress, "DONE");
            else snprintf(r->progress, sizeof r->progress, "%d%% THERE", (int)(r->frac * 100 + 0.5f));
            break; }
        case FRY_REQ_GLASS: {
            int pct = (int)(g[i].have * 100 + 0.5f), limit = (int)(ALGAE_DIRTY * 100 + 0.5f);
            snprintf(r->title, sizeof r->title, "GLASS");
            snprintf(r->words, sizeof r->words, "NO MORE THAN %d%%", limit);          /* Strato's words, 2026-09-16 */
            snprintf(r->words2, sizeof r->words2, "ALGAE COVERAGE");
            if (r->met) snprintf(r->progress, sizeof r->progress, "CURRENTLY CLEAN ENOUGH");
            else snprintf(r->progress, sizeof r->progress, "%d%% COVERED NOW", pct);
            break; }
        }
    }
    /* and the nursery: every arrival needs grass to be born in */
    {
        fry_req_t *r = &out[n++];
        memset(r, 0, sizeof *r);
        float best = 0;
        for (int b = 0; b < VEG_BEDS; b++) if (t->veg_growth[b] > best) best = t->veg_growth[b];
        r->kind = FRY_REQ_GRASS; r->met = tank_nursery_bed(t) >= 0;
        r->frac = r->met ? 1.0f : best / VEG_NURSERY;
        snprintf(r->title, sizeof r->title, "GRASS");
        snprintf(r->words, sizeof r->words, "ONE GRASS BED MUST GROW");
        snprintf(r->words2, sizeof r->words2, "TALL ENOUGH TO HIDE IN");
        if (r->met) snprintf(r->progress, sizeof r->progress, "A NURSERY BED IS READY");
        else snprintf(r->progress, sizeof r->progress, "TALLEST BED %d%% THERE", (int)(r->frac * 100 + 0.5f));
    }
    return n;
}

static bool arrival_earned(const tank_t *t) {
    if (t->n_fish >= POP_CAP || t->n_fish >= N_FISH_MAX) return false;
    if (tank_nursery_bed(t) < 0) return false;   /* no grass to be born in */
    if (!have_pair(t)) return false;             /* no two of one species to court */
    int met, total;
    arrival_conditions(t, &met, &total);
    return met == total;
}

void progression_force_arrival(tank_t *t) { if (!have_pair(t)) return; s_arrival_pending = true; do_arrival(t); }
void progression_woke(tank_t *t) { if (s_arrival_pending) do_arrival(t); }
void progression_stage_arrival(tank_t *t) { (void)t; if (!s_arrival_pending) { s_arrival_pending = true; mark_dirty(); } }

void progression_fresh(tank_t *t) {
    tank_new_population(t);          /* a new tank: two FRY, contrasting -
                                      * the keeper watches them grow up */
    for (int i = 0; i < N_FISH_MAX; i++) s_age[i] = 0;
    t->tank_ms_bits = TMS_PAIR;
    for (int i = 0; i < t->n_fish; i++) { t->fish[i].ms_bits = MS_ARRIVED; apply_growth(&t->fish[i]); }
    s_arrival_pending = false; s_spawn_in = -1; s_prev_night = t->night;
    s_ravenous = false; s_ravenous_t = 0; s_gave_up = false;
    for (int i = 0; i < N_FISH_MAX; i++) s_starve_s[i] = 0;
    s_setup_pending = true;          /* a new tank: welcome, names, colours */
    s_newborn = -1;
    s_sd_prev_feedings = 0; s_sd_pending = 0;   /* a fresh ledger (tank_init zeroed the tank's) */
    s_sale_need = 0;
    s_booted = true;
    mark_dirty();
}

void progression_reset(tank_t *t, uint32_t seed) {
    persist_port_erase();
    tank_init(t, seed);
    progression_fresh(t);
    progression_save(t);
}

void progression_set_age(tank_t *t, int idx, float seconds) {
    if (idx < 0 || idx >= t->n_fish) return;
    s_age[idx] = seconds < 0 ? 0 : seconds;
    apply_stage(&t->fish[idx], s_age[idx]);
    apply_growth(&t->fish[idx]);
    mark_dirty();
}

/* a helper's saved record (zeros when the save has none: the defaults) */
static helper_save_t sv_helper(const save_t *sv, int kind) {
    for (int r = 0; r < HELPER_SAVE_MAX; r++) if (sv->helper[r].kind == kind) return sv->helper[r];
    helper_save_t none; memset(&none, 0, sizeof none); return none;
}

/* restore the saved tank into t; false = no usable save (t untouched).
 * *saved_unix gets the save's wall-clock stamp (0 if unknown). */
static bool load_save(tank_t *t, int64_t *saved_unix) {
    save_t sv; memset(&sv, 0, sizeof sv);
    *saved_unix = 0; s_loaded_release = 0; s_loaded_unix = 0;
    size_t got = 0;
    bool loaded = persist_port_load(&sv, sizeof sv, &got) && got >= SAVE_MIN_SIZE && got <= sizeof sv;
    if (!loaded || sv.magic != SAVE_MAGIC || sv.n_fish < 2 || sv.n_fish > N_FISH_MAX) return false;
    *saved_unix = sv.saved_unix; s_loaded_unix = sv.saved_unix;
    s_loaded_release = sv.saved_release;
    t->n_fish = 0;
    for (int i = 0; i < sv.n_fish; i++) {
        fish_save_t *s = &sv.fish[i];
        tank_make_fish(t, i, s->preset % tank_roster_count(), s->sociable, s->bold, (stage_t)(s->stage & 3));
        fish_t *f = &t->fish[i];
        f->trust = s->trust; f->bold0 = s->bold0; f->sociable0 = s->sociable0;
        if (s->species > SP_FISH && s->species < SP_COUNT) {   /* (before the curiosity: the species sets its default) */
            t->n_fish = i + 1;
            tank_set_species(t, i, s->species, s->variant);
        }
        f->hunger = s->hunger; f->energy = s->energy; f->stress = s->stress; f->curiosity = s->curiosity;
        f->eaten = s->eaten; f->eaten_player = s->eaten_player;
        f->ms_bits = s->ms_bits & ~MS_RETIRED_MASK;   /* the shadow milestones, gone with it */
        f->ms_seen = s->ms_seen & f->ms_bits;
        f->rest_dx = s->rest_dx; f->rest_dy = s->rest_dy;
        f->drift_acc = s->drift_acc;
        s_age[i] = s->age_s;
        t->n_fish = i + 1;
        if (s->name[0]) { s->name[FISH_NAME_MAX] = 0; tank_set_name(t, i, s->name); }
        tank_set_look(t, i, s->body, s->accent);              /* zeros keep the preset's */
        f->parent_a = (int8_t)(s->parent_p1[0] - 1); f->parent_b = (int8_t)(s->parent_p1[1] - 1);   /* 0 = none = -1 */
        if (f->parent_a >= sv.n_fish) f->parent_a = -1;
        if (f->parent_b >= sv.n_fish) f->parent_b = -1;
    }
    s_setup_pending = sv.setup_pending != 0;
    s_newborn = sv.newborn_p1 && sv.newborn_p1 <= t->n_fish ? sv.newborn_p1 - 1 : -1;
    t->light_idle_s = sv.light_idle_s ? sv.light_idle_s : LIGHT_IDLE_S;
    t->light_auto = sv.light_auto != 0;                /* (a MANUAL save, the double-tap's: NEVER; its light_manual_off and
                                                          light_tip_seen are read no more, 2026-10-28) */
    tank_screen_set(t, sv.screen_turned != 0);
    t->orient_lock = (sv.orient & 1) != 0; if (t->orient_lock) t->orient_inv = (sv.orient & 2) != 0;
    t->autofeed_off = sv.autofeed_off != 0;
    t->theme = (uint8_t)theme_valid(sv.theme);
    t->light_override = false; t->light_on = true;   /* never restored (2026-09-15): a saved
                                                      * override once froze a tank in permanent day */
    t->feed_spot_x = sv.feed_spot_x; t->player_feedings = sv.player_feedings;
    t->hold_approaches = sv.hold_approaches; t->tank_ms_bits = sv.tank_ms_bits;
    t->tank_ms_seen = sv.tank_ms_seen & t->tank_ms_bits;
    memcpy(t->algae, sv.algae, ALGAE_CELLS);
    t->trims = sv.trims; t->cells_cleaned = sv.cells_cleaned;
    t->sd_balance = sv.sd_balance; t->sd_earned = sv.sd_earned; t->sd_unlocks = sv.sd_unlocks & ((1u << SD_ITEM_COUNT) - 1);
    for (int i = 0; i < N_FISH_MAX; i++) t->sd_paid_fish[i] = i < t->n_fish ? sv.fish[i].sd_paid : 0;
    t->sd_colonies_paid = sv.sd_colonies_paid; t->sd_inches_paid = sv.sd_inches_paid;
    t->algae_colonies = sv.algae_colonies; t->trim_px = sv.trim_px;
    /* the grass, then the plant's leaves once it is bought (a fresh young plant if the save has none) */
    for (int b = 0; b < VEG_BEDS; b++)
        if (sv.veg_h[b][0] > 0) for (int i = 0; i < VEG_FRONDS_MAX; i++) t->veg_h[b][i] = sv.veg_h[b][i];
    if (t->sd_unlocks & SD_ITEM_PLANT) {
        if (sv.veg_h[3][0] > 0) for (int i = 0; i < VEG_FRONDS_MAX; i++) t->veg_h[3][i] = sv.veg_h[3][i];
        else tank_plant_place(t);
    }
    tank_veg_sync(t);
    /* the decor, a record a piece: its spot (clamped to this board's floor), its depth (one it offers), its look */
    for (int r = 0; r < DECOR_SAVE_MAX; r++) {
        const decor_save_t *ds = &sv.decor[r];
        int k = ds->kind1 - 1;
        if (k < 0 || k >= DK_COUNT) continue;              /* an empty record, or a kind this build does not know */
        if (k == DK_AIRSTONE && ds->x > 0) tank_set_bubble_x(t, ds->x);
        decor_restore(t, k, ds->x, ds->z);
        t->decor[k].variant = ds->variant;
        t->decor[k].rgb = ds->rgb & 0xffffff;
        t->decor[k].growth = ds->growth > 0 ? ds->growth : 0;   /* 0 = full (tank_coral_growth, tank_cluster_growth) */
    }
    /* the helpers: each restores itself from its record (zeros when the save has
       none); one that keeps nothing arrives fresh when it is owned */
    for (int k = 0; k < HK_COUNT; k++) {
        if (HELPER[k].load) {
            helper_save_t h = sv_helper(&sv, SVH_OF[k]);
            helper_rec_t r = { h.n, h.m, h.x, h.y, h.amount, h.count };
            HELPER[k].load(t, &r);
        } else if (helper_owned(t, k)) helper_arrive(t, k);
    }
    s_sd_prev_feedings = t->player_feedings;         /* meals before this boot are not back-paid */
    s_sd_pending = 0;
    s_arrival_pending = sv.arrival_pending; s_spawn_in = -1;
    s_sale_need = sv.sale_meals_need > 0 ? sv.sale_meals_need : 0;
    s_prev_night = t->night;
    progression_species_sync(t);                     /* the species' bits from who is here (a save sold down past them) */
    return true;
}

/* A cold boot lives the absence exactly as a deep-sleep wake does
   (2026-09-16). It used to restore the save and pin everyone at hunger 9.6
   after an hour away - "the one offline rule" - while only the deep-sleep
   wake simulated the span; so the morning after a cell died in the night
   (a PMIC power-off, hence a cold boot) came up with no algae, no growth
   and no night credited. Now every boot with a clock and a save is a wake;
   a long night ends in ravenous begging by itself (hunger +0.8/h crosses
   the begging line in progression_tick). Without a clock nothing can be
   simulated and the tank simply resumes. Returns the hours lived through,
   -1 for none (no save, no clock). */
float progression_boot(tank_t *t) {
    return progression_wake(t, clock_port_now_unix());
}

void progression_settings_changed(void) { mark_dirty(); }

void progression_slept(tank_t *t, float seconds) {
    if (seconds <= 0) return;
    tank_tick_sleep(t, seconds);
    for (int i = 0; i < t->n_fish; i++) {     /* and they grew, slowly, in the dark */
        s_age[i] += seconds * SLEEP_GROWTH_FRAC;
        apply_stage(&t->fish[i], s_age[i]); apply_growth(&t->fish[i]);
    }
    /* the tank milestone (2026-09-15, was "first quiet night" - every fish
       resting under the old day/night cycle): one unbroken stretch of device
       sleep as long as a night. The cap keeps a week's absence a single span. */
    if (seconds >= FULL_NIGHT_S) set_tms(t, TMS_FIRST_FULL_NIGHT);
    mark_dirty();
}

float progression_wake(tank_t *t, int64_t now_unix) {
    int64_t saved_unix;
    s_booted = true;
    if (!load_save(t, &saved_unix)) { progression_fresh(t); return -1; }
    float slept = -1;
    if (now_unix > 0 && saved_unix > 0 && now_unix > saved_unix) {
        int64_t span = now_unix - saved_unix;
        if (span > PROGRESSION_SLEEP_CAP_S) span = PROGRESSION_SLEEP_CAP_S;
        progression_slept(t, (float)span);
        slept = span / 3600.0f;
    }
    progression_woke(t);                     /* a fry staged before the sleep: born at the wake */
    return slept;
}

void progression_tick(tank_t *t, float dt) {
    if (!s_booted) return;
    float aged = dt * progression_time_scale;                    /* growth: every awake second, lit or not */
    float tended = t->night ? 0 : aged;                          /* drift: pressure only while lit and lived-in */
    int n_dart = 0; bool changed_someone = false, reef_visit = false;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        s_age[i] += aged;
        apply_stage(f, s_age[i]);
        apply_growth(f);
        /* trait drift, slow: calm + fed -> bolder/more social; stressed -> shyer */
        float k = tended / (DRIFT_HOURS * 3600.0f);          /* full unit per DRIFT_HOURS of pressure */
        float pressure = 0;                                   /* what the traits WANTED to move, before the clamps */
        if (f->stress > 7) { f->bold = clampf(f->bold - k, 0.05f, 0.95f); pressure += k; }
        else if (f->hunger < 4 && f->stress < 2) { f->bold = clampf(f->bold + k * 0.5f, 0.05f, 0.95f); pressure += k * 0.5f; }
        if (f->goal.id == GOAL_FOLLOW_FRIEND) { f->sociable = clampf(f->sociable + k * 0.5f, 0.05f, 0.95f); pressure += k * 0.5f; }
        else if (f->goal.id == GOAL_EXPLORE) { f->sociable = clampf(f->sociable - k * 0.2f, 0.05f, 0.95f); pressure += k * 0.2f; }
        f->drift_acc += pressure;
        if (fabsf(f->bold - f->bold0) >= 0.11f || fabsf(f->sociable - f->sociable0) >= 0.11f) changed_someone = true;

        /* milestones: firsts the fish chose to do */
        if (f->goal.id == GOAL_DART_PLAY && f->goal_age > 1.0f) set_ms(f, MS_FIRST_DART);
        if (f->goal.id == GOAL_VISIT_BUBBLES && tank_dist(f->x, f->y, tank_bubble_x(t), t->bubble_y) < 90) set_ms(f, MS_FIRST_BUBBLES);
        if (f->goal.id == GOAL_INSPECT_REEF) {               /* its landmark: the reef cluster once there is one */
            float rx, ry; tank_reef_spot(t, &rx, &ry);
            if (tank_dist(f->x, f->y, rx, ry) < 90) {
                set_ms(f, MS_INSPECTED);                      /* no badge (0.3.0): it reveals the curiosity slider */
                if (t->sd_unlocks & SD_ITEM_CLUSTER) reef_visit = true;
            }
        }
        /* the seagrass: resting inside a canopy, or tucked into one while
           rattled - the grass as the place to be safe (2026-10-02, in the
           badge slot "first reef" had) */
        if (((f->goal.id == GOAL_REST && f->goal_age > 2.0f) || f->stress >= 5.0f) && tank_in_grass(t, f->x, f->y))
            set_ms(f, MS_FIRST_GRASS);
        if (f->goal.id == GOAL_FOLLOW_FRIEND && f->goal_age > 2.0f) set_ms(f, MS_FIRST_FOLLOW);
        if (f->goal.id == GOAL_DART_PLAY) n_dart++;
    }
    if (n_dart >= 2) set_tms(t, TMS_FIRST_PLAY_SESSION);
    if (changed_someone) set_tms(t, TMS_CHANGED_SOMEONE);
    if (t->player_feedings > 0) set_tms(t, TMS_FIRST_FEEDING);
    /* upkeep milestones + event saves (a chore done deserves to stick) */
    static int32_t s_prev_trims, s_prev_cleaned;
    if (t->trims > 0) set_tms(t, TMS_FIRST_TRIM);
    if (t->algae_colonies > 0) set_tms(t, TMS_FIRST_CLEANING);   /* the first colony wiped away, as the first cut is the first trimming
                                                                    (0.3.2; it was 30 cells, hours of film on a new tank) */
    if ((t->sd_unlocks & SD_ITEM_SHRIMP) && t->school.n >= SHRIMP_MAX) set_tms(t, TMS_FULL_SCHOOL);
    if (reef_visit) set_tms(t, TMS_FIRST_REEF);  /* a fish chose to look at the reef cluster */
    if (t->trims != s_prev_trims || t->cells_cleaned != s_prev_cleaned) {
        s_prev_trims = t->trims; s_prev_cleaned = t->cells_cleaned;
        mark_dirty();
    }
    sd_tick(t);                                  /* the sand dollars owed for all of the above */

    /* ravenous: a starving tank with empty water begs at the surface (tank.c
     * renders the wait; the trickle holds off so the keeper's pellets are the
     * event). Entered at boot after a long absence, or live whenever everyone
     * is starving - waking from device sleep lands here naturally. If nobody
     * comes, after a while the fish give up and the tank feeds itself. */
    int any_food = 0; for (int i = 0; i < MAX_FOOD; i++) any_food |= t->food[i].alive;
    if (!s_ravenous && !any_food && t->n_fish > 0) {
        float mn = 10;
        for (int i = 0; i < t->n_fish; i++) if (t->fish[i].hunger < mn) mn = t->fish[i].hunger;
        if (s_gave_up && (!t->autofeed_off || mn < 8.5f || t->player_feedings != s_gave_feedings0)) s_gave_up = false;
        if (mn >= 8.5f && !s_gave_up) { s_ravenous = true; s_ravenous_t = 0; s_rav_feedings0 = t->player_feedings; }
    }
    if (s_ravenous) {
        if (!any_food) s_ravenous_t += dt;    /* the wait; a dash for live pellets isn't giving up */
        float mx = 0;
        for (int i = 0; i < t->n_fish; i++) if (t->fish[i].hunger > mx) mx = t->fish[i].hunger;
        if (mx < 7.0f) s_ravenous = false;                    /* everyone got a bite */
        else if (s_ravenous_t > RAVENOUS_GIVE_UP_S) {         /* nobody came: back to life */
            s_ravenous = false;
            if (!t->autofeed_off) tank_scatter_food(t, 2);    /* so it doesn't re-trigger at once */
            else { s_gave_up = true; s_gave_feedings0 = t->player_feedings; }   /* AUTO FEED off: nothing falls; they
                                                                 stop begging and go hungry until the keeper feeds */
        }
    }
    /* AUTO FEED off: going hungry costs trust (the constants' note above) */
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        if (!t->autofeed_off || f->hunger < STARVE_HUNGER) { s_starve_s[i] = 0; continue; }
        if (t->night) continue;                               /* dark: they rest; the count holds */
        s_starve_s[i] += dt;
        if (s_starve_s[i] > STARVE_GRACE_S && f->trust > STARVE_TRUST_FLOOR)
            f->trust = fmaxf(STARVE_TRUST_FLOOR, f->trust - STARVE_TRUST_PER_S * dt);
    }
    /* tank.c picks the presentation: empty water = beg at the surface; live
     * pellets = feeding-frenzy dash (real starving fish DART at fresh food) */
    t->ravenous = s_ravenous;
    t->ravenous_fed = s_ravenous && t->player_feedings != s_rav_feedings0;

    /* the courtship tell: one condition shy of an arrival (or one staged),
     * the parents-to-be pair up - tank.c stages the episodes. The pair is
     * the one do_arrival will name (pick_parents). */
    t->courting = false; t->court_a = t->court_b = -1;
    if (t->n_fish < POP_CAP && t->n_fish < N_FISH_MAX && tank_nursery_bed(t) >= 0) {
        /* ... and only with a nursery: a bed tall enough to hide in. Shave
         * every bed and the courting stops until one regrows. */
        int met, total;
        arrival_conditions(t, &met, &total);
        if (s_arrival_pending || met >= total - 1) {
            int a, b;
            pick_parents(t, &a, &b);
            if (a >= 0 && b >= 0) { t->courting = true; t->court_a = (int8_t)a; t->court_b = (int8_t)b; }
        }
    }

    /* light-on: greet (the light no longer brings the fry) */
    bool light_on_edge = s_prev_night && !t->night;
    if (s_prev_night != t->night) mark_dirty();
    s_prev_night = t->night;
    if (light_on_edge) t->greet_timer = 6.0f;
    if (!s_arrival_pending && arrival_earned(t)) { s_arrival_pending = true; mark_dirty(); }

    /* the spawning (progression.h): a staged fry is born on its own, a
     * little after the last gate closed, while the keeper can see it. The
     * wait counts real awake seconds (not progression_time_scale) and pauses
     * under a page; a dance cut short (a page, the grass shaved) starts
     * over with a fresh wait. */
    if (!s_arrival_pending || !t->courting || t->ui_cover) {
        if (t->spawning) s_spawn_in = -1;
        t->spawning = false; t->spawn_danced = 0;
    } else if (!t->spawning) {
        if (s_spawn_in < 0) s_spawn_in = tank_randf(t, SPAWN_WAIT_MIN_S, SPAWN_WAIT_MAX_S);
        if ((s_spawn_in -= dt) <= 0) { s_spawn_in = -1; t->spawning = true; t->spawn_danced = 0; }
    } else if (t->spawn_danced >= SPAWN_DANCE_S) {
        do_arrival(t);                       /* between them, in the fronds */
        tank_court_puff(t, 3);
    }

    /* saves: coalesced event saves + heartbeat */
    s_since_save += dt; if (s_dirty) s_dirty_since += dt;
    if ((s_dirty && s_since_save >= SAVE_MIN_GAP_S) || s_since_save >= SAVE_HEARTBEAT_S)
        progression_save(t);
}

bool progression_save(tank_t *t) {
    save_t sv; memset(&sv, 0, sizeof sv);
    sv.magic = SAVE_MAGIC; sv.saved_unix = clock_port_now_unix(); sv.clock = t->clock;
    sv.saved_release = PT_RELEASE_NUM;
    sv.sale_meals_need = s_sale_need;
    /* light_override / light_on stay zero in the save (2026-09-15) */
    sv.light_idle_s = (uint16_t)t->light_idle_s; sv.light_auto = t->light_auto;
    sv.light_manual_off = 0; sv.light_tip_seen = 0;    /* (retired with the double-tap, 2026-10-28; the layout keeps them) */
    sv.screen_turned = t->screen_turned;
    sv.orient = (uint8_t)(t->orient_lock ? 1 | (t->orient_inv ? 2 : 0) : 0);
    sv.autofeed_off = t->autofeed_off;
    sv.theme = (uint8_t)theme_valid(t->theme);
    sv.arrival_pending = s_arrival_pending;
    sv.n_fish = (uint8_t)t->n_fish;
    sv.feed_spot_x = t->feed_spot_x; sv.player_feedings = t->player_feedings;
    sv.hold_approaches = t->hold_approaches; sv.tank_ms_bits = t->tank_ms_bits;
    sv.tank_ms_seen = t->tank_ms_seen;
    for (int b = 0; b < VEG_BEDS_MAX; b++)
        if (b < VEG_BEDS || (t->sd_unlocks & SD_ITEM_PLANT))
            for (int i = 0; i < VEG_FRONDS_MAX; i++) sv.veg_h[b][i] = t->veg_h[b][i];
    memcpy(sv.algae, t->algae, ALGAE_CELLS);
    sv.trims = t->trims; sv.cells_cleaned = t->cells_cleaned;
    sv.sd_balance = t->sd_balance; sv.sd_earned = t->sd_earned; sv.sd_unlocks = t->sd_unlocks;
    sv.sd_colonies_paid = t->sd_colonies_paid; sv.sd_inches_paid = t->sd_inches_paid;
    sv.algae_colonies = t->algae_colonies; sv.trim_px = t->trim_px;
    for (int k = 0; k < DK_COUNT; k++) {               /* the decor, a record a piece */
        const decor_t *d = &t->decor[k]; decor_save_t *ds = &sv.decor[k];
        ds->kind1 = (uint8_t)(k + 1); ds->z = d->z; ds->variant = d->variant;
        ds->x = k == DK_AIRSTONE ? tank_bubble_x(t) : d->x > 0 ? d->x : 0;
        ds->growth = d->growth; ds->rgb = d->rgb;
    }
    for (int k = 0, r = 0; k < HK_COUNT; k++) {        /* the helpers, a record each that keeps something */
        if (!HELPER[k].save) continue;
        helper_rec_t rec; memset(&rec, 0, sizeof rec);
        HELPER[k].save(t, &rec);
        helper_save_t *h = &sv.helper[r++];
        h->kind = SVH_OF[k]; h->n = rec.n; h->m = rec.m; h->x = rec.x; h->y = rec.y; h->amount = rec.amount; h->count = rec.count;
    }
    sv.setup_pending = s_setup_pending;
    sv.newborn_p1 = (uint8_t)(s_newborn >= 0 && s_newborn < t->n_fish ? s_newborn + 1 : 0);
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i]; fish_save_t *s = &sv.fish[i];
        if (strcmp(f->name, tank_roster_name(f->preset))) memcpy(s->name, f->name, FISH_NAME_MAX + 1);
        s->body = f->color; s->accent = f->accent;            /* the preset's too: harmless, exact */
        s->parent_p1[0] = (uint8_t)(f->parent_a + 1); s->parent_p1[1] = (uint8_t)(f->parent_b + 1);
        s->sd_paid = t->sd_paid_fish[i];
        s->preset = (uint8_t)f->preset; s->stage = (uint8_t)f->stage;
        s->species = f->species; s->variant = f->variant;
        s->size = f->size; s->trust = f->trust; s->bold = f->bold; s->sociable = f->sociable;
        s->bold0 = f->bold0; s->sociable0 = f->sociable0;
        s->hunger = f->hunger; s->energy = f->energy; s->stress = f->stress; s->curiosity = f->curiosity;
        s->age_s = s_age[i]; s->rest_dx = f->rest_dx; s->rest_dy = f->rest_dy;
        s->eaten = f->eaten; s->eaten_player = f->eaten_player; s->ms_bits = f->ms_bits;
        s->ms_seen = f->ms_seen;
        s->drift_acc = f->drift_acc;
    }
    if (!persist_port_save(&sv, sizeof sv)) {
        if (!s_dirty) { s_dirty = true; s_dirty_since = 0; }
        s_since_save = 0;                         /* retry after SAVE_MIN_GAP_S */
        return false;
    }
    s_since_save = 0; s_dirty = false; s_dirty_since = 0;
    return true;
}

void progression_ack_milestones(tank_t *t) {
    bool changed = false;
    for (int i = 0; i < t->n_fish; i++)
        if (t->fish[i].ms_seen != t->fish[i].ms_bits) { t->fish[i].ms_seen = t->fish[i].ms_bits; changed = true; }
    if (t->tank_ms_seen != t->tank_ms_bits) { t->tank_ms_seen = t->tank_ms_bits; changed = true; }
    if (changed) mark_dirty();
}
