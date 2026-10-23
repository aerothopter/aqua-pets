/* The creatures (2026-10-23): every creature's code - the classic fish's
 * roster and the species' table (SPECIES[], a row a kind: what it is, how it
 * moves, its hooks), making, breeding and removing them, and how each one
 * carries out the goal the model picked (update_fish). Moved from tank.c
 * unchanged; tank.c keeps the tank around them (the light, food, bubbles,
 * grass, film, touch) and calls update_fish once a creature a frame. */
#include "tank.h"
#include "tank_internal.h"
#include "tank_events.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ---- roster: six presets (the prototype's four + two), colors chosen for
 * contrast on AMOLED black. A preset is a look + temperament; bold/social are
 * rolled per tank (starting pair) or inherited (arrivals). ---- */
typedef struct {
    const char *name; uint32_t color, fin, accent;
    float size, curiosity, lazy, turn_rate;
} preset_t;
static const preset_t ROSTER[] = {
    /* name    color     fin       accent    size  cur  lazy  turn */
    { "mira", 0x38dcc7, 0x1d9f98, 0xffbd59, 1.08f, 7.5f, 0.2f, 3.2f },
    { "bolt", 0xff725c, 0xb83a43, 0xffe08a, 0.94f, 6.0f, 0.2f, 4.2f },
    { "kelp", 0x78d67d, 0x3f8b55, 0xa799ff, 0.86f, 4.0f, 0.1f, 3.2f },
    { "nori", 0xa799ff, 0x6b5ed6, 0x78d67d, 1.00f, 5.0f, 0.7f, 2.4f },
    { "pip",  0xffd166, 0xc98a1e, 0x38dcc7, 0.90f, 6.5f, 0.3f, 3.8f },
    { "sol",  0xf48fb1, 0xb0456f, 0xffe08a, 1.04f, 5.5f, 0.4f, 2.9f },
};
#define ROSTER_N ((int)(sizeof ROSTER / sizeof ROSTER[0]))
int tank_roster_count(void) { return ROSTER_N; }
const char *tank_roster_name(int preset) { return preset >= 0 && preset < ROSTER_N ? ROSTER[preset].name : "?"; }


/* the keeper's palettes (setup.c): the six roster bodies + a blue and a
 * silver; the roster's five accents + white, the stress red and a dark ink */
const uint32_t LOOK_BODY[LOOK_N]   = { 0x38dcc7, 0xff725c, 0x78d67d, 0xa799ff, 0xffd166, 0xf48fb1, 0x4da3ff, 0xe8f1f2 };
const uint32_t LOOK_ACCENT[LOOK_N] = { 0xffbd59, 0xffe08a, 0xa799ff, 0x78d67d, 0x38dcc7, 0xffffff, 0xf25b65, 0x1a2a30 };

void tank_set_name(tank_t *t, int slot, const char *name) {
    if (slot < 0 || slot >= N_FISH_MAX) return;
    fish_t *f = &t->fish[slot];
    if (!name || !*name) name = (SPECIES[f->species % SP_COUNT].flags & SPF_ROSTER) ? tank_roster_name(f->preset) : SPECIES[f->species % SP_COUNT].names[slot % 4];
    int n = 0;
    /* canonical lowercase (2026-09-15): the roster, the director and the
     * trained names are lowercase, the display uppercases at draw, and the
     * wheel used to write capitals into the slots it spun ("FeZ", "LArRY") */
    while (name[n] && n < FISH_NAME_MAX) {
        char c = name[n];
        f->name[n] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
        n++;
    }
    f->name[n] = 0;
}
static uint32_t fin_for(uint32_t body) {
    for (int i = 0; i < ROSTER_N; i++) if (ROSTER[i].color == body) return ROSTER[i].fin;
    for (int s = 1; s < SP_COUNT; s++)
        for (int v = 0; v < SP_VARIANTS; v++) if (SPECIES[s].var[v].color == body) return SPECIES[s].var[v].fin;
    /* a body colour of the keeper's own: the fin is that body at 58% */
    uint32_t r = (body >> 16 & 255) * 58 / 100, g = (body >> 8 & 255) * 58 / 100, b = (body & 255) * 58 / 100;
    return (r << 16) | (g << 8) | b;
}
void tank_set_look(tank_t *t, int slot, uint32_t body, uint32_t accent) {
    if (slot < 0 || slot >= N_FISH_MAX) return;
    fish_t *f = &t->fish[slot];
    if (body)   { f->color = body; f->fin = fin_for(body); }
    if (accent) f->accent = accent;
    if (f->accent == f->color)                        /* stripes the body's own colour would vanish */
        for (int i = 0; i < LOOK_N; i++) if (LOOK_ACCENT[i] != f->color) { f->accent = LOOK_ACCENT[i]; break; }
}

void tank_fish_face(fish_t *f) {
    f->facing = cosf(f->heading) < 0 ? -1 : 1;
    f->yaw = f->yaw_tail = f->facing; f->behind = 0;
}

void tank_make_fish(tank_t *t, int slot, int preset, float sociable, float bold, stage_t stage) {
    fish_t *f = &t->fish[slot];
    const preset_t *p = &ROSTER[preset];
    f->preset = preset; tank_set_name(t, slot, p->name);
    f->toss = 0; f->toss_vx = f->toss_vy = f->toss_spin = 0;
    f->model_name = TRAINED_NAMES[slot % N_TRAINED_NAMES];   /* names carry no signal */
    f->x = tank_randf(t, TANK_FX0 + 90, TANK_FX1 - 90); f->y = tank_randf(t, 80, TANK_BOT - 90);
    f->heading = tank_randf(t, 0, TAU); tank_fish_face(f);
    f->speed = 0; f->target_speed = 0; f->wander = f->x * 0.05f;
    f->base_size = p->size; f->size = p->size; f->turn_rate = p->turn_rate;
    f->hunger = tank_randf(t, 3, 6); f->energy = tank_randf(t, 5, 8);
    f->stress = tank_randf(t, 0, 2); f->curiosity = p->curiosity;
    f->sociable = sociable; f->bold = bold; f->lazy = p->lazy;
    f->bold0 = bold; f->sociable0 = sociable; f->drift_acc = 0;
    f->stage = stage;
    f->starve_flagged = false;
    f->trust = 5.0f;
    f->goal.id = GOAL_EXPLORE; f->goal.urgency = 3; f->goal.confidence = 1; f->goal.runner_up = GOAL_COUNT;
    f->goal_age = 0; f->ask_age = 99; f->dart_timer = 0; f->dart_x = f->x; f->dart_y = f->y; f->hesitate = 0;
    f->bored = 0; f->goal_prev = GOAL_COUNT; f->zone_last = -1; f->explore_set = false;
    /* its own order of first visits - from the slot, not the tank's RNG, so a
     * seeded run (the selftests) draws the same numbers it did before */
    for (int z = 0; z < 6; z++) f->zone_seen[z] = -(float)((slot * 37 + z * 13) % 60);
    f->eaten = 0; f->eaten_player = 0; f->at_bubbles = false;
    /* its own spot by the reef: bolder fish rest a little further out */
    f->rest_dx = 8 + slot * 24 + bold * 14; f->rest_dy = -slot * 9 - tank_randf(t, 0, 10);   /* a body apart (was 9 px per slot) */
    f->sig = 0xffffffffu; f->ms_bits = 0; f->ms_seen = 0;
    f->color = p->color; f->fin = p->fin; f->accent = p->accent;
    f->parent_a = f->parent_b = -1;
    f->species = SP_FISH; f->variant = 0;
    /* (from the slot, not the tank's RNG: a seeded run stays the run it was) */
    f->puff = 0; f->ink = 0; f->jet = (float)((slot * 29) % 100) / 100.0f; f->camo = 0; f->camo_rgb = 0;
    f->air_s = 60.0f + (float)((slot * 41) % 60); f->spark = 0; f->lure = 0; f->anchor = -1;
    f->sp_mode = SPM_NONE; f->sp_t = 0; f->sp_x = -1;
    f->flourish_wait=45+(slot*37)%80;f->surface_s=0;f->flourish_cycle=0;
}

/* ---- species (docs/species.md) ---- */
static void place_near_reef(tank_t *t, fish_t *f);
const species_def_t *tank_species(const fish_t *f) { return &SPECIES[f->species < SP_COUNT ? f->species : SP_FISH]; }
float tank_species_size(const fish_t *f) {
    if (tank_species(f)->flags & SPF_ROSTER) return ROSTER[f->preset % ROSTER_N].size;
    const species_def_t *s = &SPECIES[f->species];
    /* from the personality it was born with (saved), so a load rebuilds the same size */
    float k = f->bold0 * 7.31f + f->sociable0 * 3.17f;
    k -= floorf(k);
    return s->size_lo + (s->size_hi - s->size_lo) * k;
}
void tank_set_species(tank_t *t, int slot, int species, int variant) {
    if (slot < 0 || slot >= N_FISH_MAX || species <= SP_FISH || species >= SP_COUNT) return;
    fish_t *f = &t->fish[slot];
    const species_def_t *s = &SPECIES[species];
    const sp_variant_t *v = &s->var[(variant % SP_VARIANTS + SP_VARIANTS) % SP_VARIANTS];
    f->species = (uint8_t)species; f->variant = (uint8_t)((variant % SP_VARIANTS + SP_VARIANTS) % SP_VARIANTS);
    f->flourish_wait=45+(slot*37)%80;f->surface_s=0;f->flourish_cycle=0;
    f->base_size = tank_species_size(f); f->size = f->base_size;
    f->turn_rate = s->turn_rate; f->lazy = s->lazy; f->curiosity = s->curiosity;
    f->color = v->color; f->fin = v->fin; f->accent = v->accent;
    tank_set_name(t, slot, NULL);                      /* the species' default name */
}
float tank_fish_hit_r(const fish_t *f) {
    float r = tank_species(f)->hit_r * f->size;
    return r > TANK_HIT_MIN_R ? r : TANK_HIT_MIN_R;
}
int tank_species_n(const tank_t *t, int species) {
    int n = 0;
    for (int i = 0; i < t->n_fish; i++) n += t->fish[i].species == species;
    return n;
}
static float sp_roll(tank_t *t, float lo, float hi) { return tank_randf(t, lo, hi); }
int tank_add_species_n(tank_t *t, int species, int n) {
    if (species <= SP_FISH || species >= SP_COUNT || n < 1 || t->n_fish + n > N_FISH_MAX) return -1;
    const species_def_t *s = &SPECIES[species];
    int first = t->n_fish;
    /* (2026-10-07: the shop sells one at a time; a newcomer contrasts with the LAST of its kind
       already here, so the two that breed are a contrasting pair as the founding fish are) */
    int prev = -1; for (int i = 0; i < t->n_fish; i++) if (t->fish[i].species == species) prev = i;
    for (int k = 0; k < n; k++) {
        int slot = t->n_fish;
        /* a contrasting pair, as the founding fish are: bold apart by a third of the range */
        float bold = sp_roll(t, s->bold_lo, s->bold_hi), soc = sp_roll(t, s->soc_lo, s->soc_hi);
        if (prev >= 0 && fabsf(bold - t->fish[prev].bold) < (s->bold_hi - s->bold_lo) * 0.33f)
            bold = t->fish[prev].bold < (s->bold_lo + s->bold_hi) * 0.5f ? s->bold_hi - 0.02f : s->bold_lo + 0.02f;
        tank_make_fish(t, slot, slot % ROSTER_N, soc, bold, STAGE_JUV);
        tank_set_species(t, slot, species, (int)tank_randf(t, 0, SP_VARIANTS - 0.001f));
        place_near_reef(t, &t->fish[slot]);
        t->fish[slot].hunger = 4; t->fish[slot].trust = 4;
        prev = slot;
        t->n_fish++;
    }
    t->roster_gen++;
    return first;
}
int tank_add_species_pair(tank_t *t, int species) { return tank_add_species_n(t, species, 2); }

static void place_near_reef(tank_t *t, fish_t *f) {
    f->x = t->reef_x + tank_randf(t, -10, 30); f->y = t->reef_y - 30 + tank_randf(t, -10, 10);
    f->heading = tank_randf(t, -0.6f, 0.6f); tank_fish_face(f);
}

void tank_new_population(tank_t *t) {
    /* two random presets */
    int a = (int)tank_randf(t, 0, ROSTER_N - 0.001f);
    int b = (int)tank_randf(t, 0, ROSTER_N - 1.001f); if (b >= a) b++;
    /* personalities rolled with a guaranteed contrast so the pair reads as
     * two characters at a glance (docs/progression-next.md, Act 1) */
    float ba, bb, sa, sb;
    do { ba = tank_randf(t, 0.1f, 0.9f); bb = tank_randf(t, 0.1f, 0.9f); } while (fabsf(ba - bb) < 0.45f);
    do { sa = tank_randf(t, 0.1f, 0.9f); sb = tank_randf(t, 0.1f, 0.9f); } while (fabsf(sa - sb) < 0.3f);
    /* both start as FRY: the founding pair grows up on the keeper's watch
     * (progression halved the stage clocks to match - 0.5/3/24 tended hours) */
    tank_make_fish(t, 0, a, sa, ba, STAGE_FRY);
    tank_make_fish(t, 1, b, sb, bb, STAGE_FRY);
    t->n_fish = 2;
}

int tank_add_fish(tank_t *t, int parent_a, int parent_b) {
    if (t->n_fish >= N_FISH_MAX) return -1;
    /* a preset no fish is using, while there is one (a tank past six repeats them) */
    int used[ROSTER_N] = {0};
    for (int i = 0; i < t->n_fish; i++) used[t->fish[i].preset % ROSTER_N] = 1;
    int free_n = 0, free_idx[ROSTER_N];
    for (int i = 0; i < ROSTER_N; i++) if (!used[i]) free_idx[free_n++] = i;
    int preset = free_n ? free_idx[(int)tank_randf(t, 0, free_n - 0.001f)] : (int)tank_randf(t, 0, ROSTER_N - 0.001f);
    int ia = (parent_a >= 0 && parent_a < t->n_fish) ? parent_a : 0;
    int ib = (parent_b >= 0 && parent_b < t->n_fish) ? parent_b : (t->n_fish > 1 ? 1 : 0);
    const fish_t *pa = &t->fish[ia], *pb = &t->fish[ib];
    /* the species is the parents' (breeding is within a species: pick_parents);
       a classic fish's fry may hatch as a new species, the rare surprise */
    int species = pa->species < SP_COUNT ? pa->species : SP_FISH;
    if (SPECIES[species].mutate_p > 0 && tank_randf(t, 0, 1) < SPECIES[species].mutate_p) species = 1 + (int)tank_randf(t, 0, SP_COUNT - 1.001f);
    bool mutant = species != pa->species;
    const species_def_t *sd = &SPECIES[species];
    bool roster = (sd->flags & SPF_ROSTER) != 0;          /* (the classic fish: the whole range, as the founders') */
    float blo = !roster ? sd->bold_lo - 0.1f : 0.05f, bhi = !roster ? sd->bold_hi + 0.1f : 0.95f;
    float slo = !roster ? sd->soc_lo - 0.1f : 0.05f, shi = !roster ? sd->soc_hi + 0.1f : 0.95f;
    float bold = mutant ? tank_randf(t, sd->bold_lo, sd->bold_hi)
                        : clampf((pa->bold + pb->bold) * 0.5f + tank_randf(t, -0.15f, 0.15f), fmaxf(blo, 0.05f), fminf(bhi, 0.95f));
    float soc  = mutant ? tank_randf(t, sd->soc_lo, sd->soc_hi)
                        : clampf((pa->sociable + pb->sociable) * 0.5f + tank_randf(t, -0.15f, 0.15f), fmaxf(slo, 0.05f), fminf(shi, 0.95f));
    int slot = t->n_fish;
    tank_make_fish(t, slot, preset, soc, bold, STAGE_FRY);
    /* its look is the family's, not the preset's (2026-09-14): the body from
       one parent, the markings from the other - a coin decides which is
       which; tank_set_look keeps the markings off a matching body */
    if (tank_randf(t, 0, 1) < 0.5f) { int x = ia; ia = ib; ib = x; pa = &t->fish[ia]; pb = &t->fish[ib]; }
    if (roster) tank_set_look(t, slot, pa->color, pb->accent);
    else {
        /* a design: one parent's (80 %) or a new one; a mutant's is its own */
        int variant = !mutant && tank_randf(t, 0, 1) < 0.8f ? pa->variant : (int)tank_randf(t, 0, SP_VARIANTS - 0.001f);
        tank_set_species(t, slot, species, variant);
        if (!mutant && variant == pa->variant) {     /* the family's colours, the classic fish's rule */
            t->fish[slot].color = pa->color; t->fish[slot].fin = pa->fin;
            t->fish[slot].accent = pb->species == species ? pb->accent : pa->accent;
            if (t->fish[slot].accent == t->fish[slot].color) t->fish[slot].accent = pa->accent;
        }
    }
    t->fish[slot].parent_a = (int8_t)ia; t->fish[slot].parent_b = (int8_t)ib;
    place_near_reef(t, &t->fish[slot]);
    t->fish[slot].hunger = 4; t->fish[slot].trust = 4;
    t->n_fish++;
    return slot;
}

