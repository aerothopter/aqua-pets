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


#include "creatures.inc"   /* every creature's art: the classic fish and the species */

#include "helper_shrimp.inc"   /* the shrimp (2026-09-29) */

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
    int a = (int)((d2 - 0.72f) * 220);
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
            if (half < tip) half = tip;
            int k = (int)sp; float fr = sp - k;
            float cx = xs[k] + (xs[k + 1] - xs[k]) * fr;
            src_t s; s.v = pal[y];                              /* opaque: only .v is read */
            veg_span(c, (int)(cx - half), (int)(cx + half), y, &s, final);
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
 * he is his own thing now, draw_frogman.) */
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

#include "helper_snail.inc"   /* the snail (2026-09-16) */
#include "helper_urchin.inc"   /* the urchin (2026-10-02) */

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

#include "decor_cluster.inc"   /* the reef cluster (2026-09-24) */

#include "decor_wreck.inc"   /* the shipwreck (2026-10-10, item 7) */
static void thick_line(ctx_t *c, float x0, float y0, float x1, float y1, float w, uint32_t rgb) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy) + 1e-3f, nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    float px[4] = { x0 + nx, x1 + nx, x1 - nx, x0 - nx }, py[4] = { y0 + ny, y1 + ny, y1 - ny, y0 - ny };
    fill_poly(c, px, py, 4, rgb);
}
#include "helper_frogman.inc"   /* the frogman (2026-10-10, item 8) */


#include "helper_sub.inc"   /* the submarine (2026-10-10 night, item 9) */

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
 * frame. The rectangle: a 14 px strip of speckled stones in four tones. A
 * floor above the frame's bottom (the bowl's): the same speckle in 2 px
 * grains over a bed that runs ~70 px down to the glass, pebbles scattered
 * through it, darkening with depth. */
