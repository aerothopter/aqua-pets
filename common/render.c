/* render.c — porthole look: deep gradient water, AMOLED-black floor, procedural
 * fish (body polygon + animated tail + earned markings), bubbles, seagrass
 * the keeper trims. Everything is drawn
 * into a bare RGB565 buffer; night dims the palette. */
#include "render.h"
#include "theme.h"
#include "icons.h"
#include "progression.h"
#include "tank_events.h"
#include "setup.h"
#include "version.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TAU 6.2831853f

/* a draw target: fb + stride + night dim, plus the window it covers in tank
 * coordinates (ox,oy,w,h) - the frame is (0,0,TANK_W,TANK_H); the stats card
 * cache is a small sprite that still gets drawn in tank coordinates */
typedef struct { uint16_t *fb; int stride; float dim; int ox, oy, w, h; } ctx_t;
static ctx_t ctx_full(uint16_t *fb, int stride, float dim) {
    ctx_t c = { fb, stride, dim, 0, 0, TANK_W, TANK_H }; return c;
}
/* PAGE space (render.h): the full-screen pages, the prompts and the exported
 * drawing helpers put their 448 x 368 layout in the middle of the frame - a
 * ctx whose origin is the page's corner. In the rectangle that IS the frame. */
static bool g_dirty_hold;                          /* below, with the dirty mask */
static ctx_t ctx_page(uint16_t *fb, int stride) {
    ctx_t c = { fb, stride, 1.0f, -PAGE_X, -PAGE_Y, TANK_W, TANK_H };
    g_dirty_hold = true;                           /* a page's coordinates must never reach the mask */
    return c;
}
#define CTX_IN(c, x, y) ((unsigned)((x) - (c)->ox) < (unsigned)(c)->w && (unsigned)((y) - (c)->oy) < (unsigned)(c)->h)
#define CTX_PX(c, x, y) ((c)->fb[((y) - (c)->oy) * (c)->stride + ((x) - (c)->ox)])

/* Dirty mask (2026-09-01): one bit per pixel, set by everything drawn over
 * the baked scene (px, span, blends), cleared every frame. The porthole
 * vignette re-apply walks the mask instead of comparing each pixel with the
 * scene cache (32 pixels per word, no scene reads), and a pixel that already
 * had its vignette applied inline (frond spans, span_final) is simply not
 * marked, so no later rect darkens it twice. (A first version tagged the
 * green LSB instead and cleared it across the scene - that halved the color
 * steps of the dark vignette falloff into visible contour rings. Colors are
 * untouched now.) */
#define DIRTY_WORDS_PER_ROW ((TANK_W + 31) / 32)   /* 14 (15 on the 466 px bowl) */
static uint32_t *g_dirty = NULL;
void render_set_dirty_mask(uint32_t *buf) { g_dirty = buf; }
/* A PAGE draws in its own coordinates (ctx_page: on the bowl its origin is
 * not the frame's, and its y runs negative above the page) - marked as they
 * are, those wrote in front of the mask (2026-10-01: the settings page's
 * fill took the round board down). Nothing reads the mask after render_tank's
 * sweep, so from the first page ctx of a frame until the next render_tank
 * nothing is marked. */
static bool g_dirty_hold;
static inline void dirty_px(int x, int y) {
    if (g_dirty && !g_dirty_hold) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] |= 1u << (x & 31);
}
static inline void dirty_span(int x0, int x1, int y) {
    if (!g_dirty || g_dirty_hold) return;
    uint32_t *row = g_dirty + y * DIRTY_WORDS_PER_ROW;
    int w0 = x0 >> 5, w1 = x1 >> 5;
    if (w0 == w1) { row[w0] |= (0xFFFFFFFFu >> (31 - (x1 & 31))) & (0xFFFFFFFFu << (x0 & 31)); return; }
    row[w0] |= 0xFFFFFFFFu << (x0 & 31);
    for (int w = w0 + 1; w < w1; w++) row[w] = 0xFFFFFFFFu;
    row[w1] |= 0xFFFFFFFFu >> (31 - (x1 & 31));
}

static uint16_t rgb565(uint32_t rgb, float dim) {
    uint32_t r = (uint32_t)(((rgb >> 16) & 255) * dim);
    uint32_t g = (uint32_t)(((rgb >> 8) & 255) * dim);
    uint32_t b = (uint32_t)((rgb & 255) * dim);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* bounding box of everything drawn while g_bb_on (render_tank wraps each
 * fish in it, so its vignette rect is exactly the pixels it touched instead
 * of a fixed 88 px box - fewer PSRAM reads in the re-apply sweep) */
static bool g_bb_on; static int g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1;
static inline void bb_add(int x0, int x1, int y) {
    if (!g_bb_on) return;
    if (x0 < g_bb_x0) g_bb_x0 = x0;
    if (x1 > g_bb_x1) g_bb_x1 = x1;
    if (y < g_bb_y0) g_bb_y0 = y;
    if (y > g_bb_y1) g_bb_y1 = y;
}
static void px(ctx_t *c, int x, int y, uint16_t col) {
    if (CTX_IN(c, x, y)) {
        CTX_PX(c, x, y) = col;
        bb_add(x, x, y);
        dirty_px(x, y);
    }
}

/* A source color dimmed ONCE per shape (night palette), never per pixel:
 * the float multiply + conversions were the bulk of every blended pixel's
 * cost (vegetation, algae film, light shafts, the stats card backdrop). */
typedef struct { int r, g, b; uint16_t v; } src_t;
static inline src_t src_color(uint32_t rgb, float dim) {
    src_t s;
    s.r = (int)(((rgb >> 16) & 255) * dim);
    s.g = (int)(((rgb >> 8) & 255) * dim);
    s.b = (int)((rgb & 255) * dim);
    s.v = (uint16_t)(((s.r >> 3) << 11) | ((s.g >> 2) << 5) | (s.b >> 3));
    return s;
}
/* alpha 0..255 blend of a pre-dimmed source onto one framebuffer pixel */
static inline void blend565(uint16_t *p, const src_t *s, int a) {
    int dr = (*p >> 11) << 3, dg = ((*p >> 5) & 63) << 2, db = (*p & 31) << 3;
    int r = dr + ((s->r - dr) * a >> 8), g = dg + ((s->g - dg) * a >> 8), b = db + ((s->b - db) * a >> 8);
    *p = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static inline void px_blend_s(ctx_t *c, int x, int y, const src_t *s, int a) {
    if (CTX_IN(c, x, y)) { blend565(&CTX_PX(c, x, y), s, a); dirty_px(x, y); }
}
/* alpha 0..255 blend onto existing pixel (one-off pixels; shapes hoist) */
static void px_blend(ctx_t *c, int x, int y, uint32_t rgb, int a) {
    src_t s = src_color(rgb, c->dim);
    px_blend_s(c, x, y, &s, a);
}
/* horizontal span [x0,x1] on row y, clipped to the frame, pre-dimmed source */
static inline void span(ctx_t *c, int x0, int x1, int y, const src_t *s, int alpha) {
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    bb_add(x0, x1, y);
    dirty_span(x0, x1, y);
    uint16_t *p = &CTX_PX(c, x0, y);
    if (alpha >= 255) for (int x = x0; x <= x1; x++) *p++ = s->v;
    else              for (int x = x0; x <= x1; x++) blend565(p++, s, alpha);
}

/* row half-width of a unit circle at |t| = k/64: sqrt(1 - t^2), tabled -
 * the ESP32-S3 computes sqrtf in software and a fouled glass alone asks for
 * ~7000 of them per frame (algae blobs); the quantisation is sub-pixel */
static float ell_half(float t) {
    static float lut[66]; static bool filled;
    if (!filled) { for (int k = 0; k <= 65; k++) { float u = k / 64.0f; lut[k] = u < 1 ? sqrtf(1 - u * u) : 0; } filled = true; }
    if (t < 0) t = -t;
    return t >= 1 ? 0 : lut[(int)(t * 64 + 0.5f)];
}
static void fill_ellipse_s(ctx_t *c, float cx, float cy, float rx, float ry,
                           const src_t *s, int alpha) {
    int y0 = (int)(cy - ry), y1 = (int)(cy + ry);
    for (int y = y0; y <= y1; y++) {
        float w = ell_half((y - cy) / ry);
        if (w <= 0) continue;
        float half = rx * w;
        span(c, (int)(cx - half), (int)(cx + half), y, s, alpha);
    }
}
static void fill_ellipse(ctx_t *c, float cx, float cy, float rx, float ry,
                         uint32_t rgb, int alpha) {
    src_t s = src_color(rgb, c->dim);
    fill_ellipse_s(c, cx, cy, rx, ry, &s, alpha);
}

/* sine from a 256-entry table: the frond sway only has to look continuous,
 * and the ESP32-S3 computes sinf in software (~1000 per frame at full
 * canopy). Phases here are non-negative; & 255 keeps any sign honest. */
static float fast_sin(float x) {
    static float lut[256]; static bool filled;
    if (!filled) { for (int i = 0; i < 256; i++) lut[i] = sinf(i * (TAU / 256)); filled = true; }
    return lut[(int)(x * (256.0f / TAU)) & 255];
}

/* filled convex polygon, points in world space */
static void fill_poly_s(ctx_t *c, const float *xs, const float *ys, int n, const src_t *s, int alpha) {
    float miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < n; i++) { if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i]; }
    for (int y = (int)miny; y <= (int)maxy; y++) {
        float x0 = 1e9f, x1 = -1e9f;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float ay = ys[i], by = ys[j];
            if ((ay <= y && by > y) || (by <= y && ay > y)) {
                float x = xs[i] + (xs[j] - xs[i]) * (y - ay) / (by - ay);
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
            }
        }
        if (x0 <= x1) span(c, (int)x0, (int)x1, y, s, alpha);
    }
}
static void fill_poly(ctx_t *c, const float *xs, const float *ys, int n, uint32_t rgb) {
    src_t s = src_color(rgb, c->dim);
    fill_poly_s(c, xs, ys, n, &s, 255);
}

static uint32_t mix(uint32_t a, uint32_t b, float p) {
    int ar = a >> 16 & 255, ag = a >> 8 & 255, ab = a & 255;
    int br = b >> 16 & 255, bg = b >> 8 & 255, bb = b & 255;
    return ((uint32_t)(ar + (br - ar) * p) << 16) |
           ((uint32_t)(ag + (bg - ag) * p) << 8) |
            (uint32_t)(ab + (bb - ab) * p);
}


/* the turn (2026-09-29, was a roll that flattened the fish to a line): the
 * fish's yaw (tank.c) FORESHORTENS it along its length - side-on at +-1,
 * head-on at 0 - and the sign mirrors it, so its back stays up. The head's
 * yaw leads and the tail's follows, blended along the body, so mid-turn it
 * bends. Eased here so the swing starts and lands softly. */
static float yaw_ease(float y) { return y * (1.5f - 0.5f * y * y); }
#define YAW_MIN 0.06f                    /* the outline never folds to a line (the body's
                                         * thickness is drawn on its own: FISH_THICK) */
#define FISH_THICK 4.2f                  /* half the body's thickness, local px: turned toward
                                         * the glass a fish shows a rounded oval, not a card */
/* the yaw at body point lx: the tail's at the tail tip (-26), the head's at the nose (16) */
static float yaw_at(float yh, float yt, float lx) {
    float u = (lx + 26) * (1.0f / 42);
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    float s = yt + (yh - yt) * u;
    if (s > -YAW_MIN && s < YAW_MIN) s = s < 0 ? -YAW_MIN : YAW_MIN;
    return s;
}

/* The fish's body is a ledger of its life: stage sets size (tank.c) and fin
 * elaboration, hunger drains saturation, and earned markings stay:
 *   elder -> a longer tail and a dorsal crest. All procedural, zero flash. */
/* `mark` scales the accent stripes: 1 in the tank (every stage wears the
 * same 2 x 5 px marks), the preview's size so they read on a big fish */
static void draw_creature(ctx_t *c, const fish_t *f, float clock, bool asleep);   /* the species, below */
static void draw_lagoon_fish(ctx_t *c,const fish_t *f,float clock,bool asleep);
static void draw_blackwater_fish(ctx_t *c,const fish_t *f,float clock,bool asleep);   /* blackwater.inc */
static void draw_fish_core(ctx_t *c, const fish_t *f, float clock, bool asleep, float mark) {
    if (f->species != SP_FISH) { draw_creature(c, f, clock, asleep); return; }
    if(theme_active()==THEME_QUIET_LAGOON){draw_lagoon_fish(c,f,clock,asleep);return;}
    if(theme_active()==THEME_BLACKWATER){draw_blackwater_fish(c,f,clock,asleep);return;}
    float tail = sinf(f->jet * TAU + f->wander) *                      /* the beat's phase: tank.c integrates it (2026-10-12) */
                 (0.32f + f->speed * 0.007f);
    float stress = f->stress / 10.0f;
    float pale = f->hunger > 7 ? (f->hunger - 7) / 3.0f * 0.35f : 0;   /* hungry = washed out */
    /* pitch: the heading folded to face right (the climb or dive), then the
     * yaw scales x: X = s(lx) * (lx cos p - ly sin p), Y = lx sin p + ly cos p.
     * The BODY tilts less than the path: a fish rises and sinks on its fins
     * with its back near level, so the travel angle is eased toward a cap of
     * ~35 deg (~55 at a dash) - never nose-straight-up (Strato 09-29: "too
     * much time in a vertical position") */
    float pmax = 0.61f + 0.35f * fminf(fmaxf((f->speed - 35) * (1 / 40.0f), 0), 1);
    float p = pmax * tanhf(0.8f * atan2f(sinf(f->heading), fabsf(cosf(f->heading))) / pmax);
    float cp = cosf(p), sp = sinf(p);
    float yh = yaw_ease(f->yaw), yt = yaw_ease(f->yaw_tail);
    bool elder = f->stage == STAGE_ELDER, grown = f->stage >= STAGE_ADULT;
    float tail_len = elder ? 1.22f : 1.0f;
    uint32_t body = mix(mix(theme_creature_color(f->color, 0), 0xf25b65, stress * 0.22f), 0x7a8a8e, pale);
    uint32_t fin  = mix(mix(theme_creature_color(f->fin, 1), 0xf25b65, stress * 0.18f), 0x5a6a6e, pale);
#define TX(lx, ly) (f->x + yaw_at(yh, yt, lx) * ((lx) * cp - (ly) * sp) * f->size)
#define TY(lx, ly) (f->y + ((lx) * sp + (ly) * cp) * f->size)

    if (theme_active()) {
        bool club = theme_active() == THEME_TIDEPOOL_CLUB;
        /* Different silhouettes at every stage, with the original yaw/pitch animation. */
        float lx[7] = {-10,-25,-23,-19,-23,-25,-10};
        float ly[7] = {0,-9-tail*5,-3,0,3,9+tail*5,0};
        float xs[20],ys[20];
        for (int i=0;i<7;i++) { xs[i]=TX(lx[i],ly[i]); ys[i]=TY(lx[i],ly[i]); }
        fill_poly(c,xs,ys,7,fin);
        if (grown) {
            float fx[4]={-10,-5,3,8},fy[4]={-6,elder?-16:-12,-12,-5};
            for(int i=0;i<4;i++){xs[i]=TX(fx[i],fy[i]);ys[i]=TY(fx[i],fy[i]);}
            fill_poly(c,xs,ys,4,fin);
        }
        for(int i=0;i<20;i++) {
            float a=TAU*i/20,xx=club?14*cosf(a):16*cosf(a);
            float yy=(club?11:7.8f)*sinf(a)*(club?1:0.82f+0.18f*cosf(a));
            xs[i]=TX(xx,yy);ys[i]=TY(xx,yy);
        }
        fill_poly(c,xs,ys,20,body);
        float face=fabsf(yh);
        fill_ellipse(c,TX(1,1),TY(1,1),fmaxf(2.5f*f->size,9*face*f->size),
                     (club?7:4.5f)*f->size,mix(body,0xffefd0,.20f),210);
        if (f->stage>=STAGE_JUV) for(int i=0;i<3;i++) {
            float xx=-7+i*5, yy=club?(i==1?-3:2):0;
            fill_ellipse(c,TX(xx,yy),TY(xx,yy),fmaxf(.6f,(club?2.1f:1)*face*f->size),
                         (club?2.1f:4)*f->size,theme_creature_color(f->accent,1),180);
        }
        fill_ellipse(c,TX(-3,3),TY(-3,3),3*f->size*fmaxf(face,.2f),2*f->size,fin,230);
        fill_ellipse(c,TX(8,-2),TY(8,-2),club?2.5f:1.8f,asleep?.6f:club?2.5f:1.8f,0xfff9df,255);
        if(!asleep)fill_ellipse(c,TX(8.5f,-2),TY(8.5f,-2),1,1,0x193d32,255);
        return;
    }
    /* tail fin (triangle, flaps with tail phase) */
    {
        float lx[3] = {-11, -26 * tail_len, -26 * tail_len}, ly[3] = {0, -10 - tail * 8, 10 + tail * 8};
        float xs[3], ys[3];
        for (int i = 0; i < 3; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 3, fin);
    }
    /* dorsal crest: adults a small fin, elders a taller one */
    if (grown) {
        float h = elder ? -17 : -13;
        float lx[3] = {6, -2, -9}, ly[3] = {-8, h, -8};
        float xs[3], ys[3];
        for (int i = 0; i < 3; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 3, fin);
    }
    /* body: sampled outline of the prototype's quadratic silhouette */
    {
        static const float blx[10] = { 16, 10, 2, -8, -15, -18, -15, -8, 2, 10 };
        static const float bly[10] = { 0, -7, -10, -9, -6, 0, 6, 9, 10, 7 };
        float xs[10], ys[10];
        for (int i = 0; i < 10; i++) { xs[i] = TX(blx[i], bly[i]); ys[i] = TY(blx[i], bly[i] * (theme_active() == THEME_TIDEPOOL_CLUB ? 1.16f : 1.0f)); }
        fill_poly(c, xs, ys, 10, body);
        /* its thickness: the cross-section at the deepest point, widening as
         * the fish turns toward the glass (sin of the yaw angle) */
        float sm = yaw_at(yh, yt, 2), rx = FISH_THICK * f->size * sqrtf(fmaxf(0, 1 - sm * sm));
        if (rx >= 1) fill_ellipse(c, TX(2, 0), TY(2, 0), rx, 9.5f * f->size, body, 255);
    }
    /* accent stripes: on the flank, so they narrow with the turn and fade as
     * the fish comes head-on (seen edge-on, a flank shows nothing).
     * A FRY has none yet: its markings come in with its first growth spurt
     * (the setup's colour page keeps the accent a "?" for that reason) */
    if (f->stage >= STAGE_JUV) for (int i = -1; i <= 1; i++) {
        float lx = -3 + i * 6, s = fabsf(yaw_at(yh, yt, lx));
        if (s > 0.25f)
            fill_ellipse(c, TX(lx, 0), TY(lx, 0), 2 * mark * s, 5 * mark,
                         theme_creature_color(f->accent, 1), (int)(150 * (s - 0.25f) / 0.75f));
    }
    /* eye — closed to a lid line when asleep (resting at night) */
    if (asleep) {
        fill_ellipse(c, TX(9, -3), TY(9, -3), 2.2f, 0.7f, 0x9fb4b8, 200);
    } else {
        fill_ellipse(c, TX(9, -3), TY(9, -3), 2.2f, 2.2f, 0xffffff, 255);
        fill_ellipse(c, TX(9.7f, -3), TY(9.7f, -3), 1.1f, 1.1f, 0x031015, 255);
    }
#undef TX
#undef TY
}

/* ---- the species (2026-10-05, docs/species.md) ----
 * Nine creatures besides the classic fish, each procedural like it and
 * drawn through the same span / poly / ellipse calls, so the dirty mask and
 * the fish's bounding box (render_tank's DYN_RECT) take in every pixel - an
 * eel's whole length, a lure's halo, an ink cloud. Local coordinates as the
 * fish's: +x the way it faces, y down, 1 unit = 1 px at size 1; the yaw
 * foreshortens x (a negative yaw mirrors) and the head's yaw blends into the
 * tail's along the body, so a long creature bends through its turn. Depth
 * (z, across the glass) is shown obliquely - a hammer's span, a squid's fins
 * - and swings into x as the creature turns head-on.
 * The colours are the fish's own (f->color / fin / accent: a design's, or
 * a parent's), the design's PATTERN is f->variant. The live state (puff,
 * ink, jet, camo, lure, spark) is drawn as tank.c leaves it; only the clock
 * animates here. Cost: no per-pixel trig - the sines come from fast_sin's
 * table, once per part (the urchin and the snail measured per-pixel trig at
 * 1.8-4.5 ms a frame on the ESP32-S3). */
typedef struct {
    ctx_t *c; const fish_t *f;
    float x, y, s;                /* where, and its size */
    float cp, sp;                 /* the body's pitch */
    float yh, yt, l0, il;         /* head / tail yaw, blended over lx in [l0, l0 + 1/il] */
    float zq;                     /* how much of the depth axis shows across the glass (0 side-on) */
    float t;                      /* the clock */
    bool  asleep, marked;         /* marked: a juvenile or older wears its design's pattern */
    float pose;                   /* the octopus's / squid's jet pose 0..1 (sk_pose) */
    float fade;                   /* the pattern's strength (the octopus's camouflage fades it) */
    uint32_t body, fin, acc;
} sk_t;
static float g_u16c[16], g_u16s[16];          /* 16 directions round a circle */
static void sk_dirs(void) {
    static bool filled; if (filled) return;
    for (int i = 0; i < 16; i++) { g_u16c[i] = cosf(i * (TAU / 16)); g_u16s[i] = sinf(i * (TAU / 16)); }
    filled = true;
}
static inline float fast_cos(float x) { return fast_sin(x + TAU * 0.25f); }
static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline float sk_yaw(const sk_t *k, float lx) {
    float u = clamp01((lx - k->l0) * k->il), s = k->yt + (k->yh - k->yt) * u;
    if (s > -YAW_MIN && s < YAW_MIN) s = s < 0 ? -YAW_MIN : YAW_MIN;
    return s;
}
static inline void sk_pt(const sk_t *k, float lx, float ly, float *X, float *Y) {
    *X = k->x + sk_yaw(k, lx) * (lx * k->cp - ly * k->sp) * k->s;
    *Y = k->y + (lx * k->sp + ly * k->cp) * k->s;
}
/* a point off the side plane: z shows obliquely (obl px of y a unit), and as x once head-on */
static inline void sk_pt3(const sk_t *k, float lx, float ly, float lz, float obl, float *X, float *Y) {
    sk_pt(k, lx, ly + lz * obl, X, Y);
    *X += k->zq * lz * k->s;
}
static void sk_poly(const sk_t *k, const float *lx, const float *ly, int n, uint32_t rgb, int a) {
    float xs[16], ys[16];
    for (int i = 0; i < n; i++) sk_pt(k, lx[i], ly[i], &xs[i], &ys[i]);
    src_t s = src_color(rgb, k->c->dim);
    fill_poly_s(k->c, xs, ys, n, &s, a);
}
static void sk_tri(const sk_t *k, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb, int a) {
    float lx[3] = { x0, x1, x2 }, ly[3] = { y0, y1, y2 };
    sk_poly(k, lx, ly, 3, rgb, a);
}
/* an ellipse at a body point; `flank` ones are painted on the side, so they
   narrow with the yaw and fade as the creature comes head-on (as the fish's stripes) */
static void sk_dot_s(const sk_t *k, float lx, float ly, float rx, float ry, const src_t *s, int a, bool flank) {
    float X, Y; sk_pt(k, lx, ly, &X, &Y);
    float w = 1;
    if (flank) {
        w = fabsf(sk_yaw(k, lx));
        if (w <= 0.25f) return;
        a = (int)(a * (w - 0.25f) * (1 / 0.75f));
    }
    rx *= k->s * w; ry *= k->s;
    if (rx < 0.5f) rx = 0.5f;
    if (ry < 0.5f) ry = 0.5f;
    if (a > 0) fill_ellipse_s(k->c, X, Y, rx, ry, s, a);
}
static void sk_dot(const sk_t *k, float lx, float ly, float rx, float ry, uint32_t rgb, int a, bool flank) {
    src_t s = src_color(rgb, k->c->dim);
    sk_dot_s(k, lx, ly, rx, ry, &s, a, flank);
}
/* a pattern mark: a juvenile or older only, at the pattern's strength */
static void sk_mark(const sk_t *k, float lx, float ly, float rx, float ry, uint32_t rgb, int a) {
    if (k->marked) sk_dot(k, lx, ly, rx, ry, rgb, (int)(a * k->fade), true);
}
/* a tube through screen points (arms, tails, an eel's body): a quad a
   segment, mitred at the joints so neighbours share an edge (no disc a
   joint to fill the wedges - those were most of an eel's cost), round caps
   at the ends; a sharp joint (a crab's knee) gets its own disc instead.
   Two sqrts a point, nothing per pixel. Radii in px. */
#define SK_TUBE_MAX 20
static void tube_s(ctx_t *c, const float *X, const float *Y, const float *R, int n, const src_t *s, int a) {
    if (n < 2 || n > SK_TUBE_MAX) return;
    float dx[SK_TUBE_MAX], dy[SK_TUBE_MAX];                   /* each segment's unit direction */
    for (int i = 0; i + 1 < n; i++) {
        float ex = X[i + 1] - X[i], ey = Y[i + 1] - Y[i], l = sqrtf(ex * ex + ey * ey);
        if (l < 0.05f) { dx[i] = i ? dx[i - 1] : 1; dy[i] = i ? dy[i - 1] : 0; continue; }
        dx[i] = ex / l; dy[i] = ey / l;
    }
    float nxa = -dy[0], nya = dx[0];                          /* the normal where this segment starts */
    for (int i = 0; i + 1 < n; i++) {
        float nxb = -dy[i], nyb = dx[i];                      /* ... and where it ends: mitred, or its own at a sharp joint */
        bool sharp = false;
        if (i + 2 < n) {
            float cs = dx[i] * dx[i + 1] + dy[i] * dy[i + 1];
            if (cs > 0.6f) {
                float mx = dx[i] + dx[i + 1], my = dy[i] + dy[i + 1];
                float k = 1.0f / (mx * dx[i] + my * dy[i]);  /* the mitre: 1 / cos(half the bend), over |m| */
                nxb = -my * k; nyb = mx * k;
            } else sharp = true;
        }
        float xs[4] = { X[i] + nxa * R[i], X[i + 1] + nxb * R[i + 1], X[i + 1] - nxb * R[i + 1], X[i] - nxa * R[i] };
        float ys[4] = { Y[i] + nya * R[i], Y[i + 1] + nyb * R[i + 1], Y[i + 1] - nyb * R[i + 1], Y[i] - nya * R[i] };
        fill_poly_s(c, xs, ys, 4, s, a);
        if (sharp && R[i + 1] >= 0.9f) fill_ellipse_s(c, X[i + 1], Y[i + 1], R[i + 1], R[i + 1], s, a);
        if (sharp) { nxa = -dy[i + 1]; nya = dx[i + 1]; } else { nxa = nxb; nya = nyb; }
    }
    if (R[0] >= 0.9f) fill_ellipse_s(c, X[0], Y[0], R[0], R[0], s, a);
    if (R[n - 1] >= 0.9f) fill_ellipse_s(c, X[n - 1], Y[n - 1], R[n - 1], R[n - 1], s, a);
}
static void sk_tube(const sk_t *k, const float *lx, const float *ly, const float *r, int n, uint32_t rgb) {
    float X[SK_TUBE_MAX], Y[SK_TUBE_MAX], R[SK_TUBE_MAX];
    for (int i = 0; i < n; i++) { sk_pt(k, lx[i], ly[i], &X[i], &Y[i]); R[i] = r[i] * k->s; if (R[i] < 0.5f) R[i] = 0.5f; }
    src_t s = src_color(rgb, k->c->dim);
    tube_s(k->c, X, Y, R, n, &s, 255);
}
static void sk_line(const sk_t *k, float x0, float y0, float x1, float y1, float r0, float r1, uint32_t rgb) {
    float lx[2] = { x0, x1 }, ly[2] = { y0, y1 }, r[2] = { r0, r1 };
    sk_tube(k, lx, ly, r, 2, rgb);
}
/* the eye: white, a pupil looking ahead; asleep, a lid line. `dark`: an
   all-dark eye with a glint (sharks, crabs, lobsters, the eel) */
static void sk_eye(const sk_t *k, float lx, float ly, float r, bool dark) {
    float X, Y; sk_pt(k, lx, ly, &X, &Y);
    float R = r * k->s; if (R < 1.3f) R = 1.3f;
    if (k->asleep) { fill_ellipse(k->c, X, Y, R, R * 0.32f + 0.4f, 0x9fb4b8, 200); return; }
    float dir = sk_yaw(k, lx) < 0 ? -1 : 1;
    if (dark) {
        fill_ellipse(k->c, X, Y, R, R, 0x05080a, 255);
        fill_ellipse(k->c, X + dir * R * 0.3f, Y - R * 0.35f, R * 0.35f, R * 0.35f, 0xffffff, 200);
        return;
    }
    fill_ellipse(k->c, X, Y, R, R, 0xffffff, 255);
    fill_ellipse(k->c, X + dir * R * 0.3f, Y, R * 0.55f, R * 0.55f, 0x031015, 255);
}
/* the frame: position, size, the tints the classic fish wears (stress
   reddens, hunger washes out), the pitch from its heading - eased toward
   pmax as the fish's is, and `level`ed toward 0 at rest. A creature that
   swims BACKWARD (a squid mantle-first, an eel in reverse) tilts the other
   way: its tail end leads the climb. */
static void sk_setup(sk_t *k, ctx_t *c, const fish_t *f, float clock, bool asleep, float l0, float l1, float pmax, float level) {
    sk_dirs();
    k->c = c; k->f = f; k->x = f->x; k->y = f->y; k->s = f->size; k->t = clock; k->asleep = asleep;
    k->marked = f->stage >= STAGE_JUV; k->fade = 1;
    k->yh = yaw_ease(f->yaw); k->yt = yaw_ease(f->yaw_tail);
    k->l0 = l0; k->il = 1.0f / (l1 - l0);
    k->zq = sqrtf(fmaxf(0, 1 - k->yh * k->yh));
    float p = 0;
    if (pmax > 0) {
        float ch = cosf(f->heading), sh = sinf(f->heading);
        float dir = ch * f->yaw < 0 ? -1.0f : 1.0f;
        p = pmax * tanhf(0.8f * atan2f(sh * dir, fabsf(ch)) / pmax) * level;
    }
    k->cp = cosf(p); k->sp = sinf(p);
    float stress = f->stress / 10.0f;
    float pale = f->hunger > 7 ? (f->hunger - 7) / 3.0f * 0.35f : 0;
    k->body = mix(mix(theme_creature_color(f->color, 0), 0xf25b65, stress * 0.22f), 0x7a8a8e, pale);
    k->fin  = mix(mix(theme_creature_color(f->fin, 1), 0xf25b65, stress * 0.18f), 0x5a6a6e, pale);
    k->acc  = theme_creature_color(f->accent, 1);
}
static inline void sk_lean(sk_t *k, float p) { k->cp = cosf(p); k->sp = sinf(p); }

/* ink (octopus, squid): a dark cloud left where the startle was, swelling
   and thinning over the seconds f->ink counts down. Where it was is kept
   here, a slot each (render_tank's fish index); a preview's cloud trails
   behind it. */
static int g_sp_slot = -1;                     /* the tank slot being drawn (-1: a preview) */
typedef struct { float x, y, ink0, last; float pose, clock; } ink_mem_t;
static ink_mem_t g_ink[N_FISH_MAX];
/* a pose eased toward its target over ~1/8 s (the octopus's crawl -> jet,
   the squid's): tank.c says WHETHER it jets, the look slides into it. A
   preview takes the target as it is. */
static float sk_pose(float target, float clock) {
    if (g_sp_slot < 0 || g_sp_slot >= N_FISH_MAX) return target;
    ink_mem_t *g = &g_ink[g_sp_slot];
    float dt = clock - g->clock; g->clock = clock;
    if (dt <= 0 || dt > 0.5f) { g->pose = target; return target; }   /* a new run of frames (or a redraw): as it is */
    g->pose += (target - g->pose) * (dt * 8 > 1 ? 1 : dt * 8);
    return g->pose;
}
static void draw_ink(const sk_t *k) {
    const fish_t *f = k->f;
    float ox, oy, ink0;
    if (g_sp_slot >= 0 && g_sp_slot < N_FISH_MAX) {
        ink_mem_t *g = &g_ink[g_sp_slot];
        if (f->ink <= 0) { g->ink0 = g->last = 0; return; }
        if (g->ink0 <= 0 || f->ink > g->last + 0.05f) { g->x = f->x; g->y = f->y; g->ink0 = f->ink; }
        if (f->ink > g->ink0) g->ink0 = f->ink;
        g->last = f->ink; ox = g->x; oy = g->y; ink0 = g->ink0;
    } else {
        if (f->ink <= 0) return;
        float dir = f->yaw < 0 ? -1.0f : 1.0f;
        ox = f->x + dir * 14 * k->s; oy = f->y; ink0 = 4.5f;   /* a preview: behind the mantle, a squirt (INK_SQUIRT_S) part spent */
    }
    float u = clamp01(1 - f->ink / (ink0 > 2.5f ? ink0 : 2.5f));
    /* a spontaneous squirt (tank.c INK_SQUIRT_S, 4.5 s) is a fuller cloud than the startle's 2.5 s puff */
    float big = clamp01((ink0 - 2.5f) / 2.0f);
    /* the cloud grows from a puff to a billow; it stays dense through the first part of its life
       and only then thins (before 2026-10-10 it was most opaque at 8 px and faded as it grew: a smudge) */
    float fade = u < 0.4f ? 1 : 1 - (u - 0.4f) / 0.6f;
    float R = (10 + 26 * u) * (1 + 0.45f * big) * k->s, a = 20 + 215 * fade;
    oy -= 5 * u * k->s;                         /* it rises a little as it spreads */
    /* black ink in every theme (2026-10-10; the lagoon and tidepool clouds were tinted teal and blue-grey
       and read as water): the themes keep only a whisper of their tint and their lobe shapes */
    uint32_t dark=theme_active()==THEME_QUIET_LAGOON?0x0e1816:theme_active()==THEME_TIDEPOOL_CLUB?0x12131c:0x0a0710;
    uint32_t middle=theme_active()==THEME_QUIET_LAGOON?0x22302d:theme_active()==THEME_TIDEPOOL_CLUB?0x272a38:0x1e1622;
    src_t dk = src_color(dark, k->c->dim), mid = src_color(middle, k->c->dim);
    static const float BX[5] = { 0, -0.5f, 0.55f, -0.2f, 0.3f }, BY[5] = { 0, -0.35f, -0.25f, 0.4f, 0.3f }, BR[5] = { 0.75f, 0.55f, 0.6f, 0.5f, 0.45f };
    fill_ellipse_s(k->c, ox, oy, R, R * 0.8f, &mid, (int)(a * 0.55f));
    for (int i = 0; i < 5; i++)
        fill_ellipse_s(k->c, ox + BX[i] * R, oy + BY[i] * R, BR[i] * R, BR[i] * R * (theme_active()==THEME_TIDEPOOL_CLUB?1:0.85f), &dk, (int)(a * (theme_active()==THEME_QUIET_LAGOON?.75f:.85f)));
}

/* ---- seahorse: upright, the snout a tube, the coronet, a pot belly
   ringed with plates, a prehensile tail curled under, and the little dorsal
   fan that is its whole engine. Leans into its slow swim. ---- */
/* the turtle's shared pieces (2026-10-11), used by every theme's drawer below: */
static const float TURTLE_CX[10] = { 11, 8, 2, -5, -11, -15, -15, -11, -3, 7 };
static const float TURTLE_CY[10] = { -1, -5.5f, -8, -8.2f, -6.5f, -3, 1, 2.6f, 3, 2 };
static void turtle_flipper(const sk_t *k, float bx, float by, float st, float dir, uint32_t rgb, int a) {
    /* a paddle hung from the shoulder (bx,by): it swings on st from forward and a little down
       (-1) to straight down and back (1), the stroke a real turtle's; dir -1 for the far side.
       Off the body's side plane (sk_pt3): as the turtle turns to the glass the two paddles
       part, the near one to its near side, instead of folding onto its middle */
    float ang = 0.35f + 0.8f * st, L = 9.5f;
    float c1 = fast_cos(ang), s1 = fast_sin(ang), c2 = fast_cos(ang + 0.5f), s2 = fast_sin(ang + 0.5f);
    float lx[4] = { bx + 2.5f, bx + 1.0f + c1 * L, bx - 1.0f + c2 * L * 0.85f, bx - 2.5f };
    float ly[4] = { by, by + s1 * L * dir, by + s2 * L * 0.85f * dir, by + 0.5f };
    float X[4], Y[4];
    for (int i = 0; i < 4; i++) sk_pt3(k, lx[i], ly[i], dir * 6.0f, 0, &X[i], &Y[i]);
    src_t sc = src_color(rgb, k->c->dim);
    fill_poly_s(k->c, X, Y, 4, &sc, a);
}
/* the shell's volume (2026-10-11, Alvin: "looks flat when it turns around"): a shaded dome
   cross-section at the carapace's middle that widens as the turtle turns to the glass (as the
   lagoon's volume does), lit from the upper left - a dark rim, the shell, a shine, a shadow
   under the curve - and the plastron's flat slab beneath it; the head gets a round section
   of its own. Side-on (zq ~ 0) it draws nothing. */
static void turtle_volume(const sk_t *k, uint32_t shell, uint32_t plastron, uint32_t skin, uint32_t dark, uint32_t light) {
    float w = fabsf(sk_yaw(k, -2)), q = sqrtf(fmaxf(0, 1 - w * w));
    float RX = 8.5f * q * k->s, RY = 7.2f * k->s;
    if (RX < 1) return;
    float X, Y; sk_pt(k, -2, -1.8f, &X, &Y);
    fill_ellipse(k->c, X, Y, RX + 0.7f, RY + 0.7f, dark, 255);                           /* the rim */
    fill_ellipse(k->c, X, Y, RX, RY, shell, 255);
    fill_ellipse(k->c, X - RX * 0.3f, Y - RY * 0.35f, RX * 0.5f, RY * 0.4f, light, 70);  /* the shine */
    fill_ellipse(k->c, X, Y + RY * 0.55f, RX * 0.9f, RY * 0.45f, dark, 60);              /* the shadow under the curve */
    fill_ellipse(k->c, X, Y + RY * 0.72f, RX * 0.95f, RY * 0.28f, plastron, 255);         /* the plastron's slab */
    float HX, HY; sk_pt(k, 16.5f, -0.3f, &HX, &HY);
    fill_ellipse(k->c, HX, HY, 3.2f * q * k->s, 3.0f * k->s, skin, 255);                   /* the head's round section */
    fill_ellipse(k->c, HX - 1.0f * q * k->s, HY - 1.0f * k->s, 1.2f * q * k->s, 1.0f * k->s, light, 60);
}
static void turtle_gulp(const sk_t *k, float mx, float my) {
    if (k->f->sp_mode != SPM_GULP) return;
    for (int q = 0; q < 2; q++) {                                /* a bubble or two off the beak */
        float u = k->t * 1.5f + q * 0.5f; u -= floorf(u);
        sk_dot(k, mx + q, my - 2 - u * 7, 1.1f, 1.1f, 0x9fd8e2, (int)(150 * (1 - u)), false);
    }
}
#include "living_lagoon.inc"
#include "theme_creatures.inc"
#include "blackwater.inc"

static void draw_seahorse(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_SEAHORSE);return;}
    const fish_t *f = k->f; float t = k->t; int v = f->variant & 3;
    sk_lean(k, 0.24f * clamp01(f->speed / 25));
    /* the dorsal fan, fluttering */
    {
        float ph = f->jet * TAU + t * 4, amp = 0.8f + 0.9f * clamp01(f->speed / 10);
        float lx[5] = { -4.4f, -7.6f + amp * fast_sin(ph), -8.6f + amp * fast_sin(ph + 1.4f), -7.8f + amp * fast_sin(ph + 2.8f), -4.6f };
        float ly[5] = { -2.0f, -1.2f, 1.8f, 4.6f, 5.2f };
        sk_poly(k, lx, ly, 5, mix(k->fin, 0xffffff, 0.3f), 140);
    }
    /* the tail, curling forward under the belly */
    static const float TLX[10] = { -1.5f, -2.6f, -2.9f, -2.1f, 0.0f, 3.0f, 5.2f, 5.3f, 3.6f, 2.3f };
    static const float TLY[10] = { 6.0f, 10.0f, 14.0f, 17.5f, 20.0f, 21.0f, 19.5f, 17.0f, 16.0f, 17.2f };
    static const float TLR[10] = { 3.4f, 2.8f, 2.3f, 1.9f, 1.6f, 1.4f, 1.2f, 1.05f, 0.9f, 0.8f };
    /* anchored (tank.h), it grips the stem at (5, 10): down, then round it */
    static const float ALX[10] = { -1.5f, -2.6f, -2.0f, 0.2f, 3.0f, 5.2f, 7.4f, 7.2f, 5.2f, 3.4f };
    static const float ALY[10] = { 6.0f, 9.5f, 12.8f, 14.4f, 13.4f, 13.0f, 11.0f, 8.4f, 7.5f, 8.6f };
    bool held = f->anchor >= 0 && f->species == SP_SEAHORSE && f->speed < 6;
    const float *TX0 = held ? ALX : TLX, *TY0 = held ? ALY : TLY;
    float tlx[10], tly[10], sw = held ? 0 : 0.9f * fast_sin(t * 1.3f + f->wander);
    for (int i = 0; i < 10; i++) { tlx[i] = TX0[i] + sw * i * (1 / 9.0f); tly[i] = TY0[i]; }
    sk_tube(k, tlx, tly, TLR, 10, k->body);
    for (int i = 1; i < 6; i++) sk_dot(k, tlx[i], tly[i], TLR[i] * 0.9f, 0.35f, k->fin, 120, true);   /* its rings */
    /* the trunk: a pot belly */
    {
        static const float lx[9] = { 2.5f, 5.0f, 5.6f, 3.6f, 0.0f, -3.6f, -5.0f, -5.0f, -3.5f };
        static const float ly[9] = { -9.0f, -4.0f, 2.0f, 7.0f, 8.6f, 6.0f, 2.0f, -4.0f, -9.0f };
        sk_poly(k, lx, ly, 9, k->body, 255);
        float X, Y; sk_pt(k, 0, 0, &X, &Y);    /* its depth, turned toward the glass */
        if (k->zq * 4.5f * k->s >= 1) fill_ellipse(k->c, X, Y, k->zq * 4.5f * k->s, 8 * k->s, k->body, 255);
    }
    sk_line(k, -0.5f, -8, 0.8f, -12.5f, 3.4f, 3.0f, k->body);                   /* the neck */
    sk_dot(k, 1.2f, -14.5f, 4.0f, 3.5f, k->body, 255, false);                  /* the head */
    sk_line(k, 4, -14.6f, 10.5f, -13.6f, 1.7f, 1.15f, k->body);                 /* the snout */
    sk_dot(k, 11, -13.6f, 1.2f, 1.45f, k->body, 255, false);
    sk_tri(k, -1.4f, -17.0f, 0.0f, -20.8f, 1.5f, -17.6f, k->fin, 255);          /* the coronet */
    sk_tri(k, 0.8f, -17.6f, 2.4f, -20.2f, 3.4f, -17.0f, k->fin, 255);
    sk_dot(k, -1.8f, -12.6f, 1.5f, 1.1f, k->fin, 120 + (int)(60 * fast_sin(t * 20)), true);   /* the pectoral, a blur */
    for (int j = 0; j < 5; j++) sk_dot(k, 2.6f, -6 + j * 3.2f, 2.6f, 0.45f, k->fin, 110, true);   /* belly plates */
    if (v == 0) {                                  /* bands */
        static const float BY[4] = { -5.0f, -0.5f, 4.0f, -12.0f }, BW[4] = { 5.0f, 5.4f, 4.6f, 2.8f };
        for (int i = 0; i < 4; i++) sk_mark(k, 0.3f, BY[i], BW[i], 1.0f, k->acc, 170);
        for (int i = 1; i < 6; i += 2) sk_mark(k, tlx[i], tly[i], TLR[i], 0.8f, k->acc, 170);
    } else if (v == 1) {                           /* speckles */
        static const float SX[14] = { -2, 1, 3.5f, -3, 0.5f, 3.8f, -1.8f, 1.5f, -0.5f, 2.5f, -1.5f, -2.4f, -2.6f, 0 };
        static const float SY[14] = { -6, -3, -6, 0, 1.5f, 2, 4.5f, 5.5f, -11, -15.8f, -15.5f, 10, 14, 19.5f };
        for (int i = 0; i < 14; i++) sk_mark(k, SX[i], SY[i], 0.65f, 0.65f, k->acc, 230);
    } else if (v == 2) {                           /* white spots */
        static const float SX[8] = { -2.5f, 2, -1, 3, 0, -2.4f, -1.6f, 3.6f }, SY[8] = { -5, -2, 3, 4.5f, -13, 12, 17.5f, -8 };
        for (int i = 0; i < 8; i++) sk_mark(k, SX[i], SY[i], 1.15f, 1.15f, k->acc, 240);
    } else if (k->marked) {                        /* spines: along the back, the belly and the crown */
        static const float S[10][4] = { { -4.8f, -6, -1, -0.2f }, { -5.1f, -1, -1, 0 }, { -4.6f, 3.6f, -1, 0.2f }, { -2.8f, -10.2f, -0.8f, -0.6f },
                                        { -1.2f, -17.6f, -0.2f, -1 }, { 4.8f, -4.5f, 1, -0.2f }, { 5.5f, 1.5f, 1, 0 }, { -3.1f, 11, -1, 0 },
                                        { -3.3f, 15, -1, 0.3f }, { 3.6f, 6.5f, 0.8f, 0.6f } };
        for (int i = 0; i < 10; i++) {
            float bx = S[i][0], by = S[i][1], dx = S[i][2], dy = S[i][3];
            sk_tri(k, bx + dy * 0.9f, by - dx * 0.9f, bx + dx * 2.6f, by + dy * 2.6f, bx - dy * 0.9f, by + dx * 0.9f, k->acc, (int)(255 * k->fade));
        }
    }
    sk_eye(k, 2.6f, -15.2f, 1.6f, false);
}

/* ---- octopus: a mantle and eight arms, two poses blended by speed:
   crawling (the mantle up behind, the arms splayed on the floor, reaching
   in turn) and jetting (the mantle leading, the arms streaming behind
   together, flaring between pulses). Camouflage takes on f->camo_rgb and
   the pattern fades with it. ---- */
static void draw_octopus(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_OCTOPUS);return;}
    const fish_t *f = k->f; float t = k->t; int v = f->variant & 3;
    float jk = k->pose;
    float camo = clamp01(f->camo);
    if (camo > 0) {
        k->body = mix(k->body, f->camo_rgb, camo);
        k->fin = mix(k->fin, mix(f->camo_rgb, 0x000000, 0.3f), camo);
        k->fade = 1 - camo;
    }
    draw_ink(k);
    /* the head (eyes, arm roots) and the mantle's axis: up-back crawling,
       straight back jetting - facing is where the ARMS point (tank.h), so
       a jet travels mantle first with the arms trailing behind */
    float hx = 2.5f - 1.5f * jk, hy = -2.5f + 2.5f * jk;
    float ang = -2.3f - (TAU * 0.5f - 2.3f) * jk;             /* the mantle's axis */
    float ax = fast_cos(ang + TAU), ay = fast_sin(ang + TAU);
    float mrx = 7.8f + 2.7f * jk, mry = 6.8f - 1.3f * jk;
    float jp = f->jet - floorf(f->jet);                          /* the jet's phase: thrust below 0.3 */
    float pulse = jp < 0.3f ? fast_sin(jp * (TAU * 0.5f / 0.3f)) : 0;   /* the mantle squeezed */
    mry *= 1 - 0.12f * pulse * jk;
    float mcx = hx + ax * mrx * 0.85f, mcy = hy + ay * mrx * 0.85f;
    float rx0 = hx + 3.0f * jk, ry0 = hy + 3.2f * (1 - jk) + 0.5f * jk;   /* the arms' root */
    float floor_y = 9 + 40 * jk;                                 /* crawling, the arms lie along the ground (tank.c: walk_off 9) */
    /* the arms: [0..3] the near side (drawn in front), [4..7] the far side */
    static const float AC[8] = { 0.15f, 0.95f, 1.75f, 2.65f, 0.55f, 1.35f, 2.2f, 3.0f };
    float AX[8][6], AY[8][6], AR[6];
    for (int j = 0; j < 6; j++) AR[j] = 2.1f - 1.65f * j / 5.0f;
    float open = jp < 0.3f ? 0.1f : 0.1f + 0.9f * fast_sin((jp - 0.3f) * (TAU * 0.5f / 0.7f));   /* closed on the thrust, flaring in the glide */
    for (int i = 0; i < 8; i++) {
        float aj = ((i & 3) - 1.5f + (i >= 4 ? 0.5f : 0)) * 0.07f * (0.5f + 1.5f * open);
        float ac = AC[i] + 0.22f * fast_sin(t * (1.6f + f->speed * 0.04f) + i * 1.9f);
        float a = ac + (aj - ac) * jk;
        float cc = (AC[i] < 1.57f ? -0.30f : 0.30f) * (1 - jk) + 0.08f * fast_sin(t * 5 + i * 1.1f) * jk;
        float L = (19 + 5 * jk) * (1 + 0.08f * fast_sin(t * 1.7f + i * 2.3f)), sl = L / 5;
        float x = rx0 + (i >= 4 ? 0.8f : 0), y = ry0 - (i >= 4 ? 0.6f : 0);
        for (int j = 0; j < 6; j++) {
            AX[i][j] = x; AY[i][j] = y;
            x += sl * fast_cos(a + TAU); y += sl * fast_sin(a + TAU);
            if (y > floor_y) { y = floor_y; a = fast_cos(a + TAU) >= 0 ? 0.05f : TAU * 0.5f - 0.05f; }   /* on the ground: along it */
            a += cc * (0.4f + j * 0.36f);
        }
    }
    for (int i = 4; i < 8; i++) {                                              /* far arms: half the joints, half hidden */
        float fx[3] = { AX[i][0], AX[i][2], AX[i][5] }, fy[3] = { AY[i][0], AY[i][2], AY[i][5] }, fr[3] = { AR[0], AR[2], AR[5] };
        sk_tube(k, fx, fy, fr, 3, k->fin);
    }
    /* the mantle: an egg, fuller at its apex */
    {
        float lx[16], ly[16];
        for (int i = 0; i < 16; i++) {
            float u = mrx * g_u16c[i], w = mry * g_u16s[i] * (1 + 0.15f * g_u16c[i]);
            lx[i] = mcx + ax * u - ay * w; ly[i] = mcy + ay * u + ax * w;
        }
        sk_poly(k, lx, ly, 16, k->body, 255);
        float X, Y; sk_pt(k, mcx, mcy, &X, &Y);
        if (k->zq * mry * k->s >= 1) fill_ellipse(k->c, X, Y, k->zq * mry * k->s, mry * k->s, k->body, 255);
    }
    sk_dot(k, hx, hy, 5.0f - 0.8f * jk, 4.4f - 0.3f * jk, k->body, 255, false);   /* the head */
    float ex = hx + 1.8f - 1.6f * jk, ey = hy - 2.8f;
    sk_dot(k, ex, ey, 2.5f, 2.3f, k->body, 255, false);                        /* the eye's bump */
    sk_dot(k, rx0, ry0 - 0.5f, 4.2f, 2.6f, k->body, 255, false);              /* the web */
    for (int i = 0; i < 4; i++) sk_tube(k, AX[i], AY[i], AR, 6, k->body);      /* near arms */
    if (!k->asleep && jk < 0.5f)                                               /* suckers under the near arms */
        for (int i = 0; i < 4; i++) for (int j = 2; j < 4; j += 2)
            sk_dot(k, AX[i][j], AY[i][j] + AR[j] * 0.5f, 0.5f, 0.5f, mix(k->body, 0xffffff, 0.5f), (int)(170 * (1 - jk)), false);
    /* the pattern, at mantle-local (u, w) in radii */
    #define OC_PT(u, w, X, Y) do { float _u = (u) * mrx, _w = (w) * mry; X = mcx + ax * _u - ay * _w; Y = mcy + ay * _u + ax * _w; } while (0)
    if (v == 0) {                                  /* mottled */
        static const float MU[8] = { 0.5f, -0.1f, -0.55f, 0.15f, 0.6f, -0.5f, 0.0f, 0.3f }, MW[8] = { -0.35f, 0.4f, -0.2f, -0.5f, 0.35f, 0.45f, 0.0f, 0.1f };
        for (int i = 0; i < 8; i++) {
            float X, Y; OC_PT(MU[i], MW[i], X, Y);
            if (i & 1) sk_mark(k, X, Y, 1.4f, 1.2f, mix(k->body, k->acc, 0.6f), 190);
            else       sk_mark(k, X, Y, 2.1f, 1.7f, k->fin, 170);
        }
        for (int i = 0; i < 4; i++) sk_mark(k, AX[i][3], AY[i][3], 1.0f, 1.0f, k->fin, 150);
    } else if (v == 1) {                           /* blue rings */
        static const float RU[7] = { 0.55f, 0.0f, -0.55f, 0.25f, -0.25f, 0.6f, -0.1f }, RW[7] = { -0.3f, -0.45f, -0.1f, 0.4f, 0.45f, 0.35f, 0.05f };
        for (int i = 0; i < 7; i++) {
            float X, Y; OC_PT(RU[i], RW[i], X, Y);
            sk_mark(k, X, Y, 1.7f, 1.7f, k->acc, 255); sk_mark(k, X, Y, 0.8f, 0.8f, k->body, 255);
        }
        sk_mark(k, hx - 1.5f, hy + 1.5f, 1.4f, 1.4f, k->acc, 255); sk_mark(k, hx - 1.5f, hy + 1.5f, 0.6f, 0.6f, k->body, 255);
        for (int i = 0; i < 4; i++) for (int j = 2; j <= 2; j += 2) {
            sk_mark(k, AX[i][j], AY[i][j], AR[j] * 0.85f, AR[j] * 0.85f, k->acc, 255);
            sk_mark(k, AX[i][j], AY[i][j], AR[j] * 0.4f, AR[j] * 0.4f, k->body, 255);
        }
    } else if (v == 2) {                           /* stripes: banded arms, barred mantle */
        for (int i = 0; i < 4; i++) for (int j = 1; j < 6; j += 2)
            sk_mark(k, AX[i][j], AY[i][j], AR[j] * 1.05f, AR[j] * 1.05f, k->acc, 240);
        for (int i = -1; i <= 1; i++) { float X, Y; OC_PT(i * 0.5f, 0, X, Y); sk_mark(k, X, Y, 1.1f, mry * 0.8f, k->acc, 200); }
    } else {                                       /* spots */
        static const float SU[9] = { 0.6f, 0.2f, -0.3f, -0.65f, 0.4f, -0.1f, 0.65f, -0.45f, 0.05f }, SW[9] = { -0.2f, -0.55f, -0.4f, 0.1f, 0.4f, 0.2f, 0.3f, 0.5f, -0.05f };
        for (int i = 0; i < 9; i++) { float X, Y; OC_PT(SU[i], SW[i], X, Y); sk_mark(k, X, Y, 0.9f, 0.9f, k->acc, 230); }
        for (int i = 0; i < 4; i++) sk_mark(k, AX[i][2], AY[i][2], 0.7f, 0.7f, k->acc, 220);
    }
    #undef OC_PT
    /* the eye: the octopus's bar of a pupil */
    {
        float X, Y; sk_pt(k, ex, ey, &X, &Y);
        float R = 1.7f * k->s; if (R < 1.3f) R = 1.3f;
        if (k->asleep) fill_ellipse(k->c, X, Y, R, R * 0.32f + 0.4f, 0x9fb4b8, 200);
        else { fill_ellipse(k->c, X, Y, R, R, 0xf2ead0, 255); fill_ellipse(k->c, X, Y, R * 0.8f, R * 0.32f + 0.3f, 0x05080a, 255); }
    }
}

/* ---- pufferfish: boxy and round-faced, big eyes, a beak of a mouth, small
   fins sculling. f->puff swells it into a near-round ball and the spines
   stand out. ---- */
static void draw_puffer(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_PUFFER);return;}
    const fish_t *f = k->f; float t = k->t; int v = f->variant & 3;
    float p = clamp01(f->puff);
    static float flat[16][2]; static bool built;
    if (!built) {                                  /* a squared-off oval, once */
        for (int i = 0; i < 16; i++) {
            float c = g_u16c[i], s = g_u16s[i];
            flat[i][0] = 13 * (c < 0 ? -1 : 1) * powf(fabsf(c), 0.65f);
            flat[i][1] = 10 * (s < 0 ? -1 : 1) * powf(fabsf(s), 0.65f);
        }
        built = true;
    }
    float bx[16], by[16];
    for (int i = 0; i < 16; i++) {
        bx[i] = flat[i][0] + (14.5f * g_u16c[i] - flat[i][0]) * p;
        by[i] = flat[i][1] + (14.0f * g_u16s[i] - flat[i][1]) * p;
    }
    #define PF(x) ((x) * (1 + 0.1f * p))
    #define PFY(y) ((y) * (1 + 0.36f * p))
    /* the tail, sculling */
    float w = 2.2f * fast_sin(f->jet * TAU), tb = -11.5f - 2.5f * p;
    { float lx[4] = { tb, tb - 7.5f, tb - 8.0f, tb }, ly[4] = { -3, -5.5f + w, 4.5f + w, 3 }; sk_poly(k, lx, ly, 4, k->fin, 255); }
    float w2 = 1.4f * fast_sin(t * 9 + 1);
    sk_tri(k, PF(-3), PFY(-9.0f), PF(-8) + w2, PFY(-13.5f), PF(-9.5f), PFY(-7.0f), k->fin, 255);   /* dorsal */
    sk_tri(k, PF(-3), PFY(9.0f), PF(-8) - w2, PFY(13.5f), PF(-9.5f), PFY(7.0f), k->fin, 255);      /* anal */
    /* spines, standing out with the puff */
    if (p > 0.08f) {
        uint32_t sc = mix(k->fin, 0xffffff, 0.35f);
        float L = 1 + 0.32f * p;
        for (int i = 0; i < 16; i++) {
            if (g_u16c[i] < -0.9f) continue;       /* not through the tail */
            for (int h = 0; h < 2; h++) {          /* two rows, staggered */
                float c = h ? fast_cos(i * (TAU / 16) + TAU / 32 + TAU) : g_u16c[i];
                float s = h ? fast_sin(i * (TAU / 16) + TAU / 32) : g_u16s[i];
                float r = h ? 0.9f : 1.0f, ex = bx[i] * r, ey = by[i] * r;
                if (h) { int j = (i + 1) & 15; ex = (bx[i] + bx[j]) * 0.5f * r; ey = (by[i] + by[j]) * 0.5f * r; }
                sk_tri(k, ex * 0.95f - s * 1.0f, ey * 0.95f + c * 1.0f, ex * L, ey * L, ex * 0.95f + s * 1.0f, ey * 0.95f - c * 1.0f, sc, 255);
            }
        }
    }
    sk_poly(k, bx, by, 16, k->body, 255);
    {
        float X, Y; sk_pt(k, 0, 0, &X, &Y);
        float rx = k->zq * (9 + 4 * p) * k->s;
        if (rx >= 1) fill_ellipse(k->c, X, Y, rx, (10 + 4 * p) * k->s, k->body, 255);
    }
    { float lx[7], ly[7]; for (int i = 0; i < 7; i++) { lx[i] = bx[i + 1] * 0.97f; ly[i] = by[i + 1] * 0.97f; }   /* the pale belly */
      sk_poly(k, lx, ly, 7, mix(k->body, 0xffffff, 0.5f), 255); }
    if (v == 0) {                                  /* spots */
        static const float SX[12] = { -8, -4, 0, 4, 8, -6, -2, 2, -9.5f, 6, -1, 9.5f }, SY[12] = { -5, -7, -7.5f, -6.5f, -4.5f, -1.5f, -3, -2.5f, 0.5f, -1, 1.5f, -1.5f };
        for (int i = 0; i < 12; i++) sk_mark(k, PF(SX[i]), PFY(SY[i]), 1.15f, 1.15f, k->acc, 235);
    } else if (v == 1) {                           /* the dogface's dark mask */
        sk_mark(k, PF(-1), PFY(-6.5f), 10, 3.2f, k->fin, 130);
        sk_mark(k, PF(7.5f), PFY(-1.5f), 5.2f, 4.6f, k->acc, 175);
        sk_mark(k, PF(12), PFY(1.5f), 2.2f, 2.4f, k->acc, 200);
    } else if (v == 2) {                           /* black saddles */
        static const float SX[4] = { -9, -3, 3.5f, 9.5f }, RX[4] = { 2.2f, 2.5f, 2.5f, 1.7f }, RY[4] = { 3.6f, 4.4f, 4.4f, 2.6f };
        for (int i = 0; i < 4; i++) sk_mark(k, PF(SX[i]), PFY(-9.8f + RY[i] * 0.95f) , RX[i], RY[i] * (1 + 0.3f * p), k->acc, 240);
    } else {                                       /* golden: plain, a darker crown */
        sk_mark(k, PF(-1), PFY(-7), 10, 2.6f, k->fin, 90);
    }
    /* the pectoral fin, a blur behind the eye */
    { float q = 1.2f * fast_sin(f->jet * TAU * 2 + 1);
      sk_tri(k, PF(2.5f), PFY(0.5f), PF(-1.5f) + q, PFY(-1.8f), PF(-1.0f) + q, PFY(3.0f), mix(k->fin, 0xffffff, 0.15f), 150); }
    float mx = 13.0f + 1.4f * p;
    sk_dot(k, mx, 1.6f, 1.4f, 1.15f, mix(k->body, 0xffffff, 0.35f), 255, false);   /* the beak */
    sk_dot(k, mx + 0.4f, 1.6f, 0.5f, 0.5f, 0x2a1e18, 255, false);
    /* the big eye */
    float ex = PF(6.5f), ey = PFY(-3.6f);
    if (k->asleep) sk_eye(k, ex, ey, 3.0f, false);
    else {
        sk_dot(k, ex, ey, 3.1f, 3.1f, 0xf4f0d8, 255, false);
        sk_dot(k, ex + 0.5f, ey, 2.0f, 2.0f, 0x0c1a12, 255, false);
        sk_dot(k, ex + 1.0f, ey - 0.9f, 0.6f, 0.6f, 0xffffff, 230, false);
    }
    #undef PF
    #undef PFY
}

/* ---- anglerfish: a big head and a gape of needle teeth on a small body,
   the illicium from its brow and the esca at its tip, bobbing. The esca
   GLOWS: its colour skips the night dim (the card does the same), with a
   halo, brightest as f->lure rises. ---- */
static void draw_angler(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_ANGLER);return;}
    const fish_t *f = k->f; float t = k->t; int v = f->variant & 3;
    float w = 1.2f * fast_sin(t * 3 + f->wander);
    { float lx[5] = { -13.5f, -21, -23.5f, -21, -13.5f }, ly[5] = { -2.5f, -6.5f + w, w, 6.5f + w, 2.5f }; sk_poly(k, lx, ly, 5, k->fin, 255); }
    sk_tri(k, -5, -10, -9.5f, -12.5f, -12.5f, -6.5f, k->fin, 255);             /* dorsal */
    static const float BLX[12] = { 15.5f, 13, 9, 2, -6, -12, -15, -14, -9, -1, 8, 14 };
    static const float BLY[12] = { 4.5f, -4, -9, -11.5f, -10, -6, -1, 4, 8, 10.5f, 10, 7.5f };
    sk_poly(k, BLX, BLY, 12, k->body, 255);
    { float X, Y; sk_pt(k, 0, 0, &X, &Y); float rx = k->zq * 9 * k->s; if (rx >= 1) fill_ellipse(k->c, X, Y, rx, 10.5f * k->s, k->body, 255); }
    if (v == 0) sk_mark(k, -1, 5.5f, 9, 3.5f, mix(k->body, 0xffffff, 0.12f), 200);   /* abyss: a faint belly, nothing more */
    else if (v == 2) {                             /* blotches */
        static const float B[7][3] = { { -4, -6, 3 }, { 3, -7.5f, 2.4f }, { -9.5f, 0.5f, 2.6f }, { -2, 4, 3 }, { 5, 6.5f, 2 }, { -10, -5, 1.6f }, { 1, -1, 1.6f } };
        for (int i = 0; i < 7; i++) sk_mark(k, B[i][0], B[i][1], B[i][2], B[i][2] * 0.85f, i < 5 ? k->fin : mix(k->body, 0xffffff, 0.3f), 190);
    } else {                                       /* warts, along the outline and over the head */
        static const float WX[13] = { 10, 4, -3, -9, -13, -14, -11, -5, 3, 9.5f, 0, -6, 6.5f }, WY[13] = { -8, -11, -11, -8.5f, -4, 2, 6.5f, 9.5f, 10.5f, 9.5f, -6.5f, -1, -9.5f };
        uint32_t wc = v == 1 ? mix(k->body, k->acc, 0.45f) : mix(k->body, 0xffffff, 0.35f);
        for (int i = 0; i < 13; i++) sk_mark(k, WX[i], WY[i], 1.25f, 1.25f, wc, 255);
        if (v == 3) for (int i = 0; i < 6; i++) sk_mark(k, WX[i * 2] * 0.6f, WY[i * 2] * 0.55f, 0.9f, 0.9f, mix(k->body, 0x000000, 0.2f), 200);
    }
    /* the gape and its needles */
    float g = 0.8f * (0.5f + 0.5f * fast_sin(t * 1.1f + f->wander));
    sk_tri(k, 15.0f, -3.4f, 4.5f, 0.8f, 15.8f, 5.4f + g, 0x0c0608, 255);
    for (int i = 0; i < 4; i++) {
        float u = 0.12f + i * 0.22f;
        float px = 14.8f + (5.5f - 14.8f) * u, py = -3.2f + (0.6f + 3.2f) * u;
        sk_tri(k, px - 0.75f, py - 0.2f, px + 0.1f, py + 2.2f, px + 0.75f, py - 0.2f, 0xf2eedc, 255);
        float qx = 15.6f + (5.5f - 15.6f) * u, qy = 5.4f + g + (0.9f - 5.4f - g) * u;
        sk_tri(k, qx - 0.75f, qy + 0.3f, qx - 0.1f, qy - 2.6f, qx + 0.75f, qy + 0.3f, 0xf2eedc, 255);
    }
    { float lx[4] = { -4, 1, 2.5f, -3.5f }, ly[4] = { 7, 7.5f, 12.5f, 12 }; sk_poly(k, lx, ly, 4, k->fin, 255); }   /* the pectoral it walks on */
    sk_eye(k, 6.5f, -6.8f, 1.5f, false);
    /* the illicium and its esca */
    float bxo = 1.0f * fast_sin(t * 2.1f + f->wander), byo = 1.3f * fast_sin(t * 1.7f + 1);
    { float lx[4] = { 4, 6, 9.5f, 13.5f + bxo }, ly[4] = { -11, -16, -19.5f, -19 + byo }, r[4] = { 0.8f, 0.65f, 0.55f, 0.5f };
      sk_tube(k, lx, ly, r, 4, k->fin); }
    float X, Y; sk_pt(k, 13.5f + bxo, -19 + byo, &X, &Y);
    float br = 0.35f + 0.65f * clamp01(f->lure), s = k->s;
    src_t halo = src_color(k->acc, 1.0f), core = src_color(mix(k->acc, 0xffffff, 0.45f * br), 0.55f + 0.45f * br);
    fill_ellipse_s(k->c, X, Y, (5 + 4 * br) * s, (5 + 4 * br) * s, &halo, (int)(18 + 40 * br));
    fill_ellipse_s(k->c, X, Y, (2.6f + br) * s, (2.6f + br) * s, &halo, (int)(60 + 90 * br));
    fill_ellipse_s(k->c, X, Y, 1.6f * s + 0.3f, 1.6f * s + 0.3f, &core, 255);
}

/* ---- electric eel: a long round body in segments, a wave travelling
   down it (f->jet's phase, and more of it with speed), a blunt head, the
   belly in f->accent and the long anal fin rippling under it, no tail fin.
   A startle's f->spark crackles along it, undimmed. ---- */
#define EEL_N 14
static void draw_eel(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_EEL);return;}
    const fish_t *f = k->f; float t = k->t; int v = f->variant & 3;
    float lx[EEL_N], ly[EEL_N], r[EEL_N];
    float amp = 0.35f + 0.65f * clamp01(f->speed / 35), ph = f->jet * TAU;
    for (int i = 0; i < EEL_N; i++) {
        float u = i * (1.0f / (EEL_N - 1));
        lx[i] = 32 - 64 * u;                                    /* 1.6 x a fish's length (tank.c's bounds); the mouth at the front */
        ly[i] = (0.4f + 5.5f * u) * amp * fast_sin(ph - u * TAU * 1.25f + TAU * 2);
        r[i] = i == 0 ? 3.3f : u < 0.4f ? 4.8f : 4.8f - (u - 0.4f) * (4.0f / 0.6f);
    }
    /* the anal fin: a ridge the whole way under, rippling */
    {
        float fx[EEL_N], fy[EEL_N], fr[EEL_N]; int n = 0;
        for (int i = 5; i < EEL_N; i += 2, n++) {   /* every other point: it is a fin */
            fx[n] = lx[i]; fy[n] = ly[i] + r[i] * 0.7f;
            fr[n] = 1.3f + 0.5f * fast_sin(t * 10 - i * 1.3f + TAU * 4) + (i < EEL_N - 1 ? 0.4f : -0.6f);
        }
        sk_tube(k, fx, fy, fr, n, mix(k->acc, k->fin, 0.35f));
    }
    sk_tube(k, lx, ly, r, EEL_N, k->body);
    float sx[EEL_N], sy[EEL_N], sr[EEL_N];
    /* the back's shade: a round body, not a ribbon */
    int ns = 0;
    for (int i = 1; i < EEL_N - 1; i += 2, ns++) { sx[ns] = lx[i]; sy[ns] = ly[i] - r[i] * 0.5f; sr[ns] = r[i] * 0.42f; }
    sk_tube(k, sx, sy, sr, ns, v == 2 ? mix(k->fin, k->body, 0.4f) : k->fin);
    /* the belly */
    int nb = v == 1 ? EEL_N - 2 : 9;
    for (int i = 0; i < nb; i++) { sx[i] = lx[i + 1]; sy[i] = ly[i + 1] + r[i + 1] * (v == 1 ? 0.4f : 0.5f); sr[i] = r[i + 1] * (v == 1 ? 0.55f : 0.45f); }
    if (k->marked || v != 1) sk_tube(k, sx, sy, sr, nb, k->acc);
    if (v == 2 && k->marked) for (int i = 3; i < EEL_N - 2; i += 2) sk_dot(k, lx[i], ly[i] - r[i] * 0.05f, 0.7f, 0.7f, mix(k->body, 0xffffff, 0.4f), 200, true);   /* bronze: the lateral pores */
    if (v == 3) for (int i = 3; i < EEL_N - 1; i++) sk_mark(k, lx[i] + 1.5f, ly[i] + ((i & 1) ? -1.6f : 0.6f), 1.1f, 1.1f, k->acc, 235);
    /* the head: the mouth's line, a small eye, a little pectoral */
    float hy = ly[0];
    sk_line(k, 35, hy + 1.4f, 29.0f, hy + 1.8f, 0.35f, 0.35f, mix(k->fin, 0x000000, 0.5f));
    sk_dot(k, 24.5f, ly[1] + 2.2f, 2.0f, 1.3f, k->fin, 210, true);
    sk_eye(k, 30.5f, hy - 1.6f, 1.0f, true);
    if (f->sp_mode == SPM_GULP) {                  /* the gulp at the surface: a bubble or two off the mouth */
        src_t b = src_color(0x9fd8e2, k->c->dim);
        for (int q = 0; q < 2; q++) {
            float u = t * 1.5f + q * 0.5f; u -= floorf(u);
            sk_dot_s(k, 35 + q, hy - 2 - u * 7, 1.1f, 1.1f, &b, (int)(150 * (1 - u)), false);
        }
    }
    /* the spark: zig-zags off the body, a new set every 50 ms */
    if (f->spark > 0) {
        src_t core = src_color(0xeafcff, 1.0f), glow = src_color(0x7fe8ff, 1.0f);
        unsigned fr = (unsigned)(t * 20);
        for (int q = 0; q < 6; q++) {
            uint32_t h = (fr * 7u + (unsigned)q * 13u + 1u) * 2654435761u;
            int i = 2 + (int)((h >> 8) % (EEL_N - 4));
            float side = (h >> 20) & 1 ? -1.0f : 1.0f;
            float X[4], Y[4], R[4];
            sk_pt(k, lx[i], ly[i] + side * r[i] * 0.9f, &X[0], &Y[0]);
            float ax = ((h >> 4) & 15) / 15.0f - 0.5f;
            for (int j = 1; j < 4; j++) {
                float zig = (j & 1 ? 0.7f : -0.7f) + ax;
                X[j] = X[j - 1] + zig * 2.6f * k->s; Y[j] = Y[j - 1] + side * 3.0f * k->s;
            }
            for (int j = 0; j < 4; j++) R[j] = 1.4f;
            tube_s(k->c, X, Y, R, 4, &glow, 70);
            for (int j = 0; j < 4; j++) R[j] = 0.55f;
            tube_s(k->c, X, Y, R, 4, &core, 255);
        }
    }
}

/* ---- hammerhead: the cephalofoil, eyes at its tips, sweeping side to
   side (seen obliquely, so side-on it stands as a bar at the nose and turns
   into the T as the shark comes head-on), the tall first dorsal, pectorals,
   the heterocercal tail beating slowly, a pale belly. ---- */
static void draw_shark(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_SHARK);return;}
    const fish_t *f = k->f; float t = k->t; int v = f->variant & 3;
    uint32_t tip = v == 2 ? mix(k->fin, 0x000000, 0.45f) : k->fin;
    float tw = 1.8f * fast_sin(f->jet * TAU);
    sk_tri(k, 7.5f, 3.0f, 1.0f, 9.5f, 4.0f, 3.5f, mix(k->fin, 0x000000, 0.25f), 255);   /* the far pectoral */
    sk_tri(k, -17.5f, -2.4f, -31.0f, -13.0f + tw, -23.0f, 0.0f, k->fin, 255);          /* the tail: the long upper lobe */
    sk_tri(k, -19.5f, 0.0f, -27.0f, 6.8f + tw * 0.6f, -23.5f, 1.2f, k->fin, 255);      /* ... and the short lower */
    if (v == 2) sk_tri(k, -27.0f, -9.2f + tw * 0.85f, -31.0f, -13.0f + tw, -25.5f, -6.5f + tw * 0.6f, tip, 255);
    sk_tri(k, 2.0f, -5.6f, -6.0f, -16.5f, -9.5f, -4.6f, k->fin, 255);                   /* the first dorsal */
    if (v == 2) sk_tri(k, -3.6f, -12.0f, -6.0f, -16.5f, -7.3f, -11.8f, tip, 255);
    sk_tri(k, -14.5f, -2.8f, -17.8f, -6.5f, -19.0f, -2.2f, k->fin, 255);               /* the second */
    static const float BLX[13] = { 18, 13, 4, -6, -14, -21, -22, -21, -14, -6, 4, 13, 18 };
    static const float BLY[13] = { -1.4f, -4.2f, -6.0f, -5.2f, -3.2f, -1.6f, 0, 1.4f, 2.8f, 4.6f, 5.4f, 3.8f, 1.3f };
    sk_poly(k, BLX, BLY, 13, k->body, 255);
    { float X, Y; sk_pt(k, 2, 0, &X, &Y); float rx = k->zq * 5.5f * k->s; if (rx >= 1) fill_ellipse(k->c, X, Y, rx, 5.6f * k->s, k->body, 255); }
    { static const float lx[8] = { 18, 18, 13, 4, -6, -14, -21, -21 }, ly[8] = { 0.4f, 1.3f, 3.8f, 5.4f, 4.6f, 2.8f, 1.4f, 0.6f };   /* the pale belly */
      sk_poly(k, lx, ly, 8, k->acc, 235); }
    sk_tri(k, -14.5f, 2.6f, -17.5f, 5.2f, -18.5f, 2.0f, k->fin, 255);                   /* anal */
    sk_tri(k, -8.5f, 4.2f, -12.0f, 7.8f, -12.5f, 3.6f, k->fin, 255);                    /* pelvic */
    for (int i = 0; i < 3; i++) sk_dot(k, 9.5f + i * 1.6f, 0.0f, 0.3f, 1.7f, k->fin, 110, true);   /* gills */
    sk_dot(k, 14.5f, 3.3f, 2.2f, 0.45f, mix(k->fin, 0x000000, 0.4f), 200, true);        /* the mouth */
    sk_tri(k, 7.0f, 3.5f, -3.5f, 12.0f, 2.5f, 4.8f, k->fin, 255);                       /* the near pectoral */
    if (v == 2) sk_tri(k, -0.5f, 9.4f, -3.5f, 12.0f, 0.2f, 8.2f, tip, 255);
    /* the hammer */
    float sw = 0.16f * fast_sin(t * 0.9f + f->wander), H = 8.5f, HO = 0.85f;
    {
        static const float HX[7] = { 22.4f, 21.4f, 18.8f, 17.0f, 17.0f, 18.8f, 21.4f };
        static const float HZ[7] = { 0, 1, 1, 0.24f, -0.24f, -1, -1 };
        float xs[7], ys[7];
        for (int i = 0; i < 7; i++) sk_pt3(k, HX[i] + HZ[i] * H * sw, -0.6f, HZ[i] * H, HO, &xs[i], &ys[i]);
        src_t s = src_color(k->body, k->c->dim);
        fill_poly_s(k->c, xs, ys, 7, &s, 255);
        if (v == 3) {                              /* scalloped: the leading edge notched */
            src_t n = src_color(mix(k->fin, 0x000000, 0.2f), k->c->dim);
            for (int i = -2; i <= 2; i++) {
                float z = i * 0.42f * H, X, Y;
                sk_pt3(k, 22.0f - fabsf((float)i) * 0.25f + z * sw, -0.6f, z, HO, &X, &Y);
                fill_ellipse_s(k->c, X, Y, 0.9f * k->s, 0.6f * k->s, &n, i == 0 ? 255 : 170);
            }
        }
        for (int e = -1; e <= 1; e += 2) {         /* the eyes, at the tips */
            float X, Y; sk_pt3(k, 20.4f + e * H * 0.92f * sw, -0.6f, e * H * 0.92f, HO, &X, &Y);
            float R = 1.0f * k->s; if (R < 1.2f) R = 1.2f;
            if (k->asleep) fill_ellipse(k->c, X, Y, R, 0.6f, 0x9fb4b8, 200);
            else { fill_ellipse(k->c, X, Y, R, R, 0x05080a, 255); fill_ellipse(k->c, X + 0.3f * R, Y - 0.3f * R, 0.4f * R, 0.4f * R, 0xffffff, 190); }
        }
    }
}

/* ---- swordfish (2026-10-10 night): a long slim cruiser, the bill a third of it again out
   front, a tall sickle first dorsal, a lunate tail on a narrow keel, a silver belly. Never
   still (LOCO_CRUISE, the hammerhead's way): its tail beats on f->jet as the shark's does. ---- */
static void draw_swordfish(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_SWORDFISH);return;}
    const fish_t *f = k->f; int v = f->variant & 3;
    float tw = 2.4f * fast_sin(f->jet * TAU);
    uint32_t dark = mix(k->fin, 0x000000, 0.35f);
    sk_tri(k, 6.0f, 2.5f, 0.5f, 8.5f, 3.5f, 3.0f, dark, 255);                                      /* the far pectoral */
    sk_tri(k, -19.0f, 0.0f, -31.0f, -12.0f + tw, -24.0f, -0.5f, k->fin, 255);                     /* the lunate tail */
    sk_tri(k, -19.0f, 0.0f, -31.0f, 12.0f + tw * 0.7f, -24.0f, 0.5f, k->fin, 255);
    sk_tri(k, 5.0f, -4.5f, -1.0f, -18.0f, -11.0f, -4.0f, k->fin, 255);                            /* the sickle first dorsal */
    sk_tri(k, -1.0f, -18.0f, -4.5f, -12.0f, -11.0f, -4.0f, k->fin, 255);
    sk_tri(k, -14.0f, -2.6f, -17.0f, -6.2f, -18.5f, -2.0f, k->fin, 255);                           /* the second dorsal */
    sk_tri(k, -13.0f, 2.6f, -16.0f, 5.8f, -17.5f, 2.0f, k->fin, 255);                              /* the anal */
    static const float BLX[12] = { 16, 12, 4, -6, -14, -20, -21.5f, -20, -14, -6, 4, 12 };
    static const float BLY[12] = { -1.6f, -4.2f, -5.6f, -5.3f, -3.6f, -1.8f, 0, 1.8f, 3.4f, 4.8f, 4.9f, 3.4f };
    sk_poly(k, BLX, BLY, 12, k->body, 255);
    { float X, Y; sk_pt(k, 0, 0, &X, &Y); float rx = k->zq * 4.8f * k->s; if (rx >= 1) fill_ellipse(k->c, X, Y, rx, 5.2f * k->s, k->body, 255); }
    { static const float lx[8] = { 15, 15, 12, 4, -6, -14, -20, -20 }, ly[8] = { 0.2f, 1.2f, 3.4f, 4.9f, 4.8f, 3.4f, 1.8f, 0.6f };   /* the silver belly */
      sk_poly(k, lx, ly, 8, k->acc, 230); }
    if (v == 1 || v == 2) for (int i = 0; i < 4; i++) sk_mark(k, 8.0f - i * 6.5f, -1.5f, 0.6f, 2.6f, mix(k->acc, k->fin, 0.5f), 120);   /* the cobalt's and the sunset's bars */
    sk_line(k, 12.0f, -0.5f, -16.0f, 0.5f, 0.35f, 0.3f, mix(k->body, 0x000000, 0.3f));                /* the lateral line */
    sk_tri(k, 6.0f, 3.0f, -3.0f, 10.0f, 2.0f, 4.5f, k->fin, 255);                                   /* the near pectoral */
    sk_tri(k, -19.0f, -1.2f, -22.5f, -1.2f, -19.0f, 1.2f, dark, 255);                              /* the keel */
    sk_line(k, 15.5f, -1.0f, 33.0f, -0.4f, 1.15f, 0.25f, dark);                                     /* the bill */
    sk_line(k, 16.0f, -1.6f, 24.0f, -1.3f, 0.35f, 0.2f, mix(k->acc, 0xffffff, 0.3f));              /* its lit edge */
    sk_dot(k, 13.0f, 2.2f, 2.0f, 0.4f, dark, 180, true);                                            /* the mouth */
    sk_eye(k, 10.5f, -1.8f, 1.5f, false);
}

/* ---- turtle (2026-10-11): a sea turtle side-on - a domed carapace over a flat pale
   plastron, the head out on a short neck with a beak, two long front flippers that
   stroke on f->jet (the far one a half-stroke behind, darker), two small rear
   flippers and a stub of tail. The design is the shell: scutes, streaks, ridges or
   spots. It gulps air at the surface like the eel (SPM_GULP: bubbles off the beak). ---- */
static void draw_turtle(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_TURTLE);return;}
    const fish_t *f = k->f; int v = f->variant & 3;
    float st = k->asleep ? 0.6f : fast_sin(f->jet * TAU);
    uint32_t skin = k->fin, dark = mix(k->fin, 0x000000, 0.35f), rim = mix(k->body, 0x000000, 0.3f);
    turtle_flipper(k, 6.5f, -0.5f, -st * 0.7f, 1, dark, 255);                                       /* the far front flipper, a half-stroke behind */
    sk_tri(k, -12.0f, -1.0f, -15.0f, 0.0f, -19.0f, -4.0f, dark, 255);                                /* the far rear flipper */
    sk_poly(k, TURTLE_CX, TURTLE_CY, 10, k->body, 255);                                             /* the carapace */
    sk_line(k, 10.5f, -1.5f, -14.5f, -3.5f, 0.3f, 0.3f, rim);                                        /* its rim, dark */
    sk_dot(k, -2.0f, 3.2f, 12.0f, 2.2f, k->acc, 255, true);                                          /* the plastron */
    turtle_volume(k, k->body, k->acc, skin, rim, 0xffffff);
    if (fabsf(sk_yaw(k, -2)) < 0.45f) v = -1;                                                        /* past a half-turn the dome carries the look: no pattern piled on its middle */
    if (v == 0) {                                                                                     /* scutes: a row of plates down the middle, the rim's smaller ones */
        for (int i = 0; i < 4; i++) { float x = 6.0f - i * 5.5f; sk_line(k, x, -7.0f, x, 1.0f, 0.3f, 0.3f, rim); }
        sk_line(k, 9.0f, -4.0f, -13.0f, -5.0f, 0.3f, 0.3f, rim);
    } else if (v == 1) {                                                                              /* the hawksbill's streaks */
        for (int i = 0; i < 5; i++) sk_mark(k, 6.0f - i * 4.5f, -4.5f + (i & 1) * 1.5f, 1.8f, 0.7f, mix(k->fin, 0x000000, 0.2f), 230);
    } else if (v == 2) {                                                                              /* the leatherback's ridges */
        for (int i = 0; i < 3; i++) sk_line(k, 9.0f, -6.5f + i * 2.4f, -13.0f, -6.0f + i * 2.4f, 0.3f, 0.3f, mix(k->acc, k->body, 0.4f));
    } else if (v == 3) for (int i = 0; i < 6; i++) sk_dot(k, 7.0f - i * 4.0f, -5.0f + (i & 1) * 2.5f, 0.9f, 0.9f, k->acc, 220, false);   /* pale spots */
    sk_tri(k, -14.0f, 0.0f, -20.0f, 1.5f, -14.0f, 2.5f, skin, 255);                                  /* the tail */
    sk_tri(k, -11.0f, 2.0f, -14.0f, 3.0f, -18.0f, 7.5f, skin, 255);                                  /* the near rear flipper */
    sk_dot(k, 12.0f, 0.5f, 3.0f, 2.2f, skin, 255, false);                                            /* the neck */
    sk_dot(k, 16.5f, -0.3f, 3.6f, 3.0f, skin, 255, false);                                           /* the head */
    sk_tri(k, 19.5f, -1.2f, 21.5f, 0.2f, 19.5f, 1.4f, dark, 255);                                    /* the beak */
    sk_eye(k, 17.3f, -1.2f, 1.1f, false);
    turtle_flipper(k, 6.5f, 1.5f, st, 1, skin, 255);                                                 /* the near front flipper */
    turtle_gulp(k, 21.0f, -1.0f);
}

/* ---- squid: a torpedo mantle, the two fins at its tip rippling, eight
   arms and two long tentacles at the head, a big eye; chromatophores
   flicker in f->accent (the firefly's glow, undimmed). It swims either way:
   mantle first it jets, arms closed behind it. ---- */
static void draw_squid(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_SQUID);return;}
    const fish_t *f = k->f; float t = k->t; int v = f->variant & 3;
    float jk = k->pose, jp = f->jet - floorf(f->jet);
    float pulse = jp < 0.3f ? fast_sin(jp * (TAU * 0.5f / 0.3f)) : 0, my = 1 - 0.12f * pulse * jk;
    draw_ink(k);
    /* the fins: across the glass, shown above and below the tip */
    {
        float big = v == 2 ? 1.6f : 1.0f, x0 = v == 2 ? -4.0f : -8.0f, out = (1 - 0.45f * jk);
        uint32_t fc = mix(k->body, k->fin, 0.5f);
        for (int side = -1; side <= 1; side += 2) {
            float rp = 1.3f * fast_sin(jp * TAU * 2 + t * 3 + side + TAU);
            float lz[5] = { 3.4f * my, (8.5f + rp) * big * out, (9.5f - rp) * big * out, 1.2f, 0.0f };
            float lxx[5] = { x0, -11.5f + rp * 0.3f, -18.0f, -22.5f, -23.0f };
            float xs[5], ys[5];
            for (int i = 0; i < 5; i++) sk_pt3(k, lxx[i], 0, side * lz[i], 1.0f, &xs[i], &ys[i]);
            src_t s = src_color(fc, k->c->dim);
            fill_poly_s(k->c, xs, ys, 5, &s, 230);
        }
    }
    /* far arms and the far tentacle */
    float arx[6][4], ary[6][4], ar[4] = { 1.3f, 1.0f, 0.7f, 0.45f };
    for (int i = 0; i < 6; i++) {
        float a = ((i - 2.5f) * 0.16f + 0.14f * fast_sin(t * 3 + i * 1.3f)) * (1 - jk * 0.85f);
        float x = 9.5f, y = (-2.5f + i) * my * (1 - 0.4f * jk), L = 3.2f + 0.4f * (i & 1);
        for (int j = 0; j < 4; j++) {
            arx[i][j] = x; ary[i][j] = y;
            x += L * fast_cos(a + TAU); y += L * fast_sin(a + TAU); a += (i < 3 ? -0.08f : 0.08f) * (1 - jk);
        }
    }
    float tnx[2][6], tny[2][6], tr[6] = { 0.7f, 0.6f, 0.55f, 0.5f, 0.5f, 0.5f };
    for (int q = 0; q < 2; q++) {
        float a = (q ? 0.12f : -0.1f) + 0.18f * fast_sin(t * 2.2f + q * 2) * (1 - jk), x = 9.5f, y = (q ? 0.8f : -0.8f) * my;
        float L = (20 - 11 * jk) / 5;
        for (int j = 0; j < 6; j++) {
            tnx[q][j] = x; tny[q][j] = y;
            x += L * fast_cos(a + TAU); y += L * fast_sin(a + TAU); a += 0.06f * fast_sin(t * 1.7f + q + j);
        }
    }
    for (int i = 1; i < 6; i += 2) sk_tube(k, arx[i], ary[i], ar, 4, k->fin);
    sk_tube(k, tnx[1], tny[1], tr, 6, k->fin);
    sk_dot(k, tnx[1][5], tny[1][5], 2.2f * (1 - 0.6f * jk), 1.1f, k->fin, 255, false);
    /* the mantle */
    {
        static const float lx[9] = { 4, -3, -11, -18, -23.5f, -18, -11, -3, 4 };
        static const float ly0[9] = { -5, -5.6f, -4.8f, -2.8f, 0, 2.8f, 4.8f, 5.6f, 5 };
        float ly[9]; for (int i = 0; i < 9; i++) ly[i] = ly0[i] * my;
        sk_poly(k, lx, ly, 9, k->body, 255);
        float X, Y; sk_pt(k, -6, 0, &X, &Y);
        float rx = k->zq * 5.4f * my * k->s; if (rx >= 1) fill_ellipse(k->c, X, Y, rx, 5.4f * my * k->s, k->body, 255);
    }
    sk_dot(k, 6.5f, 0, 3.6f, 4.4f * my, k->body, 255, false);                 /* the head */
    if (v == 3) {                                  /* reef: bands */
        static const float BX[4] = { -17, -11, -5, 1 }, BH[4] = { 2.8f, 4.6f, 5.4f, 5.1f };
        for (int i = 0; i < 4; i++) sk_mark(k, BX[i], 0, 1.5f, BH[i] * my * 0.95f, k->acc, 170);
    }
    /* chromatophores, flickering */
    if (k->marked) {
        static const float CX[12] = { -1, -5, -9, -13, -17, -3, -8, 1, -11, -15, -6, -19.5f };
        static const float CY[12] = { -3, 1, -3, 0.5f, -1.5f, 3.5f, 3.8f, 0.5f, -3.6f, 2.5f, -1.5f, 0.6f };
        bool glow = v == 1;
        src_t s = src_color(k->acc, glow ? 1.0f : k->c->dim);
        int n = v == 2 ? 8 : 12;
        for (int i = 0; i < n; i++) {
            float fl = 0.5f + 0.5f * fast_sin(t * (2.0f + (i % 3)) + i * 2.1f);
            int a = (int)((glow ? 140 + 115 * fl : 60 + 150 * fl) * (v == 2 ? 0.7f : 1));
            if (glow) sk_dot_s(k, CX[i], CY[i] * my, 1.9f, 1.9f, &s, (int)(40 * fl), true);
            sk_dot_s(k, CX[i], CY[i] * my, v == 2 ? 0.6f : 0.9f, v == 2 ? 0.6f : 0.9f, &s, a, true);
        }
    }
    for (int i = 0; i < 6; i += 2) sk_tube(k, arx[i], ary[i], ar, 4, k->body);   /* near arms */
    sk_tube(k, tnx[0], tny[0], tr, 6, k->body);
    sk_dot(k, tnx[0][5], tny[0][5], 2.2f * (1 - 0.6f * jk), 1.1f, k->body, 255, false);
    sk_eye(k, 7, -0.8f, 2.6f, false);
}

/* ---- crab (2026-10-05, the scope's late two): seen from the front
   quarter - a wide carapace, eyes on stalks, two claws held up at its
   shoulders, four walking legs a side stepping in a ripple. It walks sideways,
   so it does not turn to face its way: the yaw only slides its face a
   little toward the side it shows. f->puff is its threat display: the claws
   go up and out. No pitch - it walks the floor. ---- */
static void draw_crab(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_CRAB);return;}
    const fish_t *f = k->f; float t = k->t; int v = f->variant & 3;
    float q = 1.6f * k->yh;                        /* the face, slid toward the side it shows */
    k->yh = k->yt = 1; k->zq = 0;                  /* drawn straight on: no mirroring, no foreshortening */
    sk_lean(k, 0);
    float raise = clamp01(f->puff);
    float ph = f->jet * TAU;                        /* tank.c runs it by the distance walked */
    float mv = clamp01(f->speed / 4);              /* stepping only while it walks */
    uint32_t leg = mix(k->body, k->fin, 0.45f);
    /* the legs: four a side, a ripple running down each row */
    for (int sd = -1; sd <= 1; sd += 2)
        for (int j = 0; j < 4; j++) {
            float lift = mv * fmaxf(0, fast_sin(ph + j * 1.6f + (sd > 0 ? TAU * 0.5f : 0) + TAU)) * 2.2f;
            float lx[3] = { sd * (6.5f + j * 1.4f), sd * (11.5f + j * 2.6f), sd * (13.0f + j * 3.5f) };
            float ly[3] = { 1.0f + j * 0.6f, -4.0f + j * 1.0f - lift, 7.0f - lift * 0.6f };
            float r[3] = { 1.0f, 0.75f, 0.45f };
            sk_tube(k, lx, ly, r, 3, j & 1 ? k->fin : leg);
        }
    /* the carapace: domed above, flatter below */
    float cx[16], cy[16];
    for (int i = 0; i < 16; i++) { cx[i] = 11.5f * g_u16c[i]; cy[i] = g_u16s[i] * (g_u16s[i] < 0 ? 7.5f : 4.6f); }
    sk_poly(k, cx, cy, 16, k->body, 255);
    { float lx[7], ly[7]; for (int i = 0; i < 7; i++) { lx[i] = cx[i + 1] * 0.96f; ly[i] = cy[i + 1] * 0.96f + 0.6f; }   /* the underside */
      sk_poly(k, lx, ly, 7, v == 0 ? mix(k->acc, k->body, 0.25f) : mix(k->body, 0xffffff, 0.35f), 255); }
    sk_dot(k, q * 0.5f, -3.5f, 8.5f, 2.6f, mix(k->body, 0xffffff, 0.18f), 120, false);   /* its sheen */
    if (v == 2) {                                  /* Sally Lightfoot: blue flecks */
        static const float FX[10] = { -7, -3.5f, 0.5f, 4, 7.5f, -5.5f, -1.5f, 2.5f, 6, -9 }, FY[10] = { -3, -5.5f, -6, -5, -2.5f, -1, -2.5f, -2, -0.5f, 0 };
        for (int i = 0; i < 10; i++) sk_mark(k, FX[i] + q * 0.3f, FY[i], 0.9f, 0.7f, k->acc, 240);
    } else if (v == 3) {                           /* shore: mottled */
        static const float MX[7] = { -6, -1, 4.5f, -3.5f, 2, 7.5f, -8.5f }, MY[7] = { -4, -5.5f, -4, -1.5f, -1.5f, -1.5f, -1 };
        for (int i = 0; i < 7; i++) sk_mark(k, MX[i] + q * 0.3f, MY[i], i & 1 ? 1.2f : 1.8f, i & 1 ? 1.0f : 1.3f, i & 1 ? k->acc : k->fin, 170);
    }
    /* the eyes on their stalks (folded down asleep) */
    for (int e = -1; e <= 1; e += 2) {
        float bx = e * 2.6f + q, top = k->asleep ? -7.0f : -10.8f;
        sk_line(k, bx, -6.2f, bx + e * 0.6f, top, 0.75f, 0.65f, leg);
        float X, Y; sk_pt(k, bx + e * 0.6f, top - 0.6f, &X, &Y);
        float R = 1.4f * k->s; if (R < 1.2f) R = 1.2f;
        if (k->asleep) fill_ellipse(k->c, X, Y, R, R * 0.5f, 0x2a2a2e, 255);
        else { fill_ellipse(k->c, X, Y, R, R, 0x05080a, 255); fill_ellipse(k->c, X + 0.3f * R, Y - 0.35f * R, 0.38f * R, 0.38f * R, 0xffffff, 200); }
    }
    sk_dot(k, q, 1.2f, 2.4f, 1.3f, mix(k->fin, 0x000000, 0.2f), 255, false);   /* the mouth parts */
    /* the claws: held before the mouth, raised up and out in a threat */
    uint32_t tipc = v == 1 ? k->acc : mix(k->fin, 0x000000, 0.45f);
    for (int sd = -1; sd <= 1; sd += 2) {
        float ccx = sd * (12.5f + 2.0f * raise) + q * 0.5f, ccy = -6.0f - 7.5f * raise;
        float ex = sd * (14.5f + 2.0f * raise), ey = 3.0f - 4.0f * raise;
        float lx[3] = { sd * 8.0f, ex, ccx }, ly[3] = { 2.5f, ey, ccy + 1.5f }, r[3] = { 1.5f, 1.4f, 1.8f };
        sk_tube(k, lx, ly, r, 3, k->body);
        float open = 0.35f + 0.3f * (0.5f + 0.5f * fast_sin(t * 2.3f + sd)) + 0.5f * raise;
        /* the fingers point up and in at rest, straight up in the threat */
        float dx = -sd * 0.55f * (1 - raise), dy = -0.83f - 0.17f * raise, px = -dy, py = dx;     /* along the fingers, and across them */
        uint32_t edge = mix(k->body, 0x000000, 0.35f), lit = mix(k->body, 0xffffff, 0.15f);
        sk_line(k, ccx + dx * 2.5f + px * 1.2f, ccy + dy * 2.5f + py * 1.2f, ccx + dx * 7.0f + px * 1.0f, ccy + dy * 7.0f + py * 1.0f, 1.3f, 0.5f, tipc);   /* the fixed finger */
        sk_line(k, ccx + dx * 2.5f - px * 1.2f, ccy + dy * 2.5f - py * 1.2f, ccx + dx * 6.4f - px * (1.0f + open * 2.0f), ccy + dy * 6.4f - py * (1.0f + open * 2.0f), 1.2f, 0.5f, tipc);   /* the moving one */
        sk_dot(k, ccx, ccy, 4.3f, 3.4f, edge, 255, false);                  /* the palm, edged off the shell behind it */
        sk_dot(k, ccx, ccy, 3.7f, 2.8f, lit, 255, false);
        sk_dot(k, ccx - sd * 0.8f, ccy - 0.9f, 1.5f, 0.9f, mix(k->body, 0xffffff, 0.4f), 170, false);
        if (v == 2) sk_mark(k, ccx + sd * 0.8f, ccy + 0.6f, 0.8f, 0.6f, k->acc, 230);
    }
}

/* ---- lobster: side-on - the carapace, the six-segment tail and its fan,
   two big claws forward (the spiny lobster has none: long spiny antennae
   instead), the long antennae sweeping, the walking legs stepping. A
   tail-flip (sp_mode SPM_FLIP, f->jet > 0.5) curls the tail under it. ---- */
static void draw_lobster(sk_t *k) {
    if(theme_active()){draw_theme_species(k,SP_LOBSTER);return;}
    const fish_t *f = k->f; float t = k->t; int v = f->variant & 3;
    float curl = f->sp_mode == SPM_FLIP && f->jet > 0.5f ? clamp01((f->jet - 0.5f) * 8) : 0;
    float ph = t * (2.0f + clamp01(f->speed / 20) * 7), mv = clamp01(f->speed / 6);
    uint32_t dark = mix(k->fin, 0x000000, 0.25f);
    uint32_t ant = v == 0 ? k->acc : v == 2 ? mix(k->body, k->acc, 0.35f) : k->fin;
    /* the antennae, sweeping */
    for (int a = 0; a < 2; a++) {
        float lx[9], ly[9], r[9], ang = -0.4f - a * 0.25f + 0.22f * fast_sin(t * 1.3f + a * 2 + f->wander), x = 15.5f, y = -3.2f;
        float L = v == 2 ? 4.6f : 3.8f, r0 = v == 2 ? 1.1f : 0.6f;
        for (int j = 0; j < 9; j++) {
            lx[j] = x; ly[j] = y; r[j] = r0 - (r0 - 0.35f) * j / 8.0f;
            x += L * fast_cos(ang + TAU); y += L * fast_sin(ang + TAU); ang += 0.13f + 0.03f * fast_sin(t * 2 + j);
        }
        sk_tube(k, lx, ly, r, 9, a ? dark : ant);
        if (v == 2) for (int j = 1; j < 5; j++) sk_dot(k, lx[j], ly[j] - r[j], 0.45f, 0.6f, k->acc, 220, false);
    }
    for (int a = 0; a < 2; a++) sk_line(k, 16, -2.2f, 21.5f, -4.0f + a * 1.6f, 0.4f, 0.35f, k->fin);   /* antennules */
    /* the far claw and far legs, behind */
    if (v != 2) {
        float lx[4] = { 10.5f, 15.5f, 19.5f, 25.5f }, ly[4] = { 3.5f, 6.0f, 4.0f, 3.2f }, r[4] = { 1.0f, 0.9f, 1.8f, 2.2f };
        sk_tube(k, lx, ly, r, 4, dark);
        sk_line(k, 27, 2.5f, 30.5f, 2.2f, 0.9f, 0.4f, dark);
    }
    for (int j = 0; j < 4; j++) {
        float st = mv * 1.8f * fast_sin(ph + j * 1.6f + 0.8f + TAU), lift = mv * fmaxf(0, fast_sin(ph + j * 1.6f + 2.4f + TAU)) * 1.4f;
        float bx = 9.5f - j * 3.2f;
        float lx[3] = { bx + 0.8f, bx + 2.5f, bx + 1.2f + st }, ly[3] = { 2.5f, 5.0f - lift, 7.0f - lift * 0.6f }, r[3] = { 0.7f, 0.55f, 0.4f };
        sk_tube(k, lx, ly, r, 3, dark);
    }
    /* the tail: six plates from the carapace back, curling under on a flip */
    float px = -1.5f, py = -0.6f, ang = TAU * 0.5f + 0.05f;
    static const float SH[7] = { 4.2f, 4.0f, 3.8f, 3.5f, 3.1f, 2.7f, 2.3f };
    float TX[7], TY[7], NX[7], NY[7];
    for (int j = 0; j < 7; j++) {
        TX[j] = px; TY[j] = py;
        NX[j] = -fast_sin(ang + TAU); NY[j] = fast_cos(ang + TAU);
        if (j < 6) { px += 3.2f * fast_cos(ang + TAU); py += 3.2f * fast_sin(ang + TAU); ang -= 0.05f + curl * 0.42f; }
    }
    /* the fan at its end */
    {
        float cx = TX[6], cy = TY[6], dx = NY[6], dy = -NX[6];   /* along the tail */
        float fl = 1 + 0.15f * fast_sin(t * 3);
        float lx[5] = { cx - NX[6] * 2.2f, cx + dx * 6.0f - NX[6] * 5.6f * fl, cx + dx * 7.4f, cx + dx * 6.0f + NX[6] * 5.6f * fl, cx + NX[6] * 2.2f };
        float ly[5] = { cy - NY[6] * 2.2f, cy + dy * 6.0f - NY[6] * 5.6f * fl, cy + dy * 7.4f, cy + dy * 6.0f + NY[6] * 5.6f * fl, cy + NY[6] * 2.2f };
        sk_poly(k, lx, ly, 5, k->fin, 255);
        sk_line(k, cx, cy, cx + dx * 5.5f, cy + dy * 5.5f, 0.9f, 0.6f, k->body);   /* the telson */
    }
    for (int j = 0; j < 6; j++) {
        float lx[4] = { TX[j] - NX[j] * SH[j] * 0.95f, TX[j + 1] - NX[j + 1] * SH[j + 1], TX[j + 1] + NX[j + 1] * SH[j + 1] * 0.7f, TX[j] + NX[j] * SH[j] * 0.7f };
        float ly[4] = { TY[j] - NY[j] * SH[j] * 0.95f, TY[j + 1] - NY[j + 1] * SH[j + 1], TY[j + 1] + NY[j + 1] * SH[j + 1] * 0.7f, TY[j] + NY[j] * SH[j] * 0.7f };
        sk_poly(k, lx, ly, 4, k->body, 255);
        sk_dot(k, TX[j] - NX[j] * SH[j] * 0.9f, TY[j] - NY[j] * SH[j] * 0.9f, 0.9f, 0.7f, dark, 255, false);   /* swimmerets */
        sk_line(k, TX[j] - NX[j] * SH[j] * 0.9f, TY[j] - NY[j] * SH[j] * 0.9f, TX[j] + NX[j] * SH[j] * 0.6f, TY[j] + NY[j] * SH[j] * 0.6f, 0.35f, 0.35f, dark);
    }
    /* the carapace and the rostrum */
    static const float CLX[9] = { 15, 12, 6, -1, -3, -2, 4, 11, 14.5f }, CLY[9] = { -2, -4.2f, -5, -4.6f, -1.5f, 2.8f, 3.6f, 3, 1 };
    sk_poly(k, CLX, CLY, 9, k->body, 255);
    { float X, Y; sk_pt(k, 6, -0.6f, &X, &Y); float rx = k->zq * 4.5f * k->s; if (rx >= 1) fill_ellipse(k->c, X, Y, rx, 4.4f * k->s, k->body, 255); }
    sk_tri(k, 14, -3.2f, 18.8f, -3.6f, 14.5f, -1.4f, k->body, 255);
    sk_line(k, 3.5f, -4.8f, 1.5f, 3.4f, 0.3f, 0.3f, dark);                 /* the carapace's groove */
    if (v == 1) sk_mark(k, 6, -3.2f, 6, 1.1f, k->acc, 110);               /* blue: its sheen */
    else if (v == 2) {                             /* spiny: gold spots, spines on the back */
        static const float GX[6] = { 9, 4, -0.5f, 6, 11.5f, 1 }, GY[6] = { -2.5f, -3, -2, 0.8f, 0, 1.5f };
        for (int i = 0; i < 6; i++) sk_mark(k, GX[i], GY[i], 0.9f, 0.9f, k->acc, 235);
        for (int j = 1; j < 6; j++) sk_mark(k, TX[j] - NX[j] * 1.6f, TY[j] - NY[j] * 1.6f, 0.8f, 0.8f, k->acc, 235);
        for (int i = 0; i < 4; i++) sk_tri(k, 12 - i * 3.5f, -4.0f - (i == 1 || i == 2) * 0.8f, 11.2f - i * 3.5f, -6.6f, 10.4f - i * 3.5f, -4.4f - (i == 1 || i == 2) * 0.7f, k->acc, (int)(230 * k->fade));
    } else if (v == 3) {                           /* calico: cream patches */
        sk_mark(k, 8, -2.0f, 2.6f, 1.8f, k->acc, 220); sk_mark(k, 2, 0.5f, 2.0f, 1.6f, k->acc, 220);
        for (int j = 1; j < 6; j += 2) sk_mark(k, TX[j], TY[j] - 0.8f, 1.5f, 1.4f, k->acc, 220);
    }
    /* the near claw: arm, crusher, fingers */
    if (v != 2) {
        float op = 0.3f + 0.25f * (0.5f + 0.5f * fast_sin(t * 1.7f + f->wander));
        float lx[4] = { 11.5f, 16.5f, 20.5f, 26.5f }, ly[4] = { 2.5f, 5.0f, 2.8f, 1.6f }, r[4] = { 1.1f, 1.0f, 2.1f, 2.6f };
        sk_tube(k, lx, ly, r, 4, k->body);
        uint32_t tc = v == 0 ? k->acc : v == 3 ? k->acc : k->fin;
        sk_line(k, 28.2f, 0.4f, 33.5f, -0.6f - op, 1.0f, 0.45f, tc);
        sk_line(k, 28.2f, 2.6f, 33.0f, 3.0f + op, 0.95f, 0.45f, tc);
        if (v == 3) sk_mark(k, 23.5f, 2.2f, 1.6f, 1.2f, k->acc, 220);
    }
    /* the eye on its stalk */
    sk_line(k, 13.2f, -3.8f, 14.6f, -5.6f, 0.7f, 0.6f, k->fin);
    sk_eye(k, 14.8f, -6.0f, 1.1f, true);
}

#include "jellyfish.inc"

/* every species but the classic fish: dispatch on f->species */
static void draw_creature(ctx_t *c, const fish_t *f, float clock, bool asleep) {
    sk_t k;
    switch (f->species) {
    case SP_SEAHORSE: sk_setup(&k, c, f, clock, asleep, -8, 12, 0, 0); draw_seahorse(&k); break;
    case SP_OCTOPUS: {
        float jk = sk_pose(tank_fish_jetting(f) ? 1.0f : 0.0f, clock);
        sk_setup(&k, c, f, clock, asleep, -20, 14, 0.9f, jk); k.pose = jk; draw_octopus(&k); break; }
    case SP_PUFFER:   sk_setup(&k, c, f, clock, asleep, -20, 14, 0.3f, clamp01(f->speed / 25)); draw_puffer(&k); break;
    case SP_ANGLER:   sk_setup(&k, c, f, clock, asleep, -22, 16, 0.35f, clamp01(f->speed / 25)); draw_angler(&k); break;
    case SP_EEL:      sk_setup(&k, c, f, clock, asleep, -32, 32, 0.5f, clamp01(f->speed / 20)); draw_eel(&k); break;
    case SP_SHARK:    sk_setup(&k, c, f, clock, asleep, -31, 22, 0.4f, 1); draw_shark(&k); break;
    case SP_SQUID: {
        float jk = sk_pose(tank_fish_jetting(f) ? 1.0f : 0.0f, clock);
        sk_setup(&k, c, f, clock, asleep, -23, 12, 0.5f, clamp01(f->speed / 25)); k.pose = jk; draw_squid(&k); break; }
    case SP_CRAB:     sk_setup(&k, c, f, clock, asleep, -12, 12, 0, 0); draw_crab(&k); break;
    case SP_LOBSTER:  sk_setup(&k, c, f, clock, asleep, -26, 22, 0.3f, f->sp_mode == SPM_FLIP ? 1.0f : 0.0f); draw_lobster(&k); break;
    case SP_JELLYFISH: sk_setup(&k,c,f,clock,asleep,-18,18,0,0); draw_jellyfish(&k); break;
    case SP_SWORDFISH: sk_setup(&k, c, f, clock, asleep, -34, 32, 0.4f, 1); draw_swordfish(&k); break;
    case SP_TURTLE:   sk_setup(&k, c, f, clock, asleep, -18, 14, 0.3f, clamp01(f->speed / 20)); draw_turtle(&k); break;
    default: break;
    }
}

/* the tank's fish (the previews below draw a fish of their own) */
static void draw_fish(ctx_t *c, const tank_t *t, const fish_t *f) {
    g_sp_slot = (int)(f - t->fish);
    draw_fish_core(c, f, t->clock, t->night && f->goal.id == GOAL_REST, 1.0f);
    g_sp_slot = -1;
}

/* ---- the shrimp (2026-09-29): cherry shrimp from 18 x 8 pixel grids,
 * facing right, drawn pixel by pixel through the fish's turn - the yaw
 * foreshortens x - so they stay procedural (Strato: "all aquatic marine life
 * sprites should be procedurally drawn. raster icons only for menus").
 * What makes six pixels a shrimp: the hunched back, the body thinning to a
 * fanned tail, one eye up front, antennae longer than the body; the tail,
 * legs and antennae are see-through. Character is in the motion: paddling
 * legs, a peck at the floor, the tail-flick escape. (working-assets/shrimp/
 * has the approved drafts.) Tones: o belly edge, r cherry, h the back's
 * light, s a glint, e the eye, a antenna, l legs, t tail fan. */
#define SHRIMP_TONES 8
static const char SHRIMP_KEYS[SHRIMP_TONES + 1] = "orhsealt";
static const uint32_t SHRIMP_RGB[SHRIMP_TONES]  = { 0x6e0f1c, 0xd0262e, 0xff5a4a, 0xffb8a0, 0x100408, 0xff7a70, 0xc0303a, 0xff7060 };
static const uint8_t  SHRIMP_ALPHA[SHRIMP_TONES] = { 255, 255, 255, 255, 255, 140, 190, 205 };
static const char *const SHRIMP_IDLE_A[8] = {
    "..................", "......ohhs......aa", "....ohrrrhhhs..a..", "..ohrrrrrrrrrea...",
    ".trrrrrrrrrrrrrr..", "tto..oorrrrrroo...", "t......l.l.l.l.aa.", "................a." };
static const char *const SHRIMP_IDLE_B[8] = {
    "................a.", "......ohhs......a.", "....ohrrrhhhs..a..", "..ohrrrrrrrrrea...",
    ".trrrrrrrrrrrrrr..", "tto..oorrrrrroo...", "t.....l.l.l.l..aa.", ".................." };
static const char *const SHRIMP_PECK[8] = {      /* nose to the sand */
    "..................", "......ohhs........", "....ohrrrhhhs.....", "..ohrrrrrrrrrr.aa.",
    ".trrrrrrrrrrrrea..", "tto..oorrrrrrrrr..", "t......l.l.l.lo.a.", "................a." };
static const char *const SHRIMP_DART[8] = {      /* the escape: tail snapped under, antennae streaming back */
    "..................", "aaaa..ohhs........", "....ohrrrhhhs.....", "...orrrrrrrrrea...",
    "...ttrrrrrrrrrr...", "...tt.oorrrroo....", "..................", ".................." };
static void draw_shrimp(ctx_t *c, const shrimp_t *q, const src_t *pal) {
    if(theme_active() && theme_active()!=THEME_BLACKWATER) {   /* Blackwater keeps the pixel cherry shrimp: legs, antennae, a fanned tail */
        bool club=theme_active()==THEME_TIDEPOOL_CLUB;
        float dir=q->yaw<0?-1:1;
        for(int i=0;i<5;i++)fill_ellipse(c,q->x+dir*(3-i*2),q->y-1+i*.35f,2,club?2:1.4f,
                     i%2?(club?0xe89f77:0xc9bba2):(club?0xb77359:0x89a69a),255);
        fill_ellipse(c,q->x+dir*4,q->y-2,.65f,.65f,0x29463b,255);
        for(int i=0;i<4;i++){px_blend(c,(int)(q->x+dir*(4+i)),(int)(q->y-3-i*.4f),0xd4d9b7,200);px_blend(c,(int)(q->x-i*2),(int)(q->y+2),0xa78768,220);}
        return;
    }
    float sp2 = q->vx * q->vx + q->vy * q->vy;
    const char *const *fr = q->dart > 0 ? SHRIMP_DART
                          : q->pecking ? (fmodf(q->ph * 3, 1) < 0.5f ? SHRIMP_PECK : SHRIMP_IDLE_A)
                          : (fmodf(q->ph * (sp2 > 9 ? 8 : 2.5f), 1) < 0.5f ? SHRIMP_IDLE_A : SHRIMP_IDLE_B);
    float s = q->yaw;                                   /* head-on it is a sliver, never nothing */
    if (s > -0.3f && s < 0.3f) s = s < 0 ? -0.3f : 0.3f;
    int oy = (int)floorf(q->y + 0.5f) - 4;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 18; x++) {
            char ch = fr[y][x];
            if (ch == '.') continue;
            const char *k = strchr(SHRIMP_KEYS, ch);
            if (!k) continue;
            int ti = (int)(k - SHRIMP_KEYS);
            px_blend_s(c, (int)floorf(q->x + (x - 9) * s + 0.5f), oy + y, &pal[ti], SHRIMP_ALPHA[ti]);
        }
}

/* a bed of swaying seaweed fronds; seed varies phase/heights between beds.
 * n and max_seg come from tank_veg_bed (growth-driven: the beds keep growing
 * up and out until the keeper trims them; at VEG_NUB they are green stubble).
 * layer: 0 = the fronds drawn BEHIND the fish, 1 = the fronds drawn in FRONT
 * (alternating), so a fish that dips into a canopy swims woven through it
 * instead of floating on top. */

/* algae film on the glass, drawn over everything: dappled blobs per covered
 * grid cell, thicker film = bigger and greener. The keeper wipes it off. */
static void draw_algae(ctx_t *c, const tank_t *t) {
    for (int cy = 0; cy < ALGAE_ROWS; cy++)
        for (int cx = 0; cx < ALGAE_COLS; cx++) {
            int cov = t->algae[cy * ALGAE_COLS + cx];
            if (!cov) continue;
            uint32_t h = (uint32_t)((cx * 73856093u) ^ (cy * 19349663u));
            float bx = cx * ALGAE_CELL, by = cy * ALGAE_CELL;
            for (int b = 0; b < 3; b++) {
                uint32_t hb = h ^ (b * 2654435761u);
                float ox = (float)(hb % ALGAE_CELL);
                float oy = (float)((hb >> 5) % ALGAE_CELL);
                float r = (1.5f + (float)((hb >> 10) % 3)) * (0.55f + 0.45f * cov / 255.0f)
                        + 2.2f * cov / 255.0f;
                fill_ellipse(c, bx + ox, by + oy, r, r * 0.85f,
                             (hb & 4) ? 0x3f7a45 : 0x35663d, 34 + cov * 96 / 255);
            }
        }
}

/* the snail: drawn procedurally (2026-09-16), defined after the castle
 * block below - it shares the castle's position hash */
static void draw_snail(ctx_t *c, const tank_t *t, bool upright_pass);

/* optional per-stage frame profiling (render.h) */
int64_t (*render_clock_us)(void) = NULL;
int64_t render_prof_us[7];
#define PROF_MARK() (render_clock_us ? render_clock_us() : 0)
#define PROF_ADD(i, t0) do { if (render_clock_us) { int64_t _n = render_clock_us(); render_prof_us[i] += _n - (t0); (t0) = _n; } } while (0)

/* ---- static scene: gradient, pebbles, backdrop decor (cacheable) ---- */
static uint16_t *g_scene = NULL;
static float     g_scene_dim = -1;
static unsigned  g_scene_epoch = 0;
static const uint16_t *g_primed_fb = NULL;
static unsigned  g_primed_epoch = 0;
static uint8_t  *g_vig = NULL;                  /* per-pixel vignette alpha (static) */
static bool      g_vig_filled = false;
void render_set_scene_cache(uint16_t *buf) { g_scene = buf; g_scene_dim = -1; }
void render_set_vignette_cache(uint8_t *buf) { g_vig = buf; g_vig_filled = false; }

const uint16_t *render_scene_buf(unsigned *epoch) {
    if (epoch) *epoch = g_scene_epoch;
    return g_scene_epoch ? g_scene : NULL;
}
void render_fb_primed(const uint16_t *fb, unsigned epoch) { g_primed_fb = fb; g_primed_epoch = epoch; }

/* vignette alpha at (x,y); matches the classic per-pixel loop */
static inline int vig_alpha(int x, int y) {
    float dx = (x - TANK_W * 0.5f) / (TANK_W * 0.5f);
    float dy = (y - TANK_H * 0.5f) / (TANK_H * 0.5f);
    float d2 = dx * dx + dy * dy;
    if (d2 <= 0.72f) return 0;
    int a = (int)((d2 - 0.72f) * (theme_active() == THEME_TIDEPOOL_CLUB ? 18 : theme_active() == THEME_QUIET_LAGOON ? 65 : theme_active() == THEME_BLACKWATER ? 120 : 220));
    return a > 255 ? 255 : a;
}

/* pure darkening (blend toward black), independent of ctx dim */
static inline void px_darken(uint16_t *p, int a) {
    int inv = 256 - a;
    *p = (uint16_t)(((((*p >> 11) * inv) >> 8) << 11) |
                    ((((((*p) >> 5) & 63) * inv) >> 8) << 5) |
                    (((*p & 31) * inv) >> 8));
}

/* a span that is FINAL: blended, then vignetted right here, and NOT marked
 * dirty, so the re-apply sweep never touches it (scene cache mode only) */
static inline void span_final(ctx_t *c, int x0, int x1, int y, const src_t *s, int alpha) {
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    uint16_t *p = &CTX_PX(c, x0, y);
    /* ONE vignette alpha per span, computed (a 5 px frond span changes it by
       < 4%, invisible) rather than read: the PSRAM LUT costs a cache line
       per span and fronds are vertical, which was most of the frond cost */
    int a = vig_alpha((x0 + x1) >> 1, y);
    if (alpha >= 255) {                         /* opaque: a store, no PSRAM read */
        for (int x = x0; x <= x1; x++, p++) { *p = s->v; if (a) px_darken(p, a); }
        return;
    }
    for (int x = x0; x <= x1; x++, p++) {
        blend565(p, s, alpha);
        if (a) px_darken(p, a);
    }
}

/* water gradient colour of row y (shared by the scene bake and the frond
 * pre-tint below) */
static inline uint32_t water_rgb(int y) {
    float p = (float)y / TANK_BOT;                 /* the gradient runs surface to floor; under a bowl's sand line it holds */
    if (p > 1) p = 1;
    const uint32_t *water = theme_palette(theme_active())->water;
    return p < 0.45f ? mix(water[0], water[1], p / 0.45f)
                     : mix(water[1], water[2], (p - 0.45f) / 0.55f);
}

/* Fronds are drawn OPAQUE from a per-row pre-tinted palette (2026-09-04):
 * the 220/255 blend against the water that gave them their depth tint is
 * folded in here, once per row per colour, when the day/night dim changes -
 * so each frond pixel is a store instead of a PSRAM read-modify-write
 * (a ceiling-high jungle was ~16 of 19.5 ms on the device). Only what a
 * frond covers OTHER than water changes: a fish behind a front-layer frond
 * no longer shows through at 14%. */
#define VEG_ALPHA 220
static uint16_t g_veg_row[4][TANK_H];   /* grass pair, then the sword plant's yellow-green pair */
static float    g_veg_row_dim = -1;
static void veg_tint_fill(float dim) {
    const uint32_t *frond = theme_palette(theme_active())->grass;
    if (g_veg_row_dim == dim) return;
    for (int y = 0; y < TANK_H; y++) {
        uint32_t w = water_rgb(y);
        for (int k = 0; k < 4; k++)
            g_veg_row[k][y] = rgb565(mix(w, frond[k], VEG_ALPHA / 255.0f), dim);
    }
    g_veg_row_dim = dim;
}

#define VEG_SEG_DY   VEG_SEG_PX           /* segment pitch, px of height (tank.h: 3.2, the watch 4.3) */
#define VEG_SEG_RY   2.2f                 /* the old segment ellipse's half-height */
#define VEG_MAX_SEGS (VEG_SEGS_FULL + 5)  /* tank_veg_bed tops out at VEG_SEGS_FULL */
/* final: the scene cache is live, so each frond span applies its own vignette
 * (see span_final) and needs no re-apply rect - a full canopy used to hand
 * the sweep three bed-sized boxes, the largest PSRAM traffic in the frame. */
/* a frond span - minus the castle's silhouette while the castle stands IN
 * FRONT of the grass (the pieces outside it are drawn; the mask and
 * g_veg_mask_cx live with the castle, below). Off the castle it is one span. */
#define CASTLE_FY   (TANK_BOT - 16)
#define CASTLE_ROWS 164
static uint32_t g_castle_mask[CASTLE_ROWS + 12][224 / 32];
static int      g_veg_mask_cx;
/* the pieces IN FRONT with a mask the grass leaves alone (2026-10-03: the reef
 * cluster and the coral, then the wreck): baked into the scene like the castle,
 * so the grass leaves their pixels alone too - the masks live with the decor's
 * passes, below (DECOR_DRAW) */
static int      g_front_x[DK_COUNT];                    /* this frame's: the piece's centre x, -1 = not baked in front (render_tank) */
static int      g_front_n;                              /* ... how many are (0 = none: nothing above is read) */
static int      g_bake_x[DK_COUNT];                     /* what the baked scene holds, likewise (bake_scene) */
static bool front_near(int x0, int x1, int y);
static bool front_px(int x, int y);
static void veg_span_front(ctx_t *c, int x0, int x1, int y, const src_t *s);   /* (scene cache only: always final) */
static inline void veg_span(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final) {
    bool front = g_front_n > 0 && front_near(x0, x1, y);
    if (g_veg_mask_cx >= 0 && y >= CASTLE_FY - CASTLE_ROWS && y < CASTLE_FY + 12 && x1 >= g_veg_mask_cx - 112 && x0 <= g_veg_mask_cx + 111) {
        const uint32_t *row = g_castle_mask[y - (CASTLE_FY - CASTLE_ROWS)];
        int run = -1;
        for (int x = x0; x <= x1 + 1; x++) {
            int lx = x - g_veg_mask_cx + 112;
            bool in = x <= x1 && (unsigned)lx < 224 && ((row[lx >> 5] >> (lx & 31)) & 1);
            if (x <= x1 && !in) { if (run < 0) run = x; }
            else if (run >= 0) {
                if (front) veg_span_front(c, run, x - 1, y, s);
                else if (final) span_final(c, run, x - 1, y, s, 255); else span(c, run, x - 1, y, s, 255);
                run = -1;
            }
        }
        return;
    }
    if (front) { veg_span_front(c, x0, x1, y, s); return; }
    if (final) span_final(c, x0, x1, y, s, 255);
    else       span(c, x0, x1, y, s, 255);
}
/* layer: 0 = the even fronds, 1 = the odd ones, -1 = every frond */
static void draw_veg(ctx_t *c, const tank_t *t, int b, int seed, int layer, bool final) {
    veg_tint_fill(c->dim);
    int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
    /* the sword plant (bed 3, the shop): broad lanceolate leaves - a narrow
       stem, 7 px half-width at 40% of the way up, a point at the tip - a
       lazier sway, its own yellow-green pair */
    bool sword = tank_veg_kind(t, b) == VEG_KIND_SWORD;
    float root = 2.4f, tip = 0.4f, amp = sword ? 2.5f : 4.0f;
    for (int i = 0; i < n; i++) {
        if (layer >= 0 && (i & 1) != layer) continue;
        float bx;
        /* each frond's own height (tank_t.veg_h): what the keeper cut is
           exactly what shows - no render-side variation on top */
        int segs = tank_veg_frond(t, b, i, &bx);
        if (segs < 1) segs = 1;                    /* nubs: always a bit of green */
        if (segs > VEG_MAX_SEGS) segs = VEG_MAX_SEGS;
        float sway = fast_sin(t->clock * (sword ? 0.6f : 0.9f) + (i + seed) * 1.7f) * amp;
        /* the swaying chain of segment centres (the frond's spine) */
        float xs[VEG_MAX_SEGS + 1];
        for (int seg = 0; seg <= segs; seg++)
            xs[seg] = bx + sway * seg / (float)segs * fast_sin(seg * 0.4f + t->clock * 0.6f + i + seed);
        /* one span per pixel row, centred on the spine, half-width tapering
         * toward the tip: the same silhouette the old chain of overlapping
         * ellipses drew (their union was a 5 px ribbon), at ~a quarter fewer
         * pixels and without a sqrt per row. Full canopy = ~1000 segments. */
        const uint16_t *pal = g_veg_row[(sword ? 2 : 0) + ((i + seed) & 1)];
        int y_bot = (int)(TANK_BOT - 16 + VEG_SEG_RY);
        int y_top = (int)(TANK_BOT - 16 - (segs - 1) * VEG_SEG_DY - VEG_SEG_RY);
        for (int y = y_bot; y >= y_top; y--) {
            float sp = (TANK_BOT - 16 - y) / VEG_SEG_DY;          /* fractional segment */
            if (sp < 0) sp = 0;
            if (sp > segs - 1) sp = (float)(segs - 1);
            /* taper relative to the frond's own length (2.4 px at the root,
             * 0.4 at the tip): the old fixed 0.07/segment thinned every frond
             * to nothing at 29 segments, a hidden height cap now that fronds
             * grow to the ceiling (VEG_SEGS_FULL) */
            float u = sp / (segs > 1 ? segs - 1 : 1);
            float half = sword ? 1.2f + 6.0f * (u < 0.4f ? u / 0.4f : (1 - u) / 0.6f)
                               : root - (root - tip) * u;
            if (theme_active()) {
                bool club=theme_active()==THEME_TIDEPOOL_CLUB;
                float leaf=0.5f+0.5f*cosf(sp*(sword?0.58f:1.25f)+i*.7f);
                if(sword) half=club ? 1.0f+7.0f*sinf(u*3.14159f)*(0.7f+0.3f*leaf)
                                                  : .8f+5.8f*sinf(u*3.14159f);
                else half=club ? .8f+3.7f*leaf*(1-u*.65f) : .7f+2.2f*sinf(u*3.14159f)*(1-u*.3f);
            }
            if (half < tip) half = tip;
            int k = (int)sp; float fr = sp - k;
            float cx = xs[k] + (xs[k + 1] - xs[k]) * fr;
            src_t s; s.v = pal[y];                              /* opaque: only .v is read */
            veg_span(c, (int)(cx - half), (int)(cx + half), y, &s, final);
            if(theme_active()==THEME_QUIET_LAGOON && half>1.5f) {
                src_t lit={.v=g_veg_row[sword?2:0][y]},shade={.v=g_veg_row[sword?3:1][y]};
                veg_span(c,(int)(cx-half),(int)cx,y,&lit,final);
                veg_span(c,(int)cx+1,(int)(cx+half),y,&shade,final);
                if(sword && y%9==0)veg_span(c,(int)(cx-half*.7f),(int)cx,y,&shade,final);
            }
            if (theme_active() && half>2) {
                src_t vein; vein.v=g_veg_row[(sword?2:0)+1-((i+seed)&1)][y];
                veg_span(c,(int)cx,(int)cx,y,&vein,final);
            }
        }
    }
}


#include "decor_castle.inc"   /* the castle (2026-09-16) */

/* ---- the coral, procedural (2026-09-23; Strato's coral-single.png: a
 * branching orange fan in chunky pixel art - rounded branches, a dark red
 * rim, sunlit yellow tips). Built ONCE as a tone sprite on a 2 px cell grid
 * (CORAL_CW x CORAL_CH cells): a skeleton of capsules (the trunk and its
 * branches), each cell classed by its signed distance to the nearest one -
 * the rim, a lit edge up-left, the tips, and a hashed speckle inside. The
 * colour is the keeper's (tank_coral_rgb): five tones derived from it and
 * hazed with the water per row, like the castle's stone. ~60 x 92 px. */
#define CORAL_CELL 2
#define CORAL_CW   32
#define CORAL_CH   46
#define CORAL_W    (CORAL_CW * CORAL_CELL)
#define CORAL_H    (CORAL_CH * CORAL_CELL)
#define CORAL_FY   (TANK_BOT - 14 + DECOR_SINK)   /* the base, sunk into the pebbles (a mound covers the joint) */
enum { CO_NONE = 0, CO_RIM, CO_BODY, CO_SHADE, CO_LIT, CO_TIP, CO_N };
static int      g_coral_q = -1;               /* the growth step the sprite was built for (CORAL_Q steps) */
#define CORAL_Q 128                           /* ~5.6 h per step over the 30 days: a rebuild each */
static int      g_coral_cells;                /* filled cells in the sprite (the selftest) */
static uint32_t g_coral_rgb = 1;              /* what the rows hold (1 = never) */
static float    g_coral_dim = -1;
/* the decor scratch (2026-09-24): every table the coral and the reef cluster
 * build - sprites, tinted rows, the distance grids - lives in ONE block the
 * platform hands over (render_set_decor_scratch: the firmware gives PSRAM,
 * since internal RAM is down to ~23 KB free and the display driver needs
 * 57 KB of DMA memory at boot - the first cluster build's statics cost it
 * that and the tank booted black). Without one it is calloc'd (the sim). */
#define CL_CW   72
#define CL_CH   60
#define CL_H    (CL_CH * 2)
#define CL_TONES (1 + 5 * 5)
/* the shipwreck (2026-10-10, item 7): a small sunken boat, drawn from geometry like the
 * castle, baked into the scene; IN FRONT its hull comes back over the fish through a mask
 * (as the cluster's) that leaves its two big holes out - so a fish behind the hull shows
 * through them, swimming in and out. (The frogman hung from its stern until the afternoon of 2026-10-10;
 * he is his own thing now, draw_frogman.) The hull is the theme's: three boats (draw_wreck). */
#define WK_W   128
#define WK_H   84                               /* rows above WK_FY: the mast's top */
#define WK_FY  (TANK_BOT - 14 + DECOR_SINK)
typedef struct {
    uint8_t  co_sprite[CORAL_CH][CORAL_CW];
    uint16_t co_row[CO_N][CORAL_H];           /* [tone][row], row 0 = CORAL_FY - CORAL_H */
    int8_t   co_sd[CORAL_CH][CORAL_CW];
    uint8_t  co_tp[CORAL_CH][(CORAL_CW + 7) / 8];
    uint8_t  cl_sprite[CL_CH][CL_CW];
    uint16_t cl_row[CL_TONES][CL_H];
    int8_t   cl_sd[CL_CH][CL_CW];
    uint8_t  cl_dp[CL_CH][(CL_CW + 7) / 8];
    uint32_t cl_mask[CL_H + 1][(CL_CW * 2 + 31) / 32];      /* the pixels a piece baked IN FRONT covers (sprite + mound), */
    uint32_t co_mask[CORAL_H + 1][(CORAL_W + 31) / 32];     /* row 0 = its top row, bit 0 = its left edge */
    uint32_t wk_mask[WK_H + 1][(WK_W + 31) / 32];           /* the shipwreck's hull, anchor and mast (not its holes), likewise */
    uint32_t ca_mask[CASTLE_ROWS + 12][(CASTLE_MASK_W + 31) / 32];   /* a THEMED castle's solid pixels IN FRONT (2026-10-10; the Original's front row has its own tone table) */
    uint8_t  sn_glass[35 * 35];                             /* the snail on the glass: what its heading alone decides (draw_snail) */
} decor_scratch_t;
static decor_scratch_t *g_ds;
size_t render_decor_scratch_size(void) { return sizeof(decor_scratch_t); }
static float g_sng_heading;
void   render_set_decor_scratch(void *buf) { g_ds = (decor_scratch_t *)buf; if (g_ds) memset(g_ds, 0, sizeof *g_ds); g_coral_q = -1; g_coral_rgb = 1; g_sng_heading = 1e9f; }
static inline decor_scratch_t *ds(void) { if (!g_ds) g_ds = (decor_scratch_t *)calloc(1, sizeof(decor_scratch_t)); return g_ds; }
/* the bake of a piece IN FRONT records what it covers (bake_scene sets these) */
static uint32_t *g_rec; static int g_rec_x0, g_rec_y0, g_rec_w, g_rec_rows, g_rec_wpr;
static inline void rec_px(int x, int y) {
    if (!g_rec) return;
    int lx = x - g_rec_x0, r = y - g_rec_y0;
    if ((unsigned)lx < (unsigned)g_rec_w && (unsigned)r < (unsigned)g_rec_rows) g_rec[r * g_rec_wpr + (lx >> 5)] |= 1u << (lx & 31);
}
#include "decor_coral.inc"   /* the coral (2026-09-23): its sizes, its state and its scratch are above */

/* ---- the snail, procedural (2026-09-16; Strato: "explore doing the same
 * for the snail so the aesthetic matches") ----
 * Drawn the castle's way in place of the two sprites: a little geometry with
 * a per-pixel tone rule, the castle's position hash for the foot's mottle,
 * golden light from the upper left (the lower right of the shell falls into
 * the dark tones, the rim shades, a wet gleam sits high on the left), mixed
 * with the water of its row like the fronds - a touch stronger on the pane
 * than on the floor, so the glass snail reads closer - and no outline. And
 * it moves (Strato: "the fish animations are so nice, maybe you can add a
 * subtle animation"): PEDAL WAVES run along the foot, head-ward on the glass
 * and back along the belly upright, the eye stalks sway against each other,
 * and the shell heaves a pixel with the wave.
 * UPRIGHT faces +x, the origin at the sole under the shell (snail_y +
 * SNAIL_SOLE_DY), mirrored to the heading; ~34 x 17 px. ON THE GLASS the
 * head points +y from the foot's centre, turned to the heading like the
 * sprite was; ~22 x 25 px. The tone rule runs over ~750 px per pose. */
enum { SN_S0, SN_S1, SN_S2, SN_S3, SN_S4, SN_F0, SN_F1, SN_F2, SN_F3, SN_TIP, SN_EYE, SN_NONE };
static const uint32_t SNAIL_RGB[SN_NONE] = {
    0xf8c868, 0xe6953c, 0xc4641e, 0x8c3d16, 0x4a2010,      /* the shell: lit .. the groove */
    0xf2ebe4, 0xd8cec6, 0xb3a7a6, 0x847a80,                /* the foot: lit .. its edge */
    0x1a1c34, 0x0a0c18 };                                  /* stalk tips; the eye and the mouth */
#define SNAIL_SOLE_DY   6.0f                               /* the sole below snail_y (the sprite's centre) */
#define SNAIL_TINT_FLOOR 0.84f
#define SNAIL_TINT_GLASS 0.90f
static inline int sn_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* within w of the segment (x0,y0)-(x1,y1); *u = 0..1 along it */
static inline bool sn_seg(float lx, float ly, float x0, float y0, float x1, float y1, float w, float *u) {
    float vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy;
    float t = ((lx - x0) * vx + (ly - y0) * vy) / l2;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = lx - (x0 + t * vx), ey = ly - (y0 + t * vy);
    *u = t;
    return ex * ex + ey * ey < w * w;
}
/* the shell's tone from its lighting terms: band = the whorl's lit / mid /
 * groove, lit = -1..1 (upper left .. lower right), rim = on the edge */
static inline int sn_shell_tone(int band, float lit, bool rim) {
    if(theme_active()==THEME_TIDEPOOL_CLUB)return rim?3:band==3?2:lit>.35f?0:1;
    int tone = band;
    if (lit > 0.35f) tone--; else if (lit < -0.15f) tone++;
    if (lit < -0.55f) tone++;
    if (rim) tone = lit < 0.25f ? (tone > 3 ? tone : 3) : (tone < 1 ? tone : 1);
    return tone;
}
/* UPRIGHT: the tone at local (lx, ly), or SN_NONE. top = the foot's ridge at
 * this column (computed once per column by the caller) */
typedef struct { float heave, sw1, sw2; } sn_sway_t;       /* this frame's heave and stalk sways: once, not per pixel (2026-10-03) */
static int snail_upright_tone(int lx, int ly, float top, float clock, const sn_sway_t *sw) {
    float heave = sw->heave;
    float sw1 = sw->sw1, sw2 = sw->sw2;
    float u;
    /* the eye stalks, swaying against each other */
    if (sn_seg(lx, ly, 12.5f, -7.5f, 17.5f + sw1, -13.5f, 0.7f, &u)) return u > 0.78f ? SN_TIP : SN_F1;
    if (sn_seg(lx, ly, 13.5f, -6.5f, 18.5f + sw2, -9.5f, 0.7f, &u)) return u > 0.78f ? SN_TIP : SN_F1;
    /* the shell: a disc, the whorl a spiral about an apex left of centre */
    const float cx = -3.5f, cy = -9.5f + heave, R = 8.6f;
    float dx = lx - cx, dy = ly - cy;
    float r = sqrtf(dx * dx + dy * dy * 1.0816f);
    if (r <= R) {
        float ax = lx - (cx - 1.5f), ay = ly - (cy + 0.5f);
        float ar = sqrtf(ax * ax + ay * ay), th = atan2f(ay, ax);
        const float pitch = theme_active()==THEME_TIDEPOOL_CLUB?5.0f:theme_active()?4.3f:2.25f;                                 /* ~4 turns: Strato asked for one more groove */
        float w = fmodf(ar - th / TAU * pitch, pitch); if (w < 0) w += pitch; w /= pitch;
        int tone = sn_shell_tone(w < 0.5f ? 1 : w < 0.74f ? 2 : 3, (-dx * 0.55f - dy * 0.83f) / R, r > R - 1.3f);
        if (ar < 2.2f) tone = 3;                                   /* the apex whorl is tight and dark */
        { float gx = (dx + 3.4f) * 0.8f, gy = dy + 4.6f; if (gx * gx + gy * gy < 4.0f) tone = 0; }   /* a wet gleam */
        return SN_S0 + sn_clamp(tone, 0, 4);
    }
    /* the foot: a low lens under the shell, the head rising at the front */
    if (lx >= -15 && lx <= 13 && ly >= top && ly <= 0) {
        int tone = 1;
        if (ly < top + 1.4f) tone = 0;                             /* the lit ridge */
        if (ly > -0.9f) tone = 2;                                  /* the belly line */
        if (r < R + 1.8f && dy > 0 && tone < 3) tone++;            /* the shell's shadow */
        if (ly > -2.2f && tone <= 1 && fast_sin(lx * 1.1f + clock * 3.0f + TAU) > 0.7f) tone = 0;   /* a pedal wave runs back */
        if ((chash(lx, ly) & 7) == 0) tone = sn_clamp(tone + 1, 0, 3);
        return SN_F0 + tone;
    }
    /* the head */
    { float hx = (lx - 11.5f) * 1.05f, hy = ly + 5.2f;
      if (hx * hx + hy * hy <= 9.0f) {
          float ex = lx - 12.6f, ey = ly + 5.6f;
          if (ex * ex + ey * ey < 0.64f) return SN_EYE;
          return (-(lx - 11.5f) * 0.5f - hy) > 0.9f ? SN_F0 : SN_F1;
      } }
    return SN_NONE;
}
/* ON THE GLASS: local (lx, ly) is turned to the heading; (sx, sy) is the
 * screen offset, which the light and the shadow side follow (the light
 * stays upper-left however it crawls) */
/* (the body under the stalks: the foot, whose pedal waves move with the
 * clock - *foot says so - or the shell, which depends on the place alone) */
static int snail_glass_body(float lx, float ly, float sx, float sy, float clock, bool *foot) {
    *foot = false;
    float lit = (-sx * 0.55f - sy * 0.83f) / 11.0f;
    /* the foot: an egg, broad under the shell, narrowing to the head */
    float ey = ly / 11.0f;
    if (ey >= -1 && ey <= 1) {
        float rx = 6.6f * sqrtf(1 - ey * ey) * (1 - 0.18f * ey);
        float alx = lx < 0 ? -lx : lx;
        if (rx > 0.3f && alx <= rx) {
            *foot = true;
            int tone = 1;
            if (fast_sin(ly * 0.75f - clock * 2.6f + 64 * TAU) > 0.6f) tone = 0;   /* pedal waves crawl head-ward */
            float edge = alx / rx;
            if (edge > 0.8f) tone = 2;                             /* the foot's edge */
            else if (edge > 0.62f && lit < 0 && tone < 1) tone = 1; /* the shadow side falls off */
            if (alx < 1.2f && ly > 2 && tone < 2) tone = 2;        /* the pedal groove */
            if (ly < -5 && edge < 0.5f && tone > 1) tone = 1;
            if ((chash((int)floorf(lx), (int)floorf(ly)) & 7) == 0) tone = sn_clamp(tone + 1, 0, 3);
            if (alx < 1.5f && ly >= 7.4f && ly <= 8.8f) return SN_EYE;   /* the mouth */
            return SN_F0 + tone;
        }
    }
    /* the shell behind: a crescent of whorl lobes round the top and sides */
    float qx = lx / 11.0f, qy = (ly + 2.5f) / 10.6f;
    float sr = sqrtf(qx * qx + qy * qy);
    if (sr <= 1 && ly < 6.5f) {
        float lobe = fast_sin(atan2f(ly + 2.5f, lx) * (theme_active()==THEME_TIDEPOOL_CLUB?8.0f:theme_active()?3.0f:5.0f) - 0.3f + 8 * TAU);
        int tone = sn_shell_tone(lobe > 0.15f ? 1 : 2, lit, sr > 0.88f);
        if (sr < 0.78f) tone++;                                    /* shadow where the foot meets it */
        { float gx = sx + 5.0f, gy = sy + 7.8f; if (gx * gx + gy * gy < 2.9f) tone = 0; }   /* gleam */
        return SN_S0 + sn_clamp(tone, 0, 4);
    }
    return SN_NONE;
}
/* stalk_y[0] / [1]: the left and right stalk's tip height this frame */
static int snail_glass_tone(float lx, float ly, float sx, float sy, float clock, const float *stalk_y) {
    float u;
    for (int sgn = -1; sgn <= 1; sgn += 2)                        /* the stalks: stubs by the head */
        if (sn_seg(lx, ly, sgn * 2.2f, 10.0f, sgn * 3.6f, stalk_y[sgn > 0], 0.6f, &u))
            return u > 0.7f ? SN_TIP : SN_F2;
    bool foot;
    return snail_glass_body(lx, ly, sx, sy, clock, &foot);
}
/* a row of snail pixels: the water of the row and each tone's colour over it
 * are worked out once per row and tone, not per pixel */
typedef struct { int y; uint32_t w; uint16_t have; src_t s[SN_NONE]; } sn_row_t;
static inline void snail_put(ctx_t *c, int x, int y, int tone, float tint, sn_row_t *r) {
    if (tone == SN_NONE || !CTX_IN(c, x, y)) return;
    if (r->y != y) { r->y = y; r->have = 0; r->w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y); }
    if (!(r->have >> tone & 1)) {
        static const uint32_t lagoon[SN_NONE]={0xead5b3,0xbd8c65,0xa47553,0x79533d,0x503d31,0xd8dcc3,0x9ebbaa,0x6f9682,0x496e60,0x244b40,0x19392f};
        static const uint32_t club[SN_NONE]={0xffe6ab,0xefad79,0xb7684c,0x8f513c,0x623e30,0xf3dca5,0xd9bc83,0xb39662,0x8a7454,0x334e3a,0x223c2e};
        uint32_t rgb=theme_active()==THEME_QUIET_LAGOON?lagoon[tone]:theme_active()==THEME_TIDEPOOL_CLUB?club[tone]:SNAIL_RGB[tone];
        r->s[tone]=src_color(mix(r->w,rgb,tint),c->dim);r->have|=(uint16_t)(1u<<tone);
    }
    px_blend_s(c, x, y, &r->s[tone], 255);
}
/* ON THE GLASS the shell and the empty corners depend only on the heading:
 * kept per pixel of the 35 x 35 box until the snail turns (the decor
 * scratch, PSRAM). SNG_DYN = the foot, or within reach of a stalk: asked
 * every frame. 2026-10-03: on the glass it cost the 1.8 ~4.5 ms a frame - a
 * sqrtf and an atan2f for every shell pixel, two divides and two sines for
 * every pixel of the box. */
#define SNG_N    35
#define SNG_NONE 0xff
#define SNG_DYN  0xfe
static float g_sng_heading = 1e9f;                     /* the heading sn_glass was worked out for */
/* UPRIGHT on the floor - a side view, mirrored to face the way it walks,
 * drawn in the scene with the fish (vignetted, behind the front fronds) -
 * or flat ON THE GLASS, its underside to the viewer, drawn after the algae
 * like the film itself (it is on the pane). */
static void draw_snail(ctx_t *c, const tank_t *t, bool upright_pass) {
    if (!(t->sd_unlocks & SD_ITEM_SNAIL) || t->snail_x < 0) return;
    bool upright = tank_snail_upright(t);
    if (upright != upright_pass) return;
    int ox = (int)t->snail_x, oy = (int)(t->snail_y + SNAIL_SOLE_DY);
    sn_row_t row = { .y = -(1 << 20) };
    if (upright) {
        bool flip = cosf(t->snail_heading) < 0;
        sn_sway_t sw = { 0.6f * fast_sin(t->clock * 2.4f), fast_sin(t->clock * 1.7f), fast_sin(t->clock * 1.7f + 2.2f) };
        float top[37];                                             /* the foot's ridge per column, then row by row (one water colour a row) */
        for (int lx = -16; lx <= 20; lx++) {
            float xf = (lx + 15) / 28.5f;
            top[lx + 16] = -(2.4f + 3.2f * powf(sinf(xf < 0 ? 0 : xf > 1 ? 1 : xf * 3.14159f), 0.6f));
            if (lx > 8) { float h = -(4.8f + (lx - 8) * 0.55f); if (h < top[lx + 16]) top[lx + 16] = h; }
        }
        for (int ly = -18; ly <= 0; ly++)
            for (int lx = -16; lx <= 20; lx++)
                snail_put(c, flip ? ox - lx : ox + lx, oy + ly, snail_upright_tone(lx, ly, top[lx + 16], t->clock, &sw), SNAIL_TINT_FLOOR, &row);
    } else {
        float a = t->snail_heading - 1.5708f;                      /* the head points +y locally */
        float ca = cosf(a), sa = sinf(a);
        int cx = (int)t->snail_x, cy = (int)t->snail_y;
        float stalk_y[2] = { 11.6f + 0.4f * fast_sin(t->clock * 2 + -1 + TAU), 11.6f + 0.4f * fast_sin(t->clock * 2 + 1 + TAU) };
        uint8_t *cls = ds()->sn_glass;
        if (g_sng_heading != t->snail_heading) {                   /* it turned: what the heading alone decides, again */
            g_sng_heading = t->snail_heading;
            for (int dy = -17; dy <= 17; dy++)
                for (int dx = -17; dx <= 17; dx++) {
                    float lx = ca * dx + sa * dy, ly = -sa * dx + ca * dy;
                    float alx = lx < 0 ? -lx : lx;
                    bool foot = false; int tone = SNG_DYN;
                    if (!(alx >= 1.5f && alx <= 4.3f && ly >= 9.3f && ly <= 12.7f))   /* out of both stalks' reach, wherever they sway */
                        tone = snail_glass_body(lx, ly, (float)dx, (float)dy, 0, &foot);
                    cls[(dy + 17) * SNG_N + dx + 17] = foot ? SNG_DYN : tone == SN_NONE ? SNG_NONE : (uint8_t)tone;
                }
        }
        for (int dy = -17; dy <= 17; dy++)
            for (int dx = -17; dx <= 17; dx++) {
                int k = cls[(dy + 17) * SNG_N + dx + 17];
                if (k == SNG_NONE) continue;
                if (k == SNG_DYN) {
                    float lx = ca * dx + sa * dy, ly = -sa * dx + ca * dy;   /* inverse rotation */
                    k = snail_glass_tone(lx, ly, (float)dx, (float)dy, t->clock, stalk_y);
                }
                snail_put(c, cx + dx, cy + dy, k, SNAIL_TINT_GLASS, &row);
            }
    }
}
/* ---- the urchin, procedural (2026-10-02, SD_ITEM_URCHIN) ----
 * After Strato's sea-urchin-v1 (working-assets): a domed purple urchin seen
 * a little from above, a crown of short pointed spines with pale tips, warm
 * light from the upper left, no outline. Drawn the snail's way - a tone rule
 * per pixel over a ~37 x 24 box, mixed with the water of its row - in his six
 * tones. The spines are its life: they wave slowly, each on its own beat,
 * and work faster while it chews. The dome's centre is (urchin_x,
 * URCHIN_FLOOR_Y); its flat base sits on the root line, UR_BASE below. */
enum { UR_T0, UR_T1, UR_T2, UR_T3, UR_T4, UR_T5, UR_NONE };
static const uint32_t URCHIN_RGB[UR_NONE] = { 0x320746, 0x66107d, 0x9732b1, 0xbe53bc, 0xf88ab8, 0xfff0d3 };
#define UR_SPINES 11
#define UR_FACE    5
#define UR_RX     10.0f
#define UR_RY     8.0f
#define UR_BASE   7.0f
#define UR_TINT   0.93f
typedef struct { float dx[UR_SPINES + UR_FACE], dy[UR_SPINES + UR_FACE], len[UR_SPINES + UR_FACE];
                 float rim[UR_SPINES], end[UR_SPINES], den[UR_SPINES]; } ur_pose_t;   /* per crown spine: where it leaves the dome, its tip, the wedge's taper -
                                                                                        once a frame here, not per pixel (2026-10-03: a sqrtf and two
                                                                                        divides per spine per pixel cost the 1.8 milliseconds a frame) */
/* the spines this frame: a fan from just under the left side, over the top,
 * to just under the right (the floor hides the rest), long and short in
 * turn; then a few short ones on the dome's face, pointing at the viewer
 * and down - the pale streaks of the art */
static void urchin_pose(ur_pose_t *p, float clock, bool chewing) {
    float amp = chewing ? 0.13f : 0.07f, sp = chewing ? 2.6f : 0.8f;
    for (int k = 0; k < UR_SPINES; k++) {
        float a = -3.14159f - 0.32f + k * (3.14159f + 0.64f) / (UR_SPINES - 1);
        a += amp * fast_sin(clock * sp + k * 1.7f + 8 * TAU);
        p->dx[k] = cosf(a); p->dy[k] = sinf(a);
        p->len[k] = (k & 1) ? 4.0f : 6.0f;
        p->rim[k] = 1.0f / sqrtf(p->dx[k] * p->dx[k] / (UR_RX * UR_RX) + p->dy[k] * p->dy[k] / (UR_RY * UR_RY));
        p->end[k] = p->rim[k] + p->len[k];
        p->den[k] = p->end[k] - p->rim[k] + 2;
    }
    static const float FACE_A[UR_FACE] = { -2.4f, -1.9f, 2.2f, 1.2f, 0.5f };
    for (int k = 0; k < UR_FACE; k++) {
        float a = FACE_A[k] + 0.6f * amp * fast_sin(clock * sp + k * 2.3f + 8 * TAU);
        p->dx[UR_SPINES + k] = cosf(a); p->dy[UR_SPINES + k] = sinf(a);
        p->len[UR_SPINES + k] = 4.5f;
    }
}
/* the tone at local (lx, ly) from the dome's centre, or UR_NONE */
static int urchin_tone(float lx, float ly, const ur_pose_t *p) {
    if (ly > UR_BASE) return UR_NONE;
    float lit = (-lx * 0.55f - ly * 0.83f) / UR_RX;                /* -1..1: lower right .. upper left */
    float ex = lx / UR_RX, ey = ly / UR_RY, er = ex * ex + ey * ey;
    /* the face spines first: they stand over the dome */
    for (int k = UR_SPINES; k < UR_SPINES + UR_FACE; k++) {
        static const float FX[UR_FACE] = { -4.5f, 0.5f, -5.0f, 2.0f, 5.5f }, FY[UR_FACE] = { -2.5f, -4.5f, 3.0f, 2.5f, 0.0f };
        float vx = lx - FX[k - UR_SPINES], vy = ly - FY[k - UR_SPINES];
        float u = vx * p->dx[k] + vy * p->dy[k], w = vx * p->dy[k] - vy * p->dx[k];
        if (u < 0 || u > p->len[k]) continue;
        float half = 1.6f * (1 - u / p->len[k]) + 0.35f;
        if (w * w > half * half) continue;
        return u > p->len[k] - 1.6f ? UR_T5 : lit > 0 ? UR_T4 : UR_T3;
    }
    if (er <= 1.0f) {                                               /* the dome */
        int tone = 2;
        if (lit > 0.15f) tone = 3;
        if (lit > 0.55f && er > 0.20f) tone = 4;
        if (lit < -0.30f) tone = 1;
        if (lit < -0.75f || ly > UR_BASE - 1.2f) tone = 0;
        int ix = (int)floorf(lx), iy = (int)floorf(ly);
        uint32_t h = chash(ix + 40, iy + 40);
        if ((h & 7) == 0) tone = tone > 0 ? tone - 1 : 0;           /* the mottle */
        else if ((h & 15) == 1 && tone < 4) tone++;
        return UR_T0 + tone;
    }
    /* the crown: a spine is a wedge from inside the dome out to a fine point */
    for (int k = 0; k < UR_SPINES; k++) {
        float u = lx * p->dx[k] + ly * p->dy[k], w = lx * p->dy[k] - ly * p->dx[k];
        float rim = p->rim[k], end = p->end[k];
        if (u < rim - 2 || u > end) continue;
        float half = 2.6f * (end - u) / p->den[k] + 0.3f;
        if (w * w > half * half) continue;
        float slit = -p->dx[k] * 0.55f - p->dy[k] * 0.83f;          /* the spine's own side of the light */
        if (u > end - 1.8f) return slit > -0.5f ? UR_T5 : UR_T4;    /* pale tips */
        if (u > end - 3.8f) return slit > -0.3f ? UR_T4 : UR_T3;
        return slit > 0.2f ? UR_T3 : slit > -0.5f ? UR_T2 : UR_T1;
    }
    return UR_NONE;
}
static void draw_urchin(ctx_t *c, const tank_t *t) {
    if (!(t->sd_unlocks & SD_ITEM_URCHIN) || t->urchin_x < 0) return;
    if(theme_active() && theme_active()!=THEME_BLACKWATER) {   /* Blackwater keeps the Original's fine-spined urchin */
        bool club=theme_active()==THEME_TIDEPOOL_CLUB;
        float cx=t->urchin_x,cy=URCHIN_FLOOR_Y;
        uint32_t body=club?0xcf9e95:0x849f96,tip=club?0xffdf9a:0xd5dfbd;
        for(int i=0;i<11;i++) {
            float a=3.14159f+i*3.14159f/10,rr=11+1.2f*sinf(t->clock*2+i);
            for(int j=7;j<=(int)rr;j++)fill_ellipse(c,cx+cosf(a)*j,cy+sinf(a)*j,club?1.6f:.8f,club?1.6f:.8f,j+1>rr?tip:body,255);
        }
        fill_ellipse(c,cx,cy,10,7,body,255);
        for(int i=0;i<5;i++)fill_ellipse(c,cx-6+i*3,cy-2+(i%2)*3,club?1.3f:.65f,club?1.3f:.65f,tip,240);
        return;
    }
    ur_pose_t p; urchin_pose(&p, t->clock, tank_urchin_chewing(t));
    int cx = (int)floorf(t->urchin_x + 0.5f), cy = (int)URCHIN_FLOOR_Y;
    for (int ly = -16; ly <= (int)UR_BASE; ly++) {
        int y = cy + ly;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int lx = -18; lx <= 18; lx++) {
            int tone = urchin_tone((float)lx + 0.5f, (float)ly + 0.5f, &p);
            if (tone == UR_NONE || !CTX_IN(c, cx + lx, y)) continue;
            px_blend(c, cx + lx, y, mix(w, URCHIN_RGB[tone], UR_TINT), 255);
        }
    }
}

/* where a piece stands this frame: its centre x (-1 = not in the tank), its
 * depth, and whether the keeper is dragging it on the placement page (then
 * it is drawn live over a scene baked WITHOUT it, instead of a rebake per
 * frame) */
static bool decor_state(const tank_t *t, int k, int *cx, int *z, bool *placing) {
    if (!decor_owned(t, k)) { *cx = -1; *z = DECOR_Z_FRONT; *placing = false; return false; }
    *cx = (int)decor_x(t, k); *z = decor_z(t, k);
    *placing = setup_is_place() && setup_item() == DECOR[k].shop_item;
    return true;
}
static bool g_castle_live;                                 /* tests: the old per-rect repaint instead of the mask */
void render_debug_castle_live(bool on) { g_castle_live = on; }

#include "decor_cluster.inc"   /* the reef cluster (2026-09-24) */

#include "decor_wreck.inc"   /* the shipwreck (2026-10-10, item 7) */
/* ---- the frogman (2026-10-10, item 8; he hung from the wreck's stern that morning) ----
 * A little diver floating SIDEWAYS: horizontal, face forward, air tank on his back (the
 * top), flippers kicking behind him, drifting across the water (tank.c: frog_x, frog_yaw -
 * the body thins through a turn, as a fish does), bobbing on the water's slow lift. His
 * arm reaches forward - and every 23 s, for 2.6 s, it goes up and the forearm waves from the
 * elbow. A mist of fine bubbles rises from his mask. The suit is the theme's, brighter than
 * the wreck's old grey diver: Original a yellow-and-black banded wetsuit with red fins,
 * Quiet Lagoon turquoise with coral bands and violet fins, Tidepool Club hot pink with lime
 * bands and cyan fins. Live every frame; the rect he and his mist cover comes back. */
static void thick_line(ctx_t *c, float x0, float y0, float x1, float y1, float w, uint32_t rgb) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy) + 1e-3f, nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    float px[4] = { x0 + nx, x1 + nx, x1 - nx, x0 - nx }, py[4] = { y0 + ny, y1 + ny, y1 - ny, y0 - ny };
    fill_poly(c, px, py, 4, rgb);
}
static void draw_frogman(ctx_t *c, const tank_t *t, int *bx0, int *by0, int *bx1, int *by1) {
    float clock = t->clock;
    float fx = t->frog_x, fy = t->frog_y + sinf(clock * 1.4f) * 2.5f + sinf(clock * 0.6f + 1) * 1.5f;
    float d = t->frog_yaw >= 0 ? 1 : -1, sx = fabsf(t->frog_yaw) < 0.15f ? 0.15f : fabsf(t->frog_yaw);
    int th = theme_active();
    bool bw = th == THEME_BLACKWATER;   /* Blackwater: a real diver - black neoprene, high-vis amber bands and fins, an aluminium tank */
    uint32_t suit  = bw ? 0x202428 : th == THEME_QUIET_LAGOON ? 0x2fb8a6 : th == THEME_TIDEPOOL_CLUB ? 0xff4f9a : 0xf5c242;
    uint32_t band  = bw ? 0xffb020 : th == THEME_QUIET_LAGOON ? 0xff7f66 : th == THEME_TIDEPOOL_CLUB ? 0xb6f05a : 0x1d2a33;
    uint32_t fin   = bw ? 0xf2c230 : th == THEME_QUIET_LAGOON ? 0x8a5cc8 : th == THEME_TIDEPOOL_CLUB ? 0x33c7e0 : 0xd8402c;
    uint32_t tankc = bw ? 0xd4dade : th == THEME_QUIET_LAGOON ? 0xe8d9a0 : th == THEME_TIDEPOOL_CLUB ? 0xff9f3f : 0xb8c4cc;
    uint32_t glass = bw ? 0xaee6f0 : th == THEME_QUIET_LAGOON ? 0xc4e6d1 : th == THEME_TIDEPOOL_CLUB ? 0xfff6d9 : 0x9fd8e2;
    uint32_t mask  = bw ? 0x0f1215 : th == THEME_TIDEPOOL_CLUB ? 0x5a2d7a : th == THEME_QUIET_LAGOON ? 0x264a44 : 0x1d2a33, skin = 0xf1c69b;
    uint32_t mist  = bw ? 0xc8e8ee : th == THEME_TIDEPOOL_CLUB ? 0x3d8e83 : th == THEME_QUIET_LAGOON ? 0xc4e6d1 : 0x9fd8e2;
#define FX(lx) (fx + (lx) * d * sx)                                                /* local x along the body, + = forward */
    float kick = sinf(clock * 2.6f) * 2.5f;
    if (bw) {
        /* the abyss (2026-10-12): a TORCH in the forward hand, its beam a cone of pale light ahead of him,
           undimmed; the hand is up and waving for 2.6 s every 23 s, the beam swings up with it */
        float wph2 = fmodf(clock, 23.0f), lift = wph2 < 2.6f ? fminf(1, wph2 * 4) * fminf(1, (2.6f - wph2) * 4) : 0;
        float hx = FX(12.5f) * (1 - lift) + (FX(4) + sinf(sinf(clock * 9.0f) * 0.55f) * 7 * d) * lift, hy = (fy + 2.5f) * (1 - lift) + (fy - 9 - cosf(sinf(clock * 9.0f) * 0.55f) * 7) * lift;
        float ang = -lift * 1.2f, cx_ = cosf(ang) * d, sy_ = sinf(ang);
        src_t beam = src_color(0xbfefff, 1);
        for (int i = 0; i < 3; i++) {
            float L = (30 - i * 7) * sx, hw = (8 - i * 2) * fminf(1, L / 14);
            float px[3] = { hx, hx + L * cx_ - hw * sy_ * -1, hx + L * cx_ + hw * sy_ * -1 }, py[3] = { hy, hy + L * sy_ - hw * cx_, hy + L * sy_ + hw * cx_ };
            fill_poly_s(c, px, py, 3, &beam, 16);
        }
    }
    /* the mist: a dozen fine bubbles rising from the mask, each on its own slow loop; behind him */
    for (int i = 0; i < 12; i++) {
        float ph = fmodf(clock * (0.42f + 0.03f * (i % 4)) + i * 0.083f, 1.0f);
        float bx = FX(10) + sinf(clock * 2.5f + i * 1.7f) * (1.5f + ph * 4) + (i % 3 - 1) * 1.5f, by = fy - 7 - ph * 46;
        float r = 0.6f + ph * 1.3f; int a = (int)(150 * (1 - ph * 0.8f));
        fill_ellipse(c, bx, by, r, r, mist, a);
        if (r > 1.2f) px_blend(c, (int)(bx - 0.4f), (int)(by - 0.5f), 0xffffff, 110);
    }
    /* the legs and flippers, behind: from the hips, kicking against each other */
    thick_line(c, FX(-7), fy - 1.5f, FX(-16), fy - 1.5f + kick * 0.5f, 2.6f, suit);
    thick_line(c, FX(-7), fy + 1.5f, FX(-16), fy + 1.5f - kick * 0.5f, 2.6f, suit);
    fill_ellipse(c, FX(-20), fy - 1.5f + kick, 4.5f * sx + 0.6f, 1.8f, fin, 255);
    fill_ellipse(c, FX(-20), fy + 1.5f - kick, 4.5f * sx + 0.6f, 1.8f, fin, 255);
    /* the air tank along his back (the top), its valve toward the head */
    fill_ellipse(c, FX(-1), fy - 5.2f, 7 * sx + 0.4f, 2.4f, tankc, 255);
    fill_ellipse(c, FX(6.5f), fy - 5.2f, 1.2f, 1.2f, 0x565a5d, 255);
    px_blend(c, (int)(FX(-3) + 0.5f), (int)(fy - 6), 0xffffff, 120);
    /* the torso, with its bands */
    fill_ellipse(c, FX(0), fy, 9 * sx + 0.5f, 4.2f, suit, 255);
    fill_ellipse(c, FX(-4), fy, 1.3f * sx + 0.4f, 4.0f, band, 255);
    fill_ellipse(c, FX(3), fy, 1.3f * sx + 0.4f, 4.0f, band, 255);
    /* the arm: forward along the body, or up and waving (every 23 s, for 2.6 s) */
    float wph = fmodf(clock, 23.0f);
    if (wph < 2.6f) {
        float swing = sinf(clock * 9.0f) * 0.55f, lift = fminf(1, wph * 4) * fminf(1, (2.6f - wph) * 4);
        float ex = FX(4), ey = fy - 3 - 6 * lift, hx = ex + sinf(swing) * 7 * lift * d + (1 - lift) * 6 * d * sx, hy = ey - cosf(swing) * 7 * lift;
        thick_line(c, FX(2), fy - 1, ex, ey, 2.4f, suit);
        thick_line(c, ex, ey, hx, hy, 2.2f, suit);
        fill_ellipse(c, hx, hy, 1.7f, 1.7f, band, 255);
    } else {
        thick_line(c, FX(3), fy + 1, FX(12), fy + 2.5f, 2.4f, suit);
        fill_ellipse(c, FX(12.5f), fy + 2.5f, 1.6f, 1.6f, band, 255);
    }
    /* the head: a hood, the face, the mask and its glass, the regulator's hose back to the tank */
    thick_line(c, FX(9), fy - 1, FX(6), fy - 4.5f, 1.2f, 0x3a3d40);
    fill_ellipse(c, FX(11), fy - 1, 4.2f * sx + 0.8f, 4.2f, suit, 255);
    fill_ellipse(c, FX(12.5f), fy - 0.5f, 2.6f * sx + 0.4f, 2.6f, skin, 255);
    fill_ellipse(c, FX(12.5f), fy - 2, 2.9f * sx + 0.4f, 1.9f, mask, 255);
    fill_ellipse(c, FX(12.8f), fy - 2, 2.0f * sx + 0.2f, 1.2f, glass, 255);
    px_blend(c, (int)(FX(13.5f) + 0.5f), (int)(fy - 2.5f), 0xffffff, 170);
    if (bw) { float tx = wph < 2.6f ? FX(4) : FX(12.5f); fill_ellipse(c, tx, wph < 2.6f ? fy - 9 : fy + 2.5f, 1.2f, 1.2f, 0xfff8e0, 255); }   /* the torch's lens */
#undef FX
    *bx0 = (int)fx - 46; *bx1 = (int)fx + 46; *by0 = (int)fy - 58; *by1 = (int)fy + 12;   /* 46: the Blackwater torch beam reaches 43 */
}


/* ---- the submarine (2026-10-10 night, item 9) ---- a little sub cruising the water: a hull with a
 * conning tower, dive planes, a rudder, portholes, a two-blade propeller spinning behind it while it
 * moves, a stream of bubbles from the stern; at a stop (tank.c sub_stop) the propeller idles and a
 * periscope rises from the tower, its head turning to look both ways before it sinks again. The
 * hull is the theme's: Original the yellow sub with black trim, Quiet Lagoon a moss-green navy boat
 * with brass, Tidepool Club a red-and-white toy with a cyan tower, Blackwater a grey steel boat with
 * a black waterline and rust. Live every frame; the rect it and its bubbles cover comes back. */
static void draw_submarine(ctx_t *c, const tank_t *t, int *bx0, int *by0, int *bx1, int *by1) {
    float clock = t->clock;
    float fx = t->sub_x, fy = t->sub_y + sinf(clock * 0.9f) * 1.5f;
    float d = t->sub_yaw >= 0 ? 1 : -1, sx = fabsf(t->sub_yaw) < 0.2f ? 0.2f : fabsf(t->sub_yaw);
    int th = theme_active();
    uint32_t hull  = th == THEME_QUIET_LAGOON ? 0x5f8f62 : th == THEME_TIDEPOOL_CLUB ? 0xe8524a : th == THEME_BLACKWATER ? 0x56606a : 0xf2c230;
    uint32_t belly = th == THEME_QUIET_LAGOON ? 0x3f6a48 : th == THEME_TIDEPOOL_CLUB ? 0xfff4e0 : th == THEME_BLACKWATER ? 0x23282d : 0xd9a520;
    uint32_t trim  = th == THEME_QUIET_LAGOON ? 0xc9a55a : th == THEME_TIDEPOOL_CLUB ? 0x2fb8c8 : th == THEME_BLACKWATER ? 0x8a3a2a : 0x3a3f44;
    uint32_t tower = th == THEME_QUIET_LAGOON ? 0x4e7a52 : th == THEME_TIDEPOOL_CLUB ? 0x2fb8c8 : th == THEME_BLACKWATER ? 0x3e464e : 0xf2c230;
    uint32_t glass = th == THEME_QUIET_LAGOON ? 0xc4e6d1 : th == THEME_TIDEPOOL_CLUB ? 0xfff6d9 : th == THEME_BLACKWATER ? 0xaee6f0 : 0x9fd8e2;
    uint32_t lit   = mix(hull, 0xffffff, 0.35f), metal = th == THEME_TIDEPOOL_CLUB ? 0xffc040 : 0xb8c0c6;
    uint32_t mist  = th == THEME_TIDEPOOL_CLUB ? 0x3d8e83 : th == THEME_BLACKWATER ? 0xc8e8ee : th == THEME_QUIET_LAGOON ? 0xc4e6d1 : 0x9fd8e2;
#define FX(lx) (fx + (lx) * d * sx)
    bool moving = t->sub_v > 1.5f;
    /* the bubbles: a stream from the stern while moving (fast, many), a lazy few from the tower at a stop */
    int nb = moving ? 14 : 5;
    for (int i = 0; i < nb; i++) {
        float ph = fmodf(clock * (moving ? 0.55f + 0.04f * (i % 4) : 0.3f + 0.03f * (i % 3)) + i * 0.071f, 1.0f);
        float ox = moving ? -28 - ph * 26 * sx : 2, oy = moving ? -2 - ph * 34 : -9 - ph * 40;
        float bx = FX(ox) + sinf(clock * 2.1f + i * 1.9f) * (1 + ph * 3), by = fy + oy;
        float r = 0.6f + ph * (moving ? 1.1f : 1.5f); int a = (int)(150 * (1 - ph * 0.8f));
        fill_ellipse(c, bx, by, r, r, mist, a);
        if (r > 1.2f) px_blend(c, (int)(bx - 0.4f), (int)(by - 0.5f), 0xffffff, 110);
    }
    if (th == THEME_BLACKWATER) {
        /* the abyss (2026-10-12): a HEADLIGHT from the bow, a cone of pale light widening over 40 px, three
           nested wedges of a few alpha each, undimmed - the sub lights its way at night */
        src_t beam = src_color(0xbfefff, 1);
        for (int i = 0; i < 3; i++) {
            float L = (42 - i * 9) * sx, hw = (11 - i * 2.5f) * fminf(1, L / 20);
            float px[4] = { FX(24), FX(24) + L * d, FX(24) + L * d, FX(24) }, py[4] = { fy - 1.2f, fy - hw, fy + hw, fy + 1.2f };
            fill_poly_s(c, px, py, 4, &beam, 16);
        }
    }
    /* the propeller behind the stern: two blades, spinning on the clock while moving, idling at a stop */
    { float spin = moving ? clock * 22 : clock * 1.5f, bl = fabsf(sinf(spin));
      thick_line(c, FX(-24), fy, FX(-29), fy, 1.6f, metal);                                           /* the shaft */
      fill_ellipse(c, FX(-29.5f), fy, 1.2f * sx + 0.3f, 5.5f * bl + 0.6f, metal, 230);
      fill_ellipse(c, FX(-29.5f), fy, 1.2f * sx + 0.3f, 5.5f * (1 - bl) + 0.6f, mix(metal, 0x303438, 0.4f), 200); }
    /* the rudder and the dive planes */
    { float rx[4] = { FX(-20), FX(-26), FX(-27), FX(-21) }, ry[4] = { fy - 5, fy - 10, fy - 10, fy - 2 }; fill_poly(c, rx, ry, 4, trim); }
    { float rx[4] = { FX(-20), FX(-26), FX(-27), FX(-21) }, ry[4] = { fy + 5, fy + 10, fy + 10, fy + 2 }; fill_poly(c, rx, ry, 4, trim); }
    fill_ellipse(c, FX(-19), fy + 1, 5 * sx + 0.4f, 1.4f, trim, 255);                                    /* the stern planes */
    /* the hull: a long ellipse, the belly dark, a highlight along the top */
    fill_ellipse(c, FX(0), fy, 25 * sx + 0.8f, 7.5f, hull, 255);
    fill_ellipse(c, FX(0), fy + 3.2f, 23 * sx + 0.6f, 3.6f, belly, 255);
    fill_ellipse(c, FX(2), fy - 4.3f, 15 * sx + 0.4f, 1.3f, lit, 200);
    if (th == THEME_BLACKWATER) { fill_ellipse(c, FX(-8), fy + 1, 3 * sx + 0.3f, 1.2f, 0x7a4a30, 150); fill_ellipse(c, FX(10), fy + 2, 2 * sx + 0.3f, 0.9f, 0x7a4a30, 140); }   /* rust */
    if (th == THEME_TIDEPOOL_CLUB) { fill_ellipse(c, FX(-10), fy, 2.2f * sx + 0.3f, 7, belly, 255); fill_ellipse(c, FX(12), fy, 2.2f * sx + 0.3f, 6.5f, belly, 255); }   /* the toy's white bands */
    /* the bow planes and three portholes */
    fill_ellipse(c, FX(14), fy + 1, 4 * sx + 0.3f, 1.2f, trim, 255);
    for (int i = 0; i < 3; i++) {
        float px = FX(-9 + i * 9);
        fill_ellipse(c, px, fy - 0.5f, 1.9f * sx + 0.4f, 1.9f, trim, 255);
        if (th == THEME_BLACKWATER) { src_t lamp = src_color(0xfff0c0, 1); fill_ellipse_s(c, px, fy - 0.5f, 1.2f * sx + 0.3f, 1.2f, &lamp, 255); }   /* lit from within, undimmed */
        else fill_ellipse(c, px, fy - 0.5f, 1.2f * sx + 0.3f, 1.2f, glass, 255);
        px_blend(c, (int)(px - 0.5f), (int)(fy - 1.3f), 0xffffff, 140);
    }
    /* the conning tower, its window, a rail */
    { float tx[4] = { FX(-5), FX(7), FX(6), FX(-3) }, ty[4] = { fy - 6, fy - 6, fy - 14, fy - 14 }; fill_poly(c, tx, ty, 4, tower); }
    if (th == THEME_BLACKWATER) { src_t lamp = src_color(0xfff0c0, 1); fill_ellipse_s(c, FX(4), fy - 10, 1.5f * sx + 0.3f, 1.5f, &lamp, 255); }
    else fill_ellipse(c, FX(4), fy - 10, 1.5f * sx + 0.3f, 1.5f, glass, 255);
    thick_line(c, FX(-3), fy - 14.5f, FX(6), fy - 14.5f, 1.0f, trim);
    /* the periscope: rises from the tower at a stop, the head turning to look each way */
    if (t->sub_peri > 0.02f) {
        float up = t->sub_peri * 13, px = FX(1), top = fy - 14 - up;
        float look = sinf(t->sub_look * 1.7f);                                                       /* -1 left .. 1 right */
        thick_line(c, px, fy - 14, px, top, 1.6f, metal);
        thick_line(c, px, top + 0.5f, px + look * 4.5f, top + 0.5f, 1.8f, metal);                     /* the head, turned */
        fill_ellipse(c, px + look * 4.8f, top + 0.5f, 1.3f, 1.3f, 0x20262a, 255);                     /* the lens */
        px_blend(c, (int)(px + look * 4.8f), (int)(top), 0xffffff, 200);                               /* its glint */
    }
#undef FX
    *bx0 = (int)fx - 70; *bx1 = (int)fx + 70; *by0 = (int)fy - 62; *by1 = (int)fy + 14;   /* 70: the Blackwater headlight reaches 66 */
}

/* ---- a piece IN FRONT, the castle's way (2026-10-03). The cluster and the
 * coral IN FRONT were repainted whole every frame, over everything: with
 * both there the 1.8 spent more on them than on its four fish. Now, with
 * the scene cache, they are BAKED into the scene (bake_scene, which records
 * the pixels each covers); the grass skips those pixels (veg_span), and
 * after the fish the piece comes back only where something was drawn over
 * it this frame - the dirty mask says where, the scene holds the pixel,
 * already vignetted (front_restore). The picture is the same to the pixel.
 * Not while a placement page is up: a dragged piece is drawn live, the old
 * way. */
/* the masks of the pieces baked IN FRONT: row 0 is the piece's top row (y0),
 * bit 0 its left edge (its centre x - w / 2), wpr words a row, in the decor
 * scratch at `off`. The grass leaves a `grass` piece's pixels alone (the
 * castle's front row has its own silhouette for that, g_castle_mask). */
typedef struct { int8_t k; bool grass; short w, rows, y0; size_t off; } fmask_t;
static const fmask_t FMASK[] = {
    { DK_CASTLE,  false, CASTLE_MASK_W, CASTLE_ROWS + 12, CASTLE_FY - CASTLE_ROWS, offsetof(decor_scratch_t, ca_mask) },
    { DK_CLUSTER, true,  CL_W,          CL_H + 1,         CL_FY - CL_H,            offsetof(decor_scratch_t, cl_mask) },
    { DK_WRECK,   true,  WK_W,          WK_H + 1,         WK_FY - WK_H,            offsetof(decor_scratch_t, wk_mask) },
    { DK_CORAL,   true,  CORAL_W,       CORAL_H + 1,      CORAL_FY - CORAL_H,      offsetof(decor_scratch_t, co_mask) } };
#define FMASK_N ((int)(sizeof FMASK / sizeof FMASK[0]))
static inline int fmask_wpr(const fmask_t *m) { return (m->w + 31) / 32; }
static inline uint32_t *fmask_bits(const fmask_t *m) { return (uint32_t *)((char *)ds() + m->off); }
static const fmask_t *fmask_of(int k) {
    for (int i = 0; i < FMASK_N; i++) if (FMASK[i].k == k) return &FMASK[i];
    return NULL;
}
/* this frame's row of a grass-respecting mask at tank row y, NULL = none there */
static const uint32_t *fmask_row(const fmask_t *m, int y, int *x0) {
    int fx = g_front_x[m->k];
    if (!m->grass || fx < 0 || (unsigned)(y - m->y0) >= (unsigned)m->rows) return NULL;
    *x0 = fx - m->w / 2;
    return fmask_bits(m) + (y - m->y0) * fmask_wpr(m);
}
static bool front_near(int x0, int x1, int y) {
    for (int i = 0; i < FMASK_N; i++) {
        int mx0; if (fmask_row(&FMASK[i], y, &mx0) && x1 >= mx0 && x0 < mx0 + FMASK[i].w) return true;
    }
    return false;
}
static bool front_px(int x, int y) {
    for (int i = 0; i < FMASK_N; i++) {
        int mx0; const uint32_t *row = fmask_row(&FMASK[i], y, &mx0);
        int l = x - mx0;
        if (row && (unsigned)l < (unsigned)FMASK[i].w && ((row[l >> 5] >> (l & 31)) & 1)) return true;
    }
    return false;
}
/* n (<= 32) bits of a bit row from bit l on; bits outside 0..nw*32-1 read 0 */
static inline uint32_t row_bits(const uint32_t *row, int nw, int l, int n) {
    int sh = 0;
    if (l < 0) { sh = -l; if (sh >= n) return 0; n -= sh; l = 0; }
    int w = l >> 5;
    if (w >= nw) return 0;
    uint64_t v = row[w];
    if (w + 1 < nw) v |= (uint64_t)row[w + 1] << 32;
    uint32_t out = (uint32_t)(v >> (l & 31));
    if (n < 32) out &= (1u << n) - 1;
    return out << sh;
}
/* a frond span near a piece baked IN FRONT: the pixels the piece covers
 * are left alone, the rest drawn with the WHOLE span's vignette alpha - what
 * the span had when the piece was painted over it afterwards */
static void veg_span_front(ctx_t *c, int x0, int x1, int y, const src_t *s) {
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    int f0 = x0, f1 = x1;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    int n = x1 - x0 + 1;
    const uint32_t *row[FMASK_N]; int rx0[FMASK_N];
    for (int i = 0; i < FMASK_N; i++) row[i] = fmask_row(&FMASK[i], y, &rx0[i]);
    if (n <= 32) {                              /* the usual frond: one look at the whole span */
        uint32_t m = 0;
        for (int i = 0; i < FMASK_N; i++) if (row[i]) m |= row_bits(row[i], fmask_wpr(&FMASK[i]), x0 - rx0[i], n);
        if (!m) { span_final(c, f0, f1, y, s, 255); return; }
        if (m == (n < 32 ? (1u << n) - 1 : 0xffffffffu)) return;
        int a = vig_alpha((x0 + x1) >> 1, y);
        uint16_t *p = &CTX_PX(c, x0, y);
        for (int k = 0; k < n; k++, p++) if (!((m >> k) & 1)) { *p = s->v; if (a) px_darken(p, a); }
        return;
    }
    int a = vig_alpha((x0 + x1) >> 1, y);
    uint16_t *p = &CTX_PX(c, x0, y);
    for (int x = x0; x <= x1; x++, p++) {
        bool covered = false;
        for (int i = 0; i < FMASK_N && !covered; i++) {
            int l = x - rx0[i];
            covered = row[i] && (unsigned)l < (unsigned)FMASK[i].w && ((row[i][l >> 5] >> (l & 31)) & 1);
        }
        if (covered) continue;
        *p = s->v; if (a) px_darken(p, a);
    }
}
/* the piece back over what was drawn on it this frame: every pixel that is
 * both dirty and the piece's takes the scene's (which holds the piece, lit
 * and vignetted) and is untagged, as the piece's own paint left it */
static void front_restore(uint16_t *fb, const uint32_t *mask, int wpr, int rows, int px0, int py0) {
    for (int r = 0; r < rows; r++) {
        int y = py0 + r;
        if ((unsigned)y >= (unsigned)TANK_H) continue;
        uint32_t *drow = g_dirty + y * DIRTY_WORDS_PER_ROW;
        for (int k = 0; k < wpr; k++) {
            uint32_t mw = mask[r * wpr + k];
            if (!mw) continue;
            int xb = px0 + k * 32;
            uint32_t hit = mw & row_bits(drow, DIRTY_WORDS_PER_ROW, xb, 32);
            while (hit) {
                int b = __builtin_ctz(hit); hit &= hit - 1;
                int x = xb + b;
                if ((unsigned)x >= (unsigned)TANK_W) continue;
                fb[y * TANK_W + x] = g_scene[y * TANK_W + x];
                drow[x >> 5] &= ~(1u << (x & 31));
            }
        }
    }
}
static bool g_scene_front;                                   /* the baked scene holds the pieces IN FRONT */

/* the floor's colour at (x,y), from a position hash so it is stable every
 * frame. The rectangle: a 14 px strip of speckled stones in four tones. The
 * bowl: the same speckle in 2 px grains over a bed that runs ~70 px down to
 * the glass, pebbles scattered through it, darkening with depth. */
static inline uint32_t floor_rgb(int x, int y) {
    if(theme_active()==THEME_QUIET_LAGOON) {
        unsigned grain=(unsigned)x*73856093u^(unsigned)y*19349663u;
        float depth=clamp01((y-(TANK_BOT-14))/(float)(TANK_H-(TANK_BOT-14)));
        uint32_t sand=mix(0x929380,0x4e6557,depth*.65f);
        return grain%37==0?mix(sand,0xacac96,.2f):grain%41==0?mix(sand,0x38544b,.25f):sand;
    }
    if (theme_active() == THEME_BLACKWATER) {
        /* dark volcanic sand (2026-10-10): a fine grain of three near-blacks, a scatter of pale shell
           grit and the odd rounded pebble, lit from the upper left, darker with depth */
        unsigned g = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u; g ^= g >> 13; g *= 0x5bd1e995u; g ^= g >> 15;
        const uint32_t *sand = theme_palette(THEME_BLACKWATER)->sand;
        uint32_t col = sand[(g >> 3) % 3];
        if (g % 53 == 0) col = 0x4a5668;                                   /* pale grit */
        if (g % 211 == 3) col = (g >> 9) & 1 ? 0x6ad8e8 : 0x3fb0c8;         /* a bioluminescent grain (2026-10-12) */
        int cx = x / 11, cy = (y - (TANK_BOT - 14)) / 7;                     /* one pebble, or none, per 11 x 7 cell */
        unsigned h = (unsigned)(cx + 3) * 0x9E3779B1u ^ (unsigned)(cy + 5) * 0xC2B2AE35u; h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15;
        if (h % 7 < 2) {
            float px = cx * 11 + 3 + (h >> 4) % 5, py = (TANK_BOT - 14) + cy * 7 + 3 + (h >> 8) % 2;
            float rx = 2.2f + (h >> 12) % 3, ry = 1.4f + ((h >> 16) % 2) * 0.5f;
            float dx = (x - px) / rx, dy = (y - py) / ry;
            if (dx * dx + dy * dy <= 1.0f) col = dx < -0.3f && dy < 0 ? 0x4e5a6e : dy < -0.2f ? 0x38425a : (h >> 20) % 2 ? 0x222a3a : 0x1a2030;
        }
        float depth = clamp01((y - (TANK_BOT - 14)) / (float)(TANK_H - (TANK_BOT - 14)));
        return mix(col, 0x030408, depth * 0.5f);
    }
    if (theme_active()) {
        const uint32_t *sand = theme_palette(theme_active())->sand;
        unsigned cell = (unsigned)(x / 9) * 73856093u ^ (unsigned)(y / 6) * 19349663u;
        int dx = x % 9 - 4, dy = y % 6 - 3;
        return sand[dx * dx + dy * dy * 2 < 13 ? (cell >> 4) % 3 : 3];
    }
#ifdef TANK_ROUND
    uint32_t g = (uint32_t)(x >> 1) * 0x9E3779B1u ^ (uint32_t)(y >> 1) * 0x85EBCA77u;
    g ^= g >> 15; g *= 0x2C1B3C6Du; g ^= g >> 12;
    uint32_t tone = g % 16;
    uint32_t col = tone < 2 ? 0x2e3b2c : tone < 5 ? 0x22301f : tone < 8 ? 0x1a2418 : 0x101a12;
    int top = TANK_BOT - 14, cx = x / 13, cy = (y - top) / 9;            /* one pebble, or none, per 13 x 9 cell */
    uint32_t h = (uint32_t)(cx + 7) * 0x9E3779B1u ^ (uint32_t)(cy + 3) * 0xC2B2AE35u;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15;
    if (y > top + 4 && h % 8 < 3) {
        float px = cx * 13 + 4 + (h >> 4) % 6, py = top + cy * 9 + 4 + (h >> 8) % 3;
        float rx = 2.6f + (h >> 12) % 3, ry = 1.6f + ((h >> 16) % 3) * 0.5f;
        float dx = (x - px) / rx, dy = (y - py) / ry;
        if (dx * dx + dy * dy <= 1.0f) col = dy < -0.25f ? 0x44544a : (h >> 20) % 2 ? 0x334238 : 0x2a372c;   /* lit from above */
    }
    float depth = (float)(y - top) / (TANK_H - top);
    return mix(col, 0x02080a, depth < 0 ? 0 : depth * 0.55f);
#else
    uint32_t h2 = (uint32_t)((x * 73856093u) ^ (y * 19349663u));
    uint32_t tone = (h2 >> 4) % 16;
    return tone < 2 ? 0x2e3b2c : tone < 5 ? 0x22301f : tone < 8 ? 0x1a2418 : 0x101a12;
#endif
}
/* Soft stationary light belongs only to the approved Living Lagoon direction.
 * Evaluated while baking, so the effect adds no work to a cached frame. */
static uint32_t lagoon_water(int x,int y,uint32_t water) {
    float across=x-y*.32f;
    float a=1-fabsf(across-TANK_W*.23f)/34;
    float b=1-fabsf(across-TANK_W*.48f)/17;
    float light=fmaxf(0,fmaxf(a,b))*(1-clamp01(y/(float)TANK_BOT))*.12f;
    return mix(water,0xd8e0bc,light);
}
/* Blackwater (2026-10-12): the abyss's light. A cold glow pooled at the top centre of the glass
 * (the moon through the surface far above), fading to nothing a third of the way down, a faint
 * blue haze toward the floor, and MARINE SNOW: a still scatter of pale motes by position hash,
 * the odd one a soft 3 px fleck. Bake-time only, as the lagoon's shafts. */
static uint32_t blackwater_water(int x,int y,uint32_t water) {
    float dx=(x-TANK_W*.5f)/(TANK_W*.62f),dy=(y+70)/(TANK_BOT*.78f);
    float d=dx*dx+dy*dy,moon=d<1?(1-d)*(1-d)*.3f:0;
    uint32_t col=mix(water,0x5aa6e0,moon);
    float deep=clamp01((y-TANK_BOT*.55f)/(TANK_BOT*.45f));
    col=mix(col,0x0a1e3c,deep*.1f);
    unsigned g=(unsigned)x*73856093u^(unsigned)y*19349663u;g^=g>>13;g*=0x5bd1e995u;g^=g>>15;
    if(g%2300==0)return mix(col,0xd8f0f8,.42f);
    if(g%3100==7)return mix(col,0xbfe4f0,.22f);
    unsigned c=(unsigned)(x>>2)*0x9E3779B1u^(unsigned)(y>>2)*0x85EBCA77u;c^=c>>15;c*=0x2C1B3C6Du;c^=c>>12;
    if(c%4200==0){int fx=(x&3)-2,fy=(y&3)-2;if(fx*fx+fy*fy<=2)return mix(col,0xa8d8ea,fx==0&&fy==0?.4f:.14f);}
    return col;
}
/* ---- the decor's passes (decor.h, 2026-10-21): what each piece paints, and when ----
 * A piece with a body in the static scene is baked there - BEHIND, whole; IN
 * FRONT, whole too, with its solid pixels recorded into its mask (FMASK) to
 * come back over the creatures. Without the scene cache, or dragged on the
 * placement page, it is drawn live. Every frame then has a back pass before
 * the creatures (pass BACK: the pieces BEHIND, whole; pass MIDDLE: the woven
 * ones' back halves) and a front pass after them (MIDDLE: the front halves;
 * FRONT: the pieces IN FRONT, whole). Each piece answers for itself in each
 * pass; the PAINT_* orders are the painter's. */
typedef struct { short x0, y0, x1, y1; } drect_t;
typedef struct { drect_t *r; int n, cap; bool on; } dyn_t;            /* the frame's dynamic rects (render_tank's) */
static void dyn_add(dyn_t *d, int x0, int y0, int x1, int y1) {
    if (!d->on || d->n >= d->cap) return;
    d->r[d->n].x0 = (short)x0; d->r[d->n].y0 = (short)y0; d->r[d->n].x1 = (short)x1; d->r[d->n].y1 = (short)y1; d->n++;
}
typedef struct { const tank_t *t; ctx_t *c; uint16_t *fb; bool cached; dyn_t *dr; } dframe_t;
typedef struct { int k, x, z; bool placing; } dpos_t;                 /* a piece this frame (decor_state; x -1 = not in the tank) */
typedef struct { short x; uint8_t z, pad; int q; uint32_t look; } dkey_t;   /* what the scene holds of a piece (x -1 = not in it) */
static dkey_t g_scene_key[DK_COUNT];
typedef struct {
    void (*key)(const tank_t *t, uint32_t *look, int *q);              /* the scene key beyond x and z (its look, its growth step) */
    void (*bake)(ctx_t *c, const tank_t *t, const dkey_t *k, bool front);   /* its body into the scene (front: the scene holds the pieces IN FRONT) */
    void (*bake_front)(ctx_t *c, const tank_t *t, const dkey_t *k);    /* IN FRONT: the body and its mask */
    void (*plain)(ctx_t *c, const tank_t *t, const dpos_t *p);         /* the scene without a cache */
    void (*dragged)(ctx_t *c, const tank_t *t, const dpos_t *p);       /* over a scene baked without it */
    void (*back)(dframe_t *f, const dpos_t *p, int pass);
    void (*front)(dframe_t *f, const dpos_t *p, int pass);
} decor_draw_t;
/* recording a piece's mask while it is drawn IN FRONT, and the piece back over what was drawn on it */
static void fmask_rec_begin(int k, int cx) {
    const fmask_t *m = fmask_of(k);
    memset(fmask_bits(m), 0, (size_t)m->rows * fmask_wpr(m) * sizeof(uint32_t));
    g_rec = fmask_bits(m); g_rec_x0 = cx - m->w / 2; g_rec_y0 = m->y0; g_rec_w = m->w; g_rec_rows = m->rows; g_rec_wpr = fmask_wpr(m);
}
static void fmask_rec_end(int k, int cx) { g_rec = NULL; g_bake_x[k] = cx; }
static void fmask_restore(uint16_t *fb, int k, int cx) {
    const fmask_t *m = fmask_of(k);
    front_restore(fb, fmask_bits(m), fmask_wpr(m), m->rows, cx - m->w / 2, m->y0);
}

/* the grass beds and the sword plant: a bed's fronds, alternate ones behind and in front when woven */
static const int VEG_SEED[VEG_BEDS_MAX] = { 0, 7, 3, 5 };
static void veg_back(dframe_t *f, const dpos_t *p, int pass) {
    int b = DECOR[p->k].veg_bed;
    if (p->z == pass) draw_veg(f->c, f->t, b, VEG_SEED[b], pass == DECOR_Z_BACK ? -1 : 0, f->cached);   /* no dynamic rect: with the scene cache each frond span vignettes itself */
}
static void veg_front(dframe_t *f, const dpos_t *p, int pass) {
    int b = DECOR[p->k].veg_bed;
    if (p->z == pass) draw_veg(f->c, f->t, b, VEG_SEED[b], pass == DECOR_Z_MIDDLE ? 1 : -1, f->cached);
}

/* the castle: BEHIND or IN FRONT, its body is the scene's; IN FRONT its front row
   comes back over the fish (and the snail, the food, the bubbles) where they were
   drawn - a fish in the arch swims THROUGH - before the woven grass's front fronds */
static void castle_bake(ctx_t *c, const tank_t *t, const dkey_t *k, bool front) {
    draw_castle(c, k->x, 0, true);
    /* a themed castle IN FRONT (2026-10-10): its solid pixels come back over the fish through a
       mask (front_restore), like the cluster's - it was repainted whole per dirty rect before,
       and the pineapple's per-pixel geometry over 25 creatures' rects took ~200 ms a frame */
    if (front && k->z == DECOR_Z_FRONT && theme_active()) {
        fmask_rec_begin(DK_CASTLE, k->x);
        draw_castle(c, k->x, 1, true);                                 /* the same pixels again, recorded */
        fmask_rec_end(DK_CASTLE, k->x);
    }
}
static void castle_plain(ctx_t *c, const tank_t *t, const dpos_t *p)   { draw_castle(c, p->x, 0, false); }
static void castle_dragged(ctx_t *c, const tank_t *t, const dpos_t *p) { draw_castle(c, p->x, 0, true); }
static void castle_back(dframe_t *f, const dpos_t *p, int pass) {
    if (pass == DECOR_Z_BACK && p->z == DECOR_Z_FRONT) g_veg_mask_cx = p->x;   /* the grass stops at its walls */
}
static void castle_front(dframe_t *f, const dpos_t *p, int pass) {
    if (pass != DECOR_Z_MIDDLE || p->z != DECOR_Z_FRONT) return;
    if (f->cached && g_bake_x[DK_CASTLE] == p->x && !p->placing && !g_castle_live)   /* a themed castle, baked: back from the scene where something was drawn over it */
        fmask_restore(f->fb, DK_CASTLE, p->x);
    else if (f->cached) for (int i = 0; i < f->dr->n; i++)
        draw_castle_front_rect(f->c, p->x, f->dr->r[i].x0, f->dr->r[i].y0, f->dr->r[i].x1, f->dr->r[i].y1);
    else draw_castle(f->c, p->x, 1, false);
}

/* the coral and the reef cluster: BEHIND, baked at their growth step (the
   sprite rebuilds by the step), the crown swaying over it under the grass; IN
   FRONT at the live growth, back over everything, the crown over it */
static void coral_key(const tank_t *t, uint32_t *look, int *q) { *look = tank_coral_rgb(t); *q = (int)(tank_coral_growth(t) * (CORAL_Q - 0.01f)); }
static void coral_bake(ctx_t *c, const tank_t *t, const dkey_t *k, bool front) {
    if (k->z == DECOR_Z_BACK) draw_coral(c, k->x, k->look, (k->q + 0.5f) / CORAL_Q, true);
}
static void coral_bake_front(ctx_t *c, const tank_t *t, const dkey_t *k) {
    fmask_rec_begin(DK_CORAL, k->x);
    draw_coral(c, k->x, k->look, tank_coral_growth(t), true);
    fmask_rec_end(DK_CORAL, k->x);
}
static void coral_plain(ctx_t *c, const tank_t *t, const dpos_t *p) {
    if (p->z == DECOR_Z_BACK) draw_coral(c, p->x, tank_coral_rgb(t), tank_coral_growth(t), false);
}
static void coral_dragged(ctx_t *c, const tank_t *t, const dpos_t *p) {
    if (p->z == DECOR_Z_BACK) draw_coral(c, p->x, tank_coral_rgb(t), tank_coral_growth(t), true);
}
static void coral_crown(dframe_t *f, const dpos_t *p) {
    int qx0, qy0, qx1, qy1;
    draw_coral_crown(f->c, p->x, tank_coral_rgb(f->t), tank_coral_growth(f->t), f->t->clock, &qx0, &qy0, &qx1, &qy1);
    if (qx1 >= qx0) dyn_add(f->dr, qx0, qy0, qx1, qy1);
}
static void coral_back(dframe_t *f, const dpos_t *p, int pass) { if (pass == DECOR_Z_BACK && p->z == DECOR_Z_BACK) coral_crown(f, p); }
static void coral_front(dframe_t *f, const dpos_t *p, int pass) {
    if (pass != DECOR_Z_FRONT || p->z != DECOR_Z_FRONT) return;
    if (g_front_x[DK_CORAL] >= 0) fmask_restore(f->fb, DK_CORAL, p->x);   /* baked: back only over what was drawn on it this frame (the cluster's crown too) */
    else draw_coral(f->c, p->x, tank_coral_rgb(f->t), tank_coral_growth(f->t), f->cached);
    coral_crown(f, p);
}
static void cluster_key(const tank_t *t, uint32_t *look, int *q) {
    float g = tank_cluster_growth(t);
    *look = (uint32_t)tank_cluster_scheme(t); *q = (int)((g > 1 ? 1 : g) * (CL_Q - 0.01f));
}
static void cluster_bake(ctx_t *c, const tank_t *t, const dkey_t *k, bool front) {
    if (k->z == DECOR_Z_BACK) draw_cluster(c, k->x, (int)k->look, (k->q + 0.5f) / CL_Q, true);
}
static void cluster_bake_front(ctx_t *c, const tank_t *t, const dkey_t *k) {
    fmask_rec_begin(DK_CLUSTER, k->x);
    draw_cluster(c, k->x, (int)k->look, tank_cluster_growth(t), true);   /* the live growth, as the frame's own repaints use */
    fmask_rec_end(DK_CLUSTER, k->x);
}
static void cluster_plain(ctx_t *c, const tank_t *t, const dpos_t *p) {
    if (p->z == DECOR_Z_BACK) draw_cluster(c, p->x, tank_cluster_scheme(t), tank_cluster_growth(t), false);
}
static void cluster_dragged(ctx_t *c, const tank_t *t, const dpos_t *p) {
    if (p->z == DECOR_Z_BACK) draw_cluster(c, p->x, tank_cluster_scheme(t), tank_cluster_growth(t), true);
}
static void cluster_crown(dframe_t *f, const dpos_t *p) {
    int qx0, qy0, qx1, qy1;
    draw_cluster_crown(f->c, p->x, tank_cluster_scheme(f->t), tank_cluster_growth(f->t), f->t->clock, &qx0, &qy0, &qx1, &qy1);
    if (qx1 >= qx0) dyn_add(f->dr, qx0, qy0, qx1, qy1);
}
static void cluster_back(dframe_t *f, const dpos_t *p, int pass) { if (pass == DECOR_Z_BACK && p->z == DECOR_Z_BACK) cluster_crown(f, p); }
static void cluster_front(dframe_t *f, const dpos_t *p, int pass) {
    if (pass != DECOR_Z_FRONT || p->z != DECOR_Z_FRONT) return;
    if (g_front_x[DK_CLUSTER] >= 0) fmask_restore(f->fb, DK_CLUSTER, p->x);
    else draw_cluster(f->c, p->x, tank_cluster_scheme(f->t), tank_cluster_growth(f->t), f->cached);
    cluster_crown(f, p);
}

/* the shipwreck: BEHIND, whole; IN FRONT its dark inside is behind the fish
   and only the hull over them - not its holes, so a fish behind it shows
   through them. Baked IN FRONT the hull comes back through its mask; drawn
   live (no cache, dragged, the scene without its pieces IN FRONT), the hull
   is drawn over them */
static void wreck_bake(ctx_t *c, const tank_t *t, const dkey_t *k, bool front) {
    if (k->z == DECOR_Z_BACK) draw_wreck(c, k->x, true, -1);
    else if (!front) draw_wreck(c, k->x, true, 0);
}
static void wreck_plain(ctx_t *c, const tank_t *t, const dpos_t *p) {
    draw_wreck(c, p->x, false, p->z == DECOR_Z_BACK ? -1 : 0);
}
static void wreck_bake_front(ctx_t *c, const tank_t *t, const dkey_t *k) {
    draw_wreck(c, k->x, true, 0);                                      /* its dark inside, behind the holes: not the mask's */
    fmask_rec_begin(DK_WRECK, k->x);
    draw_wreck(c, k->x, true, 1);
    fmask_rec_end(DK_WRECK, k->x);
}
static void wreck_dragged(ctx_t *c, const tank_t *t, const dpos_t *p) {
    draw_wreck(c, p->x, true, p->z == DECOR_Z_BACK ? -1 : 0);
}
static void wreck_front(dframe_t *f, const dpos_t *p, int pass) {
    if (pass != DECOR_Z_FRONT || p->z != DECOR_Z_FRONT) return;
    if (g_front_x[DK_WRECK] >= 0) fmask_restore(f->fb, DK_WRECK, p->x);
    else draw_wreck(f->c, p->x, f->cached, 1);
}

/* the airstone: its stones on the floor and the column's bubbles. BEHIND, both
   go behind the creatures (the stones over the grass's back fronds, the
   bubbles with the drifting ones, after the food); IN FRONT, both over them;
   woven, the stones stay behind and every other bubble of the column comes
   in front */
static void draw_airstone(ctx_t *c, const tank_t *t, dyn_t *dr) {
    float ax = tank_bubble_x(t), ay = TANK_BOT - 17;
    bool bw = theme_active()==THEME_BLACKWATER;   /* Blackwater: three basalt stones, lit from the upper left */
    fill_ellipse(c, ax - 6, ay + 1, 7, 4, theme_active()==THEME_TIDEPOOL_CLUB?0xcaac87:bw?0x3a3b3c:theme_active()?0x788f7a:0x2a3634, 255);
    fill_ellipse(c, ax + 5, ay + 2, 6, 3.5f, theme_active()==THEME_TIDEPOOL_CLUB?0xa78767:bw?0x2a2b2c:theme_active()?0x536e63:0x22302c, 255);
    fill_ellipse(c, ax, ay - 2, 6, 3.5f, theme_active()==THEME_TIDEPOOL_CLUB?0xead6ad:bw?0x55575a:theme_active()?0x96aa90:0x3a4a48, 255);
    fill_ellipse(c, ax - 1, ay - 3, 2, 1.2f, 0x5a6a68, 200);
    dyn_add(dr, (int)ax - 14, (int)ay - 7, (int)ax + 12, (int)ay + 7);
}
static void draw_bubble(ctx_t *c, const bubble_t *b, dyn_t *dr) {
    float r = b->column ? 2.6f : 1.8f;
    if(theme_active()==THEME_BLACKWATER) {
        /* a real bubble: a thin bright rim (the surface's reflection), a dark glassy inside, the sun's window top-left */
        for(int j=0;j<12;j++) {
            float a=TAU*j/12;
            px_blend(c,(int)(b->x+cosf(a)*r),(int)(b->y+sinf(a)*r),j>=6&&j<=10?0xe8f6ff:0x7fb4c4,j>=6&&j<=10?200:120);
        }
        fill_ellipse(c,b->x,b->y,r*.55f,r*.55f,0x9fd8e2,45);
        fill_ellipse(c,b->x-r*.4f,b->y-r*.4f,.9f,.6f,0xffffff,230);
    } else if(theme_active()) {
        bool club=theme_active()==THEME_TIDEPOOL_CLUB;
        r=club?r+0.6f:r;
        for(int j=0;j<12;j++) {
            float a=TAU*j/12;
            px_blend(c,(int)(b->x+cosf(a)*r),(int)(b->y+sinf(a)*r),club?0x3d8e83:0xc4e6d1,club?140:110);
        }
        fill_ellipse(c,b->x-r*.4f,b->y-r*.5f,club?1:.65f,.65f,0xfff6d9,240);
    } else {
        fill_ellipse(c, b->x, b->y, r, r, 0x9fd8e2, 60);
        px_blend(c, (int)(b->x - r * 0.4f), (int)(b->y - r * 0.4f), 0xffffff, 120);
    }
    dyn_add(dr, (int)b->x - 5, (int)b->y - 5, (int)b->x + 5, (int)b->y + 5);
}
static bool airstone_bubble_front(const tank_t *t, int i) {
    int z = decor_z(t, DK_AIRSTONE);
    return t->bubble[i].column && (z == DECOR_Z_FRONT || (z == DECOR_Z_MIDDLE && (i & 1)));
}
static void airstone_back(dframe_t *f, const dpos_t *p, int pass) {
    if (pass == DECOR_Z_MIDDLE && p->z != DECOR_Z_FRONT) draw_airstone(f->c, f->t, f->dr);   /* after every back frond */
}
static void airstone_front(dframe_t *f, const dpos_t *p, int pass) {
    if (pass != p->z || pass == DECOR_Z_BACK) return;
    if (p->z == DECOR_Z_FRONT) draw_airstone(f->c, f->t, f->dr);
    for (int i = 0; i < MAX_BUBBLE; i++) if (airstone_bubble_front(f->t, i)) draw_bubble(f->c, &f->t->bubble[i], f->dr);
}

static const decor_draw_t DECOR_DRAW[DK_COUNT] = {
    [DK_SEAGRASS0] = { .back = veg_back, .front = veg_front },
    [DK_SEAGRASS1] = { .back = veg_back, .front = veg_front },
    [DK_SEAGRASS2] = { .back = veg_back, .front = veg_front },
    [DK_PLANT]     = { .back = veg_back, .front = veg_front },
    [DK_CASTLE]    = { .bake = castle_bake, .plain = castle_plain, .dragged = castle_dragged, .back = castle_back, .front = castle_front },
    [DK_CORAL]     = { .key = coral_key, .bake = coral_bake, .bake_front = coral_bake_front, .plain = coral_plain, .dragged = coral_dragged,
                       .back = coral_back, .front = coral_front },
    [DK_CLUSTER]   = { .key = cluster_key, .bake = cluster_bake, .bake_front = cluster_bake_front, .plain = cluster_plain, .dragged = cluster_dragged,
                       .back = cluster_back, .front = cluster_front },
    [DK_WRECK]     = { .bake = wreck_bake, .bake_front = wreck_bake_front, .plain = wreck_plain, .dragged = wreck_dragged, .front = wreck_front },
    [DK_AIRSTONE]  = { .back = airstone_back, .front = airstone_front },
};
/* the painter's orders: the scene's bodies, the scene's pieces IN FRONT (the wreck
   first: the reef pieces stand over it), the back pass, the front pass */
static const uint8_t PAINT_SCENE[]       = { DK_CASTLE, DK_CORAL, DK_CLUSTER, DK_WRECK };
static const uint8_t PAINT_SCENE_FRONT[] = { DK_WRECK, DK_CLUSTER, DK_CORAL };
static const uint8_t PAINT_BACK[]        = { DK_CASTLE, DK_CORAL, DK_CLUSTER, DK_SEAGRASS0, DK_SEAGRASS1, DK_SEAGRASS2, DK_PLANT, DK_AIRSTONE };
static const uint8_t PAINT_FRONT[]       = { DK_CASTLE, DK_SEAGRASS0, DK_SEAGRASS1, DK_SEAGRASS2, DK_PLANT, DK_WRECK, DK_CLUSTER, DK_CORAL, DK_AIRSTONE };
#define N_OF(a) ((int)(sizeof (a) / sizeof (a)[0]))
static void decor_pass(dframe_t *f, const dpos_t *pos, const uint8_t *order, int n, bool front, int pass) {
    for (int i = 0; i < n; i++) {
        const dpos_t *p = &pos[order[i]];
        void (*fn)(dframe_t *, const dpos_t *, int) = front ? DECOR_DRAW[p->k].front : DECOR_DRAW[p->k].back;
        if (p->x >= 0 && fn) fn(f, p, pass);
    }
}
static void decor_positions(const tank_t *t, dpos_t *pos) {
    for (int k = 0; k < DK_COUNT; k++) { pos[k].k = k; decor_state(t, k, &pos[k].x, &pos[k].z, &pos[k].placing); }
}

static void draw_scene(const tank_t *t, uint16_t *fb, int stride, float dim) {
    ctx_t c = ctx_full(fb, stride, dim);
    /* water gradient #0a3c46 → #08272f → #031015 */
    for (int y = 0; y < TANK_H; y++) {
        uint16_t v = rgb565(water_rgb(y), dim);
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = theme_active()==THEME_QUIET_LAGOON?rgb565(lagoon_water(x,y,water_rgb(y)),dim):theme_active()==THEME_BLACKWATER?rgb565(blackwater_water(x,y,water_rgb(y)),dim):v;
    }
    /* pebbled bottom: irregular top edge, speckled stones in three tones.
       All variation comes from a position hash so it is stable every frame. */
    for (int x = 0; x < TANK_W; x++) {
        uint32_t h = (uint32_t)(x * 2654435761u);
        int top = TANK_BOT - 14 - (int)((h >> 8) % 5);
        for (int y = top; y < TANK_H; y++) {
            uint32_t col = floor_rgb(x, y);
            fb[y * stride + x] = rgb565(col, dim);
        }
    }
    /* (the dark oval under the left bed - the "reef rock" - is gone, 2026-10-02:
       it stood for nothing; the reef is the cluster the keeper buys) */
    dpos_t pos[DK_COUNT]; decor_positions(t, pos);
    for (int i = 0; i < N_OF(PAINT_SCENE); i++) {
        const dpos_t *p = &pos[PAINT_SCENE[i]];
        if (p->x >= 0 && DECOR_DRAW[p->k].plain) DECOR_DRAW[p->k].plain(&c, t, p);
    }
}


/* The baked scene (scene cache mode). Water gradient x porthole vignette
 * computed in 8-bit and ORDERED-DITHERED to RGB565 (4x4 Bayer: at 322 ppi
 * the pattern is invisible, the 5/6-bit banding of a dark gradient and of
 * the vignette falloff is not - Strato saw it at the edges), then the floor
 * and the backdrop decor on top, vignetted per pixel. Fills the vignette LUT on the
 * way. Bake-only: fidelity here costs nothing per frame. The light shafts
 * are gone (2026-09-01, Strato: they never looked good on this screen). */
static void bake_scene(const tank_t *t, uint16_t *sc, float dim) {
    static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    for (int y = 0; y < TANK_H; y++) {
        uint32_t col = water_rgb(y);
        int r = (int)(((col >> 16) & 255) * dim), g = (int)(((col >> 8) & 255) * dim), b = (int)((col & 255) * dim);
        for (int x = 0; x < TANK_W; x++) {
            if(theme_active()==THEME_QUIET_LAGOON||theme_active()==THEME_BLACKWATER) {
                uint32_t lit=theme_active()==THEME_QUIET_LAGOON?lagoon_water(x,y,col):blackwater_water(x,y,col);
                r=(int)(((lit>>16)&255)*dim);g=(int)(((lit>>8)&255)*dim);b=(int)((lit&255)*dim);
            }
            int a = vig_alpha(x, y);
            if (g_vig) g_vig[y * TANK_W + x] = (uint8_t)a;
            int inv = 256 - a, d = bayer[y & 3][x & 3];
            int rr = (r * inv) >> 8, gg = (g * inv) >> 8, bb = (b * inv) >> 8;
            int r5 = (rr + (d >> 1)) >> 3, g6 = (gg + (d >> 2)) >> 2, b5 = (bb + (d >> 1)) >> 3;
            if (r5 > 31) r5 = 31;
            if (g6 > 63) g6 = 63;
            if (b5 > 31) b5 = 31;
            sc[y * TANK_W + x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }
    /* pebbled bottom: irregular top edge, speckled stones in three tones,
       stable position hash; vignetted in 8-bit before quantising */
    for (int x = 0; x < TANK_W; x++) {
        uint32_t h = (uint32_t)(x * 2654435761u);
        int top = TANK_BOT - 14 - (int)((h >> 8) % 5);
        for (int y = top; y < TANK_H; y++) {
            uint32_t col = floor_rgb(x, y);
            int inv = 256 - (g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y));
            int r = (int)(((col >> 16) & 255) * dim) * inv >> 8;
            int g = (int)(((col >> 8) & 255) * dim) * inv >> 8;
            int b = (int)((col & 255) * dim) * inv >> 8;
            sc[y * TANK_W + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    ctx_t c = ctx_full(sc, TANK_W, dim);
    for (int k = 0; k < DK_COUNT; k++) g_bake_x[k] = -1;
    for (int i = 0; i < N_OF(PAINT_SCENE); i++) {                 /* the bodies, unless dragged */
        const dkey_t *key = &g_scene_key[PAINT_SCENE[i]];
        if (key->x >= 0 && DECOR_DRAW[PAINT_SCENE[i]].bake) DECOR_DRAW[PAINT_SCENE[i]].bake(&c, t, key, g_scene_front);
    }
    for (int i = 0; i < N_OF(PAINT_SCENE_FRONT) && g_scene_front; i++) {   /* the pieces IN FRONT, each noting the pixels it covers */
        const dkey_t *key = &g_scene_key[PAINT_SCENE_FRONT[i]];
        if (key->x >= 0 && key->z == DECOR_Z_FRONT && DECOR_DRAW[PAINT_SCENE_FRONT[i]].bake_front) DECOR_DRAW[PAINT_SCENE_FRONT[i]].bake_front(&c, t, key);
    }
    g_rec = NULL;
}

void render_tank(const tank_t *t, uint16_t *fb, int stride) {
    render_use_theme(t->theme);
    float dim = t->night ? 0.45f : 1.0f;
    ctx_t c = ctx_full(fb, stride, dim);
    int64_t p0 = PROF_MARK();
    bool cached = g_scene && g_dirty && stride == TANK_W;
    g_dirty_hold = false;                          /* the tank draws in the frame's coordinates: its marks count */
    if (cached) memset(g_dirty, 0, TANK_H * DIRTY_WORDS_PER_ROW * sizeof(uint32_t));
    drect_t rects[8 + MAX_FOOD + MAX_BUBBLE + N_FISH_MAX + VEG_BEDS_MAX + SHRIMP_MAX];
    dyn_t dr = { rects, 0, (int)(sizeof rects / sizeof rects[0]), cached };
#define DYN_RECT(cx0, cy0, cx1, cy1) dyn_add(&dr, (cx0), (cy0), (cx1), (cy1))

    dpos_t pos[DK_COUNT]; decor_positions(t, pos);
    dframe_t df = { t, &c, fb, cached, &dr };
    bool front = cached && !setup_is_place();      /* the pieces IN FRONT ride in the scene, but for a placement page's drag */
    for (int k = 0; k < DK_COUNT; k++) g_front_x[k] = -1;   /* set below, once the scene is known to hold them */
    g_front_n = 0;
    if (cached) {
        dkey_t key[DK_COUNT]; memset(key, 0, sizeof key);
        for (int k = 0; k < DK_COUNT; k++) {
            if (!DECOR_DRAW[k].bake) continue;     /* the scene holds what has a body there */
            key[k].x = (short)(pos[k].placing ? -1 : pos[k].x); key[k].z = (uint8_t)pos[k].z;
            if (pos[k].x >= 0 && DECOR_DRAW[k].key) DECOR_DRAW[k].key(t, &key[k].look, &key[k].q);
        }
        if (g_scene_dim != dim || g_scene_front != front || memcmp(g_scene_key, key, sizeof key)) {
            memcpy(g_scene_key, key, sizeof key); g_scene_front = front;
            /* rebuild the static scene with the vignette baked in (the
               per-frame pass then only re-darkens dynamic patches) */
            bake_scene(t, g_scene, dim);
            if (g_vig) g_vig_filled = true;
            g_scene_dim = dim; g_scene_epoch++;
        }
        if (!(g_primed_fb == fb && g_primed_epoch == g_scene_epoch))
            memcpy(fb, g_scene, TANK_W * TANK_H * sizeof(uint16_t));
        g_primed_fb = NULL;
        for (int i = 0; i < N_OF(PAINT_SCENE); i++) {   /* dragged: drawn live over a scene baked without it */
            const dpos_t *p = &pos[PAINT_SCENE[i]];
            if (p->x >= 0 && p->placing && DECOR_DRAW[p->k].dragged) DECOR_DRAW[p->k].dragged(&c, t, p);
        }
        for (int i = 0; i < FMASK_N; i++)
            if (FMASK[i].grass && g_bake_x[FMASK[i].k] >= 0) { g_front_x[FMASK[i].k] = g_bake_x[FMASK[i].k]; g_front_n++; }
    } else draw_scene(t, fb, stride, dim);
    PROF_ADD(0, p0);

    PROF_ADD(1, p0);   /* stage 1 (light shafts) retired 2026-09-01 */
    /* the decor behind the creatures: the crowns of the pieces BEHIND (over the
       baked fan, under the grass), the plant BEHIND, then the back fronds of the
       woven beds - the reef bed (left - milestone lushness widens its base, never
       withers) and two decor beds, all growing up and out with tank_t.veg_growth
       until the keeper trims them. Their front fronds draw after the fish, below. */
    g_veg_mask_cx = -1;
    decor_pass(&df, pos, PAINT_BACK, N_OF(PAINT_BACK), false, DECOR_Z_BACK);
    decor_pass(&df, pos, PAINT_BACK, N_OF(PAINT_BACK), false, DECOR_Z_MIDDLE);
    PROF_ADD(2, p0);
    /* food pellets */
    for (int i = 0; i < MAX_FOOD; i++)
        if (t->food[i].alive) {
            if(theme_active()) {
                float x=t->food[i].x,y=t->food[i].y;
                if(theme_active()==THEME_QUIET_LAGOON) {
                    float xx[4]={x-3,x+1,x+3,x-1},yy[4]={y-1,y-3,y+1,y+3};fill_poly(&c,xx,yy,4,0xddc790);
                    fill_ellipse(&c,x,y,1.6f,.6f,0xf3e7b9,230);
                } else if(theme_active()==THEME_BLACKWATER) {   /* a real flake: a tan curl with a lit edge and a dark crease */
                    float xx[5]={x-3.2f,x-.5f,x+3,x+1.5f,x-2},yy[5]={y-.5f,y-2.8f,y-.8f,y+2.4f,y+2};fill_poly(&c,xx,yy,5,0xb9834a);
                    fill_ellipse(&c,x-.8f,y-.9f,1.5f,.7f,0xe8c48a,220);
                    px_blend(&c,(int)x+1,(int)y+1,0x5a3a1c,170);
                } else {
                    fill_ellipse(&c,x,y,3.2f,3.2f,0xc07a45,255);
                    fill_ellipse(&c,x,y-.5f,2,2,0xffd889,255);
                    fill_ellipse(&c,x,y,.7f,.7f,0xc07a45,255);
                }
            } else {
            fill_ellipse(&c, t->food[i].x, t->food[i].y, 2.6f, 2.6f, 0xffbd59, 255);
            fill_ellipse(&c, t->food[i].x - 0.8f, t->food[i].y - 0.8f, 1.0f, 1.0f, 0xffe9bd, 255);
            }
            DYN_RECT((int)t->food[i].x - 5, (int)t->food[i].y - 5, (int)t->food[i].x + 5, (int)t->food[i].y + 5);
        }
    /* bubbles: the drifting ones, and the column's behind the creatures (the airstone's depth: airstone_front) */
    for (int i = 0; i < MAX_BUBBLE; i++)
        if (!airstone_bubble_front(t, i)) draw_bubble(&c, &t->bubble[i], &dr);
    /* the shrimp school: on the sand and in the grass, under the fish and
       the front fronds (the grass is their cover) */
    if (t->sd_unlocks & SD_ITEM_SHRIMP) {
        src_t pal[SHRIMP_TONES];
        for (int k = 0; k < SHRIMP_TONES; k++) pal[k] = src_color(SHRIMP_RGB[k], c.dim);
        for (int i = 0; i < t->shrimp_n; i++) {
            draw_shrimp(&c, &t->shrimp[i], pal);
            DYN_RECT((int)t->shrimp[i].x - 10, (int)t->shrimp[i].y - 5, (int)t->shrimp[i].x + 10, (int)t->shrimp[i].y + 5);
        }
    }
    /* the frogman (2026-10-10): drifting sideways in mid-water, under the fish */
    if (t->sd_unlocks & SD_ITEM_FROGMAN) { int qx0, qy0, qx1, qy1; draw_frogman(&c, t, &qx0, &qy0, &qx1, &qy1); DYN_RECT(qx0, qy0, qx1, qy1); }
    /* the submarine (2026-10-10 night): cruising above the frogman's water, under the fish */
    if (t->sd_unlocks & SD_ITEM_SUB) { int qx0, qy0, qx1, qy1; draw_submarine(&c, t, &qx0, &qy0, &qx1, &qy1); DYN_RECT(qx0, qy0, qx1, qy1); }
    PROF_ADD(3, p0);
    /* fish */
    for (int i = 0; i < t->n_fish; i++) {
        g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -1;
        draw_fish(&c, t, &t->fish[i]);
        g_bb_on = false;
        if (g_bb_x1 >= g_bb_x0) DYN_RECT(g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1);
    }
    /* the snail on the floor (upright): in the scene, under the front fronds -
       unless it walks its front lane (tank_t.snail_front), drawn below, over
       the pieces placed IN FRONT */
    if (tank_snail_upright(t)) {
        if (!t->snail_front) draw_snail(&c, t, true);
        DYN_RECT((int)t->snail_x - 21, (int)t->snail_y - 13, (int)t->snail_x + 21, (int)t->snail_y + 7);
    }
    /* the urchin on the floor: with the fish, under the front fronds - at a
       bed's foot the grass weaves over it */
    if ((t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0) {
        draw_urchin(&c, t);
        DYN_RECT((int)t->urchin_x - 19, (int)URCHIN_FLOOR_Y - 17, (int)t->urchin_x + 19, (int)(URCHIN_FLOOR_Y + UR_BASE) + 1);
    }
    /* the decor over the creatures: the castle's front row (a fish in the arch
       swims THROUGH), the front fronds of the woven beds (each span applies its
       own vignette and untags itself, so the fish rects below skip it), then
       the pieces IN FRONT over everything - baked in the scene, each comes back
       only over what was drawn on it this frame; otherwise whole */
    decor_pass(&df, pos, PAINT_FRONT, N_OF(PAINT_FRONT), true, DECOR_Z_MIDDLE);
    decor_pass(&df, pos, PAINT_FRONT, N_OF(PAINT_FRONT), true, DECOR_Z_FRONT);
    if (tank_snail_upright(t) && t->snail_front) draw_snail(&c, t, true);   /* its front lane: over them (its rect is marked above) */
    PROF_ADD(4, p0);
    /* porthole vignette: darken corners toward AMOLED black. With a scene
       cache the full-frame pass is baked into the scene and only the dynamic
       patches are re-darkened; without one, the classic per-pixel pass runs. */
    if (cached) {
        /* one row sweep over the union of the dynamic rects: pixels marked
           in the dirty mask were drawn this frame (and not already vignetted
           inline) and get the vignette re-applied exactly once. */
        for (int y = 0; y < TANK_H; y++) {
            float dyf = (y - TANK_H * 0.5f) / (TANK_H * 0.5f);
            float rem = 0.72f - dyf * dyf;
            int x_in = rem > 0 ? (int)(TANK_W * 0.5f * (1 - sqrtf(rem))) : TANK_W / 2;
            if (x_in <= 0) continue;
            short iv[sizeof rects / sizeof rects[0]][2]; int ni = 0;
            for (int i = 0; i < dr.n; i++)
                if (y >= rects[i].y0 && y <= rects[i].y1) {
                    int a0 = rects[i].x0 < 0 ? 0 : rects[i].x0;
                    int a1 = rects[i].x1 >= TANK_W ? TANK_W - 1 : rects[i].x1;
                    if (a0 > a1) continue;
                    int j = ni++;                       /* insertion sort by x0 */
                    while (j > 0 && iv[j - 1][0] > a0) { iv[j][0] = iv[j - 1][0]; iv[j][1] = iv[j - 1][1]; j--; }
                    iv[j][0] = (short)a0; iv[j][1] = (short)a1;
                }
            int end = -1;                               /* merged sweep */
            for (int i = 0; i < ni; i++) {
                int a0 = iv[i][0] > end + 1 ? iv[i][0] : end + 1;
                int a1 = iv[i][1];
                if (a1 > end) end = a1;
                for (int s = 0; s < 2; s++) {           /* clip to the two ring spans */
                    int r0 = s ? (TANK_W - x_in > a0 ? TANK_W - x_in : a0) : a0;
                    int r1 = s ? a1 : (x_in - 1 < a1 ? x_in - 1 : a1);
                    const uint32_t *drow = g_dirty + y * DIRTY_WORDS_PER_ROW;
                    for (int x = r0; x <= r1; x++) {
                        uint32_t w = drow[x >> 5] >> (x & 31);
                        if (!w) { x |= 31; continue; }         /* nothing else in this word */
                        if (!(w & 1)) continue;
                        uint16_t *p = &c.fb[y * c.stride + x];
                        int a = (g_vig && g_vig_filled) ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                        if (a) px_darken(p, a);
                    }
                }
            }
        }
    } else {
        for (int y = 0; y < TANK_H; y++) {
            float dy = (y - TANK_H * 0.5f) / (TANK_H * 0.5f);
            float rem = 0.72f - dy * dy;
            int x_in = rem > 0 ? (int)(TANK_W * 0.5f * (1 - sqrtf(rem))) : TANK_W / 2;
            for (int side = 0; side < 2; side++)
                for (int k = 0; k < x_in; k++) {
                    int x = side ? TANK_W - 1 - k : k;
                    float dx = (x - TANK_W * 0.5f) / (TANK_W * 0.5f);
                    float d2 = dx * dx + dy * dy;
                    if (d2 > 0.72f) {
                        int a = (int)((d2 - 0.72f) * (theme_active() == THEME_TIDEPOOL_CLUB ? 18 : theme_active() == THEME_QUIET_LAGOON ? 65 : theme_active() == THEME_BLACKWATER ? 120 : 220));
                        if (a > 0) px_blend(&c, x, y, 0x000000, a > 255 ? 255 : a);
                    }
                }
        }
    }
    PROF_ADD(5, p0);
    /* algae film sits ON the glass - over the water, the fish, even the
     * vignette (which is why it draws after the re-darken pass: nothing
     * behind it needs repair, and next frame's scene restore erases wiped
     * cells for free) */
    draw_algae(&c, t);
    draw_snail(&c, t, false);                    /* on the glass, over the film */
    PROF_ADD(6, p0);
#undef DYN_RECT
}

/* ---- the battery (2026-09-24 redraw): Strato is color blind and could not
 * tell the charging pill (a teal fill) from the full one (green). Now the
 * cable shows as a SHAPE - a lightning bolt left of the pill - and flowing
 * charge as MOTION - a bright band sweeping the fill; the hue only repeats
 * it. On battery the fill wears the level colors; on the cable it is the
 * calm green whatever the level (a red sliver on the charger is no alarm).
 * The info page draws the same battery large. ---- */

/* the bolt, a polygon in a 10 x 16 box: the flat top, the stroke down to
   the left, the jog across, the tail to the point */
static const float BOLT_X[7] = { 6.0f, 0.5f, 4.0f, 2.5f, 10.0f, 6.5f, 10.0f };
static const float BOLT_Y[7] = { 0.0f, 9.5f, 9.5f, 16.0f, 5.5f, 5.5f, 0.0f };
/* filled with 4 sub-rows of exact span coverage per pixel row, so it stays a
   bolt at the pill's 15 px (no stair-stepped mush) and the page's 38 */
static void bolt_fill(ctx_t *c, float ox, float oy, float s, uint32_t rgb, int alpha) {
    src_t col = src_color(rgb, 1.0f);
    int x0 = (int)floorf(ox), w = (int)ceilf(10.0f * s) + 2;
    float cov[64];
    if (w > 64) w = 64;
    for (int y = (int)floorf(oy); y <= (int)ceilf(oy + 16.0f * s); y++) {
        memset(cov, 0, sizeof cov);
        for (int sub = 0; sub < 4; sub++) {
            float yy = (y + (sub + 0.5f) * 0.25f - oy) / s, xs[8]; int n = 0;
            for (int i = 0, j = 6; i < 7; j = i++)
                if ((BOLT_Y[i] > yy) != (BOLT_Y[j] > yy))
                    xs[n++] = ox + s * (BOLT_X[j] + (yy - BOLT_Y[j]) * (BOLT_X[i] - BOLT_X[j]) / (BOLT_Y[i] - BOLT_Y[j]));
            for (int i = 1; i < n; i++) for (int k = i; k > 0 && xs[k] < xs[k - 1]; k--) { float t = xs[k]; xs[k] = xs[k - 1]; xs[k - 1] = t; }
            for (int k = 0; k + 1 < n; k += 2)                   /* each inside interval, spread over the pixels it crosses */
                for (int px_ = (int)floorf(xs[k]); px_ <= (int)floorf(xs[k + 1]); px_++) {
                    float a = fmaxf(xs[k], (float)px_), b = fminf(xs[k + 1], px_ + 1.0f);
                    if (b > a && px_ - x0 >= 0 && px_ - x0 < w) cov[px_ - x0] += (b - a) * 0.25f;
                }
        }
        for (int i = 0; i < w; i++)
            if (cov[i] > 0.02f) px_blend_s(c, x0 + i, y, &col, (int)(fminf(cov[i], 1.0f) * alpha));
    }
}
/* the bolt h px tall at (x, y), with a dark rim so it reads over bright water */
static void draw_bolt(ctx_t *c, float x, float y, float h, uint32_t rgb) {
    float s = h / 16.0f;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) bolt_fill(c, x + dx, y + dy, s, 0x04141a, 200);
    bolt_fill(c, x, y, s, rgb, 255);
}
static float bolt_w(float h) { return 10.0f * h / 16.0f; }
static int bolt_room(int H) { return (int)bolt_w(H * 1.25f) + H / 4 + 2; }   /* the bolt beside a battery H tall, and its gap */

/* a battery at (X, Y), body W x H, `line` px of outline, the nub on the right;
   on the cable the bolt stands left of it (the caller leaves the room) */
static void round_fill(ctx_t *c,int x,int y,int w,int h,int r,uint32_t rgb);
static void draw_battery(ctx_t *c, int X, int Y, int W, int H, int line, float frac, int state, float clock) {
    if(theme_active()) {
        const theme_palette_t *p=theme_palette(theme_active());
        bool club=theme_active()==THEME_TIDEPOOL_CLUB,power=BAT_ON_POWER(state);
        frac=fminf(1,fmaxf(0,frac));int pad=H>25?4:2;
        uint32_t ink=frac<.2f?0xc36c51:p->accent;
        round_fill(c,X,Y,W,H,club?H/4:4,p->border);
        round_fill(c,X+1,Y+1,W-2,H-2,club?H/4:4,p->panel);
        round_fill(c,X+W,Y+H/3,line+2,H/3,1,p->border);
        int inside=W-pad*2,fill=(int)(inside*frac);
        if(club) {
            int gap=H>25?4:2,cw=(inside-gap*3)/4;
            for(int i=0;i<4;i++) {
                int x=X+pad+i*(cw+gap),fw=(int)(cw*fminf(1,fmaxf(0,frac*4-i)));
                round_fill(c,x,Y+pad,cw,H-pad*2,2,p->border);
                if(fw>0)round_fill(c,x,Y+pad,fw,H-pad*2,2,ink);
            }
        } else if(fill>0) {
            round_fill(c,X+pad,Y+pad,fill,H-pad*2,3,ink);
            src_t gleam=src_color(mix(ink,0xf1edcf,.35f),1);
            span(c,X+pad+2,X+pad+fill-3,Y+pad+1,&gleam,255);
        }
        if(state==BAT_CHARGING && fill>3) {
            int sweep=X+pad+(int)(fmodf(clock,2)/2*fmaxf(1,fill-2));
            for(int y=Y+pad+1;y<Y+H-pad-1;y++)px_blend(c,sweep,y,p->on_accent,160);
        }
        if(power)draw_bolt(c,X-bolt_room(H),Y-H/8.f,H*1.25f,p->accent);
        return;
    }
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    bool pw = BAT_ON_POWER(state);
    uint32_t col = pw ? 0x78d67d : frac < 0.2f ? 0xf25b65 : frac < 0.45f ? 0xffbd59 : 0x78d67d;
    uint32_t edge = pw ? 0xdfeef0 : 0x9fb4b8;                  /* on the cable the rim brightens too */
    src_t bg = src_color(theme_ui_color(0x04141a), 1.0f), e = src_color(theme_ui_color(edge), 1.0f), f = src_color(col, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 215);
    for (int k = 0; k < line; k++) {
        span(c, X, X + W - 1, Y + k, &e, 255); span(c, X, X + W - 1, Y + H - 1 - k, &e, 255);
        for (int y = Y; y < Y + H; y++) { px(c, X + k, y, e.v); px(c, X + W - 1 - k, y, e.v); }
    }
    int nh = H * 2 / 5, nw = line + 2;                         /* the nub */
    for (int y = Y + (H - nh) / 2; y < Y + (H + nh) / 2; y++) span(c, X + W, X + W + nw - 1, y, &e, 255);
    int in = line + 1, iw = W - 2 * in, fw = (int)(iw * frac + 0.5f);
    if (pw && fw < 2) fw = 2;
    for (int y = Y + in; y < Y + H - in; y++) if (fw > 0) span(c, X + in, X + in + fw - 1, y, &f, 255);
    if (state == BAT_CHARGING && fw > 0) {                     /* the sweep: charge flowing in, left to right */
        float ph = fmodf(clock, 1.8f) / 1.3f;
        if (ph <= 1.0f) {
            float bw = fmaxf(4.0f, iw / 5.0f), cx = X + in - bw + ph * (fw + 2 * bw);
            src_t wh = src_color(0xffffff, 1.0f);
            for (int x = X + in; x < X + in + fw; x++) {
                float d = fabsf(x + 0.5f - cx) / bw;
                if (d < 1) for (int y = Y + in; y < Y + H - in; y++) px_blend_s(c, x, y, &wh, (int)(170 * (1 - d)));
            }
        }
    }
    if (pw) draw_bolt(c, X - bolt_room(H), Y - H / 8.0f, H * 1.25f, state == BAT_PLUGGED ? 0x9fb4b8 : 0xffffff);   /* a touch taller than the battery */
}

/* the pill, top right (render.h) */
void render_battery(uint16_t *fb, int stride, float frac, int state, float clock) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    draw_battery(&c, RENDER_BAT_X, RENDER_BAT_Y, RENDER_BAT_W, RENDER_BAT_H, 1, frac, state, clock);
}

/* ---- stats overlay (selection ring + visual card) ---- */

static void ring(ctx_t *c, float cx, float cy, float r, uint32_t rgb) {
    for (int i = 0; i < 64; i++) {
        float a = i * (TAU / 64);
        px_blend(c, (int)(cx + cosf(a) * r), (int)(cy + sinf(a) * r), rgb, 180);
    }
}

/* blend one native RGB565 pixel (icon art is pre-colored; no dim - the card
 * ignores night, matching px_blend's use with c->dim = 1) */
static void px565_blend(ctx_t *c, int x, int y, uint16_t v, int a) {
    if (!CTX_IN(c, x, y)) return;
    uint16_t *p = &CTX_PX(c, x, y);
    int r = (*p >> 11)       + (((v >> 11)       - (*p >> 11))       * a >> 8);
    int g = ((*p >> 5) & 63) + ((((v >> 5) & 63) - ((*p >> 5) & 63)) * a >> 8);
    int b = (*p & 31)        + (((v & 31)        - (*p & 31))        * a >> 8);
    *p = (uint16_t)((r << 11) | (g << 5) | b);
}

/* draw a baked icon (icons.h, generated from Strato's pixel art) at x,y.
 * alpha scales the icon's own alpha plane: 255 = as drawn, lower = dimmed
 * (unrevealed traits, torn thought bubbles). */
static void blit_icon(ctx_t *c, int x, int y, const icon_t *ic, int alpha) {
    ic = theme_icon(ic);
    for (int j = 0; j < ic->h; j++)
        for (int i = 0; i < ic->w; i++) {
            int a = ic->a[j * ic->w + i];
            if (!a) continue;
            px565_blend(c, x + i, y + j, ic->rgb[j * ic->w + i], a * alpha >> 8);
        }
}

/* a segmented meter: value 0..1 over `segs` segments of `sw` x 10 px. The
 * last partial segment fills proportionally, so it still reads analog up
 * close but "3 of 5" at a glance. */
static void meter(ctx_t *c, int x, int y, int segs, int sw, float frac, uint32_t rgb) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float per = 1.0f / segs;
    src_t on = src_color(rgb, c->dim), off = src_color(theme_ui_color(0x2a3f45), c->dim);
    for (int s = 0; s < segs; s++) {
        int sx = x + s * (sw + 2);
        float have = (frac - s * per) / per;                   /* 0..1 of this segment */
        int fw = have >= 1 ? sw : have <= 0 ? 0 : (int)(have * sw + 0.5f);
        for (int yy = 0; yy < 10; yy++) {
            if (fw > 0)  span(c, sx, sx + fw - 1, y + yy, &on, 235);
            if (fw < sw) span(c, sx + fw, sx + sw - 1, y + yy, &off, 150);
        }
    }
}

/* a trait spectrum: pole icons at both ends, the fish sits at `frac` between
 * them. Unrevealed = dimmed poles, dashed line, a "?" instead of the dot -
 * you learn who a fish is by watching it, not by reading it. */
static void slider(ctx_t *c, int x, int y, int w, float frac, uint32_t rgb,
                   const icon_t *lo, const icon_t *hi, bool revealed) {
    blit_icon(c, x, y, lo, revealed ? 255 : 70);
    blit_icon(c, x + w - 16, y, hi, revealed ? 255 : 70);
    int lx = x + 20, lw = w - 40, ly = y + 8;                  /* the line between poles */
    for (int xx = 0; xx < lw; xx++)
        if (revealed || (xx & 4))
            for (int yy = -1; yy <= 0; yy++) px_blend(c, lx + xx, ly + yy, 0x2a3f45, revealed ? 200 : 110);
    if (revealed) {
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        fill_ellipse(c, lx + lw * frac, ly - 0.5f, 3.4f, 3.4f, rgb, 255);
        fill_ellipse(c, lx + lw * frac - 1, ly - 1.5f, 1.0f, 1.0f, 0xffffff, 200);
    } else
        blit_icon(c, lx + lw / 2 - 8, y, &icon_unknown_16, 220);
}

static void button(ctx_t *c, int x, int y, int W, int H, uint32_t fill, uint32_t edge, const char *label, int scale);   /* below */
static void draw_text(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s);                                  /* the pixel font, below */
static void draw_text_8px(ctx_t *c, int x, int y, uint32_t rgb, const char *s);                                       /* below */
/* the card's species line (NULL for the classic fish): the first of
   "SPECIES - DESIGN", "DESIGN SPECIES", "TOKEN - DESIGN" that fits the card */
#define CARD_KIND_CHARS ((RENDER_CARD_W - 8) / 6)
static const char *card_kind(const fish_t *f) {
    static char line[48];
    if (f->species <= SP_FISH || f->species >= SP_COUNT) return NULL;
    const species_def_t *sp = &SPECIES[f->species];
    const char *design = sp->var[f->variant % SP_VARIANTS].name;
    char tok[16]; int i = 0;
    for (; sp->token[i] && i < 15; i++) tok[i] = (char)(sp->token[i] >= 'a' && sp->token[i] <= 'z' ? sp->token[i] - 32 : sp->token[i]);
    tok[i] = 0;
    snprintf(line, sizeof line, "%s - %s", sp->name, design);
    if ((int)strlen(line) > CARD_KIND_CHARS) snprintf(line, sizeof line, "%s %s", design, sp->name);
    if ((int)strlen(line) > CARD_KIND_CHARS) snprintf(line, sizeof line, "%s - %s", tok, design);
    if ((int)strlen(line) > CARD_KIND_CHARS) snprintf(line, sizeof line, "%s", sp->name);
    return line;
}
static void card_draw(ctx_t c, const tank_t *t, int fish_idx) {
    const fish_t *f = &t->fish[fish_idx];
    /* card: top-left, bordered in the fish's own color (that's its "name").
     * Two zones: NEEDS (things you can act on now - icon + segmented meter)
     * above the divider, WHO THEY ARE (slow traits - pole-to-pole spectrum
     * sliders) below it. Iconified 2026-08-30 with Strato's pixel art. */
    const int X = RENDER_CARD_X, Y = RENDER_CARD_Y, W = RENDER_CARD_W, H = RENDER_CARD_H; /* x clear of the curved bezel */
    /* backdrop: 28k blended pixels - hoisted (this alone was most of the
     * card's ~12 ms/frame on the device through per-pixel px_blend) */
    src_t bg = src_color(theme_ui_color(0x04141a), 1.0f);
    for (int y = Y; y < Y + H; y++) span(&c, X, X + W - 1, y, &bg, 215);
    for (int x = X; x < X + W; x++) { px(&c, x, Y, rgb565(f->color, 1)); px(&c, x, Y + H - 1, rgb565(f->color, 1)); }
    for (int y = Y; y < Y + H; y++) { px(&c, X, y, rgb565(f->color, 1)); px(&c, X + W - 1, y, rgb565(f->color, 1)); }

    /* identity row: the fish's NAME and the certainty dot - bright = the
     * model was sure of its last decision, dim = torn. (2026-09-29, Strato:
     * the name at the top; the stage pips and growth bar are gone - "a bit
     * ambiguous", the keeper sees the fish's size, and the milestones page
     * names the stage. The colour swatch went with them: the
     * border is the fish's colour, and a 7-letter name needs the row.) */
    /* a creature of a species (2026-10-05) says what it is under its name, in
       the 8 px font: "SEAHORSE - GOLDEN" - or, too long for the card, the
       design first, or the species' short word ("EEL - CHARCOAL") */
    const char *kind = card_kind(f);
    draw_text(&c, X + 8, Y + (kind ? 5 : 8), 2, 0xffffff, f->name);
    if (kind) draw_text_8px(&c, X + 6, Y + 21, 0x9fd8e2, kind);
    fill_ellipse(&c, X + W - 14, Y + 14, 3.2f, 3.2f, 0xffffff, (int)(40 + 200 * f->goal.confidence));

    /* needs: 5 segments each. Hunger is shown as FULLNESS - a full belly is
     * a full meter, and it drains as the fish gets hungry (a food icon next
     * to a growing bar read backwards) */
    struct { const icon_t *ic; float v; uint32_t rgb; } needs[4] = {
        { &icon_hunger, 10.0f - f->hunger, 0xffbd59 },
        { &icon_energy, f->energy,         0x78d67d },
        { &icon_stress, f->stress,         0xf25b65 },
        { &icon_trust,  f->trust,          0xffd166 },
    };
    for (int i = 0; i < 4; i++) {
        int ry = Y + 30 + i * 27;
        blit_icon(&c, X + 8, ry, needs[i].ic, 255);
        meter(&c, X + 38, ry + 7, 5, 14, needs[i].v / 10.0f, needs[i].rgb);
    }

    /* divider between the zones */
    for (int x = X + 8; x < X + W - 8; x++) px_blend(&c, x, Y + 142, 0x2a3f45, 200);

    /* who they are: spectrum sliders, revealed by behaviour you have seen
       this fish do (docs/progression.md habits) */
    bool saw_bold = f->ms_bits & MS_FIRST_DART;
    bool saw_social = f->ms_bits & MS_FIRST_FOLLOW;
    bool saw_curious = f->ms_bits & (MS_INSPECTED | MS_FIRST_BUBBLES);
    slider(&c, X + 8, Y + 152, W - 16, f->bold,             0xffffff, &icon_shy,      &icon_bold,    saw_bold);
    slider(&c, X + 8, Y + 178, W - 16, f->sociable,         0x38dcc7, &icon_solo,     &icon_social,  saw_social);
    slider(&c, X + 8, Y + 204, W - 16, f->curiosity / 10.0f, 0x6db9ff, &icon_cautious, &icon_curious, saw_curious);
    /* (the MORE button that stood at the foot until 2026-10-21 went with the
       main menu: the BOOT key is the way to the pages now, the card only shows) */
}

/* ---- stats card cache (2026-09-01) ----
 * The card cost ~7 ms of every frame it was up (28k blended backdrop pixels
 * + icons + meters) - fps sagged whenever the keeper inspected a fish. With a
 * cache it is redrawn at most 4x a second over the STATIC water under it
 * (from the scene cache, so the translucent backdrop looks as before; only
 * things swimming behind the card stop showing through at 16%) and copied
 * into the frame otherwise. The selection ring follows the fish per frame. */
static uint16_t *g_card = NULL;
static int g_card_fish = -1; static float g_card_t = -1; static unsigned g_card_epoch;
void render_set_card_cache(uint16_t *buf) { g_card = buf; g_card_fish = -1; }

/* the snail's card (2026-09-16, Strato: "tapping the snail show a simple
   card with the upright snail image and how much algae has been grazed so
   far"): a ring on the snail, a centred box in the modal's dress - the
   upright sprite at 2x, SNAIL, the tally. Drawn every frame (no cache: a
   few hundred blended pixels, nothing like the fish card's meters). */
static int  text_w(const char *s, int scale);                                  /* the pixel font, below */
static void draw_text(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s);
static void rect_edge(ctx_t *c, int x, int y, int w, int h, uint32_t rgb);
static void blit_icon_scaled(ctx_t *c, int x, int y, const icon_t *ic, int s, bool lit);
static void snail_card_draw(ctx_t *c, const tank_t *t) {
    ring(c, t->snail_x, t->snail_y, 20, 0x9fd8e2);
    const int W = SNAIL_CARD_W, H = SNAIL_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(theme_ui_color(0x04141a), 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    const icon_t *ic = &icon_snail_upright;
    blit_icon_scaled(c, X + (W - ic->w * 2) / 2, Y + 10, ic, 2, true);
    draw_text(c, X + (W - text_w("SNAIL", 3)) / 2, Y + 82, 3, 0xffffff, "SNAIL");
    const char *cap = "ALGAE GRAZED SO FAR";
    draw_text(c, X + (W - text_w(cap, 2)) / 2, Y + 112, 2, 0x9fd8e2, cap);
    char n[24];
    if (t->snail_grazed <= 0) snprintf(n, sizeof n, "NOTHING YET");
    else snprintf(n, sizeof n, "%d SPOT%s", (int)t->snail_grazed, t->snail_grazed == 1 ? "" : "S");
    draw_text(c, X + (W - text_w(n, 3)) / 2, Y + 134, 3, 0xffffff, n);
}

/* the urchin's card (2026-10-02), the snail's: a ring on it, the urchin
   at 3x with its spines waving, SEA URCHIN, the grass it has eaten so far
   (the trim's centimeters) and what it is up to - in words, never colour. */
static void urchin_card_draw(ctx_t *c, const tank_t *t) {
    ring(c, t->urchin_x, URCHIN_FLOOR_Y - 3, 22, 0x9fd8e2);
    const int W = URCHIN_CARD_W, H = URCHIN_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(theme_ui_color(0x04141a), 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    ur_pose_t p; urchin_pose(&p, t->clock, tank_urchin_chewing(t));
    const int S = 3, ox = X + W / 2, oy = Y + 10 + 16 * S;               /* the dome's centre at 3x */
    for (int ly = -16; ly <= (int)UR_BASE; ly++)
        for (int lx = -18; lx <= 18; lx++) {
            int tone = urchin_tone((float)lx + 0.5f, (float)ly + 0.5f, &p);
            if (tone == UR_NONE) continue;
            src_t sc = src_color(URCHIN_RGB[tone], 1.0f);
            for (int yy = 0; yy < S; yy++) span(c, ox + lx * S, ox + lx * S + S - 1, oy + ly * S + yy, &sc, 255);
        }
    draw_text(c, X + (W - text_w("SEA URCHIN", 3)) / 2, Y + 90, 3, 0xffffff, "SEA URCHIN");
    const char *cap = "GRASS GRAZED SO FAR";
    draw_text(c, X + (W - text_w(cap, 2)) / 2, Y + 118, 2, 0x9fd8e2, cap);
    char n[24]; int cm = (int)(t->urchin_grazed_px / PX_PER_CM);
    if (cm < 1) snprintf(n, sizeof n, "NOTHING YET");
    else if (cm < 1000) snprintf(n, sizeof n, "%d CM", cm);
    else snprintf(n, sizeof n, "%d.%dK CM", cm / 1000, cm % 1000 / 100);   /* 1.0K CM and up: the line never outgrows the card (0.3.2) */
    draw_text(c, X + (W - text_w(n, 3)) / 2, Y + 140, 3, 0xffffff, n);
    const char *now = tank_urchin_chewing(t) ? "CHEWING"
                    : t->urchin_frond >= 0 ? "OFF TO THE TALL GRASS"
                    : "RESTING";
    draw_text(c, X + (W - text_w(now, 2)) / 2, Y + 170, 2, 0x9fd8e2, now);
}

/* the shrimp school's card (2026-09-29, Strato: "one tap should bring up a
   card to show how much food they've eaten and progress to the next shrimp").
   The pips fill with pellets - filled vs hollow, never colour alone. */
static void shrimp_card_draw(ctx_t *c, const tank_t *t) {
    int n = t->shrimp_n;
    float cx = 0, cy = 0, r = 0;
    for (int i = 0; i < n; i++) { cx += t->shrimp[i].x; cy += t->shrimp[i].y; }
    cx /= n; cy /= n;
    for (int i = 0; i < n; i++) { float d = tank_dist(cx, cy, t->shrimp[i].x, t->shrimp[i].y); if (d > r) r = d; }
    ring(c, cx, cy, fminf(fmaxf(r + 14, 22), 90), 0x9fd8e2);
    const int W = SHRIMP_CARD_W, H = SHRIMP_CARD_H, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(theme_ui_color(0x04141a), 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    /* a shrimp at 4x, its legs paddling */
    src_t pal[SHRIMP_TONES];
    for (int k = 0; k < SHRIMP_TONES; k++) pal[k] = src_color(SHRIMP_RGB[k], 1.0f);
    const char *const *fr = fmodf(t->clock * 2.5f, 1) < 0.5f ? SHRIMP_IDLE_A : SHRIMP_IDLE_B;
    const int S = 4, sx = X + (W - 18 * S) / 2, sy = Y + 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 18; x++) {
            const char *k = fr[y][x] == '.' ? NULL : strchr(SHRIMP_KEYS, fr[y][x]);
            if (!k) continue;
            int ti = (int)(k - SHRIMP_KEYS);
            for (int yy = 0; yy < S; yy++) span(c, sx + x * S, sx + x * S + S - 1, sy + y * S + yy, &pal[ti], SHRIMP_ALPHA[ti]);
        }
    char line[32];
    snprintf(line, sizeof line, "%d SHRIMP", n);
    draw_text(c, X + (W - text_w(line, 3)) / 2, Y + 46, 3, 0xffffff, line);
    snprintf(line, sizeof line, "%d PELLET%s EATEN", (int)t->shrimp_eaten, t->shrimp_eaten == 1 ? "" : "S");
    draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 76, 2, 0x9fd8e2, line);
    const char *cap = "NEXT SHRIMP";
    draw_text(c, X + (W - text_w(cap, 2)) / 2, Y + 104, 2, 0x9fd8e2, cap);
    bool full = n >= SHRIMP_MAX;
    int have = full ? SHRIMP_PER_JOIN : t->shrimp_food;
    const int pitch = 22, px0 = X + (W - (SHRIMP_PER_JOIN - 1) * pitch) / 2, py = Y + 132;
    for (int i = 0; i < SHRIMP_PER_JOIN; i++) {
        float x = px0 + i * pitch;
        if (i < have) {
            fill_ellipse(c, x, py, 6.5f, 6.5f, 0xffbd59, 255);
            fill_ellipse(c, x - 2, py - 2, 2.2f, 2.2f, 0xffe9bd, 255);
        } else ring(c, x, py, 6, 0x5a6a6e);
    }
    if (full) snprintf(line, sizeof line, "THE SCHOOL IS FULL");
    else if (tank_shrimp_refusing(t)) snprintf(line, sizeof line, "TOO MUCH ALGAE TO EAT");
    else if (t->shrimp_food >= SHRIMP_PER_JOIN && t->shrimp_cool > 0) {
        int m = (int)ceilf(t->shrimp_cool / 60.0f);
        snprintf(line, sizeof line, "ARRIVES IN %d MIN", m < 1 ? 1 : m);
    } else {
        int more = SHRIMP_PER_JOIN - t->shrimp_food;
        snprintf(line, sizeof line, "%d MORE PELLET%s", more, more == 1 ? "" : "S");
    }
    draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 158, 2, 0xffffff, line);
}

void render_stats_card(const tank_t *t, int fish_idx, uint16_t *fb, int stride) {
    render_use_theme(t->theme);
    if (fish_idx == RENDER_CARD_SHRIMP) {
        if (!(t->sd_unlocks & SD_ITEM_SHRIMP) || t->shrimp_n <= 0) return;
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        shrimp_card_draw(&sc, t);
        return;
    }
    if (fish_idx == RENDER_CARD_URCHIN) {
        if (!(t->sd_unlocks & SD_ITEM_URCHIN) || t->urchin_x < 0) return;
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        urchin_card_draw(&sc, t);
        return;
    }
    if (fish_idx == RENDER_CARD_SNAIL) {
        if (!(t->sd_unlocks & SD_ITEM_SNAIL) || t->snail_x < 0) return;
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        snail_card_draw(&sc, t);
        return;
    }
    if (fish_idx < 0 || fish_idx >= t->n_fish) return;
    ctx_t c = ctx_full(fb, stride, 1.0f);            /* card ignores night dimming */
    const fish_t *f = &t->fish[fish_idx];
    ring(&c, f->x, f->y, render_fish_ring_r(f), f->color);
    unsigned ep; const uint16_t *scene = render_scene_buf(&ep);
    if (g_card && scene) {
        if (fish_idx != g_card_fish || ep != g_card_epoch ||
            t->clock - g_card_t > 0.25f || t->clock < g_card_t) {
            for (int y = 0; y < RENDER_CARD_H; y++)
                memcpy(g_card + y * RENDER_CARD_W,
                       scene + (RENDER_CARD_Y + y) * TANK_W + RENDER_CARD_X, RENDER_CARD_W * 2);
            ctx_t cc = { g_card, RENDER_CARD_W, 1.0f, RENDER_CARD_X, RENDER_CARD_Y, RENDER_CARD_W, RENDER_CARD_H };
            card_draw(cc, t, fish_idx);
            g_card_fish = fish_idx; g_card_epoch = ep; g_card_t = t->clock;
        }
        for (int y = 0; y < RENDER_CARD_H; y++)
            memcpy(fb + (RENDER_CARD_Y + y) * stride + RENDER_CARD_X,
                   g_card + y * RENDER_CARD_W, RENDER_CARD_W * 2);
    } else card_draw(c, t, fish_idx);
}

/* ---- the tool chip (2026-10-01; the toolbox that stood under the card is
 * the main menu's bottom row since 2026-10-21) ----
 * Drawn every frame, uncached - so the backdrop is the card's look by bit
 * shifts, not blends: an eighth of the water through, the rest the card's
 * dark. */
static void shade_box(ctx_t *c, int x, int y, int w, int h) {
    const uint16_t base = rgb565(theme_ui_color(0x04141a), 0.875f);
    for (int yy = y; yy < y + h; yy++) {
        if ((unsigned)(yy - c->oy) >= (unsigned)c->h) continue;
        int x0 = x < c->ox ? c->ox : x, x1 = x + w - 1 > c->ox + c->w - 1 ? c->ox + c->w - 1 : x + w - 1;
        uint16_t *p = &CTX_PX(c, x0, yy);
        for (int xx = x0; xx <= x1; xx++, p++) *p = (uint16_t)(((*p >> 3) & 0x18E3) + base);
    }
}
static void rect_fill(ctx_t *c, int x, int y, int w, int h, uint32_t rgb);   /* below */

/* the chip: the tool at 1x and DONE, top left where the card would be */
#ifdef TANK_ROUND                                    /* the bowl: up in the left shoulder, inside the glass */
#define TOOL_CHIP_X 84
#define TOOL_CHIP_Y 72
#else
#define TOOL_CHIP_X RENDER_CARD_X
#define TOOL_CHIP_Y RENDER_CARD_Y
#endif
#define TOOL_CHIP_H 40
static int tool_chip_w(void) { return 8 + 24 + 8 + text_w("DONE", 2) + 10; }
void render_tool_chip(const tank_t *t, int fish_idx, uint16_t *fb, int stride) {
    render_use_theme(t->theme);
    if (t->tool == TOOL_HAND || (fish_idx >= 0 && fish_idx < t->n_fish)) return;
    ctx_t c = ctx_full(fb, stride, 1.0f);
    const int X = TOOL_CHIP_X, Y = TOOL_CHIP_Y, W = tool_chip_w(), H = TOOL_CHIP_H;
    shade_box(&c, X, Y, W, H);
    rect_edge(&c, X, Y, W, H, 0xffffff); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x9fd8e2);
    const icon_t *ic = t->tool == TOOL_SCISSORS ? &icon_tool_scissors : &icon_tool_sponge;
    blit_icon(&c, X + 8, Y + (H - ic->h) / 2, ic, 255);
    draw_text(&c, X + 8 + ic->w + 8, Y + (H - 14) / 2, 2, 0xffffff, "DONE");
}
bool render_tool_chip_hit(const tank_t *t, float x, float y) {
    return t->tool != TOOL_HAND && x < TOOL_CHIP_X + tool_chip_w() + 16 && y < TOOL_CHIP_Y + TOOL_CHIP_H + 24;
}


/* ---- a small pixel font (2026-09-11) ----
 * 5x7 glyphs - upper case, digits, a little punctuation - drawn as scale x
 * scale blocks, so one table reads at 2x for a caption and 3x for a
 * button. The renderer's first text (the milestones page can borrow it for
 * labels). Lower case maps to upper; anything else advances a cell. */
static const uint8_t FONT5X7[][7] = {
    { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* A */
    { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e }, /* B */
    { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e }, /* C */
    { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e }, /* D */
    { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f }, /* E */
    { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 }, /* F */
    { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f }, /* G */
    { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* H */
    { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* I */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c }, /* J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f }, /* L */
    { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
    { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, /* N */
    { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* O */
    { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 }, /* P */
    { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d }, /* Q */
    { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 }, /* R */
    { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e }, /* S */
    { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 }, /* V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a }, /* W */
    { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 }, /* X */
    { 0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04 }, /* Y */
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }, /* Z */
    { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e }, /* 0 */
    { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* 1 */
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f }, /* 2 */
    { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e }, /* 3 */
    { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 }, /* 4 */
    { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e }, /* 5 */
    { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e }, /* 6 */
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
    { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e }, /* 8 */
    { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c }, /* 9 */
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, /* ? */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, /* ! */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c }, /* . */
    { 0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08 }, /* , */
    { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 }, /* - */
    { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 }, /* : */
    { 0x0c, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 }, /* ' */
    { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 }, /* / */
    { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 }, /* + */
    { 0x19, 0x1a, 0x02, 0x04, 0x08, 0x0b, 0x13 }, /* % */
    { 0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00 }, /* ~ (the battery page's "about") */
};
static const char FONT_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789?!.,-:'/+%~";
static const uint8_t *glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    const char *p = ch ? strchr(FONT_CHARS, ch) : NULL;
    return p ? FONT5X7[p - FONT_CHARS] : NULL;
}
static int text_w(const char *s, int scale) { int n = (int)strlen(s); return n ? n * 6 * scale - scale : 0; }
static void draw_text(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s) {
    src_t col = src_color(theme_ui_color(rgb), c->dim);
    if (theme_active() && (scale == 2 || scale == 3)) {
        int w = 5 * scale, h = 7 * scale;
        for (; *s; s++, x += 6 * scale) {
            const uint8_t *g = theme_font_glyph(theme_asset_set(theme_active()), scale, (unsigned char)*s);
            if (!g) continue;
            for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < w; xx++) {
                int k = yy * w + xx, a = ((k & 1) ? g[k / 2] & 15 : g[k / 2] >> 4) * 17;
                if (a) span(c, x + xx, x + xx, y + yy, &col, a);
            }
        }
        return;
    }
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = glyph(*s);
        if (!g) continue;
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 5; k++)
                if (g[r] & (0x10 >> k))
                    for (int yy = 0; yy < scale; yy++)
                        span(c, x + k * scale, x + k * scale + scale - 1, y + r * scale + yy, &col, 255);
    }
}

/* 8 px text (2026-09-16, the settings page's version line): the 5x7 font at
 * scale 1 with its second row drawn twice. Row 1 is a vertical-stroke row in
 * every glyph the line can hold (letters, digits, '-', '.'), so the doubling
 * only lengthens strokes and never thickens a bar - Strato wanted the line
 * smaller than the page's text (14 px) but not the font's 7 px. */
static void draw_text_8px(ctx_t *c, int x, int y, uint32_t rgb, const char *s) {
    src_t col = src_color(theme_ui_color(rgb), c->dim);
    for (; *s; s++, x += 6) {
        const uint8_t *g = glyph(*s);
        if (!g) continue;
        for (int r = 0, yy = y; r < 7; r++, yy++) {
            for (int rep = r == 1 ? 2 : 1; rep; rep--, yy += rep ? 1 : 0)
                for (int k = 0; k < 5; k++)
                    if (g[r] & (0x10 >> k)) span(c, x + k, x + k, yy, &col, 255);
        }
    }
}

/* ---- reset confirm (2026-09-11) ----
 * The keeper's "start over" (device: hold BOOT, tap the glass; sim: X). A
 * modal panel over the live tank - the fish keep swimming behind it - that
 * asks before the save is wiped. Opaque stores only, so a frame with it up
 * costs about what the milestones page does. */
static void rect_fill(ctx_t *c, int x, int y, int w, int h, uint32_t rgb) {
    src_t f = src_color(theme_ui_color(rgb), 1.0f);
    for (int yy = y; yy < y + h; yy++) span(c, x, x + w - 1, yy, &f, 255);
}
static void rect_edge(ctx_t *c, int x, int y, int w, int h, uint32_t rgb) {
    src_t e = src_color(theme_ui_color(rgb), 1.0f);
    span(c, x, x + w - 1, y, &e, 255); span(c, x, x + w - 1, y + h - 1, &e, 255);
    for (int yy = y + 1; yy < y + h - 1; yy++) { px(c, x, yy, e.v); px(c, x + w - 1, yy, e.v); }
}
static void round_fill(ctx_t *c, int x, int y, int w, int h, int r, uint32_t rgb) {
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    src_t color = src_color(rgb, 1.0f);
    for (int row = 0; row < h; row++) {
        int inset = 0, dy = row < r ? r - row - 1 : row >= h-r ? row - (h-r) : -1;
        if (dy >= 0) inset = r - (int)sqrtf((float)(r*r - dy*dy));
        span(c, x + inset, x + w - 1 - inset, y + row, &color, 255);
    }
}
static void button(ctx_t *c, int x, int y, int W, int H, uint32_t fill, uint32_t edge, const char *label, int scale) {
    if (theme_active()) {
        const theme_palette_t *p = theme_palette(theme_active());
        uint32_t fg = theme_ui_color(fill), border = theme_ui_color(edge);
        bool club=theme_active()==THEME_TIDEPOOL_CLUB;
        int r=club?(H<40?H/2:14):7;
        if(!club && fg==p->panel)fg=theme_active()==THEME_BLACKWATER?0x143040:0x214d4e;
        round_fill(c,x,y,W,H,r,club?p->accent:border);
        round_fill(c,x+(club?2:1),y+1,W-(club?4:2),H-(club?4:2),r-1,fg);
        if(!club) {
            src_t glint=src_color(mix(fg,0xb1c9b9,.18f),1);
            span(c,x+r,x+W-r-1,y+1,&glint,255);
        }
        draw_text(c, x + (W - text_w(label, scale)) / 2, y + (H - 7 * scale) / 2,
                  scale, fg == p->accent ? p->on_accent : p->text, label);
        return;
    }
    rect_fill(c, x, y, W, H, fill);
    rect_edge(c, x, y, W, H, edge); rect_edge(c, x + 1, y + 1, W - 2, H - 2, edge);
    draw_text(c, x + (W - text_w(label, scale)) / 2, y + (H - 7 * scale) / 2, scale, 0xffffff, label);
}
/* the same primitives for panels built elsewhere (render.h) */
int  render_text_w(const char *s, int scale) { return text_w(s, scale); }
void render_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s) {
    ctx_t c = ctx_page(fb, stride); draw_text(&c, x, y, scale, rgb, s);
}
void render_rect(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb) {
    ctx_t c = ctx_page(fb, stride); rect_fill(&c, x, y, w, h, rgb);
}
void render_rect_edge(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb) {
    ctx_t c = ctx_page(fb, stride); rect_edge(&c, x, y, w, h, rgb);
}
void render_rect_blend(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb, int alpha) {
    ctx_t c = ctx_page(fb, stride); src_t s = src_color(theme_ui_color(rgb), 1.0f);
    for (int yy = y; yy < y + h; yy++) span(&c, x, x + w - 1, yy, &s, alpha);
}
void render_ring(uint16_t *fb, int stride, float cx, float cy, float r, uint32_t rgb) {
    ctx_t c = ctx_page(fb, stride); ring(&c, cx, cy, r, rgb);
}
void render_button(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t fill, uint32_t edge, const char *label, int scale) {
    ctx_t c = ctx_page(fb, stride); button(&c, x, y, w, h, fill, edge, label, scale);
}
void render_glyph(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const uint8_t *rows) {
    ctx_t c = ctx_page(fb, stride); src_t col = src_color(theme_ui_color(rgb), 1.0f);
    for (int r = 0; r < 7; r++)
        for (int k = 0; k < 5; k++)
            if (rows[r] & (0x10 >> k))
                for (int yy = 0; yy < scale; yy++)
                    span(&c, x + k * scale, x + k * scale + scale - 1, y + r * scale + yy, &col, 255);
}
/* a creature of any species, side-on and calm (2026-10-05): the preview
   holds its live state still - the jet's phase from the clock (an eel's
   wave, a seahorse's fan), the lure half lit, no puff, no ink */
void render_creature_preview(uint16_t *fb, int stride, float x, float y, float size, int species, int variant,
                             uint32_t body, uint32_t fin, uint32_t accent, float clock) {
    ctx_t c = ctx_page(fb, stride);
    fish_t f; memset(&f, 0, sizeof f);
    f.x = x; f.y = y; f.heading = 0; f.size = size; f.speed = 40;
    f.stage = STAGE_ADULT;                       /* grown: the crest shows the fin colour */
    f.hunger = 4; f.stress = 0; f.goal.id = GOAL_EXPLORE;
    f.color = body; f.fin = fin; f.accent = accent;
    f.species = (uint8_t)(species > 0 && species < SP_COUNT ? species : SP_FISH); f.variant = (uint8_t)(variant & (SP_VARIANTS - 1));
    if (f.species != SP_FISH) { f.speed = f.species == SP_SHARK ? 30 : 8; f.jet = f.species == SP_LOBSTER ? 0 : clock * 1.2f - floorf(clock * 1.2f); f.lure = 0.5f; f.anchor = -1; }   /* (a lobster's jet is its tail-flip) */
    else { float b = clock * (10.2f / TAU); f.jet = b - floorf(b); }   /* the tail beat at speed 40 */
    tank_fish_face(&f);                          /* side-on, facing right */
    draw_fish_core(&c, &f, clock, false, size);
}
void render_fish_preview(uint16_t *fb, int stride, float x, float y, float size,
                         uint32_t body, uint32_t fin, uint32_t accent, float clock) {
    render_creature_preview(fb, stride, x, y, size, SP_FISH, 0, body, fin, accent, clock);
}
void render_fish_portrait(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, float clock) {
    ctx_t c = ctx_page(fb, stride);
    fish_t f; memset(&f, 0, sizeof f);
    f.x = x; f.y = y; f.heading = 0; f.size = size; f.speed = 40;
    f.stage = who->stage;
    f.hunger = 4; f.stress = 0; f.goal.id = GOAL_EXPLORE;
    f.color = who->color; f.fin = who->fin; f.accent = who->accent;
    f.species = who->species < SP_COUNT ? who->species : SP_FISH; f.variant = who->variant & (SP_VARIANTS - 1);
    if (f.species != SP_FISH) { f.speed = f.species == SP_SHARK ? 30 : 8; f.jet = f.species == SP_LOBSTER ? 0 : clock * 1.2f - floorf(clock * 1.2f); f.lure = 0.5f; f.anchor = -1; }   /* (a lobster's jet is its tail-flip) */
    else { float b = clock * (10.2f / TAU); f.jet = b - floorf(b); }
    tank_fish_face(&f);
    draw_fish_core(&c, &f, clock, false, size);
}
/* how big a species draws (2026-10-05): its half-length at size 1 (a page
   fits a creature by it) and the selection ring's radius round it */
static const float SP_HALF_LEN[SP_COUNT] = { 22, 22, 24, 22, 24, 36, 34, 26, 18, 30, 30, 36, 24 };   /* the swordfish's bill (2026-10-10 night); the turtle (2026-10-11) */
static const float SP_RING_R[SP_COUNT]   = { 17, 19, 20, 17, 19, 28, 28, 19, 16, 24, 31, 29, 21 };
_Static_assert(sizeof SP_HALF_LEN / sizeof SP_HALF_LEN[0] == SP_COUNT && sizeof SP_RING_R / sizeof SP_RING_R[0] == SP_COUNT, "a size row per species");
float render_species_half_len(int species) { return SP_HALF_LEN[species > 0 && species < SP_COUNT ? species : 0]; }
/* a fish of the tank as the pages show it (2026-10-05): its species and
   design, in the colours given (its own, or a page's dim silhouette);
   `fit` shrinks a long creature (an eel, a hammerhead) to a fish's length */
static void fish_preview(uint16_t *fb, int stride, float x, float y, float size, const fish_t *f,
                         uint32_t body, uint32_t fin, uint32_t accent, float clock, bool fit) {
    if (fit) size *= 22 / fmaxf(22, render_species_half_len(f->species));
    render_creature_preview(fb, stride, x, y, size, f->species, f->variant, body, fin, accent, clock);
}
float render_fish_ring_r(const fish_t *f) { return SP_RING_R[f->species < SP_COUNT ? f->species : 0] * f->size; }
void render_confirm_reset(uint16_t *fb, int stride, float frac) {
    ctx_t c = ctx_page(fb, stride);              /* ignores night dimming, like the card */
    const int X = RENDER_CONFIRM_X, Y = RENDER_CONFIRM_Y, W = RENDER_CONFIRM_W, H = RENDER_CONFIRM_H;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, 0x9fd8e2); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    const char *title = "RESET TANK?";
    draw_text(&c, X + (W - text_w(title, 3)) / 2, Y + 20, 3, 0xffffff, title);
    const char *l1 = "START OVER WITH TWO FRY", *l2 = "EVERYTHING ELSE IS LOST";
    draw_text(&c, X + (W - text_w(l1, 2)) / 2, Y + 58, 2, 0x9fd8e2, l1);
    draw_text(&c, X + (W - text_w(l2, 2)) / 2, Y + 78, 2, 0x9fd8e2, l2);
    /* NO is the calm one; YES wears the stress red */
    button(&c, RENDER_CONFIRM_NO_X,  RENDER_CONFIRM_BTN_Y, RENDER_CONFIRM_BTN_W, RENDER_CONFIRM_BTN_H, 0x1c2f36, 0x9fd8e2, "NO", 3);
    button(&c, RENDER_CONFIRM_YES_X, RENDER_CONFIRM_BTN_Y, RENDER_CONFIRM_BTN_W, RENDER_CONFIRM_BTN_H, 0x7a2028, 0xf25b65, "YES", 3);
    /* the prompt lets itself go: a bar draining toward the timeout */
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    const int bw = W - 48, bx = X + 24, by = Y + H - 16;
    rect_fill(&c, bx, by, bw, 3, 0x2a3f45);
    int fw = (int)(bw * frac + 0.5f);
    if (fw > 0) rect_fill(&c, bx, by, fw, 3, 0x9fd8e2);
}
int render_confirm_hit(float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    const int m = 10;                                   /* a fingertip's slop around each button */
    if (y < RENDER_CONFIRM_BTN_Y - m || y >= RENDER_CONFIRM_BTN_Y + RENDER_CONFIRM_BTN_H + m) return 0;
    if (x >= RENDER_CONFIRM_NO_X - m  && x < RENDER_CONFIRM_NO_X  + RENDER_CONFIRM_BTN_W + m) return -1;
    if (x >= RENDER_CONFIRM_YES_X - m && x < RENDER_CONFIRM_YES_X + RENDER_CONFIRM_BTN_W + m) return 1;
    return 0;
}

/* ---- the battery page (2026-09-24): a tap on the pill. The snail card's
 * dress, centered over the live tank: the battery large with its percent,
 * the state in words (the one thing no color has to carry), the estimate,
 * then two or three rows - since the cable moved, the screen-on time on
 * this charge, what a full charge lasts - and the cell voltage, dim, for
 * the curious. Rows the clock cannot answer are left out. ---- */
void render_battery_info(uint16_t *fb, int stride, const bat_info_t *bi, float clock) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    bool pw = BAT_ON_POWER(bi->state);
    char lab[3][16], val[3][32], d[16]; int nr = 0;
    if (bi->since_min >= 0) {
        snprintf(lab[nr], sizeof lab[nr], pw ? "PLUGGED IN" : "UNPLUGGED");
        if (bi->since_min < 1) snprintf(val[nr], sizeof val[nr], "JUST NOW");
        else { battery_fmt_dur(d, sizeof d, bi->since_min); snprintf(val[nr], sizeof val[nr], "%s AGO", d); }
        nr++;
    }
    if (!pw && bi->awake_min >= 0) {
        snprintf(lab[nr], sizeof lab[nr], "SCREEN ON");
        battery_fmt_dur(val[nr], sizeof val[nr], bi->awake_min); nr++;
    }
    if (bi->life_min > 0) {
        snprintf(lab[nr], sizeof lab[nr], "BATTERY LIFE");
        battery_fmt_dur(d, sizeof d, bi->life_min); snprintf(val[nr], sizeof val[nr], "~%s", d); nr++;
    }
    const int W = 336, H = (bi->mv > 0 ? 170 : 154) + nr * 26, X = (TANK_W - W) / 2, Y = (TANK_H - H) / 2;
    src_t bg = src_color(theme_ui_color(0x04141a), 1.0f);
    for (int y = Y; y < Y + H; y++) span(&c, X, X + W - 1, y, &bg, 240);
    rect_edge(&c, X, Y, W, H, 0x9fd8e2); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    /* the battery, its percent beside it */
    char pct[16]; snprintf(pct, sizeof pct, "%d%%", bi->pct < 0 ? 0 : bi->pct > 100 ? 100 : bi->pct);
    const int BW = 84, BH = 36, TS = 5;
    int bolt = pw ? bolt_room(BH) : 0;
    int gw = bolt + BW + 6 + 20 + text_w(pct, TS), gx = X + (W - gw) / 2, gy = Y + 22;
    draw_battery(&c, gx + bolt, gy, BW, BH, 2, bi->pct / 100.0f, bi->state, clock);
    draw_text(&c, gx + bolt + BW + 6 + 20, gy + (BH - 7 * TS) / 2, TS, 0xffffff, pct);
    /* the state, then what it means in time */
    const char *st = bi->state == BAT_CHARGING ? "CHARGING" : bi->state == BAT_FULL ? "FULLY CHARGED"
                   : bi->state == BAT_PLUGGED ? "PLUGGED IN" : "ON BATTERY";
    draw_text(&c, X + (W - text_w(st, 3)) / 2, Y + 78, 3, 0xffffff, st);
    char est[48];
    if (bi->state == BAT_CHARGING) { battery_fmt_dur(d, sizeof d, bi->left_min); snprintf(est, sizeof est, "FULL IN ABOUT %s", d); }
    else if (bi->state == BAT_FULL) snprintf(est, sizeof est, "READY TO UNPLUG");
    else if (bi->state == BAT_PLUGGED) snprintf(est, sizeof est, "NOT CHARGING RIGHT NOW");
    else if (bi->left_min < 5) snprintf(est, sizeof est, "ALMOST EMPTY");
    else { battery_fmt_dur(d, sizeof d, bi->left_min); snprintf(est, sizeof est, "ABOUT %s LEFT", d); }
    draw_text(&c, X + (W - text_w(est, 2)) / 2, Y + 110, 2, 0x9fd8e2, est);
    rect_fill(&c, X + 20, Y + 138, W - 40, 1, 0x1c2f36);
    for (int i = 0; i < nr; i++) {
        int ry = Y + 152 + i * 26;
        draw_text(&c, X + 20, ry, 2, 0x9fd8e2, lab[i]);
        draw_text(&c, X + W - 20 - text_w(val[i], 2), ry, 2, 0xffffff, val[i]);
    }
    if (bi->mv > 0) {
        char v[32]; snprintf(v, sizeof v, "%d.%02d V", bi->mv / 1000, bi->mv % 1000 / 10);
        draw_text_8px(&c, X + (W - (int)strlen(v) * 6 + 1) / 2, Y + H - 20, 0x5f7f86, v);
    }
}

/* ---- milestones page (2026-09-13 redesign: an achievement wall) ----
 * Rows of 40 px: the fish (render_fish_preview at its real size, so growth
 * shows), its name, a growth strip, then six 32 px badges at a 40 px pitch.
 * The tank row repeats the shape with the population strip. A locked badge
 * is the art as a flat grey silhouette (blit_icon_locked); a badge earned
 * since the keeper last closed the page wears a two-tone ring. Tapping a
 * badge / a name / a strip opens a small detail modal in the reset prompt's
 * dress (the art at 2x, a title, the words); the next tap anywhere closes
 * it back to the page. Render-local state, cleared by render_milestones_leave. */
/* The BOWL (TANK_ROUND; Strato on the glass, 2026-10-01: the top fish's
 * portrait and its last badge were cut by the circle - "nudge this down",
 * SETTINGS "to the top, dead center as it would fit in that opening",
 * UPGRADES and CLOSE "closer together centered"): the rows start where the
 * circle is wide enough for them and stand 34 px apart. (SETTINGS and
 * UPGRADES left the page for the main menu, 2026-10-21; CLOSE stays put.) */
/* (the page's numbers, MSP_*: render.h) */
#define MSP_INK       0x031015
#define MSP_DIM       0x2a3f45
#define MSP_TEAL      0x9fd8e2

typedef struct { uint32_t bit; const icon_t *icon; } badge_t;
static const badge_t FISH_BADGES[6] = {
    { MS_FIRST_MEAL_FROM_YOU, &icon_ms_first_meal },  { MS_FIRST_HOLD_APPROACH, &icon_ms_first_hold_approach },
    { MS_FIRST_GRASS, &icon_ms_first_seagrass },      { MS_FIRST_BUBBLES, &icon_ms_first_bubbles },   /* (the third was "first reef" until 0.3.0) */
    { MS_FIRST_FOLLOW, &icon_ms_first_follow },       { MS_FIRST_DART, &icon_ms_first_dart },
};
#define TANK_BADGE_N 8
static const badge_t TANK_BADGES[TANK_BADGE_N] = {
    { TMS_FIRST_FEEDING, &icon_ms_first_feeding },    { TMS_FIRST_TRIM, &icon_ms_first_trimming },
    { TMS_FIRST_CLEANING, &icon_ms_first_glass_cleaning }, { TMS_FIRST_FULL_NIGHT, &icon_ms_first_quiet_night },
    { TMS_FIRST_PLAY_SESSION, &icon_ms_first_play_session }, { TMS_CHANGED_SOMEONE, &icon_ms_tank_changed_someone },
    { TMS_FULL_SCHOOL, &icon_ms_full_school },        /* 2026-09-30: only once the shrimp are bought (tank_badge_shown) */
    { TMS_FIRST_REEF, &icon_ms_first_reef },          /* 2026-10-02: only once the reef cluster is bought */
};
/* the tank row's pages (2026-09-30, the seventh badge): six badges a page;
   with more, a small arrow at the row's right end (or a sideways swipe along
   the row) flips to the next page, pointing back from the last. */
static int g_ms_tpage;                  /* the tank row's page (kept while a modal opens and closes; 0 on leaving the page) */
/* which tank badges show: a bought thing's badge only once the thing is
   bought (the shrimp school's, the reef cluster's) - and, once earned, for
   good, whatever was sold since */
static bool tank_badge_shown(const tank_t *t, int i) {
    uint32_t bit = TANK_BADGES[i].bit;
    if (bit == TMS_FULL_SCHOOL) return (t->sd_unlocks & SD_ITEM_SHRIMP)  || (t->tank_ms_bits & bit);
    if (bit == TMS_FIRST_REEF)  return (t->sd_unlocks & SD_ITEM_CLUSTER) || (t->tank_ms_bits & bit);
    return true;
}
static int tank_badge_n(const tank_t *t) {
    int n = 0;
    for (int i = 0; i < TANK_BADGE_N; i++) n += tank_badge_shown(t, i);
    return n;
}
/* the k-th badge that shows (k < tank_badge_n) */
static const badge_t *tank_badge(const tank_t *t, int k) {
    for (int i = 0; i < TANK_BADGE_N; i++)
        if (tank_badge_shown(t, i) && k-- == 0) return &TANK_BADGES[i];
    return &TANK_BADGES[0];
}
static int tank_pages(const tank_t *t) { return (tank_badge_n(t) + MSP_PER_ROW - 1) / MSP_PER_ROW; }
/* the fish rows' pages (2026-10-05, the species' ten places): MSP_FISH_ROWS
   rows a page - the fish, then the NEW FRY row while one can still come.
   With more, a chevron in the tank row's arrow column, beside the rows (or
   a sideways swipe across them), turns the page: toward the next, back from
   the last, a pip per page under it. A modal opened on a row (or stepped to
   one by its arrows) brings that row's page along. */
static int g_ms_fpage;
static int fish_rows(const tank_t *t, int nreq) { return t->n_fish + (nreq > 0); }
static int fish_pages(const tank_t *t, int nreq) { int r = fish_rows(t, nreq); return r > MSP_FISH_ROWS ? (r + MSP_FISH_ROWS - 1) / MSP_FISH_ROWS : 1; }
#define MSP_ROW_SP_MAX 1.15f   /* a creature of a species in its row: no bigger than this (the big ones are tank-scaled up to ~2) */
#define MSP_FPG_Y (MSP_ROW_Y0 + (MSP_FISH_ROWS * MSP_ROW_H - MSP_ICON) / 2)   /* the pager's top: the rows' middle */
static const char *const STAGE_WORDS[4] = { "FRY", "JUVENILE", "ADULT", "ELDER" };
/* a fish's stage line: "ADULT", or for a creature of a species "ADULT SEAHORSE" */
static void stage_line(char *out, size_t n, const fish_t *f) {
    if (f->species > SP_FISH && f->species < SP_COUNT) snprintf(out, n, "%s %s", STAGE_WORDS[f->stage & 3], SPECIES[f->species].name);
    else snprintf(out, n, "%s", STAGE_WORDS[f->stage & 3]);
}
/* the detail modal (a tap on a badge / name / strip): what to show until
 * the next tap. caption[0] == 0 means no modal. */
static char g_ms_caption[72], g_ms_title[16];
static char g_ms_caption2[32];       /* the caption's second line (the fry checklist's sentences), or empty */
static char g_ms_sub[32];            /* a line under the caption (the fry checklist's progress), or empty */
static bool g_ms_lit;
static const icon_t *g_ms_icon;      /* the badge's art, or NULL */
static int  g_ms_fish = -1;          /* a fish's own sprite instead, or -1 */
static bool g_ms_fry;                /* the fry-to-be (a silhouette) instead */
static int  g_ms_kind = -1;          /* a gate's modal: its kind (the HOW? button shows), or -1 */
static bool g_ms_tip;                /* the gate's tip page is up instead of its modal */
/* where the modal came from (2026-09-16, Strato: arrows at the top corners
   to cycle without dropping back to the page): the row (a fish's index, or
   the TANK / NEW FRY row) and the column (-1 = the name / strip, else the
   badge or gate). ms_step moves along the group the modal belongs to: a
   fish's six badges, the tank's six, the fry's gates, or - from a fish's
   name - the fish themselves. A group of one (TANK's tally, NEW FRY's)
   shows no arrows. */
static int  g_ms_row = -1, g_ms_k = -1;
static bool g_ms_tankrow, g_ms_fryrow;
/* a fish's CARD (the modal behind its name) carries two buttons at its foot
   (2026-10-01, Strato): RENAME - the letter wheel over the live tank - and
   SELL. SELL is two taps on the placement page's idiom: the first arms it
   (the label turns to the price and OK?, the line above says TAP AGAIN), the
   second sells - but not within MSP_SELL_WAIT_S of the first, so a doubled
   tap cannot sell a fish. A fish the tank will not part with (the last pair,
   or a fry still owed its welcome) shows SELL dim, and the line above stays
   empty until the dim button is tapped - then it gives the reason (Strato,
   2026-10-01: the reason "only if the gray sell button is tapped"). The card stands where a gate's modal does (the taller one), inside
   the box every board's glass shows whole: no per-board layout. */
#define MSP_CARD_BTN_W   116
#define MSP_CARD_BTN_GAP 24
#define MSP_SELL_WAIT_S  0.6f
static bool  g_ms_sell_armed;
static bool  g_ms_sell_asked;        /* the dim SELL was tapped: the line says why it stays */
static float g_ms_sell_clock;        /* the tank clock when SELL was armed */

/* the TANK tally's picture (2026-10-02, Strato: the modal "feels bare"): the
 * school itself across the top of the box - every fish in its own colours,
 * at its own size and stage, in the order they arrived - then a dim
 * silhouette for each place still open. The count is in the words under it;
 * here it is seen (lit against dim, never by hue alone). */
#define MSP_SCHOOL_PITCH 52               /* up to six places; ten share the box's width (2026-10-05) */
#define MSP_SCHOOL_SCALE 0.74f
#define MSP_SCHOOL_MAX   1.02f            /* the size that still fits its place (a big elder) */
static void ms_school(const tank_t *t, uint16_t *fb, int stride, int X, int Y, int W) {
    /* past ten places (2026-10-07, the 25): two rows, the places shared between them */
    const int rows = POP_CAP > 10 ? 2 : 1, per = (POP_CAP + rows - 1) / rows;
    int pitch = per * MSP_SCHOOL_PITCH <= W - 16 ? MSP_SCHOOL_PITCH : (W - 16) / per;
    float k = pitch < MSP_SCHOOL_PITCH ? 1.3f * pitch / MSP_SCHOOL_PITCH : 1.0f;   /* the places shrink together */
    if (k > 1.0f) k = 1.0f;
    if (rows > 1) k *= 0.85f;                /* (two rows: a 36 px-long eel must fit a 24 px pitch, with the stagger below) */
    /* more than six: every other one a little higher and lower, so a long eel
       may reach past its place without touching its neighbour */
    for (int i = 0; i < POP_CAP; i++) {
        int row = i / per, col = i % per, n_row = i / per == rows - 1 ? POP_CAP - row * per : per;
        float cx = X + (W - n_row * pitch) / 2 + pitch / 2 + 4 * k + col * pitch;   /* (+4: a fish's centre sits ahead of its middle) */
        float cy = rows == 1 ? Y + 50 + (pitch < MSP_SCHOOL_PITCH ? (i & 1 ? 9 : -9) : 0) : Y + 34 + row * 30 + (col & 1 ? 5 : -5);
        if (i < t->n_fish) {
            float sz = t->fish[i].size * MSP_SCHOOL_SCALE * k, mx = MSP_SCHOOL_MAX * k;
            render_fish_portrait(fb, stride, cx, cy, sz > mx ? mx : sz, &t->fish[i], t->clock + i * 0.9f);
        } else render_fish_preview(fb, stride, cx, cy, 0.9f * MSP_SCHOOL_SCALE * k, MSP_DIM, MSP_DIM, MSP_DIM, t->clock + i * 0.9f);
    }
}

/* a locked badge: the same art as a flat grey silhouette - luminance keeps
 * the shapes readable, the low alpha keeps it quiet on the ink */
static uint32_t locked_rgb(uint16_t v) {
    if (theme_active()) return theme_palette(theme_active())->muted;
    int r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
    int l = (r * 77 + g * 151 + b * 28) >> 8;                /* 0..255 luminance */
    int k = 140 + l * 115 / 255;                              /* 0.55 .. 1.0, in 1/255 */
    return (uint32_t)(20 + (0x2a * k >> 8)) << 16 | (uint32_t)(20 + (0x3f * k >> 8)) << 8
         | (uint32_t)(20 + (0x45 * k >> 8));
}
static void blit_icon_locked(ctx_t *c, int x, int y, const icon_t *ic) {
    ic = theme_icon(ic);
    for (int j = 0; j < ic->h; j++)
        for (int i = 0; i < ic->w; i++) {
            int a = ic->a[j * ic->w + i];
            if (a) px_blend(c, x + i, y + j, locked_rgb(ic->rgb[j * ic->w + i]), a * 180 >> 8);
        }
}
/* the same art at an integer scale (the detail modal), lit or as the silhouette */
static void blit_icon_scaled(ctx_t *c, int x, int y, const icon_t *ic, int s, bool lit) {
    ic = theme_icon(ic);
    for (int j = 0; j < ic->h; j++)
        for (int i = 0; i < ic->w; i++) {
            int a = ic->a[j * ic->w + i];
            if (!a) continue;
            uint16_t v = ic->rgb[j * ic->w + i];
            for (int yy = 0; yy < s; yy++)
                for (int xx = 0; xx < s; xx++) {
                    if (lit) px565_blend(c, x + i * s + xx, y + j * s + yy, v, a);
                    else     px_blend(c, x + i * s + xx, y + j * s + yy, locked_rgb(v), a * 180 >> 8);
                }
        }
}
/* a tiny fish glyph facing right (the growth and population strips) */
static void fish_glyph(ctx_t *c, float cx, float cy, float r, uint32_t rgb) {
    float xs[3] = { cx - r * 1.2f, cx - r * 2.3f, cx - r * 2.3f }, ys[3] = { cy, cy - r * 0.9f, cy + r * 0.9f };
    fill_poly(c, xs, ys, 3, rgb);
    fill_ellipse(c, cx, cy, r * 1.6f, r, rgb, 255);
}
static void badge(ctx_t *c, int x, int y, const icon_t *ic, bool on, bool fresh) {
    if (on) blit_icon(c, x, y, ic, 255); else blit_icon_locked(c, x, y, ic);
    if (on && fresh) {
        rect_edge(c, x - 3, y - 3, MSP_ICON + 6, MSP_ICON + 6, MSP_TEAL);
        rect_edge(c, x - 4, y - 4, MSP_ICON + 8, MSP_ICON + 8, 0x3f6a72);
    }
}
static int bit_index(uint32_t bit) { int i = 0; while (bit > 1u) { bit >>= 1; i++; } return i; }
/* the art for a gate of the fry checklist: the card's trust icon, the tank's
   own badges for meals / a hold / grass / a change / a wiped glass, or NULL = the youngest
   fish itself (the GROW gate) */
static const icon_t *fry_req_icon(int kind) {
    switch (kind) {
    case FRY_REQ_TRUST:  return &icon_trust;
    case FRY_REQ_FEED:   return &icon_ms_first_feeding;
    case FRY_REQ_HOLD:   return &icon_ms_first_hold_approach;
    case FRY_REQ_CHANGE: return &icon_ms_tank_changed_someone;
    case FRY_REQ_GRASS:  return &icon_ms_first_trimming;
    case FRY_REQ_GLASS:  return &icon_ms_first_glass_cleaning;
    default:             return NULL;
    }
}
/* a gate as a badge in a 32 px cell: lit when met, the silhouette until then */
static void fry_badge(ctx_t *c, uint16_t *fb, int stride, const tank_t *t, int x, int y, const fry_req_t *r) {
    const icon_t *ic = fry_req_icon(r->kind);
    if (ic) {
        int off = (MSP_ICON - ic->w) / 2;        /* the 24 px card icon sits centred */
        if (r->met) blit_icon(c, x + off, y + off, ic, 255); else blit_icon_locked(c, x + off, y + off, ic);
    } else {                                     /* GROW: the youngest fish, its own colours once grown */
        const fish_t *f = &t->fish[t->n_fish - 1];
        float sz = f->size > 0.7f ? 0.7f : f->size;
        if (r->met) fish_preview(fb, stride, x + MSP_ICON / 2, y + MSP_ICON / 2, sz, f, f->color, f->fin, f->accent, t->clock, true);
        else        fish_preview(fb, stride, x + MSP_ICON / 2, y + MSP_ICON / 2, sz, f, MSP_DIM, MSP_DIM, MSP_DIM, t->clock, true);
    }
}

/* a chevron pointing left or right, its tip at (tx, cy): 3 px steps, 24 px tall */
static void ms_chevron(ctx_t *c, int tx, int cy, bool left, uint32_t rgb) {
    for (int i = 0; i < 4; i++) {
        int xx = left ? tx + i * 3 : tx - 3 - i * 3;
        rect_fill(c, xx, cy - 3 - i * 3, 3, 3, rgb);
        rect_fill(c, xx, cy + i * 3, 3, 3, rgb);
    }
}
/* the modal's group: how many things the arrows cycle through and where
   this one sits. nreq = the fry checklist's gate count (the caller has it). */
static int ms_group(const tank_t *t, int nreq, int *idx) {
    if (g_ms_fryrow)  { *idx = g_ms_k; return g_ms_k < 0 ? 1 : nreq; }
    if (g_ms_tankrow) { *idx = g_ms_k; return g_ms_k < 0 ? 1 : tank_badge_n(t); }
    if (g_ms_k < 0)   { *idx = g_ms_row; return t->n_fish; }    /* a fish's name: the fish */
    *idx = g_ms_k; return 6;
}
static int ms_open(const tank_t *t, int row, bool tank_row, bool fry_row, int k,
                   const fry_req_t *req, int nreq, bool staged);
static void ms_close(void);
/* the modal's box: a gate's and a fish's card are taller (a button row at the
   foot) and stand higher, clear of the CLOSE button */
static bool ms_card(const tank_t *t) { return g_ms_caption[0] && !g_ms_tip && !g_ms_fryrow && !g_ms_tankrow && g_ms_k < 0 && g_ms_row >= 0 && g_ms_row < t->n_fish; }
static int  ms_modal_y(const tank_t *t) { return g_ms_kind >= 0 || ms_card(t) ? MSP_FRY_MODAL_Y : MSP_MODAL_Y; }
static int  ms_modal_h(const tank_t *t) {
    return MSP_MODAL_H + (g_ms_caption2[0] ? 20 : 0) + (g_ms_sub[0] || ms_card(t) ? 24 : 0) + (g_ms_kind >= 0 || ms_card(t) ? MSP_HOW_H + 14 : 0);
}
static int  ms_card_btn_y(const tank_t *t) { return ms_modal_y(t) + ms_modal_h(t) - 10 - MSP_HOW_H; }
static int  ms_card_btn_x(int which) {            /* 0 = RENAME, 1 = SELL: the pair centred */
    return MSP_MODAL_X + (MSP_MODAL_W - 2 * MSP_CARD_BTN_W - MSP_CARD_BTN_GAP) / 2 + which * (MSP_CARD_BTN_W + MSP_CARD_BTN_GAP);
}
/* the arrows: the previous / next thing in the modal's group, wrapping */
static void ms_step(const tank_t *t, int dir) {
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    int idx, n = ms_group(t, nreq, &idx);
    if (n < 2) return;
    g_ms_sell_armed = false; g_ms_sell_asked = false;
    idx = (idx + dir + n) % n;
    if (!g_ms_fryrow && !g_ms_tankrow && g_ms_k < 0) ms_open(t, idx, false, false, -1, req, nreq, staged);
    else ms_open(t, g_ms_row, g_ms_tankrow, g_ms_fryrow, idx, req, nreq, staged);
}

void render_milestones(const tank_t *t, uint16_t *fb, int stride) {
    render_use_theme(t->theme);
    ctx_t c = ctx_page(fb, stride);
    for (int y = 0; y < TANK_H; y++)
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = rgb565(theme_ui_color(MSP_INK), 1);
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    int fpages = fish_pages(t, nreq);
    if (g_ms_fpage >= fpages) g_ms_fpage = fpages - 1;           /* (a sale took the last page's rows) */
    const int r0 = g_ms_fpage * MSP_FISH_ROWS;                  /* this page's first row */
    for (int i = r0; i < t->n_fish && i < r0 + MSP_FISH_ROWS; i++) {
        const fish_t *f = &t->fish[i];
        int top = MSP_ROW_Y0 + (i - r0) * MSP_ROW_H;
        if(theme_active()) {
            const theme_palette_t *p=theme_palette(theme_active());
            if(theme_active()==THEME_TIDEPOOL_CLUB)round_fill(&c,38,top-2,PAGE_W-76,MSP_ROW_H-2,10,p->panel);
            else rect_fill(&c,88,top+MSP_ROW_H-3,PAGE_W-128,1,p->border);
        }
        float rs = f->species && f->size > MSP_ROW_SP_MAX ? MSP_ROW_SP_MAX : f->size;   /* (a grown hammerhead or eel would leave its row) */
        fish_preview(fb, stride, MSP_FISH_X, top + MSP_ROW_MID, rs, f, f->color, f->fin, f->accent, t->clock, false);
        draw_text(&c, 92, top + 2, 2, 0xffffff, f->name);
        static const int GX[4] = { 96, 111, 129, 151 };   /* each glyph's own pitch: a 4 px gap as they grow */
        for (int s = 0; s < 4; s++)          /* growth strip: fry -> elder, lit up to the stage reached */
            fish_glyph(&c, GX[s], top + MSP_ROW_STRIP, 2.2f + s * 0.9f, s <= (int)f->stage ? f->accent : MSP_DIM);
        for (int k = 0; k < 6; k++) {
            uint32_t bit = FISH_BADGES[k].bit;
            badge(&c, MSP_BADGE_X0 + k * MSP_BADGE_DX, top + MSP_ROW_BADGE, FISH_BADGES[k].icon,
                  (f->ms_bits & bit) != 0, (f->ms_seen & bit) == 0);
        }
    }
    /* the NEW FRY row (2026-09-14): under the last fish while the tank can
       still grow - what the next arrival needs, as badges that light up when
       met (the fry-to-be a silhouette at the left, a tick per gate under
       its name, a filling bar under each badge still owed) */
    if (nreq > 0 && t->n_fish >= r0 && t->n_fish < r0 + MSP_FISH_ROWS) {
        int top = MSP_ROW_Y0 + (t->n_fish - r0) * MSP_ROW_H;
        uint32_t fry_rgb = staged ? MSP_TEAL : MSP_DIM;
        render_fish_preview(fb, stride, MSP_FISH_X, top + MSP_ROW_MID, 0.55f, fry_rgb, fry_rgb, fry_rgb, t->clock);
        draw_text(&c, 92, top + 2, 2, MSP_TEAL, "NEW FRY");
        for (int k = 0; k < nreq; k++)           /* one tick per gate, lit when met */
            rect_fill(&c, 96 + k * 14, top + MSP_ROW_STRIP - 3, 10, 6, req[k].met ? MSP_TEAL : MSP_DIM);
        for (int k = 0; k < nreq; k++) {
            int x = MSP_BADGE_X0 + k * MSP_BADGE_DX;
            fry_badge(&c, fb, stride, t, x, top + MSP_ROW_BADGE, &req[k]);
            if (!req[k].met) {                   /* the bar: how far along */
                int w = (int)(MSP_ICON * req[k].frac + 0.5f);
                rect_fill(&c, x, top + MSP_ROW_BAR, MSP_ICON, 2, MSP_DIM);
                if (w > 0) rect_fill(&c, x, top + MSP_ROW_BAR, w, 2, MSP_TEAL);
            }
        }
    }
    /* the tank's row: the sand dollar at the left (the fish rows' portrait
       slot) opens the shop (2026-09-15) */
    for (int x = 24; x < PAGE_W - 24; x++) px_blend(&c, x, MSP_TANK_Y - 4, MSP_DIM, 200);
    blit_icon(&c, MSP_SD_X, MSP_TANK_Y, &icon_ms_sand_dollar, 255);
    {   /* the balance under the coin (Strato, 2026-09-15), centred on it - the
           only room: the divider and the last row sit above, 12 px to the left */
        char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
        draw_text(&c, MSP_SD_X + (MSP_ICON - text_w(bal, 2)) / 2, MSP_TANK_Y + 34, 2, 0xffffff, bal);
    }
    draw_text(&c, 92, MSP_TANK_Y + 2, 2, MSP_TEAL, "TANK");
    if (POP_CAP <= 6)                        /* population strip: who is here, who could still arrive */
        for (int k = 0; k < POP_CAP; k++) fish_glyph(&c, 96 + k * 14, MSP_TANK_Y + 30, 2.8f, k < t->n_fish ? MSP_TEAL : MSP_DIM);
    else if (POP_CAP <= 10)                  /* ten (2026-10-05): two rows of five, smaller, clear of the badges */
        for (int k = 0; k < POP_CAP; k++)
            fish_glyph(&c, 96 + (k % 5) * 14, MSP_TANK_Y + 25 + (k / 5) * 11, 2.3f, k < t->n_fish ? MSP_TEAL : MSP_DIM);
    else                                     /* 25 (2026-10-07): three rows of nine, dots, in the same 74 px before the badges */
        for (int k = 0; k < POP_CAP; k++)
            fish_glyph(&c, 96 + (k % 9) * 8, MSP_TANK_Y + 23 + (k / 9) * 8, 1.4f, k < t->n_fish ? MSP_TEAL : MSP_DIM);
    int nb = tank_badge_n(t), np = tank_pages(t);
    if (g_ms_tpage >= np) g_ms_tpage = 0;
    for (int j = 0; j < MSP_PER_ROW; j++) {
        int k = g_ms_tpage * MSP_PER_ROW + j;
        if (k >= nb) break;
        const badge_t *tb = tank_badge(t, k);
        badge(&c, MSP_BADGE_X0 + j * MSP_BADGE_DX, MSP_TANK_Y + 4, tb->icon,
              (t->tank_ms_bits & tb->bit) != 0, (t->tank_ms_seen & tb->bit) == 0);
    }
    if (np > 1) {   /* the page arrow: toward the next page, back from the last; the badges' two-tone
                       ring round it while another page holds a badge not seen yet */
        bool last = g_ms_tpage == np - 1, fresh = false;
        for (int k = 0; k < nb; k++)
            if (k / MSP_PER_ROW != g_ms_tpage && (t->tank_ms_bits & ~t->tank_ms_seen & tank_badge(t, k)->bit)) fresh = true;
        int ax = MSP_TPG_X + 4, ay = MSP_TANK_Y + 4, aw = 16;
        ms_chevron(&c, last ? ax + 2 : ax + aw - 2, ay + MSP_ICON / 2, last, 0xffffff);
        if (fresh) { rect_edge(&c, ax - 3, ay - 3, aw + 6, MSP_ICON + 6, MSP_TEAL);
                     rect_edge(&c, ax - 4, ay - 4, aw + 8, MSP_ICON + 8, 0x3f6a72); }
        for (int p = 0; p < np; p++)        /* a pip per page under it, the current one lit */
            rect_fill(&c, ax + aw / 2 - np * 3 + p * 6 + 1, MSP_TANK_Y + 40, 4, 2, p == g_ms_tpage ? MSP_TEAL : MSP_DIM);
    }
    if (fpages > 1) {   /* the fish rows' pager: the tank row's arrow, beside the rows; the two-tone ring
                           while another page holds a fish badge not seen yet */
        bool last = g_ms_fpage == fpages - 1, fresh = false;
        for (int i = 0; i < t->n_fish; i++)
            for (int k = 0; k < 6; k++)
                if (i / MSP_FISH_ROWS != g_ms_fpage && (t->fish[i].ms_bits & ~t->fish[i].ms_seen & FISH_BADGES[k].bit)) fresh = true;
        int ax = MSP_TPG_X + 4, ay = MSP_FPG_Y, aw = 16;
        ms_chevron(&c, last ? ax + 2 : ax + aw - 2, ay + MSP_ICON / 2, last, 0xffffff);
        if (fresh) { rect_edge(&c, ax - 3, ay - 3, aw + 6, MSP_ICON + 6, MSP_TEAL);
                     rect_edge(&c, ax - 4, ay - 4, aw + 8, MSP_ICON + 8, 0x3f6a72); }
        for (int p = 0; p < fpages; p++)
            rect_fill(&c, ax + aw / 2 - fpages * 3 + p * 6 + 1, ay + MSP_ICON + 6, 4, 2, p == g_ms_fpage ? MSP_TEAL : MSP_DIM);
    }
    /* the way out: a CLOSE button in the prompt's calm dress (a tap anywhere
       else never drops the page - too much to tap for that) */
    button(&c, MSP_CLOSE_X, MSP_CLOSE_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", 2);
    /* (SETTINGS and UPGRADES stood beside it, 2026-09-15 .. 10-21: the main menu has them now) */
    /* a modal up: the page under it is out of reach (any tap only closes the
       modal), so it LOOKS out of reach - every pixel at half (Strato: with
       CLOSE lit it looked like you could still tap it). One shift per
       pixel, so cheap enough for every frame. */
    if (g_ms_caption[0])
        for (int y = 0; y < TANK_H; y++)
            for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = (uint16_t)((fb[y * stride + x] >> 1) & 0x7bef);
    /* the detail modal, in the reset prompt's dress: the art at 2x, a title
       (the fish's name / TANK, or NOT YET), the milestone's words */
    if (g_ms_caption[0] && g_ms_tip) {
        /* the tip page: HOW, then the gate's lines; any tap closes it */
        const int X = MSP_MODAL_X, Y = MSP_FRY_MODAL_Y, W = MSP_MODAL_W;
        const char *const *tip = progression_fry_tip(g_ms_kind);
        int n = 0; while (tip[n]) n++;
        const int H = 56 + n * 20 + 16;
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        char title[24]; snprintf(title, sizeof title, "%s: HOW", g_ms_title);
        draw_text(&c, X + (W - text_w(title, 3)) / 2, Y + 16, 3, 0xffffff, title);
        for (int i = 0; i < n; i++) draw_text(&c, X + (W - text_w(tip[i], 2)) / 2, Y + 56 + i * 20, 2, MSP_TEAL, tip[i]);
    } else if (g_ms_caption[0]) {
        const int X = MSP_MODAL_X, W = MSP_MODAL_W;
        const int Y = ms_modal_y(t), H = ms_modal_h(t);
        const bool card = ms_card(t);
        if (card) {                                /* a fish's card is live: its name and stage as they are now, what it
                                                      would fetch (or why it stays), and the two buttons */
            const fish_t *f = &t->fish[g_ms_row];
            bool can = progression_fish_sellable(t, g_ms_row);
            int worth = progression_fish_value(t, g_ms_row);
            if (!can) g_ms_sell_armed = false; else g_ms_sell_asked = false;
            snprintf(g_ms_title, sizeof g_ms_title, "%s", f->name);
            stage_line(g_ms_caption, sizeof g_ms_caption, f);
            if (g_ms_sell_armed) snprintf(g_ms_sub, sizeof g_ms_sub, "TAP AGAIN TO SELL");
            else if (can) snprintf(g_ms_sub, sizeof g_ms_sub, "WORTH %d SAND DOLLARS", worth);
            else if (g_ms_sell_asked) snprintf(g_ms_sub, sizeof g_ms_sub, t->n_fish <= FISH_KEEP_MIN ? "KEEP AT LEAST TWO FISH" : "NAME THE NEW FRY FIRST");
            else g_ms_sub[0] = 0;                  /* (the line's room stays: the buttons never move) */
        }
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        if (g_ms_kind >= 0)                        /* the way further in: HOW?, centred at the foot (Strato: bottom
                                                      right sat too close to CLOSE for comfort) */
            button(&c, X + (W - MSP_HOW_W) / 2, Y + H - 10 - MSP_HOW_H, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "HOW?", 2);
        if (card) {
            const int by = ms_card_btn_y(t);
            button(&c, ms_card_btn_x(0), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "RENAME", 2);
            if (!progression_fish_sellable(t, g_ms_row)) {           /* not for sale: the button in the page's dim ink, the words too */
                rect_fill(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x04141a);
                rect_edge(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, MSP_DIM);
                draw_text(&c, ms_card_btn_x(1) + (MSP_CARD_BTN_W - text_w("SELL", 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_DIM, "SELL");
            } else if (g_ms_sell_armed) {
                char ok[16]; snprintf(ok, sizeof ok, "+%d OK?", progression_fish_value(t, g_ms_row));
                button(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x155e58, 0x38dcc7, ok, 2);
            } else button(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "SELL", 2);
        }
        if (g_ms_icon) blit_icon_scaled(&c, X + (W - g_ms_icon->w * 2) / 2, Y + 16 + (32 - g_ms_icon->w), g_ms_icon, 2, g_ms_lit);
        else if (g_ms_fish >= 0 && g_ms_fish < t->n_fish) {
            const fish_t *f = &t->fish[g_ms_fish];
            if (g_ms_lit) fish_preview(fb, stride, X + W / 2, Y + 48, f->size * 1.6f, f, f->color, f->fin, f->accent, t->clock, true);
            else          fish_preview(fb, stride, X + W / 2, Y + 48, f->size * 1.6f, f, MSP_DIM, MSP_DIM, MSP_DIM, t->clock, true);
        } else if (g_ms_fry)
            render_fish_preview(fb, stride, X + W / 2, Y + 48, 0.9f, g_ms_lit ? MSP_TEAL : MSP_DIM, g_ms_lit ? MSP_TEAL : MSP_DIM, g_ms_lit ? MSP_TEAL : MSP_DIM, t->clock);
        else if (g_ms_tankrow && g_ms_k < 0) ms_school(t, fb, stride, X, Y, W);   /* TANK's tally: the school */
        draw_text(&c, X + (W - text_w(g_ms_title, 3)) / 2, Y + 92, 3, g_ms_lit ? 0xffffff : MSP_TEAL, g_ms_title);
        draw_text(&c, X + (W - text_w(g_ms_caption, 2)) / 2, Y + 124, 2, g_ms_lit ? MSP_TEAL : 0x5f8a92, g_ms_caption);
        int ly = Y + 144;
        if (g_ms_caption2[0]) { draw_text(&c, X + (W - text_w(g_ms_caption2, 2)) / 2, ly, 2, g_ms_lit ? MSP_TEAL : 0x5f8a92, g_ms_caption2); ly += 20; }
        if (g_ms_sub[0]) draw_text(&c, X + (W - text_w(g_ms_sub, 2)) / 2, ly + 4, 2, g_ms_lit ? 0xffffff : MSP_TEAL, g_ms_sub);
        {   /* the arrows (2026-09-16): the previous / next of the group at the
               top corners, in the buttons' dress, only when there is a group */
            int idx, n = ms_group(t, nreq, &idx);
            if (n > 1) {
                int ax = X + MSP_ARROW_IN, ay = Y + MSP_ARROW_IN, bx = X + W - MSP_ARROW_IN - MSP_ARROW_W;
                rect_fill(&c, ax, ay, MSP_ARROW_W, MSP_ARROW_H, 0x1c2f36);
                rect_edge(&c, ax, ay, MSP_ARROW_W, MSP_ARROW_H, MSP_TEAL); rect_edge(&c, ax + 1, ay + 1, MSP_ARROW_W - 2, MSP_ARROW_H - 2, MSP_TEAL);
                ms_chevron(&c, ax + 14, ay + MSP_ARROW_H / 2, true, 0xffffff);
                rect_fill(&c, bx, ay, MSP_ARROW_W, MSP_ARROW_H, 0x1c2f36);
                rect_edge(&c, bx, ay, MSP_ARROW_W, MSP_ARROW_H, MSP_TEAL); rect_edge(&c, bx + 1, ay + 1, MSP_ARROW_W - 2, MSP_ARROW_H - 2, MSP_TEAL);
                ms_chevron(&c, bx + MSP_ARROW_W - 14, ay + MSP_ARROW_H / 2, false, 0xffffff);
            }
        }
    }
}

int render_milestones_tap(const tank_t *t, float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    if (g_ms_caption[0]) {                       /* a modal is up */
        if (!g_ms_tip) {                         /* the arrows at its top corners: the previous / next of the group */
            fry_req_t req[FRY_REQ_MAX]; bool staged; int idx;
            int n = ms_group(t, progression_next_fry(t, req, &staged), &idx);
            const int Y = ms_modal_y(t);
            if (n > 1 && y >= Y - 12 && y < Y + MSP_ARROW_IN + MSP_ARROW_H + 24) {
                if (x < MSP_MODAL_X + MSP_ARROW_HIT)               { ms_step(t, -1); return MS_TAP_KEPT; }
                if (x >= MSP_MODAL_X + MSP_MODAL_W - MSP_ARROW_HIT) { ms_step(t, +1); return MS_TAP_KEPT; }
            }
        }
        if (ms_card(t)) {                        /* a fish's card: RENAME / SELL across its foot - each button's half of
                                                    the panel, the HOW? button's depth (a miss costs the modal) */
            const int by = ms_card_btn_y(t), fish = g_ms_row;
            if (x >= MSP_MODAL_X && x < MSP_MODAL_X + MSP_MODAL_W && y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN) {
                if (x < MSP_MODAL_X + MSP_MODAL_W / 2) { ms_close(); return MS_TAP_RENAME + fish; }   /* the platform opens the wheel */
                if (!progression_fish_sellable(t, fish)) { g_ms_sell_asked = true; return MS_TAP_KEPT; }   /* the line says why, from now on */
                if (!g_ms_sell_armed) { g_ms_sell_armed = true; g_ms_sell_clock = t->clock; return MS_TAP_KEPT; }
                if (t->clock - g_ms_sell_clock < MSP_SELL_WAIT_S) return MS_TAP_KEPT;                /* a doubled tap: still armed */
                ms_close(); return MS_TAP_SELL + fish;                                               /* the platform sells it */
            }
        }
        if (g_ms_kind >= 0 && !g_ms_tip) {       /* a gate's: the HOW? button opens its tip page */
            const int H = ms_modal_h(t);
            const int bx = MSP_MODAL_X + (MSP_MODAL_W - MSP_HOW_W) / 2, by = MSP_FRY_MODAL_Y + H - 10 - MSP_HOW_H;
            if (x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X && y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN) {
                g_ms_tip = true; return MS_TAP_KEPT; }
        }
        ms_close(); return MS_TAP_KEPT;                  /* any other tap: back to the page */
    }
    if (y >= MSP_CLOSE_Y - 4) return x >= MSP_CLOSE_X - (PAGE_BOWL ? 6 : 8) ? MS_TAP_CLOSE : MS_TAP_NONE;   /* the foot: CLOSE, slop out to the glass edge */
    int row = -1; bool tank_row = false, fry_row = false;
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    if (y >= MSP_ROW_Y0 - 2 && y < MSP_ROW_Y0 + MSP_FISH_ROWS * MSP_ROW_H) {
        int fpages = fish_pages(t, nreq);
        if (fpages > 1 && x >= MSP_TPG_X) {          /* the rows' pager: the next page, wrapping */
            g_ms_fpage = (g_ms_fpage + 1) % fpages; return MS_TAP_KEPT; }
        if (g_ms_fpage >= fpages) g_ms_fpage = fpages - 1;
        row = (int)((y - MSP_ROW_Y0) / MSP_ROW_H);
        if (row < 0) row = 0;
        row += g_ms_fpage * MSP_FISH_ROWS;
        if (row == t->n_fish && nreq > 0) fry_row = true;   /* the NEW FRY row */
        else if (row >= t->n_fish) return MS_TAP_NONE;      /* an empty row */
    } else if (y >= MSP_TANK_Y - 6 && y < MSP_CLOSE_Y - 4) tank_row = true;   /* down to the button strip: fingers near the
                                                                              bottom bezel report LOW */
    else return MS_TAP_NONE;
    if (tank_row && x >= MSP_TPG_X && tank_pages(t) > 1) {   /* the page arrow: the next page, wrapping */
        g_ms_tpage = (g_ms_tpage + 1) % tank_pages(t); return MS_TAP_KEPT; }
    int k;                                           /* badge column, or -1 for the name / strip cluster */
    if (x >= MSP_BADGE_X0 - 4 && x < MSP_BADGE_X0 + 6 * MSP_BADGE_DX) {
        k = (int)((x - MSP_BADGE_X0 + 4) / MSP_BADGE_DX);
        if (k > 5) k = 5;
    } else if (x >= 20 && x < MSP_BADGE_X0 - 4) k = -1;
    else return MS_TAP_NONE;
    if (tank_row && x < 88) return MS_TAP_SHOP;      /* the sand dollar: the shop page */
    if (tank_row && k >= 0) {                        /* this page's badge; an empty cell on the last page is nothing */
        k += g_ms_tpage * MSP_PER_ROW;
        if (k >= tank_badge_n(t)) return MS_TAP_NONE;
    }
    return ms_open(t, row, tank_row, fry_row, k, req, nreq, staged);
}
/* open the detail modal for a row's name / strip (k < 0) or its k-th badge
   or gate: the words, the art, and where it came from (the arrows' group) */
static int ms_open(const tank_t *t, int row, bool tank_row, bool fry_row, int k,
                   const fry_req_t *req, int nreq, bool staged) {
    g_ms_caption2[0] = 0; g_ms_sub[0] = 0; g_ms_fry = false; g_ms_kind = -1; g_ms_tip = false; g_ms_sell_armed = false; g_ms_sell_asked = false;
    g_ms_row = row; g_ms_k = k; g_ms_tankrow = tank_row; g_ms_fryrow = fry_row;
    if (!tank_row && row >= 0) g_ms_fpage = row / MSP_FISH_ROWS;   /* the page follows the modal's row */
    if (fry_row) {
        if (k < 0) {                                 /* the name: the tally, and when it comes */
            int met = 0; for (int i = 0; i < nreq; i++) met += req[i].met;
            snprintf(g_ms_title, sizeof g_ms_title, "NEW FRY");
            if (staged) { snprintf(g_ms_caption, sizeof g_ms_caption, "EVERY STEP IS DONE. A FRY");   /* the spawning, 2026-09-24 */
                          snprintf(g_ms_caption2, sizeof g_ms_caption2, "WILL BE BORN IN THE GRASS");
                          snprintf(g_ms_sub, sizeof g_ms_sub, "ON ITS WAY"); }
            else { snprintf(g_ms_caption, sizeof g_ms_caption, "WHEN ALL NEEDS ARE MET, A");     /* Strato's words, 2026-09-14 */
                   snprintf(g_ms_caption2, sizeof g_ms_caption2, "NEW FRY IS READY TO BE BORN");
                   snprintf(g_ms_sub, sizeof g_ms_sub, "%d OF %d DONE", met, nreq); }
            g_ms_lit = staged; g_ms_icon = NULL; g_ms_fish = -1; g_ms_fry = true;
        } else if (k < nreq) {                       /* a gate: the words, and where it stands */
            const fry_req_t *r = &req[k];
            snprintf(g_ms_title, sizeof g_ms_title, "%s", r->title);
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", r->words);
            snprintf(g_ms_caption2, sizeof g_ms_caption2, "%s", r->words2);
            snprintf(g_ms_sub, sizeof g_ms_sub, "%s", r->progress);
            g_ms_lit = r->met; g_ms_icon = fry_req_icon(r->kind); g_ms_kind = r->kind;
            g_ms_fish = g_ms_icon ? -1 : t->n_fish - 1;
        } else return MS_TAP_NONE;
    } else if (tank_row) {
        if (k < 0) {
            snprintf(g_ms_title, sizeof g_ms_title, "TANK");
            snprintf(g_ms_caption, sizeof g_ms_caption, "%d OF %d FISH SO FAR", t->n_fish, POP_CAP);
            g_ms_lit = true; g_ms_icon = NULL; g_ms_fish = -1;
        } else {
            uint32_t bit = tank_badge(t, k)->bit; bool on = (t->tank_ms_bits & bit) != 0;
            g_ms_tpage = k / MSP_PER_ROW;            /* the arrows can cross pages: the row follows */
            snprintf(g_ms_title, sizeof g_ms_title, on ? "TANK" : "NOT YET");
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", TMS_NAMES[bit_index(bit)]);
            g_ms_lit = on; g_ms_icon = tank_badge(t, k)->icon; g_ms_fish = -1;
        }
    } else {
        const fish_t *f = &t->fish[row];
        if (k < 0) {
            snprintf(g_ms_title, sizeof g_ms_title, "%s", f->name);
            stage_line(g_ms_caption, sizeof g_ms_caption, f);
            g_ms_lit = true; g_ms_icon = NULL; g_ms_fish = row;
        } else {
            uint32_t bit = FISH_BADGES[k].bit; bool on = (f->ms_bits & bit) != 0;
            snprintf(g_ms_title, sizeof g_ms_title, "%s", on ? f->name : "NOT YET");
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", MS_NAMES[bit_index(bit)]);
            g_ms_lit = on; g_ms_icon = FISH_BADGES[k].icon; g_ms_fish = -1;
        }
    }
    return MS_TAP_KEPT;
}
/* a sideways swipe along the TANK row: the next page (leftward) or the
   previous (rightward), no wrap. False = not a page swipe (the platforms then
   treat the release as they did before). */
bool render_milestones_swipe(const tank_t *t, float x, float y, float dx) {
    (void)x; y -= PAGE_Y;
    if (!g_ms_caption[0] && y >= MSP_ROW_Y0 - 2 && y < MSP_ROW_Y0 + MSP_FISH_ROWS * MSP_ROW_H) {   /* across the fish rows: their page */
        fry_req_t req[FRY_REQ_MAX]; bool staged;
        int fp = fish_pages(t, progression_next_fry(t, req, &staged));
        if (fp < 2) return false;
        int p = g_ms_fpage + (dx < 0 ? 1 : -1);
        if (p >= 0 && p < fp) g_ms_fpage = p;
        return true;
    }
    if (g_ms_caption[0] || tank_pages(t) < 2 || y < MSP_TANK_Y - 6 || y >= MSP_CLOSE_Y - 4) return false;
    int p = g_ms_tpage + (dx < 0 ? 1 : -1);
    if (p >= 0 && p < tank_pages(t)) g_ms_tpage = p;
    return true;
}
/* the modal down, the page (and the tank row's page) kept */
static void ms_close(void) { g_ms_sell_armed = false; g_ms_sell_asked = false; g_ms_kind = -1; g_ms_tip = false; g_ms_caption[0] = 0; g_ms_caption2[0] = 0; g_ms_title[0] = 0; g_ms_sub[0] = 0; g_ms_icon = NULL; g_ms_fish = -1; g_ms_fry = false;
                                     g_ms_row = -1; g_ms_k = -1; g_ms_tankrow = false; g_ms_fryrow = false; }
void render_milestones_leave(void) { ms_close(); g_ms_tpage = 0; g_ms_fpage = 0; }
void render_milestones_show_fish(const tank_t *t, int fish) {
    render_use_theme(t->theme);
    if (fish >= 0 && fish < t->n_fish) ms_open(t, fish, false, false, -1, NULL, 0, false);
}
bool render_milestones_card(const tank_t *t, int *fish, int *rename_x, int *sell_x, int *btn_y) {
    if (!ms_card(t)) return false;
    if (fish) *fish = g_ms_row;
    if (rename_x) *rename_x = PAGE_X + ms_card_btn_x(0) + MSP_CARD_BTN_W / 2;
    if (sell_x)   *sell_x   = PAGE_X + ms_card_btn_x(1) + MSP_CARD_BTN_W / 2;
    if (btn_y)    *btn_y    = PAGE_Y + ms_card_btn_y(t) + MSP_HOW_H / 2;
    return true;
}
bool render_milestones_arrow(const tank_t *t, bool right, int *x, int *y) {
    if (!g_ms_caption[0] || g_ms_tip) return false;
    if (x) *x = PAGE_X + (right ? MSP_MODAL_X + MSP_MODAL_W - MSP_ARROW_IN - MSP_ARROW_W / 2 : MSP_MODAL_X + MSP_ARROW_IN + MSP_ARROW_W / 2);
    if (y) *y = PAGE_Y + ms_modal_y(t) + MSP_ARROW_IN + MSP_ARROW_H / 2;
    return true;
}
void render_milestones_row(int row, int *name_x, int *y) {
    if (name_x) *name_x = PAGE_X + 100;
    if (y)      *y      = PAGE_Y + MSP_ROW_Y0 + row % MSP_FISH_ROWS * MSP_ROW_H + MSP_ROW_MID;
}
int  render_milestones_fish_page(void) { return g_ms_fpage; }
bool render_milestones_pager(const tank_t *t, int *x, int *y) {
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    if (fish_pages(t, progression_next_fry(t, req, &staged)) < 2) return false;
    if (x) *x = PAGE_X + MSP_TPG_X + 12;
    if (y) *y = PAGE_Y + MSP_FPG_Y + MSP_ICON / 2;
    return true;
}

/* ---- announcement modal (notice.h, 2026-09-15) ----
 * The milestones page's detail modal, over the live tank: the badge art at
 * 2x (a fish's own sprite for a stage), the name, the caption, and a thin
 * bar along the foot that runs out with the notice's time. */
void render_notice(const tank_t *t, uint16_t *fb, int stride, int kind, int fish, uint32_t bit, float frac_left) {
    render_use_theme(t->theme);
    ctx_t c = ctx_page(fb, stride);
    const int X = MSP_MODAL_X, W = MSP_MODAL_W, Y = MSP_MODAL_Y, H = MSP_MODAL_H;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    char title[FISH_NAME_MAX + 16] = "THE TANK", caption[40] = "";
    const char *caption2 = NULL;                              /* a second line: the title and both move up */
    const icon_t *ic = NULL;
    const fish_t *f = fish >= 0 && fish < t->n_fish ? &t->fish[fish] : NULL;
    if (kind == 5) {                                          /* NOTICE_LIGHTS_OUT (2026-10-03): the first double-tap that turned the light off */
        snprintf(title, sizeof title, "LIGHTS OUT");
        snprintf(caption, sizeof caption, "YOU DOUBLE-TAPPED THE GLASS");
        caption2 = "DOUBLE-TAP TO TURN IT ON";
        /* a crescent moon: a disc with a bite out of it */
        const int R = 20, PX = X + W / 2 - 3, PY = Y + 36;
        for (int dy = -R; dy <= R; dy++) for (int dx = -R; dx <= R; dx++) {
            int bx = dx - 10, by = dy + 7;
            if (dx * dx + dy * dy <= R * R && bx * bx + by * by > 17 * 17) rect_fill(&c, PX + dx, PY + dy, 1, 1, MSP_TEAL);
        }
    } else if (kind == 4) {                                   /* NOTICE_UPDATED (2026-09-30): the first boot of a new release */
        snprintf(title, sizeof title, "UPDATED");
        snprintf(caption, sizeof caption, "YOUR TANK IS NOW V%s", PT_RELEASE);
        /* a tick in a ring, teal: the shape says it, not the hue */
        const int R = 22, PX = X + W / 2, PY = Y + 48;
        ring(&c, PX, PY, R, MSP_TEAL); ring(&c, PX, PY, R - 1, MSP_TEAL);
        for (int i = 0; i < 6; i++)  rect_fill(&c, PX - 11 + i, PY + i - 1, 3, 3, MSP_TEAL);
        for (int i = 0; i < 12; i++) rect_fill(&c, PX - 6 + i, PY + 4 - i, 3, 3, MSP_TEAL);
    } else if (kind == 3) {                                   /* NOTICE_LOW_BATTERY */
        snprintf(title, sizeof title, "LOW BATTERY");
        snprintf(caption, sizeof caption, "PLEASE CHARGE THE TANK");
        /* the pill, large: outline + nub, the last sliver lit red */
        const int PW = 60, PH = 28, PX = X + (W - PW) / 2, PY = Y + 34;
        rect_edge(&c, PX, PY, PW, PH, 0x9fb4b8); rect_edge(&c, PX + 1, PY + 1, PW - 2, PH - 2, 0x9fb4b8);
        rect_fill(&c, PX + PW, PY + 8, 5, PH - 16, 0x9fb4b8);
        rect_fill(&c, PX + 4, PY + 4, 7, PH - 8, 0xf25b65);
    } else if (kind == 2) {                                   /* NOTICE_STAGE */
        if (f) { snprintf(title, sizeof title, "%s", f->name);
                 snprintf(caption, sizeof caption, "IS NOW %s %s", f->stage == STAGE_ADULT || f->stage == STAGE_ELDER ? "AN" : "A", STAGE_WORDS[f->stage & 3]);
                 fish_preview(fb, stride, X + W / 2, Y + 48, f->size * 1.6f, f, f->color, f->fin, f->accent, t->clock, true); }
    } else if (kind == 1) {                                   /* NOTICE_TANK_MILESTONE */
        for (int k = 0; k < TANK_BADGE_N; k++) if (TANK_BADGES[k].bit == bit) ic = TANK_BADGES[k].icon;
        int bi = 0; while (bi < 31 && !(bit & (1u << bi))) bi++;
        snprintf(caption, sizeof caption, "%s", bi < TMS_COUNT ? TMS_NAMES[bi] : "");
        if (!ic && t->n_fish) {                               /* a population milestone: the newest fish */
            const fish_t *n = &t->fish[t->n_fish - 1];
            fish_preview(fb, stride, X + W / 2, Y + 48, n->size * 1.6f, n, n->color, n->fin, n->accent, t->clock, true);
        }
    } else {                                                  /* NOTICE_MILESTONE */
        for (int k = 0; k < 6; k++) if (FISH_BADGES[k].bit == bit) ic = FISH_BADGES[k].icon;
        int bi = 0; while (bi < 31 && !(bit & (1u << bi))) bi++;
        if (f) snprintf(title, sizeof title, "%s", f->name);
        snprintf(caption, sizeof caption, "%s", bi < MS_FISH_COUNT ? MS_NAMES[bi] : "");
        if (!ic && f) fish_preview(fb, stride, X + W / 2, Y + 48, f->size * 1.6f, f, f->color, f->fin, f->accent, t->clock, true);
    }
    if (ic) blit_icon_scaled(&c, X + (W - ic->w * 2) / 2, Y + 16 + (32 - ic->w), ic, 2, true);
    draw_text(&c, X + (W - text_w(title, 3)) / 2, Y + (caption2 ? 70 : 92), 3, 0xffffff, title);
    draw_text(&c, X + (W - text_w(caption, 2)) / 2, Y + (caption2 ? 104 : 124), 2, MSP_TEAL, caption);
    if (caption2) draw_text(&c, X + (W - text_w(caption2, 2)) / 2, Y + 126, 2, MSP_TEAL, caption2);
    if (frac_left < 0) frac_left = 0;
    if (frac_left > 1) frac_left = 1;
    rect_fill(&c, X + 2, Y + H - 4, (int)((W - 4) * frac_left), 2, 0x1c2f36);
}

/* ---- the shop (2026-09-15): sand dollars, and what they buy ----
 * The milestones page's dress. A 64 px coin and the balance at the top, a
 * row per item (SD_ITEMS), HOW TO EARN bottom left, CLOSE bottom right. A
 * row opens the item's modal (the art at 2x, the words, the price, UNLOCK -
 * dim when the balance is short, IN THE TANK once owned); HOW TO EARN a
 * modal of the sources. The page dims under a modal like the milestones
 * page. Fingers land low here too: the row bands run 8 px above and to the
 * next row, the buttons' bands to the glass edge. */
/* (the page's numbers, SHP_*: render.h) */
static int  g_shp_modal = -1;        /* the item whose modal is up, or -1 */
static bool g_shp_earn;              /* the HOW TO EARN modal is up */
static bool g_shp_sell_armed;        /* SELL tapped once: the next tap on it sells */
static const icon_t *shop_icon(int item) {
    static const icon_t *const SP_ICONS[SP_COUNT - 1] = {   /* the species' pairs (2026-10-05), in species order */
        &icon_shop_seahorse, &icon_shop_octopus, &icon_shop_puffer, &icon_shop_angler, &icon_shop_eel,
        &icon_shop_shark, &icon_shop_squid, &icon_shop_crab, &icon_shop_lobster, &icon_shop_jellyfish, &icon_shop_swordfish, &icon_shop_turtle };
    if (progression_item_species(item) > 0) return SP_ICONS[progression_item_species(item) - 1];
    return item == 0 ? &icon_shop_plant : item == 1 ? &icon_shop_snail : item == 2 ? &icon_shop_castle : item == 3 ? &icon_shop_coral : item == 4 ? &icon_shop_cluster : item == 5 ? &icon_shop_shrimp : item == SD_ITEM_WRECK_IDX ? &icon_shop_wreck : item == SD_ITEM_FROGMAN_IDX ? &icon_shop_frogman : item == SD_ITEM_SUB_IDX ? &icon_shop_sub : &icon_shop_urchin; }
/* a species' creature needs one free place (2026-10-07; a pair needed two): with no room its UNLOCK reads NO ROOM, dim */
static bool shop_no_room(const tank_t *t, int item) { return progression_item_species(item) > 0 && !progression_has_room_one(t); }
/* pages (2026-09-23, the fourth item): SHP_PER_PAGE rows fit between the
 * coin and the foot buttons once the filler caption went (HOW TO EARN says
 * the same). With more items than a page holds, arrows at the header's
 * right flip through the pages; with one page nothing shows. */
static int g_shp_page;
static void shop_arrow(ctx_t *c, int x, int y, bool right, uint32_t rgb) {   /* a chevron in a button */
    button(c, x, y, SHP_ARROW_W, SHP_ARROW_H, 0x1c2f36, rgb, "", 2);
    int cx = x + SHP_ARROW_W / 2, cy = y + SHP_ARROW_H / 2;
    for (int i = 0; i < 7; i++) {                            /* a chevron: two strokes meeting at the tip */
        int x = right ? cx - 3 + i : cx + 3 - i;
        rect_fill(c, x, cy - 6 + i, 2, 1, rgb);
        rect_fill(c, x, cy + 6 - i, 2, 1, rgb);
    }
}
static void price_tag(ctx_t *c, int x, int y, int price, uint32_t rgb) {   /* the small coin + the number */
    blit_icon(c, x, y - 1, &icon_shop_sand_dollar_16, 255);
    char n[16]; snprintf(n, sizeof n, "%d", price);
    draw_text(c, x + 20, y, 2, rgb, n);
}
void render_shop(const tank_t *t, uint16_t *fb, int stride) {
    render_use_theme(t->theme);
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    blit_icon(&c, SHP_COIN_X, SHP_COIN_Y, &icon_shop_sand_dollar_64, 255);
    draw_text(&c, SHP_HEAD_X, SHP_COIN_Y + 6, 2, MSP_TEAL, "SAND DOLLARS");
    char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
    draw_text(&c, SHP_HEAD_X, SHP_COIN_Y + 28, 4, 0xffffff, bal);
    for (int x = 24; x < PAGE_W - 24; x++) px_blend(&c, x, SHP_ROW_Y0 - 10, MSP_DIM, 200);
    if (SHP_PAGES > 1) {
        shop_arrow(&c, SHP_ARROW_X0, SHP_ARROW_Y, false, g_shp_page > 0 ? MSP_TEAL : MSP_DIM);
        shop_arrow(&c, SHP_ARROW_X1, SHP_ARROW_Y, true, g_shp_page < SHP_PAGES - 1 ? MSP_TEAL : MSP_DIM);
    }
    for (int i = g_shp_page * SHP_PER_PAGE; i < SD_ITEM_COUNT && i < (g_shp_page + 1) * SHP_PER_PAGE; i++) {
        const sd_item_t *it = &SD_ITEMS[i];
        int top = SHP_ROW_Y0 + (i - g_shp_page * SHP_PER_PAGE) * SHP_ROW_DY;
        if(theme_active()==THEME_TIDEPOOL_CLUB) {
            const theme_palette_t *p=theme_palette(theme_active());
            round_fill(&c,25,top-5,PAGE_W-50,SHP_ROW_DY-8,14,p->border);
            round_fill(&c,26,top-4,PAGE_W-52,SHP_ROW_DY-11,13,p->panel);
        } else if(theme_active()==THEME_QUIET_LAGOON) {
            const theme_palette_t *p=theme_palette(theme_active());
            rect_fill(&c,28,top+SHP_ROW_DY-9,PAGE_W-56,1,p->border);
        }
        /* (a species' row is never "owned": one is bought as often as there is room, 2026-10-07) */
        bool owned = (t->sd_unlocks & it->bit) != 0 && progression_item_species(i) <= 0, room = !shop_no_room(t, i), can = t->sd_balance >= it->price && room;
        /* a thing not yet bought is its silhouette; a species is always shown as the creature
         * (2026-10-07: its bit means "in the tank", not "bought" - the silhouette read as a missing
         * picture for every species the keeper had not got yet) */
        if (owned || i >= SD_ITEM_SP_FIRST) blit_icon(&c, 32, top, shop_icon(i), 255); else blit_icon_locked(&c, 32, top, shop_icon(i));   /* (the rows are where the bowl is wide: their own column) */
        draw_text(&c, 76, top + 2, 2, 0xffffff, it->name);
        if (owned) draw_text(&c, 76, top + 20, 2, MSP_TEAL, "IN THE TANK");
        else price_tag(&c, 76, top + 20, it->price, can ? MSP_TEAL : MSP_DIM);
        if (owned)     button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, MSP_INK, MSP_DIM, "IN TANK", 2);
        else if (!room) button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_DIM, "NO ROOM", 2);
        else if (can) { button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, MSP_TEAL, MSP_TEAL, "UNLOCK", 2);
                        if(!theme_active())draw_text(&c, SHP_BTN_X + (SHP_BTN_W - text_w("UNLOCK", 2)) / 2, top + (SHP_BTN_H - 14) / 2, 2, MSP_INK, "UNLOCK"); }
        else           button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_DIM, "UNLOCK", 2);
    }
    button(&c, SHP_EARN_X, MSP_CLOSE_Y, SHP_EARN_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "HOW TO EARN", 2);
    button(&c, SHP_CLOSE_X, MSP_CLOSE_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", 2);
    if (g_shp_modal < 0 && !g_shp_earn) return;
    for (int y = 0; y < TANK_H; y++)                          /* the page out of reach under a modal */
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = (uint16_t)((fb[y * stride + x] >> 1) & 0x7bef);
    const int X = SHP_MODAL_X, W = SHP_MODAL_W;
    if (g_shp_earn) {
        const int Y = SHP_EARN_MODAL_Y, H = SHP_EARN_MODAL_H;
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        draw_text(&c, X + (W - text_w("HOW TO EARN", 3)) / 2, Y + 14, 3, 0xffffff, "HOW TO EARN");
        const char *const *lines = progression_sd_earn_lines();
        for (int i = 0; lines[i]; i++) draw_text(&c, X + 14, Y + 50 + i * 24, 2, MSP_TEAL, lines[i]);
        draw_text(&c, X + (W - text_w("TAP TO CLOSE", 2)) / 2, Y + H - 24, 2, 0x3f6a72, "TAP TO CLOSE");
        return;
    }
    const sd_item_t *it = &SD_ITEMS[g_shp_modal];
    const int Y = SHP_MODAL_Y, H = SHP_MODAL_H;
    bool owned = (t->sd_unlocks & it->bit) != 0 && progression_item_species(g_shp_modal) <= 0, room = !shop_no_room(t, g_shp_modal), can = t->sd_balance >= it->price && room;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    blit_icon_scaled(&c, X + (W - 64) / 2, Y + 14, shop_icon(g_shp_modal), 2, true);
    draw_text(&c, X + (W - text_w(it->name, 3)) / 2, Y + 88, 3, 0xffffff, it->name);
    draw_text(&c, X + (W - text_w(it->words, 2)) / 2, Y + 118, 2, MSP_TEAL, it->words);
    draw_text(&c, X + (W - text_w(it->words2, 2)) / 2, Y + 138, 2, MSP_TEAL, it->words2);
    const int bx = X + (W - MSP_HOW_W) / 2, by = Y + H - 12 - MSP_HOW_H;
    if (owned && tank_decor_placeable(g_shp_modal)) {         /* a placeable piece: MOVE re-opens the placement page, SELL (twice) sells it back */
        char sell[24]; snprintf(sell, sizeof sell, "SELLS BACK FOR %d", progression_sell_value(g_shp_modal));
        draw_text(&c, X + (W - text_w("IN THE TANK", 2)) / 2, Y + 164, 2, MSP_TEAL, "IN THE TANK");
        draw_text(&c, X + (W - text_w(sell, 2)) / 2, Y + 184, 2, MSP_DIM, sell);
        button(&c, SHP_TWO_X0, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "MOVE", 2);
        if (g_shp_sell_armed) { snprintf(sell, sizeof sell, "+%d OK?", progression_sell_value(g_shp_modal));
                                button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, sell, 2);
                                draw_text(&c, SHP_TWO_X1 + (MSP_HOW_W - text_w(sell, 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_INK, sell); }
        else button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "SELL", 2);
    } else if (owned) draw_text(&c, X + (W - text_w("IN THE TANK", 2)) / 2, by + 8, 2, MSP_TEAL, "IN THE TANK");   /* the snail: a permanent resident */
    else {
        char line[32]; snprintf(line, sizeof line, "%d", it->price);
        int pw = 20 + text_w(line, 2);
        price_tag(&c, X + (W - pw) / 2, Y + 164, it->price, can ? 0xffffff : MSP_DIM);
        int msp = progression_item_species(g_shp_modal), nsp = msp > 0 ? tank_species_n(t, msp) : 0;
        if (!room) draw_text(&c, X + (W - text_w("THE TANK IS FULL", 2)) / 2, Y + 184, 2, MSP_DIM, "THE TANK IS FULL");
        else if (!can) { snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance);
                         draw_text(&c, X + (W - text_w(line, 2)) / 2, Y + 184, 2, MSP_DIM, line); }
        else if (nsp > 0) { snprintf(line, sizeof line, "%d IN THE TANK", nsp);   /* (a species: how many of its kind are here) */
                            draw_text(&c, X + (W - text_w(line, 2)) / 2, Y + 184, 2, MSP_TEAL, line); }
        if (!room) button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "NO ROOM", 2);
        else if (can) { button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, "UNLOCK", 2);
                   draw_text(&c, bx + (MSP_HOW_W - text_w("UNLOCK", 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_INK, "UNLOCK"); }
        else button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "UNLOCK", 2);
    }
}
int render_shop_tap(const tank_t *t, float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    if (g_shp_earn) { g_shp_earn = false; return SHOP_TAP_KEPT; }
    if (g_shp_modal >= 0) {
        int item = g_shp_modal; const sd_item_t *it = &SD_ITEMS[item];
        bool owned = (t->sd_unlocks & it->bit) != 0 && progression_item_species(item) <= 0, can = t->sd_balance >= it->price && !shop_no_room(t, item);   /* (a species: never owned, 2026-10-07) */
        const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H;
        bool row = y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN;
        bool on_btn = row && x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X;
        if (owned && tank_decor_placeable(item)) {                /* two buttons: MOVE, and SELL armed then confirmed */
            bool on_move = row && x >= SHP_TWO_X0 - MSP_HOW_SLOP_X && x < SHP_TWO_X0 + MSP_HOW_W + SHP_TWO_GAP / 2;
            bool on_sell = row && x >= SHP_TWO_X1 - SHP_TWO_GAP / 2 && x < SHP_TWO_X1 + MSP_HOW_W + MSP_HOW_SLOP_X;
            if (on_sell && !g_shp_sell_armed) { g_shp_sell_armed = true; return SHOP_TAP_KEPT; }
            g_shp_sell_armed = false; g_shp_modal = -1;
            if (on_sell) return SHOP_TAP_SELL + item;
            if (on_move) return SHOP_TAP_MOVE + item;
            return SHOP_TAP_KEPT;
        }
        g_shp_modal = -1; g_shp_sell_armed = false;
        if (on_btn && !owned && can) return SHOP_TAP_BUY + item;
        return SHOP_TAP_KEPT;
    }
    if (x >= SHP_CLOSE_X - 6 && y >= MSP_CLOSE_Y - 4) return SHOP_TAP_CLOSE;
    if (x < SHP_EARN_X + SHP_EARN_W + 8 && y >= MSP_CLOSE_Y - 4) { g_shp_earn = true; return SHOP_TAP_KEPT; }
    if (SHP_PAGES > 1 && y < SHP_ROW_Y0 - 10 && x >= SHP_ARROW_X0 - 8) {   /* the page arrows, in the header band */
        if (x < SHP_ARROW_X1 - 4) { if (g_shp_page > 0) g_shp_page--; }
        else if (g_shp_page < SHP_PAGES - 1) g_shp_page++;
        return SHOP_TAP_KEPT;
    }
    for (int i = g_shp_page * SHP_PER_PAGE; i < SD_ITEM_COUNT && i < (g_shp_page + 1) * SHP_PER_PAGE; i++) {
        int top = SHP_ROW_Y0 + (i - g_shp_page * SHP_PER_PAGE) * SHP_ROW_DY;
        if (x >= 20 && y >= top - 8 && y < top + SHP_ROW_DY - 8) { g_shp_modal = i; return SHOP_TAP_KEPT; }
    }
    return SHOP_TAP_NONE;
}
void render_shop_leave(void) { g_shp_modal = -1; g_shp_earn = false; g_shp_page = 0; g_shp_sell_armed = false; }

/* ---- the main menu (2026-10-21): the BOOT key's page ----
 * Six tiles, three across: TANK (the milestones page), UPGRADES (the shop),
 * SETTINGS; then the SPONGE, the SCISSORS and an empty cell. The page
 * icons are placeholders (the bubbles badge) until their own art lands:
 * assets/icons/menu_*.png through tools/gen_icons.py, then the table below.
 * The tool in hand is lit (the toolbox's look: a white double edge on a
 * lighter fill). */
static const icon_t *const MENU_ICON[MENU_TILES] = {
    &icon_ms_first_bubbles, &icon_ms_first_bubbles, &icon_ms_first_bubbles,   /* TANK, UPGRADES, SETTINGS (placeholders) */
    &icon_tool_sponge, &icon_tool_scissors, NULL,
};
static const char *const MENU_LABEL[MENU_TILES] = { "TANK", "UPGRADES", "SETTINGS", "SPONGE", "SCISSORS", NULL };
static void menu_tile_page(int i, int *x, int *y) {
    *x = MENU_X0 + (i % 3) * (MENU_TILE_W + MENU_GAP_X);
    *y = MENU_Y0 + (i / 3) * (MENU_TILE_H + MENU_GAP_Y);
}
void render_menu_tile_rect(int i, int *x, int *y, int *w, int *h) {
    menu_tile_page(i, x, y); *x += PAGE_X; *y += PAGE_Y; *w = MENU_TILE_W; *h = MENU_TILE_H;
}
void render_menu(const tank_t *t, uint16_t *fb, int stride) {
    render_use_theme(t->theme);
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    draw_text(&c, (PAGE_W - text_w("MENU", 3)) / 2, MENU_Y0 - 38, 3, MSP_TEAL, "MENU");
    for (int i = 0; i < MENU_TILES; i++) {
        if (!MENU_ICON[i]) continue;                 /* the empty cell */
        int x, y; menu_tile_page(i, &x, &y);
        bool held = (i == 3 && t->tool == TOOL_SPONGE) || (i == 4 && t->tool == TOOL_SCISSORS);
        rect_fill(&c, x, y, MENU_TILE_W, MENU_TILE_H, held ? 0x356670 : 0x14252b);
        uint32_t edge = held ? 0xffffff : MSP_TEAL;
        rect_edge(&c, x, y, MENU_TILE_W, MENU_TILE_H, edge);
        if (held) rect_edge(&c, x + 1, y + 1, MENU_TILE_W - 2, MENU_TILE_H - 2, edge);
        const icon_t *ic = MENU_ICON[i];
        blit_icon_scaled(&c, x + (MENU_TILE_W - ic->w * 2) / 2, y + 10 + (64 - ic->h * 2) / 2, ic, 2, true);
        draw_text(&c, x + (MENU_TILE_W - text_w(MENU_LABEL[i], 2)) / 2, y + MENU_TILE_H - 24, 2, 0xffffff, MENU_LABEL[i]);
    }
}
int render_menu_tap(const tank_t *t, float x, float y) {
    (void)t;
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    const int gw = 3 * MENU_TILE_W + 2 * MENU_GAP_X, gh = 2 * MENU_TILE_H + MENU_GAP_Y;
    if (x < MENU_X0 - MENU_SLOP || x >= MENU_X0 + gw + MENU_SLOP || y < MENU_Y0 - MENU_SLOP || y >= MENU_Y0 + gh + MENU_SLOP)
        return MENU_TAP_CLOSE;                   /* off the grid: back to the tank */
    static const int CODE[MENU_TILES] = { MENU_TAP_TANK, MENU_TAP_UPGRADES, MENU_TAP_SETTINGS, MENU_TAP_SPONGE, MENU_TAP_SCISSORS, MENU_TAP_NONE };
    for (int i = 0; i < MENU_TILES; i++) {       /* each tile with half the gap around it, so a near miss still lands */
        int tx, ty; menu_tile_page(i, &tx, &ty);
        if (x >= tx - MENU_GAP_X / 2 && x < tx + MENU_TILE_W + MENU_GAP_X / 2 && y >= ty - MENU_GAP_Y / 2 && y < ty + MENU_TILE_H + MENU_GAP_Y / 2)
            return CODE[i];
    }
    return MENU_TAP_NONE;                        /* the grid's slop */
}

/* the toast: "+N" by a coin, top centre, for TOAST_S on the tank clock */
#define TOAST_S 2.5f
static int   g_toast_n;
static float g_toast_until = -1;
void render_sd_toast(const tank_t *t, uint16_t *fb, int stride) {
    render_use_theme(t->theme);
    int n = progression_sd_take_award();
    if (n > 0) {
        if (t->clock < g_toast_until) g_toast_n += n; else g_toast_n = n;
        g_toast_until = t->clock + TOAST_S;
    }
    if (t->clock >= g_toast_until || t->clock < g_toast_until - TOAST_S - 1) return;   /* (a reset sends the clock back) */
    ctx_t c = ctx_full(fb, stride, 1.0f);
    char txt[16]; snprintf(txt, sizeof txt, "+%d", g_toast_n);
    const int W = 30 + text_w(txt, 2), H = 22, X = (TANK_W - W) / 2, Y = PAGE_BOWL ? 44 : 8;   /* (the bowl's top is a narrow cap: lower) */
    for (int y = Y; y < Y + H; y++)
        for (int x = X; x < X + W; x++) px_blend(&c, x, y, theme_ui_color(0x04141a), 215);
    rect_edge(&c, X, Y, W, H, MSP_TEAL);
    blit_icon(&c, X + 5, Y + 3, &icon_shop_sand_dollar_16, 255);
    draw_text(&c, X + 25, Y + 4, 2, 0xffffff, txt);
}

/* ---- settings page (2026-09-15) ----
 * The milestones page's foot used to carry the brightness row; Strato:
 * "a new UI for settings and leave milestones alone - we may need more
 * room there anyway". Rows of segment buttons, generous hit bands (fingers
 * land low near the bezel, as on the milestones page). 0.3.2: LIGHTS OUT is
 * one row (a value between two arrows, where MANUAL / AUTO and a big seconds
 * selector stood), and AUTO FEED and ROTATION have the room it gave back. */
/* (the page's numbers, SET_*: render.h) */
/* BRIGHTNESS (2026-10-10, evening): ten steps, 10 % to 100 %, a value between two arrows like
   LIGHTS OUT (four segments 15/30/60/100 before). An odd saved value rounds to the nearest step. */
static const char *const SET_BRIGHT[10] = { "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" };
static const int         SET_BRIGHT_PCT[10] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
#define SET_BRIGHT_N 10
static int bright_index(int pct) { int i = (pct + 5) / 10 - 1; return i < 0 ? 0 : i >= SET_BRIGHT_N ? SET_BRIGHT_N - 1 : i; }
static const char *const SET_VOLUME[3] = { "OFF", "QUIET", "NORMAL" };
static const char *const SET_LIGHT[LIGHT_IDLE_N + 1] = { "DOUBLE-TAP",   /* MANUAL, the default: the row says how the light is worked */
    "5 SEC", "15 SEC", "30 SEC", "1 MIN", "3 MIN", "5 MIN", "10 MIN", "30 MIN" };
static const char *const SET_FEED[2]   = { "ON", "OFF" };          /* the default first */
#if TANK_WORN
static const char *const SET_SCREEN[2] = { "NORMAL", "TURNED" };   /* the default first */
#endif

static void set_row_w(ctx_t *c, int row_y, const char *label, const char *const names[], int n, int chosen, int w, int dx) {
    draw_text(c, SET_LABEL_X, row_y, 2, MSP_TEAL, label);
    for (int i = 0; i < n; i++) {
        int x = SET_SEG_X + i * dx, y = SET_SEG_Y(row_y);
        if (i == chosen) {                       /* lit: teal, ink lettering */
            button(c, x, y, w, SET_SEG_H, MSP_TEAL, MSP_TEAL, names[i], 2);
            draw_text(c, x + (w - text_w(names[i], 2)) / 2, y + (SET_SEG_H - 14) / 2, 2, MSP_INK, names[i]);
        } else button(c, x, y, w, SET_SEG_H, 0x1c2f36, MSP_DIM, names[i], 2);
    }
}
static void set_row(ctx_t *c, int row_y, const char *label, const char *const names[], int n, int chosen) { set_row_w(c, row_y, label, names, n, chosen, SET_SEG_W, SET_SEG_DX); }
/* an arrow button of the LIGHTS OUT row: dim at the end of the list */
static void set_arrow(ctx_t *c, int x, int y, bool right, bool live) {
    uint32_t rgb = live ? MSP_TEAL : 0x2c4a52;
    button(c, x, y, SET_ARW_W, SET_SEG_H, 0x1c2f36, rgb, "", 2);
    int cx = x + SET_ARW_W / 2, cy = y + SET_SEG_H / 2;
    for (int i = 0; i < 7; i++) {                           /* a solid triangle, 7 wide, 13 tall */
        int px = right ? cx - 3 + i : cx + 3 - i, hh = 6 - i;
        rect_fill(c, px, cy - hh, 1, 2 * hh + 1, rgb);
    }
}
/* (the watch has SCREEN where ROTATION is) */
/* ROTATION's picture: a padlock inside a turning arrow - shut when the way
 * up is locked, its shackle swung open while the picture follows the tank
 * (the two differ in shape, not in color alone) */
static void set_lock_icon(ctx_t *c, int cx, int cy, bool locked, uint32_t rgb) {
    for (int dy = -15; dy <= 15; dy++)                      /* the ring, open at the top right */
        for (int dx = -15; dx <= 15; dx++) {
            float r2 = (float)(dx * dx + dy * dy);
            if (r2 < 11.5f * 11.5f || r2 > 14.0f * 14.0f) continue;
            if (dx > 1 && dy < 0 && dy < -dx * 0.45f) continue;
            px_blend(c, cx + dx, cy + dy, rgb, 255);
        }
    for (int i = 0; i < 6; i++)                             /* its arrowhead, at the top, pointing clockwise */
        rect_fill(c, cx + 1 + i, cy - 13 - (5 - i), 1, 2 * (5 - i) + 1, rgb);
    rect_fill(c, cx - 6, cy - 1, 12, 8, rgb);              /* the padlock's body */
    int top = locked ? cy - 7 : cy - 10;                    /* the shackle: down in the body, or lifted with one leg free */
    rect_fill(c, cx - 4, top, 8, 2, rgb);
    rect_fill(c, cx - 4, top, 2, cy - 1 - top, rgb);
    rect_fill(c, cx + 2, top, 2, locked ? cy - 1 - top : 4, rgb);
}
/* a row of one value between two arrow buttons (LIGHTS OUT, and BRIGHTNESS since 2026-10-10) */
static void set_arrow_row(ctx_t *c, int row_y, const char *label, const char *text, bool can_prev, bool can_next) {
    int y = SET_SEG_Y(row_y), bx = SET_SEG_X + SET_ARW_W + 4, bw = SET_SPAN_W - 2 * (SET_ARW_W + 4);
    draw_text(c, SET_LABEL_X, row_y, 2, MSP_TEAL, label);
    set_arrow(c, SET_SEG_X, y, false, can_prev);
    button(c, bx, y, bw, SET_SEG_H, MSP_TEAL, MSP_TEAL, "", 2);
    draw_text(c, bx + (bw - text_w(text, 2)) / 2, y + (SET_SEG_H - 14) / 2, 2, MSP_INK, text);
    set_arrow(c, SET_SEG_X + SET_SPAN_W - SET_ARW_W, y, true, can_next);
}
static void render_original_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume) {
    render_use_theme(t->theme);
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, MSP_INK);
    button(&c, (PAGE_W - 244) / 2, SET_TITLE_Y - 8, 244, 40, MSP_INK, MSP_DIM, "SETTINGS / THEMES", 2);
    int bi = bright_index(bright_pct);
    set_arrow_row(&c, SET_ROW1_Y, "BRIGHTNESS", SET_BRIGHT[bi], bi > 0, bi < SET_BRIGHT_N - 1);   /* < the percent > */
    set_row(&c, SET_ROW2_Y, "VOLUME", SET_VOLUME, 3, volume < 0 ? 0 : volume > 2 ? 2 : volume);
    draw_text(&c, SET_LABEL_X, SET_NOTE_Y, 2, MSP_DIM, "FISH ARE QUIET AT NIGHT");
    { int ch = tank_light_choice(t); set_arrow_row(&c, SET_ROW3_Y, "LIGHTS OUT", SET_LIGHT[ch], ch > 0, ch < LIGHT_IDLE_N); }   /* < the choice > */
    set_row(&c, SET_ROW4_Y, "AUTO FEED", SET_FEED, 2, t->autofeed_off ? 1 : 0);
#if TANK_WORN
    /* SCREEN (2026-10-02): the way up of a watch worn either way round - TURNED
       for buttons toward the elbow. The picture turns as the finger lifts. */
    set_row(&c, SET_ROW5_Y, "SCREEN", SET_SCREEN, 2, t->screen_turned ? 1 : 0);
    draw_text(&c, SET_LABEL_X, SET_NOTE5_Y, 2, MSP_DIM, "WORN THE OTHER WAY AROUND?");
#else
    /* ROTATION (0.3.2): the picture turns over with the tank, unless locked */
    {
        int x = SET_SEG_X, y = SET_SEG_Y(SET_ROW5_Y);
        draw_text(&c, SET_LABEL_X, SET_ROW5_Y, 2, MSP_TEAL, "ROTATION");
        if (t->orient_lock) button(&c, x, y, SET_SEG_W, SET_SEG_H, MSP_TEAL, MSP_TEAL, "", 2);
        else                button(&c, x, y, SET_SEG_W, SET_SEG_H, 0x1c2f36, MSP_DIM, "", 2);
        set_lock_icon(&c, x + SET_SEG_W / 2, y + SET_SEG_H / 2 + 1, t->orient_lock, t->orient_lock ? MSP_INK : MSP_TEAL);
        draw_text(&c, SET_ROT_WORD_X, SET_ROW5_Y, 2, t->orient_lock ? 0xffffff : MSP_DIM, t->orient_lock ? "LOCKED" : "UNLOCKED");
    }
#endif
    /* the firmware version, hugging the bottom left of the frame (6 px up,
       on the labels' x; the bezel's curve is clear there), small (8 px) and
       dim: it is for the keeper who asks "how do I update?", not for
       reading - Strato: "FISH ARE QUIET AT NIGHT is meant to be read; the
       fw version is something most people should not care about. minimize
       it", "make it 8px tall", "hug the bottom of the frame" (2026-09-16).
       The installer page shows the version it would write in the same words. */
    /* 2026-09-29: the release number first ("V0.2.0 ALPHA"), the build id after it */
    char ver[64]; snprintf(ver, sizeof ver, "V%s %s  BUILD %s", PT_RELEASE, PT_RELEASE_STAGE, version_port_string());
#if TANK_WORN                                    /* the watch: centred under the foot, between the lower corners */
    draw_text_8px(&c, (PAGE_W - ((int)strlen(ver) * 6 - 1)) / 2, SET_FOOT_Y + MSP_CLOSE_H + 2, MSP_DIM, ver);
#else
    draw_text_8px(&c, SET_LABEL_X + (PAGE_BOWL ? 96 : 0), PAGE_H - 8 - 6, MSP_DIM, ver);
#endif
    button(&c, SET_CLOSE_X, SET_FOOT_Y, SET_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", 2);
    /* UPDATES (2026-09-30, docs/OTA.md): bottom left; ABOUT and RESET (2026-10-10) between */
    button(&c, SET_UPD_X, SET_FOOT_Y, SET_UPD_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "UPDATES", 2);
    button(&c, SET_ABT_X, SET_ABT_Y, SET_ABT_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "ABOUT", 2);
    button(&c, SET_RST_X, SET_RST_Y, SET_RST_W, MSP_CLOSE_H, 0x1c2f36, 0xe0785a, "RESET", 2);   /* a warm edge: it starts the tank over (after the prompt) */
}
/* ---- the ABOUT page (2026-10-10): the game's name, who makes it, the release and the build,
 * a little fish crossing above them and bubbles rising; a jingle plays while it is up (the
 * platform's: SET_TAP_ABOUT). Inside the settings bounds on every board, in the theme's colours. */
static void render_about_page(const tank_t *t, uint16_t *fb, int stride) {
    int x, y, w, h; render_settings_bounds(&x, &y, &w, &h);
    const theme_palette_t *p = theme_palette(t->theme);
    ctx_t c = ctx_full(fb, stride, 1); g_dirty_hold = true;
    rect_fill(&c, 0, 0, TANK_W, TANK_H, p->background);
    int cx = x + w / 2, ty = y + h * 3 / 10;
    /* bubbles: a dozen on their own slow loops, rising the page's height */
    for (int i = 0; i < 12; i++) {
        float ph = fmodf(t->clock * (0.09f + 0.013f * (i % 5)) + i * 0.37f, 1.0f);
        float bx = x + 12 + (i * 37 + 11) % (w - 24) + sinf(t->clock * 1.3f + i) * 4, by = y + h - 50 - ph * (h - 60);
        float r = 1.5f + (i % 3) * 0.9f;
        fill_ellipse(&c, bx, by, r, r, p->accent, (int)(90 * (1 - ph * 0.6f)));
        px_blend(&c, (int)(bx - r * 0.4f), (int)(by - r * 0.4f), p->text, 120);
    }
    /* the theme's creature (2026-10-10 night, Alvin: "different creature, movement and music for each
       of the theme"): Original a fish crossing above the title, turning at the ends; Quiet Lagoon
       a seahorse drifting on a slow loop, bobbing, facing the way it drifts; Tidepool Club a crab
       scuttling along the foot, pausing at each end to raise its claws; Blackwater a moon jelly
       pulsing up the left side and sinking back, its bell driven by the pulse */
    { fish_t f; memset(&f, 0, sizeof f); float k = t->clock;
      f.size = 0.95f; f.stage = STAGE_ADULT; f.color = 0x66caca; f.fin = 0x417b96; f.accent = 0x92dcaf; f.facing = 1;
      if (t->theme == THEME_QUIET_LAGOON) {
          f.species = SP_SEAHORSE; f.variant = 1; f.color = 0xe0a040; f.fin = 0xd07030; f.accent = 0xf8e0a0; f.size = 1.1f;
          float u = sinf(k * 0.21f);
          f.x = cx + u * (w / 2 - 84); f.y = y + 44 + sinf(k * 0.9f) * 6 + cosf(k * 0.37f) * 10;
          float d = cosf(k * 0.21f) >= 0 ? 1 : -1; f.yaw = f.yaw_tail = d * fminf(1, fabsf(cosf(k * 0.21f)) * 2.5f + 0.3f); f.facing = (int8_t)d;
          f.heading = d > 0 ? 0 : 3.14159f;
      } else if (t->theme == THEME_TIDEPOOL_CLUB) {
          f.species = SP_CRAB; f.variant = 0; f.color = 0xf0603a; f.fin = 0xffa060; f.accent = 0xffe0b0; f.size = 1.0f;
          float ph = fmodf(k * 0.11f, 1.0f), s4 = ph < 0.4f ? ph / 0.4f : ph < 0.5f ? 1 : ph < 0.9f ? 1 - (ph - 0.5f) / 0.4f : 0;   /* across, pause, back, pause */
          f.x = x + 50 + s4 * (w - 100); f.y = y + h - 80;
          bool paused = ph >= 0.4f && ph < 0.5f;
          if (ph >= 0.9f) paused = true;
          f.yaw = f.yaw_tail = ph < 0.5f ? 1 : -1; f.facing = ph < 0.5f ? 1 : -1; f.heading = ph < 0.5f ? 0 : 3.14159f;
          f.puff = paused ? fminf(1, fabsf(sinf(k * 2.2f)) * 1.4f) : 0;
      } else if (t->theme == THEME_BLACKWATER) {
          f.species = SP_JELLYFISH; f.variant = 3; f.color = 0x9fd8e2; f.fin = 0xb8e8ff; f.accent = 0xe0f8ff; f.size = 1.15f;
          f.jet = fmodf(k * 0.55f, 1.0f);
          f.x = x + 52 + sinf(k * 0.31f) * 16; f.y = y + h / 2 - 10 - sinf(k * 0.17f) * (h / 2 - 70) + sinf(f.jet * 6.2831853f) * 2;
          f.yaw = f.yaw_tail = 1;
      } else {
          float u = sinf(k * 0.45f);
          f.x = cx + u * (w / 2 - 44); f.y = y + 30 + sinf(k * 1.1f) * 3;
          f.yaw = f.yaw_tail = cosf(k * 0.45f) >= 0 ? 1 : -1; f.yaw *= fminf(1, fabsf(cosf(k * 0.45f)) * 3); f.yaw_tail = f.yaw;
      }
      draw_fish_core(&c, &f, k, false, 1); }
    draw_text(&c, cx - text_w("AQUA PETS", 3) / 2, ty, 3, p->text, "AQUA PETS");
    round_fill(&c, cx - 40, ty + 30, 80, 3, 1, p->accent);
    draw_text(&c, cx - text_w("DEVELOPED BY", 2) / 2, ty + 48, 2, p->muted, "DEVELOPED BY");
    draw_text(&c, cx - text_w("SOFTWORKZ PTE LTD", 2) / 2, ty + 70, 2, p->text, "SOFTWORKZ PTE LTD");
    char ver[48]; snprintf(ver, sizeof ver, "VERSION %s %s", PT_RELEASE, PT_RELEASE_STAGE);
    for (char *q = ver; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
    draw_text(&c, cx - text_w(ver, 2) / 2, ty + 104, 2, p->accent, ver);
    char bld[48]; snprintf(bld, sizeof bld, "BUILD %s", version_port_string());
    for (char *q = bld; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
    draw_text_8px(&c, cx - ((int)strlen(bld) * 6 - 1) / 2, ty + 128, p->muted, bld);
    button(&c, cx - 56, y + h - 44, 112, 44, p->panel, p->border, "BACK", 2);
}
static int set_segment_w(float x, int n, int dx) {
    if (x < SET_SEG_X - 10) return -1;
    int i = (int)((x - SET_SEG_X + 3) / dx);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}
static int set_segment(float x, int n) { return set_segment_w(x, n, SET_SEG_DX); }
/* the hit test: what a TAP at (x,y) means. *value: BRIGHT the percent,
 * VOLUME 0..2, FEED 1 = ON, SCREEN 1 = TURNED; ROTATE carries none (a
 * toggle); the LIGHTS OUT row's own hits are LIGHT_PREV / LIGHT_NEXT (its
 * left and right halves). */
enum { SET_HIT_LIGHT_PREV = 100, SET_HIT_LIGHT_NEXT, SET_HIT_THEMES, SET_HIT_THEME_PICK, SET_HIT_THEME_BACK, SET_HIT_PAGE, SET_HIT_BRIGHT_PREV, SET_HIT_BRIGHT_NEXT, SET_HIT_CYCLE_VOLUME, SET_HIT_TOGGLE_FEED, SET_HIT_TOGGLE_SCREEN,
       SET_HIT_ABOUT, SET_HIT_ABOUT_BACK, SET_HIT_RESET };   /* (2026-10-10) ABOUT, the about page's BACK, RESET */
static bool g_settings_themes, g_settings_about;
static int g_settings_page, g_settings_bright = 60, g_settings_volume = 2;
static int original_settings_tap(float x, float y, int *value) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
    if (x >= (PAGE_W - 244) / 2 && x < (PAGE_W + 244) / 2 && y >= SET_TITLE_Y - 8 && y < SET_TITLE_Y + 32) return SET_HIT_THEMES;
    if (x >= SET_ABT_X - 6 && x < SET_ABT_X + SET_ABT_W + 6 && y >= SET_ABT_Y - 6 && y < SET_ABT_Y + MSP_CLOSE_H + 8) return SET_HIT_ABOUT;   /* (before the foot's halves, on a wide foot they stand between them) */
    if (x >= SET_RST_X - 6 && x < SET_RST_X + SET_RST_W + 6 && y >= SET_RST_Y - 6 && y < SET_RST_Y + MSP_CLOSE_H + 8) return SET_HIT_RESET;
    if (x >= SET_CLOSE_X - 8 && y >= SET_FOOT_Y - 4) return SET_TAP_CLOSE;
    if (x < SET_UPD_X + SET_UPD_W + 8 && y >= SET_FOOT_Y - 4) return SET_TAP_UPDATES;
    /* the row bands: from a little above each segment down to the next row
       (fingers report low); the last one stops at the foot's */
    int seg = set_segment(x, 3), two = set_segment(x, 2);
    if (y >= SET_SEG_Y(SET_ROW1_Y) - 12 && y < SET_SEG_Y(SET_ROW2_Y) - 12) { if (seg < 0) return SET_TAP_NONE; *value = 0; return x < SET_SPAN_MID ? SET_HIT_BRIGHT_PREV : SET_HIT_BRIGHT_NEXT; }
    if (y >= SET_SEG_Y(SET_ROW2_Y) - 12 && y < SET_SEG_Y(SET_ROW3_Y) - 12) { if (seg < 0) return SET_TAP_NONE; *value = seg; return SET_TAP_VOLUME; }
    if (y >= SET_SEG_Y(SET_ROW3_Y) - 12 && y < SET_SEG_Y(SET_ROW4_Y) - 12) { if (seg < 0) return SET_TAP_NONE; *value = 0; return x < SET_SPAN_MID ? SET_HIT_LIGHT_PREV : SET_HIT_LIGHT_NEXT; }
    if (y >= SET_SEG_Y(SET_ROW4_Y) - 12 && y < SET_SEG_Y(SET_ROW5_Y) - 12) { if (two < 0) return SET_TAP_NONE; *value = two == 0; return SET_TAP_FEED; }
    if (y >= SET_SEG_Y(SET_ROW5_Y) - 12 && y < (TANK_WORN ? SET_ABT_Y - 6 : SET_FOOT_Y - 4)) {
        if (two < 0) return SET_TAP_NONE;
#if TANK_WORN
        *value = two == 1; return SET_TAP_SCREEN;
#else
        *value = 0; return SET_TAP_ROTATE;       /* the button or its word: one toggle */
#endif
    }
    return SET_TAP_NONE;
}
/* Short pages inside an inscribed rectangle; all targets share these bounds. */
void render_settings_bounds(int *x, int *y, int *w, int *h) {
#if PAGE_BOWL
    *x = 73; *y = 81; *w = 320; *h = 304;
#elif TANK_WORN
    *x = 28; *y = 33; *w = 354; *h = 436;
#else
    *x = 22; *y = 20; *w = 404; *h = 328;
#endif
}
/* the theme picker's tiles (2026-10-10, four themes): the page's height above the DONE button shared
   between them, a 62 px pitch at most (the 1.8 gets 53, the bowl 47, the watch 62) */
void render_theme_tile_rect(int i, int *tx, int *ty, int *tw, int *th) {
    int x, y, w, h; render_settings_bounds(&x, &y, &w, &h);
    int pitch = (h - 44 - 8 - 64) / THEME_COUNT; if (pitch > 62) pitch = 62;
    *tx = x; *ty = y + 64 + i * pitch; *tw = w; *th = pitch - 6;
}
void render_settings_leave(void) { g_settings_themes = false; g_settings_about = false; g_settings_page = 0; }
static bool hit_rect(float x, float y, int bx, int by, int w, int h) {
    return x >= bx && y >= by && x < bx+w && y < by+h;
}
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume) {
    render_use_theme(t->theme);
    g_settings_bright = bright_pct;
    g_settings_volume = volume < 0 ? 0 : volume > 2 ? 2 : volume;
    if (g_settings_about) { render_about_page(t, fb, stride); return; }
    /* one layout for every theme (2026-10-10, Alvin: "unify the menu to follow the original theme
       menu layout"): the rows of segments and arrows, in the theme's own colours and font (button,
       draw_text and theme_ui_color do that); only the theme picker keeps its tiles */
    if (!g_settings_themes) { render_original_settings(t, fb, stride, bright_pct, volume); return; }
    int x,y,w,h; render_settings_bounds(&x,&y,&w,&h);
    const theme_palette_t *p = theme_palette(t->theme);
    ctx_t c = ctx_full(fb,stride,1); g_dirty_hold = true;
    rect_fill(&c,0,0,TANK_W,TANK_H,p->background);
    draw_text(&c,x,y+2,3,p->text,g_settings_themes ? "CHOOSE A THEME" : "SETTINGS");
    draw_text(&c,x,y+34,2,p->muted,g_settings_themes ? "YOUR TANK, YOUR STYLE" : "CREATURE COMFORTS");
    if (g_settings_themes) {
        for (int i=0;i<THEME_COUNT;i++) {
            int tx,by,tw,th; render_theme_tile_rect(i,&tx,&by,&tw,&th);   /* the tiles share the page above DONE (four since 2026-10-10) */
            const theme_palette_t *tile = theme_palette(i);
            /* A tile previews its own colours; restore the active palette before returning. */
            theme_activate(i);
            int r=i==THEME_TIDEPOOL_CLUB?18:6; if(r>th/3)r=th/3;
            round_fill(&c,x,by,w,th,r,i==t->theme ? tile->accent : p->border);
            round_fill(&c,x+2,by+2,w-4,th-4,r-2,tile->panel);
            const icon_t *ic = theme_preview_icon(i);
            if (ic) blit_icon(&c,x+8,by+(th-ic->h)/2,ic,255);
            else {
                fish_t f; memset(&f,0,sizeof f); f.x=x+34; f.y=by+th/2+1; f.size=.85f;
                f.yaw=f.yaw_tail=1; f.color=0x66caca; f.fin=0x417b96; f.accent=0x92dcaf;
                f.stage=STAGE_ADULT; draw_fish_core(&c,&f,0,false,1);
            }
            draw_text(&c,x+66,by+th/2-16,2,tile->text,tile->name);
            round_fill(&c,x+66,by+th/2+8,22,6,3,tile->accent);
            round_fill(&c,x+92,by+th/2+8,22,6,3,tile->gold);
            if (i==t->theme) {
                fill_ellipse(&c,x+w-21,by+th/2,7,7,tile->accent,255);
                draw_text(&c,x+w-26,by+th/2-6,1,tile->on_accent,"+");
            }
        }
        theme_activate(theme_valid(t->theme));
        button(&c,x+(w-112)/2,y+h-44,112,44,p->panel,p->border,"DONE",2);
        return;
    }
}
int render_settings_tap(float tx, float ty, int *value) {
    *value=0;
    int x,y,w,h; render_settings_bounds(&x,&y,&w,&h);
    if (g_settings_about) return hit_rect(tx,ty,x+(w-112)/2-8,y+h-52,128,56)?SET_HIT_ABOUT_BACK:SET_TAP_NONE;
    if (g_settings_themes) {
        for (int i=0;i<THEME_COUNT;i++) { int ax,ay,aw,ah; render_theme_tile_rect(i,&ax,&ay,&aw,&ah); if (hit_rect(tx,ty,ax,ay,aw,ah)) { *value=i; return SET_HIT_THEME_PICK; } }
        return hit_rect(tx,ty,x+(w-112)/2,y+h-44,112,44)?SET_HIT_THEME_BACK:SET_TAP_NONE;
    }
    return original_settings_tap(tx,ty,value);
}

int render_settings_touch(tank_t *t, float x, float y, bool down, int *value) {
    static bool s_down; static float s_px, s_py; static int s_hit, s_hv;
    render_use_theme(t->theme);
    int r = SET_TAP_NONE; *value = 0;
    if (down && !s_down) {                                  /* press */
        s_px = x; s_py = y;
        s_hit = render_settings_tap(x, y, &s_hv);
    } else if (!down && s_down) {                           /* release: a tap, if it stayed on what it pressed */
        int v = 0, h = render_settings_tap(x, y, &v);
        float dx = x - s_px, dy = y - s_py;
        if (h == s_hit && v == s_hv && dx * dx + dy * dy < 24 * 24) {
            if (h == SET_HIT_THEMES) { g_settings_themes = true; }
            else if (h == SET_HIT_THEME_BACK) { g_settings_themes = false; }
            else if (h == SET_HIT_THEME_PICK) {
                if (t->theme != v) {
                    t->theme = (uint8_t)theme_valid(v);
                    render_use_theme(t->theme);
                    progression_settings_changed();
                    progression_save(t); /* theme selections survive an immediate restart */
                    tank_emit(TEV_WHEEL_TICK, -1);
                }
                r = SET_TAP_THEME; *value = t->theme;
            }
            else if (h == SET_HIT_ABOUT) { g_settings_about = true; r = SET_TAP_ABOUT; *value = 1; }
            else if (h == SET_HIT_RESET) { r = SET_TAP_RESET; *value = 0; }
            else if (h == SET_HIT_ABOUT_BACK) { g_settings_about = false; r = SET_TAP_ABOUT; *value = 0; }
            else if (h == SET_HIT_BRIGHT_PREV || h == SET_HIT_BRIGHT_NEXT) {   /* one step either way; the ends hold */
                int bi = bright_index(g_settings_bright) + (h == SET_HIT_BRIGHT_NEXT ? 1 : -1);
                if (bi >= 0 && bi < SET_BRIGHT_N) { *value = g_settings_bright = SET_BRIGHT_PCT[bi]; r = SET_TAP_BRIGHT; tank_emit(TEV_WHEEL_TICK, -1); }
            }
            else if (h == SET_HIT_TOGGLE_FEED) {
                t->autofeed_off = !t->autofeed_off; progression_settings_changed();
                r = SET_TAP_FEED; *value = !t->autofeed_off;
            }
            else if (h == SET_HIT_TOGGLE_SCREEN) {
                tank_screen_set(t, !t->screen_turned); progression_settings_changed();
                r = SET_TAP_SCREEN; *value = t->screen_turned;
            }
            else
            if (h == SET_HIT_LIGHT_PREV || h == SET_HIT_LIGHT_NEXT) {   /* one choice along; either way the light comes on */
                int was = tank_light_choice(t), now = was + (h == SET_HIT_LIGHT_NEXT ? 1 : -1);
                if (now >= 0 && now <= LIGHT_IDLE_N) {
                    tank_light_choice_set(t, now); tank_emit(TEV_WHEEL_TICK, -1); progression_settings_changed();
                    if ((was == 0) != (now == 0)) { r = SET_TAP_LIGHT; *value = now != 0; }
                    else { r = SET_TAP_IDLE; *value = t->light_idle_s; }
                }
            } else if (h == SET_TAP_FEED) {                    /* ON (1, the default) / OFF */
                if ((v == 0) != t->autofeed_off) { t->autofeed_off = v == 0; progression_settings_changed(); }
                r = SET_TAP_FEED; *value = v;
            } else if (h == SET_TAP_ROTATE) {                  /* lock the way up it has now, or let it turn again */
                tank_orient_lock(t, !t->orient_lock); progression_settings_changed();
                r = SET_TAP_ROTATE; *value = t->orient_lock;
            } else if (h == SET_TAP_SCREEN) {                  /* the way up: NORMAL (0, the default) / TURNED (1) */
                if ((v != 0) != t->screen_turned) { tank_screen_set(t, v != 0); progression_settings_changed(); }
                r = SET_TAP_SCREEN; *value = v;
            } else if (h == SET_TAP_CLOSE || h == SET_TAP_BRIGHT || h == SET_TAP_VOLUME || h == SET_TAP_UPDATES) { r = h; *value = v; }
        }
    }
    s_down = down;
    if (r == SET_TAP_CLOSE || r == SET_TAP_UPDATES || r == SET_TAP_RESET) { g_settings_themes = false; g_settings_about = false; g_settings_page = 0; }
    return r;
}

void render_use_theme(int id) {
    id = theme_valid(id);
    if (theme_active() == id) return;
    theme_activate(id);
    g_scene_dim = g_veg_row_dim = g_castle_dim = g_coral_dim = g_cl_dim = -1;
    g_vig_filled = false;
    g_coral_q=g_cl_q=-1; g_sng_heading=1e9f;
    memset(g_castle_mask,0,sizeof g_castle_mask);
    g_card_fish = -1; g_card_t = -1;
    g_primed_fb = NULL;
    ++g_scene_epoch;
}