bool tank_remove_fish(tank_t *t, int idx) {
    if (idx < 0 || idx >= t->n_fish) return false;
    for (int i = idx; i < t->n_fish - 1; i++) {
        t->fish[i] = t->fish[i + 1];
        t->sd_paid_fish[i] = t->sd_paid_fish[i + 1];
        /* its spot by the reef is its slot's (tank_make_fish): a slot nearer,
           so the next arrival's spot is free again */
        t->fish[i].rest_dx -= 24; t->fish[i].rest_dy += 9;
    }
    t->n_fish--;
    t->sd_paid_fish[t->n_fish] = 0;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        if (f->parent_a == idx) f->parent_a = -1; else if (f->parent_a > idx) f->parent_a--;
        if (f->parent_b == idx) f->parent_b = -1; else if (f->parent_b > idx) f->parent_b--;
    }
    t->courting = false; t->court_a = t->court_b = -1; t->court_active = 0;   /* progression picks the pair again */
    t->spawning = false; t->spawn_danced = 0;
    t->stage_fish = -1;
    t->roster_gen++;
    return true;
}

/* the courtship circle, shared by the tell's episodes and the spawning: the
 * pair weaves a low loop through the nursery on opposite sides - performed
 * rather than printed (reflex presentation; the advisor still owns both
 * goals). Down in the fronds, not mid-water: Strato (2026-09-04) - by the
 * reef it read as two friends at the bubbles. */
static void court_steer(const tank_t *t, const fish_t *f, int idx, float *desired, float *speed) {
    float ph = t->clock * 1.7f + (idx == t->court_b ? 3.14159f : 0);
    float cx, cy, rx; court_site(t, &cx, &cy, &rx);
    float wx = cx + cosf(ph) * rx, wy = cy + sinf(ph) * 6;
    float to = atan2f(wy - f->y, wx - f->x);
    *desired = norm_ang(*desired + norm_ang(to - *desired) * 0.85f);
    *speed = tank_dist(f->x, f->y, cx, cy) > 90 ? 46 : 30;
}

/* the schema's 3 x 2 zone grid (advisor_core.c encodes the same), 0..5 */
static int zone_of(float x, float y) {
    int col = (int)(x / (TANK_W / 3.0f)); if (col > 2) col = 2; if (col < 0) col = 0;
    int row = (int)(y / (TANK_BOT / 2.0f)); if (row > 1) row = 1; if (row < 0) row = 0;
    return row * 3 + col;
}

/* ---- the species' motion (2026-10-05, docs/species.md "How they move") ----
 * The model still picks every creature's goal; this is the reflex layer
 * carrying the goal out the way the animal would - its own targets, speeds,
 * posture and way of moving - plus the species' own reflexes (a startle's
 * puff, ink and spark, the eel's breath), the way the greeting and the famine
 * override the steering above. Every creature comes through here, the classic
 * fish too (2026-10-23): its row's flee, dance and swim are its own, and the
 * species' hooks are empty for it, so nothing here changes what it does or
 * draws on the tank's RNG for it.
 * The heading / facing convention is fish_t's (tank.h, THE CONVENTION). */
static const sp_motion_t *sp_mo(const fish_t *f) { return &SPECIES[f->species < SP_COUNT ? f->species : SP_FISH].mo; }
static float wrap01(float v) { return v - floorf(v); }
/* a fixed 0..1 from two ints: the species' small randomness (a crab's step,
 * the eel's next breath) without a draw on the tank's RNG */
static float sp_hash01(int a, int b) {
    uint32_t h = (uint32_t)a * 0x9E3779B1u ^ (uint32_t)b * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    return (float)(h & 0xffff) / 65535.0f;
}
float tank_body_half_len(const fish_t *f) { return 21.0f * f->size * sp_mo(f)->len_k; }
float tank_walk_off(const fish_t *f) { return sp_mo(f)->walk_off * f->size; }
/* the body's axis: the travel's climb or dive, the head on the facing side */
static float body_axis(const fish_t *f) {
    int fc = f->facing ? f->facing : (cosf(f->heading) < 0 ? -1 : 1);
    return atan2f(sinf(f->heading), fc * fabsf(cosf(f->heading)));
}
void tank_fish_head(const fish_t *f, float *hx, float *hy) {
    const species_def_t *s = tank_species(f);
    if (s->flags & SPF_UPRIGHT) { *hx = f->x + f->facing * 5 * f->size; *hy = f->y - 13 * f->size; return; }   /* upright: the snout up top */
    if (s->mo.head_k == 0) { *hx = f->x; *hy = f->y; return; }   /* the mouth mid-body (the classic fish, the octopus, the squid, ...) */
    float a = body_axis(f), r = tank_body_half_len(f) * sp_mo(f)->head_k;
    *hx = f->x + cosf(a) * r; *hy = f->y + sinf(a) * r;
}
bool tank_fish_backward(const fish_t *f) {
    return f->species < SP_COUNT && (SPECIES[f->species].flags & SPF_BACKS) && f->speed > 1 && cosf(f->heading) * f->facing < -0.1f;
}
bool tank_fish_jetting(const fish_t *f) { return f->species < SP_COUNT && (SPECIES[f->species].flags & SPF_JETS) && f->sp_mode == SPM_JET; }
/* the ground a walker stands on: the sand line, raised by a piece it can
 * climb (the reef cluster's rock: decor.c ground_cluster) */
float tank_ground_y(const tank_t *t, float x) { return decor_ground(t, x, TANK_BOT - 16); }
/* the floor a walker may use at height y: inside the glass by its half length */
static void sp_floor_span(const fish_t *f, float y, float *lo, float *hi) {
    float hl = tank_body_half_len(f);
    *lo = fmaxf(tank_glass_x0(y), TANK_FX0) + hl + 4; *hi = fminf(tank_glass_x1(y), TANK_FX1) - hl - 4;
    if (*lo > *hi) *lo = *hi = TANK_W * 0.5f;
}
/* what an octopus sits on, for its camouflage: a piece under it (decor.c's
 * camo rows, the renderer's own colours mirrored), the grass, or the floor's
 * middle pebble tone (render.c floor_rgb) */
#define SP_FLOOR_RGB 0x22301f
#define SP_GRASS_RGB 0x3f7a34
static uint32_t sp_under_rgb(const tank_t *t, float x, float y) {
    uint32_t rgb;
    if (decor_camo(t, x, &rgb)) return rgb;
    if (tank_in_grass(t, x, y)) return SP_GRASS_RGB;
    return SP_FLOOR_RGB;
}
/* a den: at the first piece that digs one (decor.c's den rows: under the edge
 * of the reef cluster's rock, a castle tower's foot, the wreck's holes, the
 * coral's foot), or the reef bed's foot. The octopus, the crab and the lobster
 * rest in one (two of a kind take either side) */
static void sp_den(const tank_t *t, const fish_t *f, int idx, float *x, float *y) {
    float side = (idx & 1) ? 1.0f : -1.0f, x0;
    if (!decor_den_x(t, side, &x0)) x0 = t->reef_x - 10 + side * 6;
    x0 += SPECIES[f->species].den_dx * side;           /* not on top of each other */
    float lo, hi; sp_floor_span(f, TANK_BOT - 20, &lo, &hi);
    *x = clampf(x0, lo, hi); *y = tank_ground_y(t, *x) - tank_walk_off(f);
}
/* ---- the seahorse's frond: the stem at mid-height, swaying as render.c
 * draws it (draw_veg's sway, amp 4 and its per-bed seeds) ---- */
static const int VEG_SWAY_SEED[VEG_BEDS_MAX] = { 0, 7, 3, 5 };
static bool sp_frond_pt(const tank_t *t, int anchor, float *x, float *y) {
    int b = anchor >> 4, i = anchor & 15;
    if (anchor < 0 || b >= tank_veg_beds(t) || tank_veg_kind(t, b) != VEG_KIND_GRASS) return false;
    int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
    if (i >= n) return false;
    float bx; int segs = tank_veg_frond(t, b, i, &bx);
    if (segs < 2) segs = 2;
    int seg = segs / 2, sd = VEG_SWAY_SEED[b];
    float sway = sinf(t->clock * 0.9f + (i + sd) * 1.7f) * 4.0f;
    *x = bx + sway * seg / (float)segs * sinf(seg * 0.4f + t->clock * 0.6f + i + sd);
    *y = TANK_BOT - 16 - seg * VEG_SEG_PX;
    if (*y > TANK_BOT - 16 - 24) *y = TANK_BOT - 16 - 24;            /* a cropped frond: as high as the tail can reach */
    return true;
}
/* the nearest grass frond no other seahorse holds, within maxd; a frond
 * standing ~40 px or more is preferred to a cropped one */
static int sp_pick_frond(const tank_t *t, int idx, float maxd) {
    const fish_t *f = &t->fish[idx];
    int best = -1; bool best_tall = false; float bd = maxd;
    for (int b = 0; b < tank_veg_beds(t); b++) {
        if (tank_veg_kind(t, b) != VEG_KIND_GRASS) continue;
        int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
        for (int i = 0; i < n; i++) {
            int a = b * 16 + i; bool taken = false;
            for (int k = 0; k < t->n_fish; k++) taken |= k != idx && t->fish[k].anchor == a;
            float fx, fy; if (taken || !sp_frond_pt(t, a, &fx, &fy)) continue;
            bool tall = tank_veg_frond(t, b, i, NULL) >= 12;
            float d = tank_dist(f->x, f->y, fx, fy);
            if (d >= maxd) continue;
            if ((tall && !best_tall) || (tall == best_tall && d < bd)) { best = a; bd = d; best_tall = tall; }
        }
    }
    return best;
}
/* the pellet a floor walker goes for: one on the floor, the nearest along it;
 * else the nearest in the water - it walks under it and waits for it to land */