static inline uint32_t floor_rgb(int x, int y) {
#if TANK_BOT < TANK_H                                    /* a floor above the frame's bottom (the bowl's): a deep bed of sand */
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
}
static void castle_plain(ctx_t *c, const tank_t *t, const dpos_t *p)   { draw_castle(c, p->x, 0, false); }
static void castle_dragged(ctx_t *c, const tank_t *t, const dpos_t *p) { draw_castle(c, p->x, 0, true); }
static void castle_back(dframe_t *f, const dpos_t *p, int pass) {
    if (pass == DECOR_Z_BACK && p->z == DECOR_Z_FRONT) g_veg_mask_cx = p->x;   /* the grass stops at its walls */
}
static void castle_front(dframe_t *f, const dpos_t *p, int pass) {
    if (pass != DECOR_Z_MIDDLE || p->z != DECOR_Z_FRONT) return;
    if (f->cached) for (int i = 0; i < f->dr->n; i++)
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
    fill_ellipse(c, ax - 6, ay + 1, 7, 4, 0x2a3634, 255);
    fill_ellipse(c, ax + 5, ay + 2, 6, 3.5f, 0x22302c, 255);
    fill_ellipse(c, ax, ay - 2, 6, 3.5f, 0x3a4a48, 255);
    fill_ellipse(c, ax - 1, ay - 3, 2, 1.2f, 0x5a6a68, 200);
    dyn_add(dr, (int)ax - 14, (int)ay - 7, (int)ax + 12, (int)ay + 7);
}
static void draw_bubble(ctx_t *c, const bubble_t *b, dyn_t *dr) {
    float r = b->column ? 2.6f : 1.8f;
    fill_ellipse(c, b->x, b->y, r, r, 0x9fd8e2, 60);
    px_blend(c, (int)(b->x - r * 0.4f), (int)(b->y - r * 0.4f), 0xffffff, 120);
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

/* ---- the helpers (helper.h): never in the scene, all drawn live. Each frame
 * every owned one is asked, in kind order, in each of four slots: under the
 * creatures, over them (under the front fronds), over the decor IN FRONT, and
 * on the glass over the film. Each answers for itself in each slot - the snail
 * walks the floor in either lane and grazes the glass. ---- */
enum { HS_UNDER, HS_OVER, HS_FRONT, HS_GLASS };
static void snail_paint(dframe_t *f, int slot) {
    const tank_t *t = f->t;
    /* on the floor (upright): with the fish, under the front fronds - unless it walks
       its front lane (snail.front), drawn over the pieces IN FRONT (its rect is marked here) */
    if (slot == HS_OVER && tank_snail_upright(t)) {
        if (!t->snail.front) draw_snail(f->c, t, true);
        dyn_add(f->dr, (int)t->snail.x - 21, (int)t->snail.y - 13, (int)t->snail.x + 21, (int)t->snail.y + 7);
    }
    if (slot == HS_FRONT && tank_snail_upright(t) && t->snail.front) draw_snail(f->c, t, true);
    if (slot == HS_GLASS) draw_snail(f->c, t, false);     /* on the glass, over the film */
}
static void urchin_paint(dframe_t *f, int slot) {
    const tank_t *t = f->t;
    if (slot != HS_OVER || t->urchin.x < 0) return;       /* with the fish, under the front fronds - at a bed's foot the grass weaves over it */
    draw_urchin(f->c, t);
    dyn_add(f->dr, (int)t->urchin.x - 19, (int)URCHIN_FLOOR_Y - 17, (int)t->urchin.x + 19, (int)(URCHIN_FLOOR_Y + UR_BASE) + 1);
}
static void shrimp_paint(dframe_t *f, int slot) {
    if (slot != HS_UNDER) return;                         /* on the sand and in the grass, under the fish and the front fronds (the grass is their cover) */
    const tank_t *t = f->t;
    src_t pal[SHRIMP_TONES];
    for (int k = 0; k < SHRIMP_TONES; k++) pal[k] = src_color(SHRIMP_RGB[k], f->c->dim);
    for (int i = 0; i < t->school.n; i++) {
        draw_shrimp(f->c, &t->school.q[i], pal);
        dyn_add(f->dr, (int)t->school.q[i].x - 10, (int)t->school.q[i].y - 5, (int)t->school.q[i].x + 10, (int)t->school.q[i].y + 5);
    }
}
static void frogman_paint(dframe_t *f, int slot) {        /* (2026-10-10) drifting sideways in mid-water, under the fish */
    if (slot != HS_UNDER) return;
    int x0, y0, x1, y1; draw_frogman(f->c, f->t, &x0, &y0, &x1, &y1); dyn_add(f->dr, x0, y0, x1, y1);
}
static void sub_paint(dframe_t *f, int slot) {            /* (2026-10-10 night) cruising the water, under the fish */
    if (slot != HS_UNDER) return;
    int x0, y0, x1, y1; draw_submarine(f->c, f->t, &x0, &y0, &x1, &y1); dyn_add(f->dr, x0, y0, x1, y1);
}
typedef struct {
    void (*paint)(dframe_t *f, int slot);
    int  card;                                            /* the RENDER_CARD_* a tap on it opens (0 = none) */
    void (*card_draw)(ctx_t *c, const tank_t *t);
} helper_draw_t;
static void snail_card_draw(ctx_t *c, const tank_t *t);
static void urchin_card_draw(ctx_t *c, const tank_t *t);
static void shrimp_card_draw(ctx_t *c, const tank_t *t);
static const helper_draw_t HELPER_DRAW[HK_COUNT] = {
    [HK_SNAIL]   = { snail_paint,   RENDER_CARD_SNAIL,  snail_card_draw },
    [HK_URCHIN]  = { urchin_paint,  RENDER_CARD_URCHIN, urchin_card_draw },
    [HK_SHRIMP]  = { shrimp_paint,  RENDER_CARD_SHRIMP, shrimp_card_draw },
    [HK_FROGMAN] = { frogman_paint, 0, NULL },
    [HK_SUB]     = { sub_paint,     0, NULL },
};
static void helper_pass(dframe_t *f, int slot) {
    for (int k = 0; k < HK_COUNT; k++)
        if (HELPER_DRAW[k].paint && helper_owned(f->t, k)) HELPER_DRAW[k].paint(f, slot);
}
static int helper_of_card(int card) {
    if (card <= 0) return -1;
    for (int k = 0; k < HK_COUNT; k++) if (HELPER_DRAW[k].card == card) return k;
    return -1;
}
bool render_card_is_helper(const tank_t *t, int sel) { int k = helper_of_card(sel); return k >= 0 && helper_owned(t, k); }
int render_helper_tap(tank_t *t, float x, float y, int *sel) {
    int k = helper_hit(t, x, y);
    if (k < 0) return -1;
    int card = HELPER_DRAW[k].card, act = HELPER[k].tap ? HELPER[k].tap(t, x, y) : 1;
    if (act > 0) *sel = *sel == card ? -1 : card;
    else if (act < 0) *sel = -1;
    return k;
}

static void draw_scene(const tank_t *t, uint16_t *fb, int stride, float dim) {
    ctx_t c = ctx_full(fb, stride, dim);
    /* water gradient #0a3c46 → #08272f → #031015 */
    for (int y = 0; y < TANK_H; y++) {
        uint16_t v = rgb565(water_rgb(y), dim);
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = v;
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
            fill_ellipse(&c, t->food[i].x, t->food[i].y, 2.6f, 2.6f, 0xffbd59, 255);
            fill_ellipse(&c, t->food[i].x - 0.8f, t->food[i].y - 0.8f, 1.0f, 1.0f, 0xffe9bd, 255);
            DYN_RECT((int)t->food[i].x - 5, (int)t->food[i].y - 5, (int)t->food[i].x + 5, (int)t->food[i].y + 5);
        }
    /* bubbles: the drifting ones, and the column's behind the creatures (the airstone's depth: airstone_front) */
    for (int i = 0; i < MAX_BUBBLE; i++)
        if (!airstone_bubble_front(t, i)) draw_bubble(&c, &t->bubble[i], &dr);
    helper_pass(&df, HS_UNDER);                    /* the shrimp school, the frogman, the sub */
    PROF_ADD(3, p0);
    /* fish */
    for (int i = 0; i < t->n_fish; i++) {
        g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -1;
        draw_fish(&c, t, &t->fish[i]);
        g_bb_on = false;
        if (g_bb_x1 >= g_bb_x0) DYN_RECT(g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1);
    }
    helper_pass(&df, HS_OVER);                     /* the snail on the floor, the urchin */
    /* the decor over the creatures: the castle's front row (a fish in the arch
       swims THROUGH), the front fronds of the woven beds (each span applies its
       own vignette and untags itself, so the fish rects below skip it), then
       the pieces IN FRONT over everything - baked in the scene, each comes back
       only over what was drawn on it this frame; otherwise whole */
    decor_pass(&df, pos, PAINT_FRONT, N_OF(PAINT_FRONT), true, DECOR_Z_MIDDLE);
    decor_pass(&df, pos, PAINT_FRONT, N_OF(PAINT_FRONT), true, DECOR_Z_FRONT);
    helper_pass(&df, HS_FRONT);                    /* the snail's front lane */
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
                        int a = (int)((d2 - 0.72f) * 220);
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
    helper_pass(&df, HS_GLASS);                  /* the snail on the glass, over the film */
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
    if (t->snail.x < 0) return;
    ring(c, t->snail.x, t->snail.y, 20, 0x9fd8e2);
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
    if (t->snail.grazed <= 0) snprintf(n, sizeof n, "NOTHING YET");
    else snprintf(n, sizeof n, "%d SPOT%s", (int)t->snail.grazed, t->snail.grazed == 1 ? "" : "S");
    draw_text(c, X + (W - text_w(n, 3)) / 2, Y + 134, 3, 0xffffff, n);
}

/* the urchin's card (2026-10-02), the snail's: a ring on it, the urchin
   at 3x with its spines waving, SEA URCHIN, the grass it has eaten so far
   (the trim's centimeters) and what it is up to - in words, never colour. */
static void urchin_card_draw(ctx_t *c, const tank_t *t) {
    if (t->urchin.x < 0) return;
    ring(c, t->urchin.x, URCHIN_FLOOR_Y - 3, 22, 0x9fd8e2);
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
    char n[24]; int cm = (int)(t->urchin.grazed_px / PX_PER_CM);
    if (cm < 1) snprintf(n, sizeof n, "NOTHING YET");
    else if (cm < 1000) snprintf(n, sizeof n, "%d CM", cm);
    else snprintf(n, sizeof n, "%d.%dK CM", cm / 1000, cm % 1000 / 100);   /* 1.0K CM and up: the line never outgrows the card (0.3.2) */
    draw_text(c, X + (W - text_w(n, 3)) / 2, Y + 140, 3, 0xffffff, n);
    const char *now = tank_urchin_chewing(t) ? "CHEWING"
                    : t->urchin.frond >= 0 ? "OFF TO THE TALL GRASS"
                    : "RESTING";
    draw_text(c, X + (W - text_w(now, 2)) / 2, Y + 170, 2, 0x9fd8e2, now);
}

/* the shrimp school's card (2026-09-29, Strato: "one tap should bring up a
   card to show how much food they've eaten and progress to the next shrimp").
   The pips fill with pellets - filled vs hollow, never colour alone. */
static void shrimp_card_draw(ctx_t *c, const tank_t *t) {
    int n = t->school.n;
    if (n <= 0) return;
    float cx = 0, cy = 0, r = 0;
    for (int i = 0; i < n; i++) { cx += t->school.q[i].x; cy += t->school.q[i].y; }
    cx /= n; cy /= n;
    for (int i = 0; i < n; i++) { float d = tank_dist(cx, cy, t->school.q[i].x, t->school.q[i].y); if (d > r) r = d; }
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
    snprintf(line, sizeof line, "%d PELLET%s EATEN", (int)t->school.eaten, t->school.eaten == 1 ? "" : "S");
    draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 76, 2, 0x9fd8e2, line);
    const char *cap = "NEXT SHRIMP";
    draw_text(c, X + (W - text_w(cap, 2)) / 2, Y + 104, 2, 0x9fd8e2, cap);
    bool full = n >= SHRIMP_MAX;
    int have = full ? SHRIMP_PER_JOIN : t->school.food;
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
    else if (t->school.food >= SHRIMP_PER_JOIN && t->school.cool > 0) {
        int m = (int)ceilf(t->school.cool / 60.0f);
        snprintf(line, sizeof line, "ARRIVES IN %d MIN", m < 1 ? 1 : m);
    } else {
        int more = SHRIMP_PER_JOIN - t->school.food;
        snprintf(line, sizeof line, "%d MORE PELLET%s", more, more == 1 ? "" : "S");
    }
    draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 158, 2, 0xffffff, line);
}