static int sp_floor_food(const tank_t *t, const fish_t *f) {
    int best = -1, any = -1; float bd = 1e9f, ad = 1e9f;
    for (int i = 0; i < MAX_FOOD; i++) {
        const food_t *p = &t->food[i];
        if (!p->alive) continue;
        float d = fabsf(p->x - f->x);
        if (p->y >= FOOD_FLOOR_Y - 1 && d < bd) { bd = d; best = i; }
        if (d < ad) { ad = d; any = i; }
    }
    return best >= 0 ? best : any;
}
static int sp_nearest_kin(const tank_t *t, int idx) {
    const fish_t *f = &t->fish[idx]; int best = -1; float bd = 1e9f;
    for (int i = 0; i < t->n_fish; i++) {
        if (i == idx || t->fish[i].species != f->species) continue;
        float d = tank_dist(f->x, f->y, t->fish[i].x, t->fish[i].y);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

#define INK_SQUIRT_S 4.5f   /* seconds a spontaneous ink cloud lasts (render.c scales the cloud to it) */
/* Small idle displays, independent of the advisor and theme. No save-layout changes.
 * Pause the countdown for care/rest/interaction, and abort a trip for an urgent need. */
static void sp_flourish(tank_t *t,int idx,fish_t *f,float dt) {
    uint32_t fl=SPECIES[f->species].flags;
    bool visitor=(fl&SPF_VISITS)!=0;
    bool effect=(fl&(SPF_INKS|SPF_SPARKS))!=0;
    if(!visitor&&!effect)return;
    bool busy=t->night||t->startled||t->hold_active||idx==t->stage_fish||
              (t->spawning&&(idx==t->court_a||idx==t->court_b))||
              f->hunger>6.5f||f->energy<2.5f||f->goal.id==GOAL_FLEE_SHADOW||f->goal.id==GOAL_REST||
              (f->goal.id==GOAL_SEEK_FOOD&&tank_nearest_food(t,f,0)>=0);
    if(busy){f->surface_s=0;return;}
    if(f->surface_s>0){f->surface_s-=dt;if(f->surface_s<=0)f->surface_s=-18;return;}
    if(f->surface_s<0){f->surface_s=fminf(0,f->surface_s+dt);return;}
    if(f->sp_mode!=SPM_NONE||f->ink>0||f->spark>0)return;
    if((f->flourish_wait-=dt)>0)return;
    bool inker=(fl&SPF_INKS)!=0;
    /* the inkers squirt more often than the others surface (2026-10-10: every 50-120 s of idle day,
       was 100-210; the octopus makes every third idle turn (the first of them) a trip to the surface, was every other) */
    f->flourish_wait=(inker?50+70*sp_hash01(idx+f->species*17,++f->flourish_cycle):100+110*sp_hash01(idx+f->species*17,++f->flourish_cycle));
    if(fl&SPF_SPARKS){f->spark=1.3f;return;}
    if((fl&SPF_INKS)&&!((fl&SPF_VISITS)&&f->flourish_cycle%3==1)){   /* an inker that also visits (the octopus): a trip first, then two squirts */
        /* a squirt of its own: a fuller cloud than the startle's (INK_SQUIRT_S of it, the startle's is 2.5)
           left where it was as the jet carries it off */
        f->ink=INK_SQUIRT_S;f->sp_mode=SPM_JET;f->sp_t=2.5f;f->jet=0;return;
    }
    f->surface_s=40;
    if(fl&SPF_PUFFS)f->sp_t=35;
}

/* a double tap ON a creature (the touch ports, 2026-10-10): its party trick, with none of a
 * startle's stress or lost trust - the pufferfish puffs up, the squid and the octopus squirt a
 * black cloud and jet off. True when the creature has one (the port then keeps its card down);
 * for the rest a double tap is just the card up and down again. */
static void puffer_poke(fish_t *f) { f->sp_t = 5.0f; }                                       /* a ball, held ~5 s */
static void inker_poke(fish_t *f)  { f->ink = INK_SQUIRT_S; f->sp_mode = SPM_JET; f->sp_t = 2.5f; f->jet = 0; }
bool tank_poke(tank_t *t, int idx) {
    if (idx < 0 || idx >= t->n_fish) return false;
    fish_t *f = &t->fish[idx];
    if (f->species >= SP_COUNT || !SPECIES[f->species].poke) return false;
    tank_handled(t); SPECIES[f->species].poke(f);
    return true;
}

/* ---- per frame: the species' own state - its puff and ink, the eel's
 * breath, the angler's lure, the octopus's colour ---- */
static void puffer_state(tank_t *t, int idx, fish_t *f, float dt) {     /* it gulps water: a ball in a quarter of a second, held, then let out slowly (~8 s) */
    (void)t; (void)idx;
    if (f->sp_t > 0) { f->sp_t -= dt; f->puff = fminf(1, f->puff + dt * 4); }
    else f->puff = fmaxf(0, f->puff - dt * 0.12f);
}
static void jelly_state(tank_t *t, int idx, fish_t *f, float dt) {      /* the hurry after a startle runs out on its own */
    (void)t; (void)idx;
    if (f->sp_t > 0) f->sp_t = fmaxf(0, f->sp_t - dt);
}
static void jet_state(tank_t *t, int idx, fish_t *f, float dt) {        /* the squid's; the octopus's starts the same */
    (void)t; (void)idx;
    if (f->goal.id == GOAL_DART_PLAY && f->sp_mode != SPM_JET) { f->sp_mode = SPM_JET; f->sp_t = 0; f->jet = 0; }
    if (f->sp_mode == SPM_JET && (f->sp_t -= dt) <= 0 && f->goal.id != GOAL_DART_PLAY) { f->sp_mode = SPM_NONE; f->sp_t = 0; }
}
static void octopus_state(tank_t *t, int idx, fish_t *f, float dt) {
    jet_state(t, idx, f, dt);
    /* camouflage: still on the bottom, its skin takes on what it sits on
     * (~1.5 s, the real thing is quicker still); moving, it fades back */
    bool still = f->speed < 4 && f->y >= tank_ground_y(t, f->x) - tank_walk_off(f) - 3;
    if (still) { f->camo = fminf(1, f->camo + dt / 1.5f); f->camo_rgb = sp_under_rgb(t, f->x, f->y); }
    else if (f->speed > 6) f->camo = fmaxf(0, f->camo - dt * 1.2f);
}
static void seahorse_state(tank_t *t, int idx, fish_t *f, float dt) {
    (void)idx;
    if (f->sp_mode == SPM_CLING && !t->startled) f->sp_mode = SPM_NONE;
    if (f->goal.id != GOAL_REST && f->sp_mode != SPM_CLING) f->anchor = -1;      /* lets go of its frond */
    if (f->sp_t > 0) { if ((f->sp_t -= dt) <= 0) f->sp_t = -0.9f; }             /* the snick, then a beat to swallow */
    else if (f->sp_t < 0) f->sp_t = fminf(0, f->sp_t + dt);
}
static void angler_state(tank_t *t, int idx, fish_t *f, float dt) {
    (void)idx;
    if (f->sp_x < 0) f->sp_x = f->x;
    if (f->sp_mode == SPM_LUNGE && (f->sp_t -= dt) <= 0) { f->sp_mode = SPM_NONE; f->sp_t = -1.5f; f->sp_x = f->x; }
    /* the lure: a slow pulse, brighter in the dark and while it hunts */
    float hunt = f->goal.id == GOAL_SEEK_FOOD && tank_nearest_food(t, f, 0) >= 0 ? 0.3f : 0;
    float want = clampf(0.35f + (t->night ? 0.4f : 0) + hunt + 0.12f * sinf(t->clock * 1.7f + f->wander), 0, 1);
    f->lure += (want - f->lure) * clampf(dt * 2, 0, 1);
}
static void breather_state(tank_t *t, int idx, fish_t *f, float dt) {   /* air-breathers (the turtle since 2026-10-11): up for a gulp every minute or two, whatever they were doing */
    if (f->sp_mode < SPM_RISE || f->sp_mode > SPM_SINK) {
        if ((f->air_s -= dt) <= 0) { f->sp_mode = SPM_RISE; f->air_s = 0; }
    } else {
        f->air_s -= dt;                                   /* the trip's seconds, negative */
        float hx, hy; tank_fish_head(f, &hx, &hy);
        if (f->sp_mode == SPM_RISE && (hy - tank_glass_top(hx) < 22 || f->air_s < -30)) { f->sp_mode = SPM_GULP; f->sp_t = 1.2f; }
        else if (f->sp_mode == SPM_GULP && (f->sp_t -= dt) <= 0) { f->sp_mode = SPM_SINK; f->sp_t = 3.0f; }
        else if (f->sp_mode == SPM_SINK && (f->sp_t -= dt) <= 0) { f->sp_mode = SPM_NONE; f->sp_t = 0; f->air_s = 60 + 60 * sp_hash01(idx, (int)t->clock); }
    }
}
static void crab_state(tank_t *t, int idx, fish_t *f, float dt) {       /* claws up for a moment - the threat display - then the scuttle */
    (void)t; (void)idx;
    if (f->sp_mode == SPM_CLAWS) {
        f->puff = fminf(1, f->puff + dt * 6);
        if ((f->sp_t -= dt) <= 0) { f->sp_mode = SPM_SCUTTLE; f->sp_t = 1.4f; }
    } else {
        f->puff = fmaxf(0, f->puff - dt);
        if (f->sp_mode == SPM_SCUTTLE && (f->sp_t -= dt) <= 0) { f->sp_mode = SPM_NONE; f->sp_t = 0; }
    }
}
static void sp_state(tank_t *t, int idx, fish_t *f, float dt) {
    if (f->ink > 0) f->ink = fmaxf(0, f->ink - dt);
    if (f->spark > 0) f->spark = fmaxf(0, f->spark - dt);
    if (SPECIES[f->species].state) SPECIES[f->species].state(t, idx, f, dt);
}

/* the triple tap reaches a creature (tank_startle): its own reaction starts */
static void puffer_startle(tank_t *t, int idx, fish_t *f, float x, float y) { (void)t; (void)idx; (void)x; (void)y; f->sp_t = 5.0f; }   /* held ~5 s */
static void inker_startle(tank_t *t, int idx, fish_t *f, float x, float y)  { (void)t; (void)idx; (void)x; (void)y; f->ink = 2.5f; f->sp_mode = SPM_JET; f->sp_t = 2.0f; f->jet = 0; }
static void eel_startle(tank_t *t, int idx, fish_t *f, float x, float y)    { (void)t; (void)idx; (void)x; (void)y; f->spark = 0.6f; }   /* harmless, at this size */
static void seahorse_startle(tank_t *t, int idx, fish_t *f, float x, float y) {   /* grips the nearest stem, if one is near */
    (void)x; (void)y;
    if (f->anchor < 0) f->anchor = (int8_t)sp_pick_frond(t, idx, 45);
    if (f->anchor >= 0) f->sp_mode = SPM_CLING;
}
static void crab_startle(tank_t *t, int idx, fish_t *f, float x, float y)   { (void)t; (void)idx; (void)x; (void)y; if (f->sp_mode == SPM_NONE) { f->sp_mode = SPM_CLAWS; f->sp_t = 0.6f; } }
static void jelly_startle(tank_t *t, int idx, fish_t *f, float x, float y)  { (void)t; (void)idx; (void)x; (void)y; f->sp_t = 4.0f; }   /* a hurried pulse for a few seconds: its whole alarm */
static void lobster_startle(tank_t *t, int idx, fish_t *f, float x, float y) {   /* faces the threat and shoots backward from it */
    (void)t; (void)idx; (void)y;
    if (f->sp_mode != SPM_FLIP) {
        f->facing = x > f->x ? 1 : -1; f->yaw = f->yaw_tail = f->facing;
        f->sp_mode = SPM_FLIP; f->sp_t = 0; f->jet = 0;
    }
}
void sp_startle_hit(tank_t *t, int idx, float x, float y) {
    fish_t *f = &t->fish[idx];
    if (f->species < SP_COUNT && SPECIES[f->species].startle) SPECIES[f->species].startle(t, idx, f, x, y);
}
/* while the tank is spooked: how each one gets away (speeds in its own px/s) */
static void seahorse_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v) {   /* no bolt: it freezes - or, a stem in reach, goes to cling to it */
    if (f->anchor >= 0) { float fx, fy; if (sp_frond_pt(t, f->anchor, &fx, &fy)) { *away = atan2f(fy - 10 * f->size - f->y, fx - f->facing * 5 * f->size - f->x); *w = 1; *v = 10; return; } }
    *w = 0; *v = 0;
}
static void puffer_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v)    { (void)t; (void)f; (void)away; *w = 0.9f; *v = 6; }   /* a ball can only drift */
static void inker_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v)     { (void)t; (void)away; *w = 0.95f; *v = lerpf(65, 95, f->bold) * SPECIES[f->species].burst_k; }
static void eel_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v)       { (void)t; (void)away; *w = 0.7f; *v = lerpf(40, 60, f->bold); }
static void shark_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v)     { (void)t; (void)f; (void)away; (void)v; *w = 0.3f; }   /* hardly bothered: a mild turn away, its own pace */
static void swordfish_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v) { (void)t; (void)away; *w = 0.5f; *v = lerpf(70, 100, f->bold) * SPECIES[f->species].burst_k; }   /* a flash of speed away */
static void turtle_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v)    { (void)t; (void)away; *w = 0.6f; *v = lerpf(26, 40, f->bold) * SPECIES[f->species].burst_k; }   /* a few hard strokes, then it settles (2026-10-11) */
static void angler_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v)    { (void)t; (void)f; (void)away; *w = 0.6f; *v = 14; }
static void crab_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v) {
    *away = f->x >= t->startle_x ? 0 : 3.14159f; *w = 1;
    *v = f->sp_mode == SPM_SCUTTLE ? 60 * SPECIES[f->species].burst_k : f->sp_mode == SPM_CLAWS ? 0 : 14;
}
static void lobster_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v)   { *away = f->x >= t->startle_x ? 0 : 3.14159f; *w = 1; *v = 10; }
static void jelly_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v)     { (void)t; (void)f; (void)away; *w = 0.6f; *v = 22; }   /* it pulses away, no faster than a hurried drift */
static void sp_startle_steer(const tank_t *t, const fish_t *f, float *desired, float *speed) {
    float away = atan2f(f->y - t->startle_y, f->x - t->startle_x), w = 0.85f, v = -1;
    if (SPECIES[f->species].flee) SPECIES[f->species].flee(t, f, &away, &w, &v);
    if (w > 0) *desired = norm_ang(*desired + norm_ang(away - *desired) * w);
    *speed = v;
}
/* the eel's trip for air: straight up to the glass, a gulp there, back down */
static void sp_breath(const tank_t *t, const fish_t *f, target_t *tg) {
    float top = tank_glass_top(f->x);
    if (f->sp_mode == SPM_RISE)      { tg->x = f->x + f->facing * 10; tg->y = top + 10; tg->speed = 40; }
    else if (f->sp_mode == SPM_GULP) { tg->x = f->x + f->facing * 4;  tg->y = top + 10; tg->speed = 3; }
    else                             { tg->x = f->x + f->facing * 30; tg->y = TANK_BOT * 0.6f; tg->speed = 15; }
    tg->valid = true;
}
/* the courtship, species by species (the fish's loop is court_steer): a
 * target to swim, so each moves its own way. Seahorses face each other and
 * rise together, then sink back - the pair's dance (theirs is at dawn; here
 * it is whenever the pair is ready). Hammerheads keep moving: two lanes past
 * the nursery. The floor's creatures and the octopus simply meet there. */
static void seahorse_court(const tank_t *t, const fish_t *f, bool b, float cx, float cy, float rx, target_t *tg) {
    (void)f; (void)rx; tg->x = cx + (b ? 9 : -9); tg->y = cy - 26 * (0.5f - 0.5f * cosf(t->clock * 0.5f)); tg->speed = 12;
}
static void cruiser_court(const tank_t *t, const fish_t *f, bool b, float cx, float cy, float rx, target_t *tg) {
    (void)f; tg->x = cx + cosf(t->clock * 0.3f) * (rx + 60); tg->y = cy - 30 - (b ? 0 : 16); tg->speed = 22;
}
static void floor_court(const tank_t *t, const fish_t *f, bool b, float cx, float cy, float rx, target_t *tg) {
    (void)cy; (void)rx; tg->x = cx + (b ? 14 : -14); tg->y = tank_ground_y(t, tg->x) - tank_walk_off(f); tg->speed = 10;
}
static void sp_court(const tank_t *t, fish_t *f, int idx, target_t *tg) {
    if (f->sp_mode != SPM_CLING) f->anchor = -1;               /* a seahorse lets go of its frond to dance */
    float cx, cy, rx; court_site(t, &cx, &cy, &rx);
    bool b = idx == t->court_b;
    if (SPECIES[f->species].court) SPECIES[f->species].court(t, f, b, cx, cy, rx, tg);
    else { float ph = t->clock * 1.1f + (b ? 3.14159f : 0);
           tg->x = cx + cosf(ph) * rx; tg->y = cy + sinf(ph) * 6; tg->speed = 18; }
    if (tank_dist(f->x, f->y, cx, cy) > 90) tg->speed *= 1.5f;
    tg->valid = true;
}

/* ---- the goal's target, the species' way (target_for_goal calls it with
 * the fish's target already made; glance = the hesitation's look, which
 * must change nothing) ---- */