void render_stats_card(const tank_t *t, int fish_idx, uint16_t *fb, int stride) {
    render_use_theme(t->theme);
    int hk = helper_of_card(fish_idx);
    if (hk >= 0) {                                   /* a helper's card (each says when it has nothing to show) */
        if (!helper_owned(t, hk)) return;
        ctx_t sc = ctx_full(fb, stride, 1.0f);
        HELPER_DRAW[hk].card_draw(&sc, t);
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

/* the chip: the tool at 1x and DONE, top left where the card would be (TOOL_CHIP_X / _Y: the board's) */
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
/* (the page's numbers, MSP_*: render.h, and the board's in its layout header -
 * the bowl's rows start where the circle is wide enough for them) */
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
    if (y >= MSP_CLOSE_Y - 4) return x >= MSP_CLOSE_X - MSP_CLOSE_SLOP ? MS_TAP_CLOSE : MS_TAP_NONE;   /* the foot: CLOSE, slop out to the glass edge */
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
                        draw_text(&c, SHP_BTN_X + (SHP_BTN_W - text_w("UNLOCK", 2)) / 2, top + (SHP_BTN_H - 14) / 2, 2, MSP_INK, "UNLOCK"); }
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
    } else if (owned && helper_kind_of_item(g_shp_modal) >= 0) {   /* a helper (2026-10-22): SELL alone, where UNLOCK was - tapped twice, it sells it back */
        char sell[24]; snprintf(sell, sizeof sell, "SELLS BACK FOR %d", progression_sell_value(g_shp_modal));
        draw_text(&c, X + (W - text_w("IN THE TANK", 2)) / 2, Y + 164, 2, MSP_TEAL, "IN THE TANK");
        draw_text(&c, X + (W - text_w(sell, 2)) / 2, Y + 184, 2, MSP_DIM, sell);
        if (g_shp_sell_armed) { snprintf(sell, sizeof sell, "+%d OK?", progression_sell_value(g_shp_modal));
                                button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, MSP_TEAL, MSP_TEAL, sell, 2);
                                draw_text(&c, bx + (MSP_HOW_W - text_w(sell, 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_INK, sell); }
        else button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "SELL", 2);
    } else if (owned) draw_text(&c, X + (W - text_w("IN THE TANK", 2)) / 2, by + 8, 2, MSP_TEAL, "IN THE TANK");
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
        if (owned && helper_kind_of_item(item) >= 0) {           /* a helper: SELL alone, armed then confirmed */
            if (on_btn && !g_shp_sell_armed) { g_shp_sell_armed = true; return SHOP_TAP_KEPT; }
            g_shp_sell_armed = false; g_shp_modal = -1;
            return on_btn ? SHOP_TAP_SELL + item : SHOP_TAP_KEPT;
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
    const int W = 30 + text_w(txt, 2), H = 22, X = (TANK_W - W) / 2, Y = RENDER_TOAST_Y;
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
#if DEVICE_WORN
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
    draw_text(&c, (PAGE_W - text_w("SETTINGS", 3)) / 2, SET_TITLE_Y, 3, 0xffffff, "SETTINGS");
    int bi = bright_index(bright_pct);
    set_arrow_row(&c, SET_ROW1_Y, "BRIGHTNESS", SET_BRIGHT[bi], bi > 0, bi < SET_BRIGHT_N - 1);   /* < the percent > */
    set_row(&c, SET_ROW2_Y, "VOLUME", SET_VOLUME, 3, volume < 0 ? 0 : volume > 2 ? 2 : volume);
    draw_text(&c, SET_LABEL_X, SET_NOTE_Y, 2, MSP_DIM, "FISH ARE QUIET AT NIGHT");
    { int ch = tank_light_choice(t); set_arrow_row(&c, SET_ROW3_Y, "LIGHTS OUT", SET_LIGHT[ch], ch > 0, ch < LIGHT_IDLE_N); }   /* < the choice > */
    set_row(&c, SET_ROW4_Y, "AUTO FEED", SET_FEED, 2, t->autofeed_off ? 1 : 0);
#if DEVICE_WORN
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
    draw_text_8px(&c, SET_VER_X((int)strlen(ver) * 6 - 1), SET_VER_Y, MSP_DIM, ver);   /* where the board puts it (the watch: centred under the foot) */
    button(&c, SET_CLOSE_X, SET_FOOT_Y, SET_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", 2);
    /* UPDATES (2026-09-30, docs/OTA.md): bottom left; ABOUT and RESET (2026-10-10) between */
    button(&c, SET_UPD_X, SET_FOOT_Y, SET_UPD_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "UPDATES", 2);
    button(&c, SET_ABT_X, SET_ABT_Y, SET_ABT_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "ABOUT", 2);
    button(&c, SET_RST_X, SET_RST_Y, SET_RST_W, MSP_CLOSE_H, 0x1c2f36, 0xe0785a, "RESET", 2);   /* a warm edge: it starts the tank over (after the prompt) */
}
/* ---- the ABOUT page (2026-10-10): the game's name, who makes it, the release and the build,
 * a little fish crossing above them and bubbles rising; a jingle plays while it is up (the
 * platform's: SET_TAP_ABOUT). Inside the settings bounds on every board, in the theme's colours.
 * (Each theme had its own creature here: git tag multi-board-multi-theme.) */
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
    { fish_t f; memset(&f, 0, sizeof f); float k = t->clock;
      f.size = 0.95f; f.stage = STAGE_ADULT; f.color = 0x66caca; f.fin = 0x417b96; f.accent = 0x92dcaf; f.facing = 1;
      /* a fish crossing above the title, turning at the ends */
      float u = sinf(k * 0.45f);
      f.x = cx + u * (w / 2 - 44); f.y = y + 30 + sinf(k * 1.1f) * 3;
      f.yaw = f.yaw_tail = cosf(k * 0.45f) >= 0 ? 1 : -1; f.yaw *= fminf(1, fabsf(cosf(k * 0.45f)) * 3); f.yaw_tail = f.yaw;
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
enum { SET_HIT_LIGHT_PREV = 100, SET_HIT_LIGHT_NEXT, SET_HIT_PAGE, SET_HIT_BRIGHT_PREV, SET_HIT_BRIGHT_NEXT, SET_HIT_CYCLE_VOLUME, SET_HIT_TOGGLE_FEED, SET_HIT_TOGGLE_SCREEN,
       SET_HIT_ABOUT, SET_HIT_ABOUT_BACK, SET_HIT_RESET };   /* (2026-10-10) ABOUT, the about page's BACK, RESET */
static bool g_settings_about;
static int g_settings_page, g_settings_bright = 60, g_settings_volume = 2;
static int original_settings_tap(float x, float y, int *value) {
    x -= PAGE_X; y -= PAGE_Y;                    /* the page's own coordinates */
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
    if (y >= SET_SEG_Y(SET_ROW5_Y) - 12 && y < SET_ROW5_END) {
        if (two < 0) return SET_TAP_NONE;
#if DEVICE_WORN
        *value = two == 1; return SET_TAP_SCREEN;
#else
        *value = 0; return SET_TAP_ROTATE;       /* the button or its word: one toggle */
#endif
    }
    return SET_TAP_NONE;
}
/* Short pages inside an inscribed rectangle; all targets share these bounds. */
void render_settings_bounds(int *x, int *y, int *w, int *h) {
    *x = SET_BOUNDS_X; *y = SET_BOUNDS_Y; *w = SET_BOUNDS_W; *h = SET_BOUNDS_H;   /* the board's (its layout header) */
}
void render_settings_leave(void) { g_settings_about = false; g_settings_page = 0; }
static bool hit_rect(float x, float y, int bx, int by, int w, int h) {
    return x >= bx && y >= by && x < bx+w && y < by+h;
}
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume) {
    render_use_theme(t->theme);
    g_settings_bright = bright_pct;
    g_settings_volume = volume < 0 ? 0 : volume > 2 ? 2 : volume;
    if (g_settings_about) { render_about_page(t, fb, stride); return; }
    render_original_settings(t, fb, stride, bright_pct, volume);
}
int render_settings_tap(float tx, float ty, int *value) {
    *value=0;
    int x,y,w,h; render_settings_bounds(&x,&y,&w,&h);
    if (g_settings_about) return hit_rect(tx,ty,x+(w-112)/2-8,y+h-52,128,56)?SET_HIT_ABOUT_BACK:SET_TAP_NONE;
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
            if (h == SET_HIT_ABOUT) { g_settings_about = true; r = SET_TAP_ABOUT; *value = 1; }
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
    if (r == SET_TAP_CLOSE || r == SET_TAP_UPDATES || r == SET_TAP_RESET) { g_settings_about = false; g_settings_page = 0; }
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