static void jelly_target(tank_t *t, int idx, fish_t *f, goal_id_t goal, bool glance, target_t *tg, const sp_aim_t *aim) {
    (void)t; (void)idx; (void)glance; (void)aim;
    if(goal==GOAL_REST){tg->x=TANK_W*.5f+f->rest_dx;tg->y=TANK_BOT*.55f;tg->speed=3;}
    else if(goal==GOAL_EXPLORE){tg->y=clampf(tg->y,tank_glass_top(tg->x)+60,TANK_BOT-75);tg->speed=fminf(tg->speed,16);}
}
static void seahorse_target(tank_t *t, int idx, fish_t *f, goal_id_t goal, bool glance, target_t *tg, const sp_aim_t *aim) {
    if (goal == GOAL_REST && !glance) {
        /* rest = the tail round a stem: the nearest frond, its middle */
        if (f->anchor < 0) f->anchor = (int8_t)sp_pick_frond(t, idx, 1e9f);
        float fx, fy;
        if (f->anchor >= 0 && sp_frond_pt(t, f->anchor, &fx, &fy)) { tg->x = fx - f->facing * 5 * f->size; tg->y = fy - 10 * f->size; tg->speed = fmaxf(tg->speed, 5); }
    } else if (goal == GOAL_SEEK_FOOD && tg->valid) {
        /* a slow approach, then the snick: the snout flicks forward and sucks it in */
        if (!glance && f->sp_t == 0 && tank_dist(aim->hx, aim->hy, aim->food_x, aim->food_y) < 24 * f->size) f->sp_t = 0.25f;
        if (f->sp_t > 0) tg->speed = 60;
    } else if (goal != GOAL_REST) {
        /* a seahorse keeps a small home range and works it up and down -
           along the stems, through the water column - more than across:
           the way across is taken a part at a time */
        tg->x = f->x + (tg->x - f->x) * 0.4f;
    }
}
static void octopus_target(tank_t *t, int idx, fish_t *f, goal_id_t goal, bool glance, target_t *tg, const sp_aim_t *aim) {
    (void)glance; (void)aim;
    if (goal == GOAL_REST) { sp_den(t, f, idx, &tg->x, &tg->y); tg->speed = fmaxf(tg->speed, 6); }
    else if (goal == GOAL_EXPLORE) tg->y = tank_ground_y(t, tg->x) - tank_walk_off(f);
    else if (goal == GOAL_INSPECT_REEF) {         /* over the rock, arm by arm */
        float rx, ry; tank_reef_spot(t, &rx, &ry);
        tg->x = rx + 50 * sinf(t->clock * 0.12f + idx * 1.9f); tg->y = tank_ground_y(t, tg->x) - tank_walk_off(f); tg->speed = 7;
    }
}
static void squid_target(tank_t *t, int idx, fish_t *f, goal_id_t goal, bool glance, target_t *tg, const sp_aim_t *aim) {
    (void)glance; (void)aim;
    if (goal == GOAL_REST) { tg->x = t->reef_x + 70 + f->rest_dx; tg->y = TANK_BOT - 85 + f->rest_dy * 0.5f; tg->speed = 6; }   /* hovers, low */
    else if (goal == GOAL_FOLLOW_FRIEND && tg->valid) {
        /* a little school: station beside its own kind, a body apart, the same way up */
        int j = sp_nearest_kin(t, idx);
        if (j >= 0) {
            const fish_t *o = &t->fish[j];
            tg->x = o->x + (idx < j ? -32 : 32) * f->size; tg->y = o->y + 3;
            tg->speed = clampf(tank_dist(f->x, f->y, tg->x, tg->y) * 0.6f, 0, 30);
        }
    }
}
static void angler_target(tank_t *t, int idx, fish_t *f, goal_id_t goal, bool glance, target_t *tg, const sp_aim_t *aim) {
    /* an ambush hunter: low in the tank and still; it lunges only at what
     * comes into reach, and the lure does the work */
    if (goal == GOAL_SEEK_FOOD && tg->valid) {
        if (!glance && f->sp_mode != SPM_LUNGE && f->sp_t >= 0 && tank_dist(aim->hx, aim->hy, aim->food_x, aim->food_y) < 55 * f->size) { f->sp_mode = SPM_LUNGE; f->sp_t = 0.45f; }
        if (f->sp_mode == SPM_LUNGE) tg->speed = 60 * SPECIES[f->species].burst_k;
        else { tg->y = fmaxf(tg->y, aim->low); tg->speed = 5; }          /* creeps under it, waits for it to sink into reach */
        return;
    }
    float home = f->sp_x >= 0 ? f->sp_x : f->x;
    if (goal == GOAL_EXPLORE)            { tg->x = clampf(tg->x, home - 50, home + 50); tg->speed = 7; }   /* a short walk on its fins */
    else if (goal == GOAL_REST)          tg->x = home;
    else if (goal == GOAL_VISIT_BUBBLES) { tg->x = tank_bubble_x(t) + ((idx & 1) ? 34 : -34); tg->y = TANK_BOT - 50; }
    else if (goal == GOAL_INSPECT_REEF)  { float rx, ry; tank_reef_spot(t, &rx, &ry); tg->x = rx + ((idx & 1) ? 46 : -46); tg->y = TANK_BOT - 45; }
    else if (goal == GOAL_DART_PLAY)     tg->speed = lerpf(65, 95, f->bold) * 0.4f;
    if (goal == GOAL_EXPLORE || goal == GOAL_REST) tg->y = tank_ground_y(t, tg->x) - tank_walk_off(f);
    tg->y = fmaxf(tg->y, aim->low);
}
static void breather_target(tank_t *t, int idx, fish_t *f, goal_id_t goal, bool glance, target_t *tg, const sp_aim_t *aim) {
    (void)idx; (void)glance; (void)aim;
    if (goal == GOAL_REST) {                       /* stretched out along the floor (the turtle naps there too, 2026-10-11) */
        float lo, hi; sp_floor_span(f, TANK_BOT - 30, &lo, &hi);
        tg->x = clampf(t->reef_x + 70 + f->rest_dx, lo, hi); tg->y = TANK_BOT - 16 - tank_walk_off(f); tg->speed = fmaxf(tg->speed, 5);
    }
}
static void cruiser_target(tank_t *t, int idx, fish_t *f, goal_id_t goal, bool glance, target_t *tg, const sp_aim_t *aim) {
    (void)t; (void)idx; (void)glance; (void)aim;
    if (goal == GOAL_REST) {                       /* rest is a slow patrol: end to end along a lane, turning wide */
        float lo = tank_glass_x0(f->y) + 70, hi = tank_glass_x1(f->y) - 70;
        tg->x = f->facing > 0 ? (f->x > hi - 50 ? lo : hi) : (f->x < lo + 50 ? hi : lo);
        tg->y = TANK_BOT * 0.6f + f->rest_dy * 0.3f; tg->speed = SPECIES[f->species].min_speed * 1.1f;
    }
    if (goal != GOAL_SEEK_FOOD) tg->y = clampf(tg->y, TANK_BOT * 0.25f, TANK_BOT * 0.75f);   /* it cruises mid-water */
}
/* the floor's two: every goal is a spot on the ground (dart_v: the fastest it plays) */
static void floor_target(tank_t *t, int idx, fish_t *f, goal_id_t goal, target_t *tg, const sp_aim_t *aim, float dart_v) {
    if (goal == GOAL_SEEK_FOOD) { int i = sp_floor_food(t, f); if (i >= 0) { tg->x = t->food[i].x - (aim->hx - f->x); tg->valid = true; tg->speed = fmaxf(tg->speed, 8); } }
    else if (goal == GOAL_REST) sp_den(t, f, idx, &tg->x, &tg->y);
    else if (goal == GOAL_VISIT_BUBBLES) tg->x = tank_bubble_x(t) + ((idx & 1) ? 26 : -26);
    else if (goal == GOAL_INSPECT_REEF) { float rx, ry; tank_reef_spot(t, &rx, &ry); tg->x = rx + 40 * sinf(t->clock * 0.1f + idx * 2.3f); }
    else if (goal == GOAL_DART_PLAY) tg->speed = fminf(tg->speed, dart_v);
    tg->y = tank_ground_y(t, tg->x) - tank_walk_off(f);
}
static void crab_target(tank_t *t, int idx, fish_t *f, goal_id_t goal, bool glance, target_t *tg, const sp_aim_t *aim)    { (void)glance; floor_target(t, idx, f, goal, tg, aim, 60); }
static void lobster_target(tank_t *t, int idx, fish_t *f, goal_id_t goal, bool glance, target_t *tg, const sp_aim_t *aim) { (void)glance; floor_target(t, idx, f, goal, tg, aim, 20); }
static void sp_target(tank_t *t, int idx, goal_id_t goal, bool glance, target_t *tg) {
    fish_t *f = &t->fish[idx];
    const species_def_t *s = &SPECIES[f->species];
    tg->speed *= (goal == GOAL_DART_PLAY || goal == GOAL_FLEE_SHADOW) ? s->burst_k : s->speed_k;
    sp_aim_t aim = { .low = TANK_BOT * (2 / 3.0f), .food_x = tg->x, .food_y = tg->y };
    tank_fish_head(f, &aim.hx, &aim.hy);
    if (goal == GOAL_SEEK_FOOD && tg->valid) { tg->x -= aim.hx - f->x; tg->y -= aim.hy - f->y; }   /* the mouth to the pellet */
    if (s->target) s->target(t, idx, f, goal, glance, tg, &aim);
}

/* ---- the ways of moving ---- */
/* stop-and-go: true while stepping. sp_t > 0 = the step's seconds left, < 0 = the pause's */
static bool sp_gait(const tank_t *t, fish_t *f, int idx, float dt, float w0, float w1, float p0, float p1) {
    if (f->sp_t > 0) {
        if ((f->sp_t -= dt) <= 0) f->sp_t = -(p0 + (p1 - p0) * sp_hash01(idx * 7 + 1, (int)(t->clock * 8)));
        return f->sp_t > 0;
    }
    if ((f->sp_t += dt) >= 0) f->sp_t = w0 + (w1 - w0) * sp_hash01(idx * 7 + 2, (int)(t->clock * 8));
    return f->sp_t > 0;
}
/* on the ground: the crab sideways, the lobster and the walking angler head
 * first, the octopus on its arms. tx is where it goes; with use_dir (a
 * reflex steering it) only the way desired points along the floor counts */
static void sp_walk(tank_t *t, int idx, fish_t *f, float tx, float want, bool use_dir, float desired, float dt) {
    const sp_motion_t *mo = sp_mo(f);
    const species_def_t *s = &SPECIES[f->species];
    bool crab = (s->flags & SPF_SIDEWAYS) != 0;
    float lo, hi; sp_floor_span(f, f->y, &lo, &hi);
    tx = clampf(tx, lo, hi);
    float c = cosf(desired), dx = tx - f->x;
    int dir = use_dir ? (c > 0.15f ? 1 : c < -0.15f ? -1 : 0) : (dx > 3 ? 1 : dx < -3 ? -1 : 0);
    float v = dir ? want : 0;
    if (dir && !use_dir) v *= clampf(fabsf(dx) / 15, 0.35f, 1);
    if (dir && f->sp_mode == SPM_NONE && s->gait.k > 0) {
        /* a crab's stop-and-go (quicker steps to keep its pace); the angler's
         * few fin-steps and a long sit */
        bool step = sp_gait(t, f, idx, dt, s->gait.w0, s->gait.w1, s->gait.p0, s->gait.p1);
        v = step ? v * s->gait.k : 0;
    }
    if (!f->facing) f->facing = 1;
    float brake = 1;
    if (crab) {                                        /* face-on: facing only says which way it is going */
        if (dir) f->facing = (int8_t)dir;
        f->yaw += clampf(-f->yaw, -mo->yaw_rate * dt, mo->yaw_rate * dt);
    } else {                                           /* head first: it turns round on the spot */
        if (dir && dir != f->facing) f->facing = (int8_t)dir;
        f->yaw += clampf(f->facing - f->yaw, -mo->yaw_rate * dt, mo->yaw_rate * dt);
        brake = 1 - mo->brake * (1 - fabsf(f->yaw));
    }
    f->yaw_tail += (f->yaw - f->yaw_tail) * clampf(dt * 6, 0, 1);
    f->speed = lerpf(f->speed, v, clampf(dt * mo->resp, 0, 1));
    float sgn = crab ? f->facing : (f->yaw >= 0 ? 1.0f : -1.0f);
    float mv = sgn * f->speed * brake * dt;
    f->x = clampf(f->x + mv, lo, hi);
    float gy = tank_ground_y(t, f->x) - tank_walk_off(f);
    if (f->y < gy - 0.5f) f->y = fminf(gy, f->y + 45 * dt);   /* off the ground (a jet, a flip): it sinks back down to it */
    else f->y = gy;                                       /* up the rock, down the far side */
    float slope = (tank_ground_y(t, f->x + 3) - tank_ground_y(t, f->x - 3)) / 6;
    f->heading = atan2f(slope * sgn, sgn);
    if (s->walk_step > 0) f->jet = wrap01(f->jet + fabsf(mv) / s->walk_step + (s->walk_idle > 0 ? dt * s->walk_idle : 0));
}
/* the lobster's escape: three strokes of the tail, each curling the abdomen
 * under (jet > 0.5: the thrust) and opening it again (the glide), shooting it
 * backward - away from the way it faces - and a little up; then it sinks
 * back to the floor and walks on */
#define FLIP_STROKE_S 0.3f
#define FLIP_STROKES  3
static void sp_flip(fish_t *f, float dt) {
    f->sp_t += dt;
    int k = (int)(f->sp_t / FLIP_STROKE_S);
    if (k >= FLIP_STROKES) { f->sp_mode = SPM_NONE; f->sp_t = 0; f->jet = 0; f->speed *= 0.3f; return; }
    f->jet = f->sp_t / FLIP_STROKE_S - k;
    float peak = 70 * SPECIES[f->species].burst_k;
    if (f->jet > 0.5f) f->speed += (peak - f->speed) * clampf(dt * 14, 0, 1);
    else f->speed *= 1 - clampf(dt * 3, 0, 1);
    f->heading = atan2f(-0.35f, -(float)f->facing);
    f->x += cosf(f->heading) * f->speed * dt; f->y += sinf(f->heading) * f->speed * dt;
}
/* the anchored seahorse: its tail round the stem, swaying with it. false =
 * not there yet (it swims on) */
static bool sp_hold(const tank_t *t, fish_t *f, float dt) {
    float fx, fy;
    if (!sp_frond_pt(t, f->anchor, &fx, &fy)) { f->anchor = -1; return false; }
    float ax = fx - f->facing * 5 * f->size, ay = fy - 10 * f->size;
    if (tank_dist(f->x, f->y, ax, ay) > 10) return false;
    float k = clampf(dt * 5, 0, 1);
    f->x += (ax - f->x) * k; f->y += (ay - f->y) * k;
    f->speed = 0;
    f->yaw += clampf(f->facing - f->yaw, -sp_mo(f)->yaw_rate * dt, sp_mo(f)->yaw_rate * dt);
    f->yaw_tail += (f->yaw - f->yaw_tail) * clampf(dt * 6, 0, 1);
    f->jet = wrap01(f->jet + dt * 0.8f);               /* the dorsal fin idles */
    return true;
}
/* the hammerhead: never slower than min_speed (it breathes by swimming), and
 * its turns are wide - a slow committed YAW (into the glass and round, ~3 s
 * for a U-turn) begun a body and a couple of seconds' swim short of the glass,
 * never a hover. Climbs and dives stay gentle. */
static void sp_cruise(fish_t *f, float desired, float want, float dt) {
    const species_def_t *s = &SPECIES[f->species];
    const sp_motion_t *mo = sp_mo(f);
    if (!f->facing) f->facing = cosf(f->heading) < 0 ? -1 : 1;
    f->speed = fmaxf(s->min_speed, lerpf(f->speed, fmaxf(want, s->min_speed), clampf(dt * mo->resp, 0, 1)));
    float ahead = f->x + f->facing * (tank_body_half_len(f) + f->speed * 2.5f);
    bool wall = ahead < tank_glass_x0(f->y) + 8 || ahead > tank_glass_x1(f->y) - 8;
    bool behind = cosf(desired) * f->facing < -0.15f;
    if (f->behind < 0) f->behind = fminf(0, f->behind + dt);        /* a turn under way is finished first */
    else if (wall || behind) {
        f->behind += dt;
        if (wall || f->behind > 0.8f) { f->facing = (int8_t)-f->facing; f->behind = -2.5f; }
    } else f->behind = 0;
    f->yaw += clampf(f->facing - f->yaw, -mo->yaw_rate * dt, mo->yaw_rate * dt);
    f->yaw_tail += (f->yaw - f->yaw_tail) * clampf(dt * 3, 0, 1);
    float lim = f->goal.id == GOAL_SEEK_FOOD ? 0.9f : 0.45f;
    float pitch = atan2f(sinf(f->heading), fabsf(cosf(f->heading)));
    float want_p = clampf(atan2f(sinf(desired) * s->vert_k, fabsf(cosf(desired))), -lim, lim);
    pitch += clampf(want_p - pitch, -0.5f * dt, 0.5f * dt);
    float vx = f->yaw * cosf(pitch), vy = sinf(pitch);
    f->heading = atan2f(vy, vx);
    f->x += vx * f->speed * dt; f->y += vy * f->speed * dt;
    f->jet = wrap01(f->jet + dt * (0.5f + f->speed / 40));          /* the tail beat */
}
/* everyone else swims free: the seahorse, the puffer, the squid, the eel, the
 * angler off the floor and the octopus off the bottom. No committed U-turn:
 * a turn is a pivot - the head swings round at the species' yaw rate and the
 * swim brakes while it does (the puffer and the seahorse all but stop: a turn
 * on the spot). A goal close behind is backed up to (the eel, the puffer,
 * the squid). A jet (octopus, squid) travels mantle first, the arms - facing -
 * trailing, in pulses: thrust while the phase is < 0.3, then the glide. */
static void sp_swim(tank_t *t, int idx, fish_t *f, const target_t *tg, float desired, float want, bool steer_only, float dt) {
    const sp_motion_t *mo = sp_mo(f);
    const species_def_t *s = &SPECIES[f->species];
    bool jetting = tank_fish_jetting(f);
    bool snap = ((s->flags & SPF_UPRIGHT) && f->sp_t > 0) || f->sp_mode == SPM_LUNGE;
    if (!f->facing) f->facing = cosf(f->heading) < 0 ? -1 : 1;
    float dist = tg->valid ? tank_dist(f->x, f->y, tg->x, tg->y) : 999;
    float c = cosf(desired);
    bool behind = c * f->facing < -0.2f;
    bool rev = !jetting && !steer_only && mo->rev_px > 0 && behind && dist < mo->rev_px;
    int8_t was = f->facing;
    /* a courting seahorse at its place in the dance faces its partner and
       drifts up and down beside it, whichever way that is */
    const fish_t *mate = (s->flags & SPF_UPRIGHT) && (t->spawning || t->court_active > 0) && dist < 16 &&
                         (idx == t->court_a || idx == t->court_b) ? &t->fish[idx == t->court_a ? t->court_b : t->court_a] : NULL;
    if (mate) { f->facing = mate->x > f->x ? 1 : -1; rev = true; }
    else if (jetting) { if (c * f->facing > 0.2f) f->facing = (int8_t)-f->facing; }
    else if (behind && !rev) f->facing = (int8_t)-f->facing;
    if (f->facing != was) f->heading = norm_ang(3.14159f - f->heading);   /* the way turns with it, its climb kept */
    float side = jetting ? -1.0f : 1.0f;
    if (!rev) { float sd = side * f->facing; if (c * sd < 0) desired = atan2f(sinf(desired), sd * fabsf(c)); }
    float rate = f->turn_rate * (jetting ? 1.5f : 1.0f) * dt;
    f->heading = norm_ang(f->heading + clampf(norm_ang(desired - f->heading), -rate, rate));
    if ((s->flags & SPF_BREATHES) && f->goal.id == GOAL_REST && dist < 12)   /* lying flat along the floor */
        f->heading = norm_ang(f->heading + norm_ang((f->facing > 0 ? 0 : 3.14159f) - f->heading) * clampf(dt * 2, 0, 1));
    f->yaw += clampf(f->facing - f->yaw, -mo->yaw_rate * dt, mo->yaw_rate * dt);
    f->yaw_tail += (f->yaw - f->yaw_tail) * clampf(dt * 8, 0, 1);
    float pivot = 1 - mo->brake * (1 - fabsf(f->yaw));
    if (rev) want = fminf(want * 0.6f, 12);
    if (!steer_only && !jetting && !snap && tg->valid) want *= clampf(dist / 25, 0, 1);   /* it can stop dead at its spot */
    if ((s->flags & SPF_PUFFS) && f->surface_s<=0) want *= 1 - 0.8f * f->puff;                          /* a ball barely moves */
    if (!(s->flags & SPF_UPRIGHT))
        for (int b = 0; b < tank_veg_beds(t); b++) if (veg_inside(t, b, f->x, f->y)) { want *= VEG_SLOW; break; }
    float jelly_hz=0;                                          /* the jellyfish's pulse: rest slows it, flight hurries it */
    if(s->flags & SPF_PULSES) {
        bool hurried = f->sp_t > 0 || f->goal.id == GOAL_FLEE_SHADOW;   /* a startle (sp_startle_hit), or a flight */
        jelly_hz = hurried ? 1.3f : f->goal.id == GOAL_REST ? .18f : .6f;
        f->jet=wrap01(f->jet+dt*jelly_hz);
        float thrust=.5f+.5f*sinf(f->jet*TAU);
        f->speed=lerpf(f->speed,want*(.35f+thrust*.95f),clampf(dt*3,0,1));
    } else if (jetting) {
        f->jet = wrap01(f->jet + dt * 1.25f);                                       /* ~1.25 pulses a second */
        if (f->jet < 0.3f) f->speed += (want * 1.25f - f->speed) * clampf(dt * 9, 0, 1);
        else f->speed *= 1 - clampf(dt * 2.6f, 0, 1);
    } else {
        f->speed = lerpf(f->speed, want, clampf(dt * (snap ? 12 : mo->resp), 0, 1));
        f->jet = wrap01(f->jet + dt * (s->fin_div > 0 ? s->fin_hz + f->speed / s->fin_div : s->fin_hz));
    }
    float hc = cosf(f->heading), hs = sinf(f->heading);
    float vx = rev ? hc : side * f->yaw * fabsf(hc);
    float bob = (s->flags & SPF_PULSES) ? -sinf(f->jet*TAU)*jelly_hz*23   /* the contraction lifts it ~4 px, it settles as the bell opens */
              : s->bob_amp > 0 ? sinf(t->clock * s->bob_hz + f->wander) * s->bob_amp : 0;
    f->x += vx * f->speed * pivot * dt;
    f->y += hs * f->speed * pivot * dt + bob * dt;
}
static bool floor_walking(const tank_t *t, const fish_t *f) { (void)t; (void)f; return true; }   /* the crab, the lobster: always */
static bool octopus_walking(const tank_t *t, const fish_t *f) {
    goal_id_t g = f->goal.id;
    if (tank_fish_jetting(f)) return false;
    if (g == GOAL_EXPLORE || g == GOAL_REST || g == GOAL_INSPECT_REEF) return true;
    if (g == GOAL_SEEK_FOOD) { int i = tank_nearest_food(t, f, 0); return i >= 0 && t->food[i].y > TANK_BOT - 70; }
    return false;
}
static bool angler_walking(const tank_t *t, const fish_t *f) {
    (void)t; goal_id_t g = f->goal.id;
    return f->sp_mode != SPM_LUNGE && (g == GOAL_EXPLORE || g == GOAL_REST);
}
static bool sp_walking(const tank_t *t, const fish_t *f) {
    return SPECIES[f->species].walking && SPECIES[f->species].walking(t, f);
}
/* the whole body inside the glass: its head and its tail brought in (a long
 * eel or shark along the side glass, the bowl's curve, the watch's corners),
 * then the middle */
static void sp_clamp_body(fish_t *f) {
    const species_def_t *s = &SPECIES[f->species];
    float hl = tank_body_half_len(f) * 0.9f, th = (s->clamp_th > 0 ? s->clamp_th : 8) * f->size, ax, ay;
    if (s->flags & (SPF_UPRIGHT | SPF_PULSES)) { ax = 0; ay = 1; }     /* upright, or a bell: its length is up and down */
    else if (s->flags & SPF_SIDEWAYS) { ax = 1; ay = 0; }
    else { float a = body_axis(f); ax = cosf(a); ay = sinf(a); }
    for (int pass = 0; pass < 2; pass++) {             /* twice: in the bowl, bringing one end in can take the other out */
        for (int k = -1; k <= 1; k += 2) {
            float px = f->x + ax * hl * k, py = f->y + ay * hl * k, qx = px, qy = py;
            tank_glass_clamp(&qx, &qy, th);
            f->x += qx - px; f->y += qy - py;
        }
        tank_glass_clamp(&f->x, &f->y, th);
    }
}
static void sp_bounds(fish_t *f, bool walker) {
    float y0 = f->y;
    sp_clamp_body(f);
    float dy = f->y - y0;
    if (!walker && !(SPECIES[f->species].flags & SPF_LEVEL) && fabsf(dy) > 0.01f && dy * sinf(f->heading) < 0) {
        f->heading = -f->heading;                      /* off the top or the floor: the climb turned back ... */
        sp_clamp_body(f);                              /* ... and the body with it */
    }
}
/* a species' own way of moving, when it has one this frame: the lobster's flip, the seahorse
   holding its frond, the hammerhead's cruise (always) */
static bool lobster_move(tank_t *t, fish_t *f, float desired, float want, bool override, float dt) {
    (void)t; (void)desired; (void)want; (void)override;
    if (f->sp_mode != SPM_FLIP) return false;
    sp_flip(f, dt); return true;
}
static bool seahorse_move(tank_t *t, fish_t *f, float desired, float want, bool override, float dt) {
    (void)desired; (void)want;
    return f->anchor >= 0 && ((f->goal.id == GOAL_REST && !override) || f->sp_mode == SPM_CLING) && sp_hold(t, f, dt);
}
static bool shark_move(tank_t *t, fish_t *f, float desired, float want, bool override, float dt) {
    (void)t; (void)override;
    sp_cruise(f, desired, want, dt); return true;
}
static void sp_move(tank_t *t, int idx, fish_t *f, const target_t *tg, float desired, float want, bool override, float dt) {
    bool walker = false;
    const species_def_t *s = &SPECIES[f->species];
    if (f->surface_s!=0) sp_swim(t,idx,f,tg,desired,want,override,dt);
    else if (s->move && s->move(t, f, desired, want, override, dt)) {}
    else if ((walker = sp_walking(t, f))) sp_walk(t, idx, f, tg->x, want, override, desired, dt);
    else sp_swim(t, idx, f, tg, desired, want, override, dt);
    sp_bounds(f, walker);
}




/* ---- the creatures' own ways where the classic fish and the species part
 * (2026-10-23: the classic fish is a row too). A species' row leaves these
 * empty and gets the species' way; the fish's row brings its own. ---- */
/* the startle: the fish bolts straight away from the tap, at its own pace */
static void fish_flee(const tank_t *t, const fish_t *f, float *away, float *w, float *v) { (void)t; (void)away; (void)w; *v = lerpf(55, 90, f->bold); }
/* the courtship's steering: the fish's circle (court_steer), or a species' place in the dance (sp_court) */
static void fish_dance(const tank_t *t, fish_t *f, int idx, float vk, target_t *tg, float *desired, float *speed) {
    (void)vk; (void)tg; court_steer(t, f, idx, desired, speed);
}
static void species_dance(const tank_t *t, fish_t *f, int idx, float vk, target_t *tg, float *desired, float *speed) {
    (void)speed; sp_court(t, f, idx, tg); *desired = atan2f((tg->y - f->y) * vk, tg->x - f->x);
}
/* the move, once steered: a species' own (sp_move, at its pace) */
static void species_swim(tank_t *t, int idx, fish_t *f, const target_t *tg, float desired, float touch_speed, float hes_speed, bool sp_units, bool agile, float dt) {
    (void)agile;
    if (touch_speed > 0 && !sp_units) touch_speed *= SPECIES[f->species].speed_k;   /* the reflexes' fish speeds, at its pace */
    bool override = touch_speed >= 0 || hes_speed >= 0;
    float want = touch_speed >= 0 ? touch_speed : hes_speed >= 0 ? hes_speed : tg->speed * (0.8f + f->goal.urgency * 0.05f);
    if (f->energy < 1.2f) want *= 0.45f;
    sp_move(t, idx, f, tg, desired, want, override, dt);
}
/* ... and the classic fish's: committed U-turns, the yaw and the tail beat, the glass */
static void fish_swim(tank_t *t, int idx, fish_t *f, const target_t *tg, float desired, float touch_speed, float hes_speed, bool sp_units, bool agile, float dt) {
    (void)idx; (void)sp_units;
    /* urgency scales cruise speed a touch (0..9 → 0.8..1.25) */
    float ugain = 0.8f + f->goal.urgency * 0.05f;
    /* a U-turn is a YAW (2026-09-29): a fish whose way lies behind it no longer
     * loops over through vertical and rolls flat - it turns round where it is,
     * toward the glass. (A YouTube viewer on the old roll: "that looks bad".)
     * It swims on one SIDE (facing) and changes side only by a committed turn:
     * the way must stay behind it for a moment - ~2 s hovering (3 at its
     * spot), a third of a second cruising, at once when it matters (a dash,
     * a startle, darting play) - and a turn is followed by ~1.3 s of settling. Until it commits
     * it glances back (a part turn) and eases off, steering on its own side:
     * the way mirrored across vertical, its climb or dive kept. Strato, the
     * same night, on the first cut (a turn the moment the way fell behind):
     * "too much time in a vertical position or flip flopping back and forth" -
     * 21 flips a fish-minute, 34 at the bubble column, where the lane swings
     * side to side every ~5 s. */
    const float BEHIND_BAND  = 0.15f;    /* |cos| past vertical before the way is "behind" */
    const float TURN_SETTLE  = 1.3f;     /* s after a turn before the next can begin */
    if (!f->facing) f->facing = cosf(f->heading) < 0 ? -1 : 1;
    bool urgent = agile || (t->ravenous && f->hunger > 6.5f) || (t->startled && touch_speed >= 55);
    bool behind = cosf(desired) * f->facing < -BEHIND_BAND;
    if (behind) f->behind += dt;
    else f->behind = f->behind > 0 ? 0 : fminf(0, f->behind + dt);
    /* hanging about its spot (the goal's point within ~30 px) a fish is in
     * no hurry: the lane swinging past it is a glance, not a turn */
    bool at_spot = tg->valid && tank_dist(f->x, f->y, tg->x, tg->y) < 30;
    float commit = urgent ? 0 : lerpf(2.0f, 0.3f, clampf((f->speed - 10) * (1 / 25.0f), 0, 1)) + (at_spot ? 1.2f : 0);
    if (behind && f->behind >= commit) {
        f->facing = (int8_t)-f->facing;
        f->heading = norm_ang(3.14159f - f->heading);   /* the climb or dive kept */
        f->behind = urgent ? 0 : -TURN_SETTLE;
    }
    bool glancing = f->behind > 0;       /* the way is behind, the turn not yet committed */
    if (cosf(desired) * f->facing < 0)   /* on its own side until it turns */
        desired = atan2f(sinf(desired), f->facing * fabsf(cosf(desired)));
    float turn = clampf(norm_ang(desired - f->heading), -f->turn_rate * dt, f->turn_rate * dt);
    f->heading = norm_ang(f->heading + turn);
    /* the yaw follows the side: a full turn in ~0.5 s at mira's turn rate
     * (bolt 0.4, nori 0.7), quicker with urgency; a glance is 40% of one.
     * The tail follows the head a beat behind */
    float hc = cosf(f->heading);
    float face = f->facing * (glancing ? 0.6f : 1.0f);
    float yrate = f->turn_rate * 1.2f * (1 + f->goal.urgency * 0.06f) * dt;
    f->yaw += clampf(face - f->yaw, -yrate, yrate);
    f->yaw_tail += (f->yaw - f->yaw_tail) * clampf(dt * 10, 0, 1);
    float want = touch_speed >= 0 ? touch_speed : hes_speed >= 0 ? hes_speed : tg->speed * ugain;
    f->target_speed = want * (f->energy < 1.2f ? 0.45f : 1) * (glancing ? 0.6f : 1);
    /* swimming through a canopy is slow going (a fleeing fish crashes through) */
    if (f->goal.id != GOAL_FLEE_SHADOW)
        for (int b = 0; b < tank_veg_beds(t); b++)
            if (veg_inside(t, b, f->x, f->y)) { f->target_speed *= VEG_SLOW; break; }
    f->speed = lerpf(f->speed, f->target_speed, clampf(dt * 2.6f, 0, 1));
    /* the tail beat's phase (2026-10-12, Alvin: the Blackwater creatures' movement "not smooth"): it used to be
       clock x (8 + speed x .055) in the drawer, so every change of speed JUMPED the phase by clock x .055 x the
       change - a random tail flick on every acceleration, worse the longer the tank had run. Integrated here
       instead, at the same rate, so the beat only quickens and slows */
    f->jet = wrap01(f->jet + dt * (8 + f->speed * 0.055f) * (1.0f / TAU));
    /* the swim follows the facing: side-on it is the heading's own, and as
     * the fish turns head-on its level travel crosses zero and it brakes to
     * 40%. Only a LEVEL turn: steep, the heading steers as it always did (a
     * diver's small corrections at the pellet don't wait on a flip) */
    float ahc = fabsf(hc), w = clampf((ahc - 0.2f) * (1 / 0.4f), 0, 1);
    float brake = 1 - 0.6f * (1 - fabsf(f->yaw)) * ahc * w;
    f->x += (hc + (f->yaw * ahc - hc) * w) * f->speed * brake * dt;
    f->y += sinf(f->heading) * f->speed * brake * dt + sinf(t->clock * 1.4f + f->wander) * dt * 1.7f;

    /* hard bounds. The side glass mirrors a heading only while it points
     * INTO the glass: the facing lags the mirror, and a second bounce on the
     * next frame would send the fish back into the glass */
    float m = 15 * f->size;
#ifdef TANK_ROUND                                          /* the bowl's glass: back onto the circle m inside it, the heading mirrored
                                                              in the glass while it points out, the body turned to swim back in */
    { float dx = f->x - TANK_RAD, dy = f->y - TANK_RAD, d = sqrtf(dx * dx + dy * dy), lim = TANK_RAD - m;
      if (d > lim && d > 1) {
          float nx = dx / d, ny = dy / d;
          f->x = TANK_RAD + nx * lim; f->y = TANK_RAD + ny * lim;
          float hx = cosf(f->heading), hy = sinf(f->heading), out = hx * nx + hy * ny;
          if (out > 0) {
              hx -= 2 * out * nx; hy -= 2 * out * ny; f->heading = atan2f(hy, hx);
              if (nx < -0.5f && f->facing < 0) { f->facing = 1; f->behind = -TURN_SETTLE; }
              if (nx > 0.5f && f->facing > 0)  { f->facing = -1; f->behind = -TURN_SETTLE; }
          }
      }
      if (f->y > TANK_BOT - m) { f->y = TANK_BOT - m; f->heading = -f->heading; } }
#else
    if (f->x < m)          { f->x = m;          if (cosf(f->heading) < 0) f->heading = norm_ang(3.14159f - f->heading);
                                                if (f->facing < 0) { f->facing = 1; f->behind = -TURN_SETTLE; } }
    if (f->x > TANK_W - m) { f->x = TANK_W - m; if (cosf(f->heading) > 0) f->heading = norm_ang(3.14159f - f->heading);
                                                if (f->facing > 0) { f->facing = -1; f->behind = -TURN_SETTLE; } }
    if (f->y < m)          { f->y = m;          f->heading = -f->heading; }
    if (f->y > TANK_H - m) { f->y = TANK_H - m; f->heading = -f->heading; }
#ifdef TANK_CORNER_R                                       /* the watch's corners: back onto the arc m inside it, the heading mirrored
                                                              in the glass while it points out (the bowl's rule, a corner at a time) */
    { float cx, cy, d = corner_in(f->x, f->y, &cx, &cy), lim = TANK_CORNER_R - m;
      if (d > lim && d > 1) {
          float nx = (f->x - cx) / d, ny = (f->y - cy) / d;
          f->x = cx + nx * lim; f->y = cy + ny * lim;
          float hx = cosf(f->heading), hy = sinf(f->heading), out = hx * nx + hy * ny;
          if (out > 0) {
              hx -= 2 * out * nx; hy -= 2 * out * ny; f->heading = atan2f(hy, hx);
              if (nx < -0.5f && f->facing < 0) { f->facing = 1; f->behind = -TURN_SETTLE; }
              if (nx > 0.5f && f->facing > 0)  { f->facing = -1; f->behind = -TURN_SETTLE; }
          }
      } }
#endif
#endif
}
/* ---- the species (2026-10-05, docs/species.md): the classic fish's row is
 * a placeholder - its looks and temperament are the ROSTER's ---- */
const species_def_t SPECIES[SP_COUNT] = {
    /* name, token, default names, designs {name, body, fin, accent},
       size lo/hi, bold lo/hi, social lo/hi, curiosity, lazy, turn, loco,
       speed_k, burst_k, vert_k, min_speed, hit_r, big */
    { "FISH", "fish", { "mira", "bolt", "kelp", "nori" },
      { { "CLASSIC", 0x38dcc7, 0x1d9f98, 0xffbd59 }, { "CLASSIC", 0x38dcc7, 0x1d9f98, 0xffbd59 },
        { "CLASSIC", 0x38dcc7, 0x1d9f98, 0xffbd59 }, { "CLASSIC", 0x38dcc7, 0x1d9f98, 0xffbd59 } },
      0.86f, 1.08f, 0.10f, 0.90f, 0.10f, 0.90f, 5.5f, 0.3f, 3.2f, LOCO_FIN,
      1.00f, 1.00f, 0.72f, 0.0f, 38, false,
      .mo = { 2.4f, 0.00f,  0, 2.6f, 1.00f, 0.00f,  0 },   /* the turn is its own (fish_swim); the length is for tank_body_half_len */
      .flags = SPF_ROSTER | SPF_ROAMS, .mutate_p = SP_MUTATE_P,
      .flee = fish_flee, .dance = fish_dance, .swim = fish_swim },
    { "SEAHORSE", "seahorse", { "pearl", "sandy", "ripple", "coral" },
      { { "GOLDEN", 0xffc93c, 0xd9962a, 0xff8a3d }, { "CRIMSON", 0xe0443a, 0x9e2a26, 0xffd6c9 },
        { "BLACK", 0x34343c, 0x1e1e24, 0xf2f2f2 },  { "LAVENDER", 0xb38ce8, 0x7a5ec2, 0xffe08a } },
      0.80f, 1.00f, 0.08f, 0.35f, 0.50f, 0.85f, 4.0f, 0.7f, 1.6f, LOCO_UPRIGHT,
      0.35f, 0.45f, 1.60f, 0.0f, 32, false,
      .mo = { 0.8f, 0.90f,  0, 1.6f, 0.85f, 0.00f,  0 },   /* a slow pivot (the snout: tank_fish_head) */
      .flags = SPF_UPRIGHT | SPF_BACKS, .fin_hz = 3.0f, .bob_amp = 2, .bob_hz = 1.1f,
      .state = seahorse_state, .startle = seahorse_startle, .flee = seahorse_flee, .court = seahorse_court, .target = seahorse_target, .move = seahorse_move },
    { "OCTOPUS", "octopus", { "inky", "otto", "squish", "ollie" },
      { { "COMMON", 0xc0583a, 0x8a3a26, 0xf2b48a }, { "BLUE-RINGED", 0xd9b45a, 0x9e7e30, 0x2a8cff },
        { "MIMIC", 0x8a5a3a, 0x5a3a26, 0xf2ead8 },  { "VIOLET", 0x8a4fd0, 0x5a2e96, 0xff9ad8 } },
      1.00f, 1.25f, 0.40f, 0.80f, 0.05f, 0.25f, 8.5f, 0.4f, 3.0f, LOCO_JET,
      0.55f, 1.60f, 1.00f, 0.0f, 38, false,
      .mo = { 3.0f, 0.60f,  0, 2.6f, 1.00f, 0.00f,  9 },   /* arms all round the mouth; it crawls */
      .flags = SPF_BACKS | SPF_JETS | SPF_INKS | SPF_VISITS, .fin_hz = 0.5f, .walk_step = 14, .walk_idle = 0.3f,
      .poke = inker_poke, .state = octopus_state, .startle = inker_startle, .flee = inker_flee, .court = floor_court, .target = octopus_target, .walking = octopus_walking },
    { "PUFFERFISH", "puffer", { "puff", "spike", "pebble", "bubbles" },
      { { "SPOTTED", 0xd8c48a, 0x9e8a5a, 0x3a3226 }, { "DOGFACE", 0xa8aca4, 0x6e726c, 0x2a2a2e },
        { "SADDLED", 0xf2f0e6, 0xb0aea4, 0x2a2a30 }, { "GOLDEN", 0xffc53a, 0xd99a1e, 0xfff0b0 } },
      0.85f, 1.05f, 0.25f, 0.60f, 0.20f, 0.50f, 7.0f, 0.5f, 4.6f, LOCO_HOVER,
      0.45f, 0.70f, 0.90f, 0.0f, 34, false,
      .mo = { 1.6f, 0.95f, 40, 4.0f, 0.85f, 0.60f,  0 },   /* stops dead, turns on the spot, backs up */
      .flags = SPF_BACKS | SPF_PUFFS | SPF_VISITS, .fin_hz = 1.6f, .bob_amp = 3, .bob_hz = 2.2f,
      .poke = puffer_poke, .state = puffer_state, .startle = puffer_startle, .flee = puffer_flee },
    { "ANGLERFISH", "angler", { "lumen", "glim", "ember", "murk" },
      { { "ABYSS", 0x2e2a36, 0x1c1a22, 0x7ffcff }, { "FROGFISH", 0xff8a3d, 0xc05a1e, 0xffd23f },
        { "MOTTLED", 0x7a5a3a, 0x4e3a26, 0xb9f04a }, { "PINK WARTY", 0xe38fb0, 0xa85a7a, 0xfff07a } },
      0.95f, 1.20f, 0.50f, 0.80f, 0.05f, 0.20f, 2.5f, 0.9f, 1.4f, LOCO_AMBUSH,
      0.30f, 1.70f, 0.60f, 0.0f, 38, false,
      .mo = { 1.0f, 0.90f,  0, 3.0f, 1.00f, 0.75f, 11 },   /* the mouth is most of the front */
      .flags = SPF_BACKS | SPF_VISITS | SPF_HOME, .fin_hz = 0.4f, .gait = { 2.0f, 4.0f, 3.0f, 8.0f, 1.0f }, .walk_step = 14,
      .state = angler_state, .flee = angler_flee, .court = floor_court, .target = angler_target, .walking = angler_walking },
    { "ELECTRIC EEL", "eel", { "zap", "volt", "sparky", "ohm" },
      { { "OLIVE", 0x5a6a3a, 0x3a4626, 0xff8a3d }, { "CHARCOAL", 0x3a3f45, 0x24282c, 0xffd23f },
        { "BRONZE", 0x8a6a3a, 0x5a4426, 0xffb070 },  { "SPOTTED", 0x4e7a4a, 0x2e4e2e, 0xd8f07a } },
      0.98f, 1.23f, 0.60f, 0.90f, 0.10f, 0.30f, 4.5f, 0.6f, 1.8f, LOCO_UNDULATE,   /* 2026-10-10: 30 % smaller (was 1.40-1.75) */
      0.70f, 1.10f, 0.80f, 0.0f, 46, true,
      .mo = { 2.0f, 0.30f, 70, 2.0f, 1.60f, 0.90f,  6 },   /* the head at the front of a long body */
      .flags = SPF_BACKS | SPF_BREATHES | SPF_SPARKS, .fin_hz = 0.4f, .fin_div = 30,
      .state = breather_state, .startle = eel_startle, .flee = eel_flee, .target = breather_target },
    { "HAMMERHEAD", "shark", { "hammer", "finn", "ridge", "bruce" },
      { { "GREY", 0x8a9aa6, 0x5e6e7a, 0xe8eef2 },  { "BRONZE", 0xa88a5a, 0x7a603a, 0xf2e6cc },
        { "SLATE", 0x6a83a8, 0x46607e, 0xdde8f4 }, { "SCALLOPED", 0xc0ccd4, 0x8e9aa2, 0xffffff } },
      1.12f, 1.37f, 0.80f, 0.95f, 0.50f, 0.80f, 5.0f, 0.1f, 1.5f, LOCO_CRUISE,   /* 2026-10-10: 30 % smaller (was 1.60-1.95) */
      0.95f, 1.20f, 0.50f, 16.0f, 52, true,
      .mo = { 0.6f, 0.00f,  0, 0.8f, 1.00f, 0.80f,  0 },   /* the slow yaw IS its wide turn */
      .flags = SPF_LEVEL,
      .flee = shark_flee, .court = cruiser_court, .target = cruiser_target, .move = shark_move },
    { "SQUID", "squid", { "squee", "jett", "sumi", "kiki" },
      { { "PINK", 0xf2a6c0, 0xc06a8a, 0xe0443a },   { "FIREFLY", 0x3a6aff, 0x2446b0, 0x9ffcff },
        { "BIGFIN", 0xeef2f4, 0xb8c4cc, 0xff9ad8 }, { "REEF", 0xf2b04a, 0xb87a26, 0x8a3a26 } },
      0.90f, 1.10f, 0.30f, 0.60f, 0.70f, 0.95f, 6.0f, 0.3f, 3.5f, LOCO_JET,
      0.65f, 1.80f, 1.10f, 0.0f, 34, false,
      .mo = { 2.5f, 0.70f, 60, 2.5f, 1.00f, 0.00f,  0 },
      .flags = SPF_BACKS | SPF_JETS | SPF_INKS, .fin_hz = 0.9f, .bob_amp = 1.5f, .bob_hz = 1.3f,
      .poke = inker_poke, .state = jet_state, .startle = inker_startle, .flee = inker_flee, .target = squid_target },
    { "CRAB", "crab", { "pinch", "nipper", "clack", "sidney" },
      { { "RED ROCK", 0xd8402a, 0x9a2a1c, 0xffe0c0 }, { "BLUE", 0x4a7ab8, 0x2e5080, 0xff6a3a },
        { "LIGHTFOOT", 0xff6a2a, 0xb03a1a, 0x3ac0e8 }, { "SHORE", 0x5a8a3a, 0x3a5a26, 0xd8c48a } },
      0.80f, 1.00f, 0.40f, 0.80f, 0.20f, 0.50f, 6.5f, 0.5f, 3.5f, LOCO_SIDEWALK,
      0.50f, 1.30f, 0.30f, 0.0f, 30, false,
      .mo = { 4.0f, 0.00f,  0, 6.0f, 0.75f, 0.00f,  7 },   /* face-on, its width the length; claws either side */
      .flags = SPF_SIDEWAYS | SPF_VISITS, .den_dx = 10, .gait = { 0.5f, 1.3f, 0.25f, 0.7f, 1.5f }, .walk_step = 9,
      .state = crab_state, .startle = crab_startle, .flee = crab_flee, .court = floor_court, .target = crab_target, .walking = floor_walking },
    { "LOBSTER", "lobster", { "larry", "claws", "rocky", "pinchy" },
      { { "COMMON", 0x3a4a3e, 0x24302a, 0xff7a3a }, { "BLUE", 0x2a6aff, 0x1a44b0, 0xa8d0ff },
        { "SPINY", 0x3a8a7a, 0x2a5a50, 0xf2d23a }, { "CALICO", 0xd8402a, 0x9a2a1c, 0xf2e6cc } },
      1.10f, 1.40f, 0.50f, 0.85f, 0.05f, 0.25f, 5.5f, 0.6f, 2.0f, LOCO_WALK,
      0.40f, 2.20f, 0.30f, 0.0f, 38, false,
      .mo = { 1.2f, 0.90f,  0, 3.0f, 1.20f, 0.70f,  7 },
      .flags = SPF_BACKS | SPF_VISITS, .den_dx = -10,
      .startle = lobster_startle, .flee = lobster_flee, .court = floor_court, .target = lobster_target, .walking = floor_walking, .move = lobster_move },
    { "JELLYFISH", "jellyfish", { "jelly", "wisp", "moon", "ripple" },
      { { "PEACH", 0xe5a777, 0x73a69b, 0xf1d29e }, { "PEARL", 0xb6d2c1, 0x639e92, 0xe4dec0 },
        { "ROSE", 0xd99bb5, 0x8677ad, 0xf3d5df }, { "BLUE", 0x82bacd, 0x6286ad, 0xc9e8df } },
      0.95f, 1.15f, 0.10f, 0.40f, 0.40f, 0.75f, 5.0f, 0.7f, 1.6f, LOCO_HOVER,
      0.40f, 0.75f, 1.10f, 0.0f, 38, false,
      .mo = { 1.2f, 0.30f,  0, 2.5f, 1.50f, 0.00f,  0 },   /* upright bell and long trailing arms */
      .flags = SPF_BACKS | SPF_PULSES, .clamp_th = 18,
      .state = jelly_state, .startle = jelly_startle, .flee = jelly_flee, .target = jelly_target },
    { "SWORDFISH", "swordfish", { "blade", "marlin", "sabre", "dash" },   /* 2026-10-10 night: a cruiser like the hammerhead, faster, never still */
      { { "STEEL", 0x5f7f99, 0x3d5468, 0xe9eef2 }, { "COBALT", 0x2f5cc8, 0x1d3a86, 0xa8d4ff },
        { "SUNSET", 0xb8743a, 0x7a4a22, 0xffd79a }, { "GHOST", 0xc9d6de, 0x8ea0ac, 0xffffff } },
      1.10f, 1.35f, 0.70f, 0.95f, 0.30f, 0.60f, 5.5f, 0.1f, 1.3f, LOCO_CRUISE,
      1.25f, 1.60f, 0.50f, 18.0f, 52, true,
      .mo = { 0.7f, 0.00f,  0, 1.0f, 1.25f, 0.85f,  0 },   /* the hammerhead's wide turn, the bill most of the front */
      .flags = SPF_BACKS,
      .flee = swordfish_flee, .court = cruiser_court, .target = cruiser_target },
    { "TURTLE", "turtle", { "shelly", "moss", "pebble", "tide" },   /* 2026-10-11: a sea turtle - slow strokes, long glides, a nap on the sand, up for a breath */
      { { "GREEN", 0x6f9a3e, 0x8fb45c, 0xe9dc9c }, { "HAWKSBILL", 0xb57a2c, 0x6b4318, 0xf3cf78 },
        { "LEATHERBACK", 0x45546a, 0x2c3542, 0xcfd9e6 }, { "SEAFOAM", 0x3f9c8a, 0x2c6c63, 0xdaf3e6 } },
      1.05f, 1.30f, 0.45f, 0.80f, 0.20f, 0.50f, 6.0f, 0.6f, 1.6f, LOCO_HOVER,
      0.55f, 1.20f, 1.00f, 0.0f, 46, false,
      .mo = { 0.9f, 0.60f,  0, 1.4f, 1.10f, 0.70f,  6 },   /* a slow, half-braked turn, never backs; the head out front; it sits on the sand */
      .flags = SPF_BACKS | SPF_BREATHES, .fin_hz = 0.7f,
      .state = breather_state, .flee = turtle_flee, .target = breather_target },
};
/* `goal` is normally f->goal.id; the hesitation glance asks for the runner-up's
 * target, in which case nothing is mutated (no dart burst is started). */
static target_t target_for_goal(tank_t *t, int idx, goal_id_t goal, bool glance) {
    fish_t *f = &t->fish[idx];
    float tm = t->clock;
    target_t tg = {
        f->x + cosf(f->heading + sinf(f->wander) * 0.8f) * 50,
        f->y + sinf(f->heading + cosf(f->wander * 0.7f) * 0.45f) * 39,
        lerpf(12, 23, f->bold) * (1 - f->lazy * 0.35f), true,
    };
    switch (goal) {
    case GOAL_SEEK_FOOD: {
        float d; int i = tank_nearest_food(t, f, &d);
        if (i >= 0) {
            /* hunger governs pursuit aggression regardless of which brain set
             * the goal: a peckish fish saunters (~0.75x), a starving one
             * charges (~1.3x) and barely brakes on approach */
            float h = clampf(f->hunger / 10.0f, 0, 1);
            float slow = clampf(d / 60, 0.4f + h * 0.35f, 1);
            tg.x = t->food[i].x; tg.y = t->food[i].y;
            tg.speed = lerpf(29, 62, f->bold) * slow * (0.75f + h * 0.55f);
            sp_target(t, idx, goal, glance, &tg);
            return tg;   /* skip the margin clamp: pellets rest at the floor */
        }
        tg.valid = false;
        break;
    }
    case GOAL_FLEE_SHADOW: {
        /* the roaming shadow was removed 2026-09-13; the token stays in the
         * frozen schema (the state line always reads `shadow none`, which the
         * model was trained on). If it still says flee, the fish bolts away
         * from the surface centre - the old no-shadow fallback. */
        float sx = TANK_W * 0.5f, sy = -60;
        float away = atan2f(f->y - sy, f->x - sx);
        tg.x = f->x + cosf(away) * 105; tg.y = f->y + sinf(away) * 83;
        tg.speed = lerpf(51, 83, f->bold);
        break;
    }
    case GOAL_VISIT_BUBBLES:
        /* each visitor keeps its own lane: a per-fish phase and radius, so
         * four fish orbit the column instead of stacking on one point */
        tg.x = tank_bubble_x(t) + sinf(tm * 1.2f + f->wander + idx * 1.9f) * (26 + idx * 6);
        tg.y = t->bubble_y - 74 + cosf(tm * 0.8f + f->wander + idx * 1.3f) * (34 + idx * 6);
        tg.speed = 23;
        break;
    case GOAL_FOLLOW_FRIEND: {
        float d; int i = tank_nearest_friend(t, idx, &d);
        if (i >= 0) {
            const fish_t *fr = &t->fish[i];
            float ang = atan2f(f->y - fr->y, f->x - fr->x) + sinf(tm + f->wander) * 0.35f;
            tg.x = fr->x + cosf(ang) * 54; tg.y = fr->y + sinf(ang) * 34;   /* a body length off, not on top */
            tg.speed = clampf(d - 40, 10, 42);
        } else tg.valid = false;
        break;
    }
    case GOAL_REST:
        tg.x = t->reef_x + 13 + f->rest_dx;
        tg.y = TANK_BOT - 50 + f->rest_dy;
        tg.speed = 6 + f->bold * 4;
        break;
    case GOAL_DART_PLAY:
        if (glance) { tg.valid = false; break; }
        if (f->dart_timer <= 0) {
            rand_water(t, 38, 40, TANK_BOT - 64, &f->dart_x, &f->dart_y);
            f->dart_timer = tank_randf(t, 0.8f, 1.5f);
        }
        tg.x = f->dart_x; tg.y = f->dart_y;
        tg.speed = lerpf(65, 95, f->bold);
        break;
    case GOAL_INSPECT_REEF: {
        float rx, ry; tank_reef_spot(t, &rx, &ry);   /* the reef cluster, once there is one */
        tg.x = rx + sinf(tm * 0.65f + f->wander + idx * 1.7f) * (39 + idx * 4);
        tg.y = ry - REEF_ORBIT_UP + cosf(tm * 0.8f + f->wander + idx * 1.1f) * (15 + idx * 4);
        tg.speed = 15 + f->curiosity * 1.7f;
        break;
    }
    case GOAL_EXPLORE: {
        /* a destination, not a drift (2026-09-14). Before this, explore was
         * the wander target above - 50 px ahead of the nose with a wobble -
         * a random walk that never left the neighbourhood, so an explore
         * decision looked like idling (Strato: "they don't explore the tank
         * very much"). Now the fish picks the zone it has seen least recently
         * (one of the two stalest, so two explorers don't take the same line),
         * cruises to a point inside it, and on arrival picks the next. The
         * model still owns the goal; this is the reflex layer resolving the
         * concrete target, as it does for every other goal. */
        if (glance) { if (!f->explore_set) tg.valid = false; else { tg.x = f->explore_x; tg.y = f->explore_y; } break; }
        if (!f->explore_set || tank_dist(f->x, f->y, f->explore_x, f->explore_y) < 26) {
            int cur = zone_of(f->x, f->y), best = -1, second = -1;
            for (int z = 0; z < 6; z++) {
                if (z == cur) continue;
                if (best < 0 || f->zone_seen[z] < f->zone_seen[best]) { second = best; best = z; }
                else if (second < 0 || f->zone_seen[z] < f->zone_seen[second]) second = z;
            }
            int z = (second >= 0 && (tank_xr(t) % 3) == 0) ? second : best;
            int col = z % 3, row = z / 3;
            f->explore_x = tank_randf(t, col * (TANK_W / 3.0f) + 40, (col + 1) * (TANK_W / 3.0f) - 40);
            f->explore_y = tank_randf(t, row * (TANK_BOT / 2.0f) + 42, (row + 1) * (TANK_BOT / 2.0f) - 40);
#if defined(TANK_ROUND) || defined(TANK_CORNER_R)
            tank_glass_clamp(&f->explore_x, &f->explore_y, 44);   /* a zone's corner is outside the bowl */
#endif
            f->explore_set = true;
        }
        /* the wander wobble bends the line so it reads as a swim, not a bee-line */
        tg.x = f->explore_x + sinf(f->wander) * 18;
        tg.y = f->explore_y + cosf(f->wander * 0.7f) * 12;
        break;
    }
    default: if (glance) tg.valid = false; break;
    }
    sp_target(t, idx, goal, glance, &tg);   /* its own way of doing it (the classic fish's row changes nothing) */
    float m = 23;
#ifdef TANK_ROUND
    tank_glass_clamp(&tg.x, &tg.y, m + 4);
#else
    tg.x = clampf(tg.x, m, TANK_W - m);
    tg.y = clampf(tg.y, m + 9, TANK_H - m);
    corner_clamp(&tg.x, &tg.y, m + 4);
#endif
    return tg;
}

static float wall_avoidance(const fish_t *f, bool *hit) {
    const float m = 34;
    float vx = 0, vy = 0;
#ifdef TANK_ROUND                                          /* the bowl: away from the glass along its radius, and up off the floor */
    float dx = f->x - TANK_RAD, dy = f->y - TANK_RAD, d = sqrtf(dx * dx + dy * dy), gap = TANK_RAD - d;
    if (gap < m && d > 1) { float p = 1 - (gap < 0 ? 0 : gap) / m; vx -= dx / d * p; vy -= dy / d * p; }
    if (f->y > TANK_BOT - m) vy -= 1 - (TANK_BOT - f->y) / m;
#else
    if (f->x < m)          vx += 1 - f->x / m;
    if (f->x > TANK_W - m) vx -= 1 - (TANK_W - f->x) / m;
    if (f->y < m + 6)      vy += 1 - (f->y - 6) / m;
    if (f->y > TANK_H - m) vy -= 1 - (TANK_H - f->y) / m;
#ifdef TANK_CORNER_R                                       /* the watch: in a corner the arc is nearer than either straight wall */
    { float cx, cy, d = corner_in(f->x, f->y, &cx, &cy), gap = TANK_CORNER_R - d;
      if (d > 1 && gap < m) { float p = 1 - (gap < 0 ? 0 : gap) / m; vx -= (f->x - cx) / d * p; vy -= (f->y - cy) / d * p; } }
#endif
#endif
    *hit = (vx != 0 || vy != 0);
    return *hit ? atan2f(vy, vx) : 0;
}

static void eat_nearby_food(tank_t *t, fish_t *f) {
    float ex = f->x, ey = f->y;                  /* a species eats at its mouth (the eel's is at its front) */
    tank_fish_head(f, &ex, &ey);
    for (int i = 0; i < MAX_FOOD; i++) {
        if (!t->food[i].alive) continue;
        if (tank_dist(ex, ey, t->food[i].x, t->food[i].y) < 12 * f->size + 4) {
            t->food[i].alive = false;
            f->hunger = clampf(f->hunger - 4.3f, 0, 10);
            f->energy = clampf(f->energy + 1.0f, 0, 10);
            f->curiosity = clampf(f->curiosity + 0.5f, 0, 10);   /* was 0.8: a trickle burst
                                                                     re-synced the school's curiosity */
            f->eaten++;
            tank_emit(TEV_EAT, (int)(f - t->fish));
            if (t->food[i].from_player) {
                f->eaten_player++; f->ms_bits |= MS_FIRST_MEAL_FROM_YOU;
                if (t->feed_open) { t->player_feedings++; t->feed_open = false; }   /* the gesture became a meal */
            }
            /* post-meal reflex from the prototype */
            if (f->hunger < 2.2f && f->goal.id == GOAL_SEEK_FOOD)
                f->goal.id = (tank_xr(t) & 1) ? GOAL_VISIT_BUBBLES : GOAL_EXPLORE;
            return;   /* one bite per frame: a fish in a cloud of pellets takes
                         them one at a time, so a tank-mate gets a look in */
        }
    }
}

/* tossed (2026-10-10): the throw carries the creature, damping out; it tumbles (the spin on its
 * heading - the renderer pitches the body by it) and bounces off the glass, the floor and the
 * surface; its yaw follows the throw's direction. When the time is up it is level again, its
 * species mode cleared so the species' own logic takes over from wherever it landed. */
static void toss_tick(tank_t *t, fish_t *f, float dt) {
    f->toss -= dt;
    float damp = expf(-dt * 1.5f);
    f->toss_vx *= damp; f->toss_vy = f->toss_vy * damp + 10 * dt;             /* a slow sink as it tumbles */
    f->toss_spin *= damp;
    f->x += f->toss_vx * dt; f->y += f->toss_vy * dt;
    float lo = TANK_FX0 + 14, hi = TANK_FX1 - 14, top = tank_glass_top(f->x) + 16, bot = TANK_BOT - 18;
    if (f->x < lo) { f->x = lo; f->toss_vx = fabsf(f->toss_vx) * 0.6f; }
    if (f->x > hi) { f->x = hi; f->toss_vx = -fabsf(f->toss_vx) * 0.6f; }
    if (f->y < top) { f->y = top; f->toss_vy = fabsf(f->toss_vy) * 0.6f; }
    if (f->y > bot) { f->y = bot; f->toss_vy = -fabsf(f->toss_vy) * 0.5f; }
    f->heading += f->toss_spin * dt;
    f->speed = sqrtf(f->toss_vx * f->toss_vx + f->toss_vy * f->toss_vy);
    f->yaw = f->toss_vx >= 0 ? 1 : -1; f->yaw_tail = f->yaw; f->facing = (int8_t)f->yaw;
    if (f->toss <= 0) {                                                        /* level again: the species' logic resumes */
        f->toss = 0; f->heading = f->yaw > 0 ? 0 : 3.14159f; f->target_speed = 0; f->sp_mode = SPM_NONE;
    }
}

void update_fish(tank_t *t, int idx, float dt) {
    fish_t *f = &t->fish[idx];
    if (f->toss > 0) { toss_tick(t, f, dt); return; }
    /* drives (prototype rates, speed rescaled by the same 0.55) */
    f->hunger    = clampf(f->hunger + dt * (HUNGER_PER_S + f->bold * HUNGER_BOLD_PER_S +
                          (f->goal.id == GOAL_DART_PLAY ? HUNGER_DART_PER_S : 0)), 0, 10);
    /* curiosity: exploring feeds it (prototype) and - new 2026-09-01 -
     * satisfying it SPENDS it: a fish that has reached the bubbles or the
     * reef uses curiosity up there, the way rest restores energy and a meal
     * drops hunger. Before this a fed fish had no sink at all (+0.8 per
     * pellet, nothing ever took it back), so after days the whole school sat
     * at curiosity 9 and the advisor - reading "fed, calm, curious" exactly
     * as trained - parked all four at the bubble column for good. The model
     * still owns the goal; it just sees an honest drive, and the curiosity
     * band is in the re-ask signature so it gets to react. */
    {
        float dc = f->goal.id == GOAL_EXPLORE ? 0.08f : -0.025f;
        if (f->goal.id == GOAL_VISIT_BUBBLES || f->goal.id == GOAL_INSPECT_REEF) {
            bool bub = f->goal.id == GOAL_VISIT_BUBBLES;
            float sx = tank_bubble_x(t), sy = t->bubble_y - 74;
            if (!bub) { tank_reef_spot(t, &sx, &sy); sy -= REEF_ORBIT_UP; }
            bool at = tank_dist(f->x, f->y, sx, sy) < CURIOSITY_SPEND_RADIUS;
            if (at) dc = -CURIOSITY_SPEND_PER_S;
            if (bub && at && !f->at_bubbles) tank_emit(TEV_BUBBLES, idx);   /* arrival at the column: the play cue */
            f->at_bubbles = bub && at;
        } else f->at_bubbles = false;
        f->curiosity = clampf(f->curiosity + dt * dc, 0, 10);
    }
    f->stress    = clampf(f->stress - dt * (f->goal.id == GOAL_REST ? 0.48f : 0.18f), 0, 10);
    /* energy: the prototype's per-second drain (full to empty in ~3 min of
     * cruising) kept the school at energy ~3 - permanently tired, so the
     * advisor never had a fish fit enough to dart and read every content
     * fish as "relaxed": bubbles, reef, follow (2026-09-01 census). Now a
     * fish cruises ~10 min on a full tank and a rest refills it in ~35 s. */
    f->energy    = clampf(f->energy + dt * (f->goal.id == GOAL_REST ? 0.30f
                          : -0.012f - f->speed / 4000.0f), 0, 10);
    /* a canopy is a place to hide: a fish inside one calms faster (below) */
    bool hidden = false;
    for (int b = 0; b < tank_veg_beds(t) && !hidden; b++)
        hidden = t->veg_growth[b] >= VEG_BARE && veg_inside(t, b, f->x, f->y);
    /* vegetation comfort (2026-09-04 rework: fish LIKE cover). Three regimes,
     * each seeking its own equilibrium against the natural decay above:
     *  - SMOTHERED: the second-tallest bed past VEG_SMOTHER (85% of the way to
     *    its own ceiling, tank_veg_cap - since 2026-09-23 a bed stops short of
     *    the surface) - at least two beds crowding their ceilings - is the tank
     *    being overrun: real stress, ramping from nothing at 85% to the full
     *    press at 100% (settles ~6-7 with every bed at its ceiling). One bed
     *    at the ceiling is just a good hiding place, never a stressor.
     *  - BARE: no bed past VEG_BARE - nowhere to hide - is a mild unease
     *    (settles ~1.3) that lifts the moment one tuft regrows.
     *  - otherwise COVER CALMS: stress decays faster the more canopy there is
     *    (up to +0.18/s, doubling the base rate, at a tank full of grass), and
     *    faster again for a fish tucked inside a canopy. */
    {
        float g1 = 0, g2 = 0, gmean = 0;               /* tallest, second tallest */
        int nb = tank_veg_beds(t);
        for (int b = 0; b < nb; b++) {
            float g = t->veg_growth[b];
            gmean += g / nb;                          /* cover calms by real height */
            g /= veg_cap_mean(t, b);                  /* the smother band by fullness */
            if (g > g1) { g2 = g1; g1 = g; } else if (g > g2) g2 = g;
        }
        float over = (g2 - VEG_SMOTHER) / (1.0f - VEG_SMOTHER);
        if (over > 0)
            f->stress = clampf(f->stress + fminf(1, over) * 0.15f * (8.0f - f->stress) * dt, 0, 10);
        else if (g1 < VEG_BARE)
            f->stress = clampf(f->stress + 0.15f * (2.5f - f->stress) * dt, 0, 10);
        else
            f->stress = clampf(f->stress - dt * (0.18f * gmean + (hidden ? 0.18f : 0)), 0, 10);
    }

    f->wander += dt * (0.65f + f->curiosity * 0.04f) + sinf(t->clock + f->x * 0.01f) * dt * 0.12f;
    if (f->dart_timer > 0) f->dart_timer -= dt;
    f->goal_age += dt; f->ask_age += dt;
    /* boredom: the same pastime goes stale (a minute to the top); a meal or a
     * night's rest is never boring. A zone the fish has not seen for a while
     * is a small relief - so a real explore across the tank roughly pays for
     * itself, while an orbit at the bubble column or a tail-chase behind a
     * friend only accrues. (A zone boundary under the orbit gives nothing:
     * the zone has to be BORED_ZONE_STALE_S stale.) The new-goal relief is in
     * tank_tick where goals change. The band is in state_signature, so a fish
     * crossing into "bored" is re-asked - and the v4 model reads the value. */
    {
        goal_id_t g = f->goal.id;
        bool leisure = g == GOAL_VISIT_BUBBLES || g == GOAL_FOLLOW_FRIEND || g == GOAL_INSPECT_REEF ||
                       g == GOAL_DART_PLAY || g == GOAL_EXPLORE || (g == GOAL_REST && !t->night);
        f->bored = clampf(f->bored + dt * (leisure ? BORED_PER_S : -BORED_RELIEF_PER_S), 0, 10);
        int z = zone_of(f->x, f->y);
        if (z != f->zone_last) {
            if (f->zone_last >= 0 && t->clock - f->zone_seen[z] > BORED_ZONE_STALE_S)
                f->bored = clampf(f->bored - BORED_NEW_ZONE, 0, 10);
            f->zone_last = (int8_t)z;
        }
        f->zone_seen[z] = t->clock;
    }

    /* its own state first: a species' puff, ink, breath, lure (sp_state) */
    const species_def_t *sd = &SPECIES[f->species];
    sp_state(t, idx, f, dt); sp_flourish(t,idx,f,dt);
    target_t tg = target_for_goal(t, idx, f->goal.id, false);
    /* fish prefer shallow climb/dive angles while cruising; full vertical
     * agility stays available for urgent goals (a species has its own
     * preference: the seahorse's > 1 favours the climb) */
    bool agile = f->goal.id == GOAL_FLEE_SHADOW || f->goal.id == GOAL_DART_PLAY;
    float vk = agile ? 1.0f : sd->vert_k;                  /* (the classic fish's row: 0.72) */
    float desired = atan2f((tg.y - f->y) * vk, tg.x - f->x);
    /* final food approach: let the fish dip to the floor for the pellet
     * instead of hovering above it on wall-avoidance (the "staring" bug) */
    bool final_approach = f->goal.id == GOAL_SEEK_FOOD &&
                          tank_dist(f->x, f->y, tg.x, tg.y) < 40;
    /* (a species settling on a spot by the glass or the floor - the eel's
       bed, the den - is not pushed off it either) */
    if (!(sd->flags & SPF_ROAMS) && tg.valid && tank_dist(f->x, f->y, tg.x, tg.y) < 40) final_approach = true;
    bool hit; float push = wall_avoidance(f, &hit);
    if (hit && !final_approach)
        desired = norm_ang(desired + norm_ang(push - desired) * 0.62f);

    /* hesitation: a low-confidence decision is shown, not hidden - the fish
     * hovers and glances between its new target and the runner-up's for a
     * moment before committing. Never for flight or high urgency. */
    float hes_speed = -1;
    if (f->hesitate > 0) {
        f->hesitate -= dt;
        target_t alt = f->goal.runner_up < GOAL_COUNT
                     ? target_for_goal(t, idx, f->goal.runner_up, true) : (target_t){0, 0, 0, false};
        float phase = sinf(t->clock * 5.0f + f->wander);
        if (alt.valid && phase < 0) desired = atan2f((alt.y - f->y) * 0.72f, alt.x - f->x);
        hes_speed = 5;
    }

    /* touch: spooked fish bolt from the tap site; trusting fish drift to a
     * resting finger (reflex-layer, independent of the advisor's goal) */
    float touch_speed = -1;
    bool sp_units = false;                 /* a species' reflex set touch_speed in its own px/s */
    bool breathing = f->species < SP_COUNT && (SPECIES[f->species].flags & SPF_BREATHES) && f->sp_mode >= SPM_RISE && f->sp_mode <= SPM_SINK;
    if (t->startled && f->goal.id != GOAL_FLEE_SHADOW && !breathing) {
        float d = tank_dist(f->x, f->y, t->startle_x, t->startle_y);
        if (d < STARTLE_RADIUS * 1.6f) {
            sp_startle_steer(t, f, &desired, &touch_speed); sp_units = true;   /* (its row's flee) */
        }
    } else if (idx == t->stage_fish) {
        /* on stage (setup): a lazy figure-of-eight round the page's clear
         * spot, turning at each end so both flanks show; a fish far from it
         * cruises over first. Above every other presentation: the keeper is
         * looking at THIS fish. */
        float ph = t->clock * 1.1f + f->wander;
        float wx = t->stage_x + cosf(ph) * 24, wy = t->stage_y + sinf(ph * 2) * 6;
        float to = atan2f(wy - f->y, wx - f->x);
        desired = norm_ang(desired + norm_ang(to - desired) * 0.85f);
        touch_speed = tank_dist(f->x, f->y, t->stage_x, t->stage_y) > 80 ? 44 : 20;
    } else if (breathing) {
        /* the eel's breath (an obligate air-breather): up to the glass for a
         * gulp, whatever the goal - a reflex, as the famine's dash is */
        sp_breath(t, f, &tg);
        desired = atan2f(tg.y - f->y, tg.x - f->x);
    } else if (f->surface_s!=0) {
        tg.x=TANK_W*.5f+sinf(t->clock*.22f+idx)*24;
        tg.y=f->surface_s>0?tank_glass_top(tg.x)+fmaxf(32,12*f->size):tank_ground_y(t,tg.x)-tank_walk_off(f);
        tg.valid=true;tg.speed=tank_dist(f->x,f->y,tg.x,tg.y)<18?4:(SPECIES[f->species].flags&SPF_PUFFS)?18:28;
        desired=atan2f(tg.y-f->y,tg.x-f->x);touch_speed=tg.speed;sp_units=true;
    } else if (t->spawning && (idx == t->court_a || idx == t->court_b) &&
               f->goal.id != GOAL_FLEE_SHADOW && !(t->ravenous && f->hunger > 6.5f)) {
        /* the spawning (2026-09-24): the courtship that ends in a fry. It
         * outranks a resting finger and the greeting - this is the moment the
         * keeper is here for - and hunger short of the famine doesn't veto
         * it; a starving parent still begs first (feed it, it comes back). */
        (sd->dance ? sd->dance : species_dance)(t, f, idx, vk, &tg, &desired, &touch_speed);
    } else if (t->hold_active && t->hold_time >= HOLD_ATTRACT_S &&
               f->goal.id != GOAL_FLEE_SHADOW && f->trust >= 4.0f &&
               f->hunger < HOLD_HUNGER_VETO) {
        /* only a settled hold (~3 s of contact) draws fish in - quick taps and
         * card-taps never twitch the school - and a strongly hungry fish has
         * better things to do than visit a finger */
        float d = tank_dist(f->x, f->y, t->hold_x, t->hold_y);
        if (d < HOLD_RADIUS) {
            float w = (f->trust - 4.0f) / 6.0f;          /* 0..1 with trust */
            float to = atan2f(t->hold_y - f->y, t->hold_x - f->x);
            if (d > 28) desired = norm_ang(desired + norm_ang(to - desired) * (0.5f + 0.45f * w));
            /* far fish cross the tank at a cruise, not a drift; close in
             * they slow to arrive and hover */
            float far = d > 120 ? 1.0f : d / 120.0f;
            touch_speed = d > 28 ? lerpf(14, 30, w) + far * lerpf(10, 20, w) : 4;
        }
    } else if (t->greet_timer > 0 && f->trust >= 7.0f && f->goal.id != GOAL_FLEE_SHADOW &&
               f->goal.id != GOAL_SEEK_FOOD) {
        /* light-on greeting: trusting fish come up front to see who's there */
        float gx = TANK_W * 0.5f + (idx - t->n_fish * 0.5f) * 34, gy = 70;
        float d = tank_dist(f->x, f->y, gx, gy);
        if (d > 24) {
            float to = atan2f(gy - f->y, gx - f->x);
            desired = norm_ang(desired + norm_ang(to - desired) * 0.7f);
            touch_speed = 26;
        } else touch_speed = 5;
    } else if (t->ravenous && f->goal.id != GOAL_FLEE_SHADOW && f->hunger > 6.5f) {
        /* starving tank: reflex presentation of the famine, like the greet/
         * hold overrides above - the advisor still owns the goal. Two phases:
         * empty water = beg where meals come from (quick darts back and forth
         * under the feed spot, unmistakably "feed me"); live pellets = the
         * DASH - a real starving fish bolts the moment food hits the water,
         * it doesn't wait to think it over (the advisor's seek_food arrives
         * seconds later and takes back a fish that is already eating). A fish
         * that has eaten (hunger <= 6.5) drops out of the frenzy at once. */
        float fd; int fi = tank_nearest_food(t, f, &fd);
        if (fi >= 0) {
            float to = atan2f(t->food[fi].y - f->y, t->food[fi].x - f->x);
            desired = norm_ang(desired + norm_ang(to - desired) * 0.92f);
            touch_speed = fd > 14 ? lerpf(85, 115, clampf(f->hunger / 10.0f, 0, 1)) : 30;
        } else {
            float spot = t->feed_spot_x >= 0 ? t->feed_spot_x : TANK_W * 0.5f;
            float wx = spot + sinf(t->clock * (1.6f + f->bold * 0.9f) + idx * 2.1f) * (34 + idx * 6);
            float wy = 30 + idx * 4 + sinf(t->clock * 3.1f + f->wander) * 6;
            float to = atan2f(wy - f->y, wx - f->x);
            desired = norm_ang(desired + norm_ang(to - desired) * 0.85f);
            touch_speed = tank_dist(f->x, f->y, wx, wy) > 18
                        ? lerpf(40, 68, clampf(f->hunger / 10.0f, 0, 1)) : 22;
        }
    } else if (t->court_active > 0 && !t->ravenous &&
               (idx == t->court_a || idx == t->court_b) &&
               f->goal.id != GOAL_FLEE_SHADOW && f->hunger < HOLD_HUNGER_VETO) {
        /* the tell: an episode of the courtship circle, the arrival close */
        (sd->dance ? sd->dance : species_dance)(t, f, idx, vk, &tg, &desired, &touch_speed);
    }

    /* separation - personal space. Fish are ~40 px long; the old 16 px
     * radius let four content fish stack on one landmark like a pile of
     * cards (2026-09-01). Now a body length, weighted by how close they are;
     * a fleeing fish keeps its line. */
    if (f->goal.id != GOAL_FLEE_SHADOW)
        for (int i = 0; i < t->n_fish; i++) {
            if (i == idx) continue;
            float d = tank_dist(f->x, f->y, t->fish[i].x, t->fish[i].y), r = 36 * f->size;
            if (d < r) {
                float away = atan2f(f->y - t->fish[i].y, f->x - t->fish[i].x);
                desired = norm_ang(desired + norm_ang(away - desired) * (0.35f + 0.5f * (1 - d / r)));
            }
        }
    /* the gentle tank (docs/species.md): a small creature gives the big ones
     * (the hammerhead, the eel) a wide berth - a nudge away inside
     * SP_AVOID_R x their size, never so strong a hungry one can't reach its
     * pellet (a quarter of it on the final approach). Nobody is ever harmed;
     * a tank of classic fish has no big one, and nothing changes. */
    if (!SPECIES[f->species < SP_COUNT ? f->species : 0].big && f->goal.id != GOAL_FLEE_SHADOW)
        for (int i = 0; i < t->n_fish; i++) {
            const fish_t *o = &t->fish[i];
            if (i == idx || o->species >= SP_COUNT || !SPECIES[o->species].big) continue;
            float d = tank_dist(f->x, f->y, o->x, o->y), r = SP_AVOID_R * o->size;
            if (d < r) {
                float away = atan2f(f->y - o->y, f->x - o->x), w = 0.6f * (1 - d / r);
                if (final_approach) w *= 0.25f;
                desired = norm_ang(desired + norm_ang(away - desired) * w);
                if (!(sd->flags & SPF_ROAMS) && touch_speed < 0 && tg.valid && !final_approach) {   /* one holding a station is moved off it, gently */
                    tg.x += cosf(away) * (r - d) * 0.5f; tg.y += sinf(away) * (r - d) * 0.5f;
                }
            }
        }

    /* the move, its row's way: the classic fish's swim (fish_swim), a species' (species_swim) */
    (sd->swim ? sd->swim : species_swim)(t, idx, f, &tg, desired, touch_speed, hes_speed, sp_units, agile, dt);
    eat_nearby_food(t, f);
}

