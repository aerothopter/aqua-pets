/* ---- every creature's art (2026-10-23, the creatures refactor: one file,
 * not one a species) ----
 * Included by render.c where the creatures are drawn, after the raster kit
 * (fill_ellipse, fill_poly, mix, ...). In order: the classic fish's body
 * (the turn, draw_fish_core), the species' sketch kit (sk_*), the themes'
 * looks for every species, each species' Original look (draw_seahorse ...
 * draw_lobster, the jellyfish), then draw_creature, which picks a species'
 * drawer, and draw_fish, the tank's way in. The model never sees any of it. */

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
static void draw_blackwater_fish(ctx_t *c,const fish_t *f,float clock,bool asleep);   /* the Blackwater looks, below */
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
/* Living Lagoon: native shaded geometry, no textures or per-frame allocation.
 * The light ramp is computed once per shape, then quantized to 24 RGB565 tones.
 * Existing transforms, damage tracking, night dimming and species state apply. */
static void lagoon_poly(ctx_t *c,const float *x,const float *y,int n,uint32_t rgb) {
    float top=1e6f,bottom=-1e6f;
    for(int i=0;i<n;i++){top=fminf(top,y[i]);bottom=fmaxf(bottom,y[i]);}
    src_t shade[24];
    for(int i=0;i<24;i++) {
        float q=i/23.f;
        shade[i]=src_color(q<.42f?mix(rgb,0xf0e8c9,.58f*(1-q/.42f)):
            mix(rgb,0x123b3d,(q-.42f)*.95f),c->dim);
    }
    int lo=(int)top<c->oy?c->oy:(int)top,hi=(int)bottom;
    if(hi>=c->oy+c->h)hi=c->oy+c->h-1;
    for(int yy=lo;yy<=hi;yy++) {
        float left=1e6f,right=-1e6f;
        for(int i=0;i<n;i++){int j=(i+1)%n;
            if((y[i]<=yy&&y[j]>yy)||(y[j]<=yy&&y[i]>yy)) {
                float xx=x[i]+(x[j]-x[i])*(yy-y[i])/(y[j]-y[i]);
                left=fminf(left,xx);right=fmaxf(right,xx);
            }
        }
        if(left>right)continue;
        float v=(yy-top)/fmaxf(1,bottom-top),mid=(left+right)*.5f;
        /* Nine spans per row give a rounded cross section without pixel shaders. */
        int start=(int)left,end=(int)right,width=end-start+1;
        for(int j=0;j<9;j++) {
            int x0=start+width*j/9,x1=start+width*(j+1)/9-1;
            if(x1<x0)continue;
            float u=((x0+x1)*.5f-mid)/fmaxf(1,(right-left)*.5f);
            float q=v*.65f+u*u*.28f+u*.08f;
            int s=(int)(fminf(1,fmaxf(0,q))*23);
            span(c,x0,x1,yy,&shade[s],255);
        }
    }
}
static void lagoon_shape(sk_t *k,const float *x,const float *y,int n,uint32_t rgb) {
    float X[20],Y[20];
    for(int i=0;i<n;i++)sk_pt(k,x[i],y[i],&X[i],&Y[i]);
    lagoon_poly(k->c,X,Y,n,rgb);
}
/* the body's thickness (2026-10-10): a shaded cross-section at (x, y), `depth` px deep and
   `ry` tall, that widens as the creature turns toward the glass (sqrt(1 - yaw^2), as the
   Original fish's FISH_THICK) - so a turn never folds a lagoon creature to a line. Before
   this only the ellipse bodies had one, a sliver a fifth of their width, and only head-on. */
static void lagoon_volume(sk_t *k,float x,float y,float depth,float ry,uint32_t rgb) {
    float w=fabsf(sk_yaw(k,x)),f=sqrtf(fmaxf(0,1-w*w));
    float RX=depth*f*k->s,RY=ry*k->s;if(RX<1)return;
    float X,Y;sk_pt(k,x,y,&X,&Y);
    float px[20],py[20];for(int i=0;i<20;i++){float a=i*TAU/20;px[i]=X+RX*fast_cos(a);py[i]=Y+RY*fast_sin(a);}
    lagoon_poly(k->c,px,py,20,rgb);
}
static void lagoon_body(sk_t *k,float x,float y,float rx,float ry,uint32_t rgb) {
    float X[20],Y[20];
    for(int i=0;i<20;i++){float a=i*TAU/20;X[i]=x+rx*fast_cos(a);Y[i]=y+ry*fast_sin(a);}
    lagoon_shape(k,X,Y,20,rgb);
    lagoon_volume(k,x,y,fminf(rx,ry)*.8f,ry*.95f,rgb);   /* a rounded body: as deep as it is tall, nearly */
}
static void lagoon_limb(sk_t *k,const float *x,const float *y,const float *r,int n,uint32_t rgb) {
    sk_tube(k,x,y,r,n,mix(rgb,0x123b3d,.35f));
    float yy[SK_TUBE_MAX],rr[SK_TUBE_MAX];
    for(int i=0;i<n;i++){yy[i]=y[i]-r[i]*.25f;rr[i]=r[i]*.65f;}
    sk_tube(k,x,yy,rr,n,mix(rgb,0xe2dec0,.25f));
}
static void lagoon_fin(sk_t *k,float x,float y,float tx,float top,float bottom) {
    sk_tri(k,x,y,tx,top,tx+1,bottom,mix(k->fin,0x436f6b,.35f),175);
    for(int j=0;j<5;j++)sk_line(k,x,y,tx+.6f,top+(bottom-top)*j/4,.23f,.14f,mix(k->fin,0xd8dabc,.35f));
}
static void lagoon_pattern(sk_t *k,float cx,float cy,float width,float height) {
    if(!k->marked)return;
    uint32_t rgb=mix(k->acc,k->body,.4f);
    for(int i=0;i<9;i++) {
        float x=cx+(i%3-1)*width,y=cy+(i/3-1)*height;
        sk_mark(k,x,y,k->f->variant==2?.7f:.4f,k->f->variant==1?1.2f:.5f,rgb,90*k->fade);
    }
}
static void draw_lagoon_species(sk_t *k,int species) {
    float beat=k->asleep?0:fast_sin(k->t*2.6f+k->f->wander);   /* a fixed rate (2026-10-12): t x (2 + speed x k) jumped the phase with every change of speed */
    uint32_t body=k->body;
    if(species==SP_OCTOPUS&&k->f->camo>0) {
        body=mix(body,k->f->camo_rgb,clamp01(k->f->camo));
        k->fade=1-clamp01(k->f->camo)*.8f;
    }
    switch(species) {
    case SP_FISH: {
        lagoon_fin(k,-12,0,-27,-8-beat*2,8+beat*2);
        if(k->f->stage>=STAGE_ADULT)lagoon_fin(k,4,-4,-3,k->f->stage==STAGE_ELDER?-16:-13,-4);
        lagoon_fin(k,5,4,1,7,13);
        float x[12]={17,14,7,-3,-12,-17,-13,-5,4,12,16,17};
        float y[12]={0,-4,-7,-8,-5,0,5,7,7,4,1,0};
        lagoon_shape(k,x,y,12,body);lagoon_volume(k,0,0,5.5f,7.2f,body);
        for(int i=0;i<5;i++)for(int j=0;j<3;j++)sk_mark(k,-8+i*3,j*3-3,.8f,.3f,0xe0dfbf,85);
        sk_line(k,9,-5,7,0,.35f,.3f,mix(body,0x123b3d,.5f));
        sk_line(k,7,0,10,5,.3f,.2f,mix(body,0x123b3d,.5f));
        lagoon_pattern(k,-2,0,3,2);lagoon_fin(k,3,1,-5,4,8);sk_eye(k,12,-2,1.15f,true);break;
    }
    case SP_SEAHORSE: {
        float x[10]={0,-3,-6,-7,-4,1,4,3,0,-1},y[10]={5,10,14,18,21,21,18,15,15,17},r[10]={3,3,2.7f,2.3f,2,1.8f,1.4f,1.2f,.9f,.4f};
        lagoon_limb(k,x,y,r,10,body);lagoon_fin(k,-4,-3,-10,-5+beat,5);
        lagoon_body(k,0,0,5.5f,10,body);lagoon_body(k,0,-13,5,5,body);
        sk_line(k,3,-13,11,-11,2,1.4f,body);sk_dot(k,11,-11,.5f,1,0x204743,220,false);
        sk_tri(k,-3,-16,-4,-21,1,-17,k->fin,220);
        for(int i=0;i<5;i++)sk_line(k,-2,-5+i*3,3,-6+i*3,.3f,.25f,mix(body,0xf0dec0,.35f));
        sk_eye(k,2,-15,1.1f,true);break;
    }
    case SP_OCTOPUS: case SP_SQUID: {
        bool squid=species==SP_SQUID;draw_ink(k);
        for(int i=0;i<(squid?6:8);i++) {
            float u=i-(squid?2.5f:3.5f),w=k->asleep?0:fast_sin(k->t*3+i*.8f);
            float x[5]={-2,-9,-15-k->pose*2,-20-k->pose*2,-19-k->pose*2+w};
            float y[5]={u,u*1.5f,u*(2.5f-k->pose)+w,u*(2-k->pose)+w*2,u*(1.5f-k->pose)+w};
            float r[5]={1.7f,1.35f,1,.65f,.3f};lagoon_limb(k,x,y,r,5,body);
            if(!squid)for(int j=1;j<4;j++)sk_dot(k,x[j],y[j]+.6f,.4f,.35f,0xd4d4b7,130,false);
        }
        if(squid) {
            lagoon_fin(k,15,0,-3,-12,0);lagoon_fin(k,15,0,-3,0,12);
            float x[8]={17,11,1,-6,-8,-6,1,11},y[8]={0,-5,-7,-5,0,5,7,5};lagoon_shape(k,x,y,8,body);lagoon_volume(k,3,0,5,6.3f,body);
        }else lagoon_body(k,3,-1,10,9.5f,body);
        lagoon_pattern(k,1,-1,2.5f,2);sk_eye(k,squid?-4:8,-2,1.4f,true);break;
    }
    case SP_PUFFER: {
        float puff=clamp01(k->f->puff),rx=11+4*puff,ry=8+7*puff;
        lagoon_fin(k,-7,0,-20,-5-beat,5+beat);
        for(int i=0;i<16;i++){float a=i*TAU/16;sk_line(k,fast_cos(a)*rx,fast_sin(a)*ry,fast_cos(a)*(rx+1+puff*3),fast_sin(a)*(ry+1+puff*3),.45f,.15f,k->fin);}
        lagoon_body(k,0,0,rx,ry,body);lagoon_pattern(k,-2,-1,4,3);
        for(int i=0;i<8;i++)sk_mark(k,-7+(i%4)*4,-4+(i/4)*7,.5f,.7f,0xd9d8b7,170);
        lagoon_fin(k,-1,3,-7,4,10);sk_eye(k,8,-3,1.6f,true);sk_dot(k,rx,2,1,.7f,0x2a5149,255,false);break;
    }
    case SP_ANGLER: {
        lagoon_fin(k,-10,1,-23,-6,7);lagoon_fin(k,4,-5,-5,-14,-4);
        lagoon_body(k,0,1,13,10,body);lagoon_pattern(k,-4,-1,3,2);
        float x[5]={0,0,3,9,13},y[5]={-7,-15,-18,-18,-13+beat},r[5]={.8f,.7f,.6f,.5f,.3f};lagoon_limb(k,x,y,r,5,k->fin);
        sk_dot(k,13,-13+beat,3,3,0xd6dfac,35+(int)(90*k->f->lure),false);
        sk_dot(k,13,-13+beat,1.3f,1.3f,0xfff0bd,255,false);
        sk_dot(k,10,5,3,3,0x183c3c,255,true);
        for(int i=0;i<3;i++)sk_line(k,8+i*1.4f,3,8.3f+i*1.4f,4.5f,.2f,.1f,0xddddbc);
        sk_eye(k,8,-3,1.3f,true);break;
    }
    case SP_EEL: {
        float x[16],y[16],r[16];for(int i=0;i<16;i++){float u=i/15.f;x[i]=23-u*60;y[i]=fast_sin(u*7-k->t*(k->asleep?.5f:3))*u*6;r[i]=4.1f*(1-u)+.4f;}
        lagoon_limb(k,x,y,r,16,body);lagoon_body(k,21,0,6,3.8f,body);sk_eye(k,24,-1,1,true);
        for(int i=2;i<13;i+=2)sk_mark(k,x[i],y[i],.4f,.8f,k->acc,120);
        if(k->f->spark>0)for(int j=0;j<4;j++){float xx=-25+j*12,sgn=j%2?1:-1,w=fast_sin(k->t*18+j)*2;sk_line(k,xx,sgn*5,xx+3+w,sgn*9,.5f,.35f,0xb4ebd5);sk_line(k,xx+3+w,sgn*9,xx+1,sgn*13,.35f,.2f,0xe4f8d6);}
        break;
    }
    case SP_SHARK: {
        lagoon_fin(k,-18,0,-32,-12-beat,8+beat);sk_tri(k,-10,-4,-3,-15,2,-4,k->fin,220);lagoon_fin(k,5,3,-7,7,14);
        float x[10]={16,11,-1,-15,-22,-15,-1,10,16,17},y[10]={-3,-5,-6,-3,0,3,5,4,2,0};lagoon_shape(k,x,y,10,body);lagoon_volume(k,-2,0,5.2f,5.2f,body);
        lagoon_body(k,15,0,3,9,body);sk_eye(k,16,-7,1,true);sk_eye(k,16,7,.8f,true);
        for(int i=0;i<3;i++)sk_line(k,8+i*1.3f,-2,7.5f+i*1.3f,2,.22f,.2f,mix(body,0x123b3d,.5f));
        break;
    }
    case SP_SWORDFISH: {
        float tw=k->asleep?0:fast_sin(k->f->jet*TAU)*2.2f;
        lagoon_fin(k,-19,0,-31,-12+tw,12+tw*.7f);
        lagoon_fin(k,5,-4,-1,-18,-4);lagoon_fin(k,-14,-2.5f,-18,-6.5f,-2);lagoon_fin(k,-13,2.5f,-17,2,6);
        float x[12]={16,12,4,-6,-14,-20,-21.5f,-20,-14,-6,4,12},y[12]={-1.6f,-4.2f,-5.6f,-5.3f,-3.6f,-1.8f,0,1.8f,3.4f,4.8f,4.9f,3.4f};
        lagoon_shape(k,x,y,12,body);lagoon_volume(k,0,0,4.6f,5.2f,body);
        for(int i=0;i<6;i++)for(int j=0;j<2;j++)sk_mark(k,-12+i*4,j*3-1,.8f,.3f,0xe0dfbf,70);
        sk_line(k,12,-.3f,-16,.4f,.3f,.25f,mix(body,0x123b3d,.5f));
        lagoon_fin(k,5,3,-3,4,10);
        sk_line(k,15.5f,-1,33,-.4f,1.1f,.25f,mix(k->fin,0x123b3d,.45f));
        sk_line(k,16,-1.6f,24,-1.3f,.3f,.2f,mix(k->acc,0xf0e8c9,.4f));
        sk_eye(k,10.5f,-1.8f,1.4f,true);break;
    }
    case SP_TURTLE: {   /* 2026-10-11: a shaded dome of a shell, paddles in the skin colour, a pale plastron */
        float st=k->asleep?.6f:fast_sin(k->f->jet*TAU);int v=k->f->variant&3;
        uint32_t skin=k->fin,dark=mix(k->fin,0x123b3d,.4f);
        turtle_flipper(k,6.5f,-.5f,-st*.7f,1,dark,230);sk_tri(k,-12,-1,-15,0,-19,-4,dark,230);
        lagoon_shape(k,TURTLE_CX,TURTLE_CY,10,body);
        sk_line(k,10.5f,-1.5f,-14.5f,-3.5f,.3f,.3f,mix(body,0x123b3d,.5f));
        sk_dot(k,-2,3.2f,12,2.2f,mix(k->acc,0xe2dec0,.3f),255,true);
        turtle_volume(k,body,mix(k->acc,0xe2dec0,.3f),skin,mix(body,0x123b3d,.5f),0xe2dec0);
        if(fabsf(sk_yaw(k,-2))<.45f)v=-1;
        if(v==0){for(int i=0;i<4;i++){float x=6-i*5.5f;sk_line(k,x,-7,x,1,.3f,.3f,mix(body,0x123b3d,.5f));}sk_line(k,9,-4,-13,-5,.3f,.3f,mix(body,0x123b3d,.5f));}
        else if(v==1)for(int i=0;i<5;i++)sk_mark(k,6-i*4.5f,-4.5f+(i&1)*1.5f,1.8f,.7f,mix(k->fin,0x123b3d,.3f),230);
        else if(v==2)for(int i=0;i<3;i++)sk_line(k,9,-6.5f+i*2.4f,-13,-6+i*2.4f,.3f,.3f,mix(k->acc,body,.4f));
        else if(v==3)lagoon_pattern(k,-2,-3,5,2.5f);
        sk_tri(k,-14,0,-20,1.5f,-14,2.5f,skin,255);sk_tri(k,-11,2,-14,3,-18,7.5f,skin,255);
        lagoon_body(k,12,.5f,3,2.2f,skin);lagoon_body(k,16.5f,-.3f,3.6f,3,skin);
        sk_tri(k,19.5f,-1.2f,21.5f,.2f,19.5f,1.4f,dark,255);
        sk_eye(k,17.3f,-1.2f,1.2f,true);
        turtle_flipper(k,6.5f,1.5f,st,1,skin,255);turtle_gulp(k,21,-1);break;
    }
    case SP_CRAB: case SP_LOBSTER: {
        bool lobster=species==SP_LOBSTER;
        /* The crab's broad carapace remains visible when it sidles head-on. */
        sk_t crab=*k;if(!lobster){crab.yh=crab.yh<0?-1:1;crab.yt=crab.yh;k=&crab;}
        if(lobster){for(int i=5;i>=0;i--)lagoon_body(k,-8-i*3,1+beat*i*.2f,3.8f,3.4f-i*.3f,body);lagoon_fin(k,-22,1,-28,-4,6);}
        for(int side=-1;side<=1;side+=2) {
            for(int i=0;i<4;i++){float x[3]={-5+i*3,-10+i*5,-12+i*6},y[3]={side*4,side*(8+i)+beat,side*(11+i)+beat},r[3]={.8f,.55f,.25f};lagoon_limb(k,x,y,r,3,k->fin);}
            float cy=side*(lobster?7:11)-k->f->puff*5;
            float x[3]={5,11,16},y[3]={side*3,cy,cy-1},r[3]={1.5f,1.2f,1};lagoon_limb(k,x,y,r,3,body);
            lagoon_body(k,17,cy-1,4,3,body);sk_line(k,19,cy-2,23,cy-4-beat*.5f,1.2f,.2f,body);sk_line(k,19,cy,23,cy+1+beat*.5f,1,.2f,body);
            if(lobster)sk_line(k,8,side*2,22,side*9,.3f,.12f,k->fin);
        }
        lagoon_body(k,0,0,lobster?10:9,lobster?5:8,body);lagoon_pattern(k,0,0,2.5f,2);
        for(int side=-1;side<=1;side+=2){sk_line(k,6,side*3,9,side*5,.6f,.4f,k->fin);sk_eye(k,9,side*5,.9f,true);}break;
    }
    }
}
static void draw_lagoon_fish(ctx_t *c,const fish_t *f,float clock,bool asleep) {
    sk_t k;sk_setup(&k,c,f,clock,asleep,-27,17,.61f,1);
    draw_lagoon_species(&k,SP_FISH);
}
/* Native vector art for the two modern themes. Included after the sk_* primitives.
 * Existing species motion/state and per-creature colours remain the inputs. */
static void draw_blackwater_species(sk_t *k,int species);   /* the Blackwater looks, after these */
static void theme_creature_pattern(sk_t *k,float x,float y,float spread) {
    if(!k->marked)return;
    bool club=theme_active()==THEME_TIDEPOOL_CLUB;
    for(int i=0;i<3;i++) {
        float xx=x+(i-1)*spread, yy=y+((k->f->variant&1)?(i%2?1.5f:-1.5f):0);
        float rx=club?1.8f:1.0f,ry=club?1.8f:3.0f;
        if(k->f->variant==2){rx*=1.4f;ry*=.6f;}
        if(k->f->variant==3){rx*=.65f;ry*=.65f;}
        sk_mark(k,xx,yy,rx,ry,k->acc,club?235:155);
    }
}
static void draw_theme_species(sk_t *k,int species) {
    if(theme_active()==THEME_QUIET_LAGOON){draw_lagoon_species(k,species);return;}
    if(theme_active()==THEME_BLACKWATER){draw_blackwater_species(k,species);return;}
    bool club=theme_active()==THEME_TIDEPOOL_CLUB;
    float t=k->t, beat=sinf(t*3+k->f->wander), round=club?1.15f:1;
    uint32_t belly=mix(k->body,0xffefd1,club?.42f:.26f);
    float eye=club?2:1.4f;
    switch(species) {
    case SP_SEAHORSE: {
        float x[9]={0,-4,-7,-6,-2,1,0,-3,-4},y[9]={3,10,16,21,22,19,17,17,19},r[9]={4,3.5f,3,2.5f,2,1.7f,1.2f,1,.6f};
        sk_tube(k,x,y,r,9,k->body);
        sk_dot(k,-1,0,6*round,10,k->body,255,false);
        sk_dot(k,0,-12,6*round,6,k->body,255,false);
        sk_line(k,3,-11,11,-9,club?3:2,2,k->body);
        sk_dot(k,-1,2,3,6,belly,255,true);
        sk_tri(k,-5,-3,-11,-6+beat,-7,6,k->fin,210);
        sk_dot(k,-2,-18,club?3:1.5f,2,k->fin,255,false);
        theme_creature_pattern(k,-1,-1,2);sk_eye(k,3,-14,eye,false);break;
    }
    case SP_OCTOPUS:
    case SP_SQUID: {
        bool squid=species==SP_SQUID;draw_ink(k);
        float jet=k->pose;
        for(int i=0;i<(squid?6:8);i++) {
            float u=(i-(squid?2.5f:3.5f)),wave=sinf(t*3+i*.8f)*(k->asleep?.3f:1);
            float x[4]={-3+u*.6f,-9-jet*2,-16-jet*3,-20-jet*3+wave*2};
            float y[4]={u, u*2+wave,u*(2.5f-jet)+wave*2,u*(2-jet)+wave*3};
            float r[4]={club?2:1.4f,1.4f,1,.5f};sk_tube(k,x,y,r,4,k->body);
        }
        if(squid){sk_tri(k,13,0,1,-13*round,-7,0,k->fin,230);sk_tri(k,13,0,1,13*round,-7,0,k->fin,230);}
        sk_dot(k,3,0,squid?12:10,squid?6:10*round,k->body,255,false);
        sk_dot(k,4,3,6,squid?2:4,belly,240,true);
        theme_creature_pattern(k,0,-2,3);sk_eye(k,9,-2,eye,false);break;
    }
    case SP_PUFFER: {
        float puff=clamp01(k->f->puff),r=10+5*puff;
        for(int i=0;i<12;i++){float a=i*TAU/12;sk_line(k,cosf(a)*r,sinf(a)*r,cosf(a)*(r+1+puff*3),sinf(a)*(r+1+puff*3),.7f,.4f,k->fin);}
        sk_tri(k,-8,0,-19,-5-beat,-19,5+beat,k->fin,255);
        sk_dot(k,0,0,r,r*(club?1:.9f),k->body,255,false);
        sk_dot(k,1,4,r*.7f,r*.45f,belly,255,true);
        theme_creature_pattern(k,-2,-3,4);sk_eye(k,7,-3,eye+1,false);
        sk_dot(k,11,2,2,1.3f,k->acc,255,false);break;
    }
    case SP_ANGLER: {
        sk_tri(k,-10,1,-22,-6,-22,7,k->fin,255);
        sk_dot(k,0,0,13,club?12:9,k->body,255,false);
        sk_dot(k,4,4,9,4,belly,230,true);
        float x[4]={0,1,9,14},y[4]={-7,-17,-19,-15+beat},r[4]={1.4f,1,1,.6f};sk_tube(k,x,y,r,4,k->fin);
        sk_dot(k,14,-15+beat,club?3:2,club?3:2,0xffe9ad,255,false);
        sk_eye(k,7,-4,eye,false);theme_creature_pattern(k,-5,0,3);break;
    }
    case SP_EEL: {
        float x[12],y[12],r[12];
        for(int i=0;i<12;i++){float u=i/11.f;x[i]=20-u*59;y[i]=sinf(u*6-t*4)*u*6;r[i]=(club?5:3.8f)*(1-u)+.7f;}
        sk_tube(k,x,y,r,12,k->body);
        for(int i=2;i<10;i+=2)sk_mark(k,x[i],y[i],club?2:1,club?2:2.5f,k->acc,210);
        sk_dot(k,18,0,7,club?5:4,k->body,255,false);sk_eye(k,21,-1,eye,true);
        if(k->f->spark>0)for(int j=0;j<4;j++) {
            float xx=-27+j*12,side=j%2?1:-1,w=sinf(t*15+j)*2;
            uint32_t electric=club?0xd5a137:0xb4ebd5;
            if(club){sk_line(k,xx,side*5,xx+4,side*9,1,1,electric);sk_line(k,xx+4,side*9,xx-1+w,side*12,1,1,electric);sk_line(k,xx-1+w,side*12,xx+4,side*16,.8f,.5f,electric);}
            else{sk_line(k,xx,side*5,xx+3+w,side*9,.6f,.6f,electric);sk_dot(k,xx+3+w,side*11,1,1,0xe4f8c5,220,false);}
        }
        break;
    }
    case SP_SHARK: {
        float x[10]={19,12,-2,-15,-22,-17,-9,1,14,19},y[10]={0,-6,-7,-4,0,4,6,7,4,0};
        sk_poly(k,x,y,10,k->body,255);
        sk_tri(k,-17,0,-30,-11-beat,-26,8+beat,k->fin,255);
        sk_tri(k,0,-4,-5,club?-15:-12,-11,-4,k->fin,255);
        sk_dot(k,1,3,10,2.7f,belly,255,true);
        sk_line(k,15,-7*round,15,7*round,club?3.5f:2.4f,club?3.5f:2.4f,k->body);
        sk_eye(k,17,-6*round,eye,true);theme_creature_pattern(k,-4,0,3);break;
    }
    case SP_SWORDFISH: {
        float tw=sinf(k->f->jet*TAU)*2.5f;
        float x[12]={16,12,4,-6,-14,-20,-21.5f,-20,-14,-6,4,12},y[12]={-2,-5*round,-6.5f*round,-6*round,-4,-2,0,2,4,5.5f*round,6*round,4*round};
        sk_poly(k,x,y,12,k->body,255);
        sk_tri(k,-19,0,-31,-12+tw,-24,0,k->fin,255);sk_tri(k,-19,0,-31,12+tw*.7f,-24,0,k->fin,255);
        sk_tri(k,5,-5,-2,club?-19:-17,-11,-4,k->fin,255);sk_tri(k,-14,-3,-18,-7,-18.5f,-2,k->fin,255);
        sk_dot(k,-2,3,12,club?3:2.5f,belly,255,true);
        theme_creature_pattern(k,-4,-1,5);
        sk_tri(k,6,3,-3,club?11:9,2,4.5f,k->fin,255);
        sk_line(k,15.5f,-1,club?31:33,-.4f,club?1.6f:1.2f,club?.6f:.3f,mix(k->fin,0x000000,.3f));
        sk_eye(k,10.5f,-2,eye,false);break;
    }
    case SP_TURTLE: {   /* 2026-10-11: a fat round shell, stubby paddles, a big-eyed head */
        float st=k->asleep?.6f:sinf(k->f->jet*TAU);int v=k->f->variant&3;
        uint32_t skin=k->fin,dark=mix(k->fin,0x000000,.3f);
        turtle_flipper(k,6,0,-st*.7f,1,dark,255);sk_tri(k,-11,-1,-14,0,-18*round,-4,dark,255);
        sk_dot(k,-2,-2,13*round*fmaxf(fabsf(sk_yaw(k,-2)),.08f),7*round,k->body,255,false);   /* the shell, narrowing with the turn (turtle_volume takes over) */
        sk_dot(k,-2,3.5f,11.5f,2.6f*round,belly,255,true);
        turtle_volume(k,k->body,belly,skin,mix(k->body,0x000000,.3f),0xffefd1);
        if(fabsf(sk_yaw(k,-2))<.45f)v=-1;
        if(v==0){for(int i=0;i<3;i++)sk_dot(k,5-i*6.5f,-3,2.4f*round,2.4f*round,mix(k->body,0x000000,.22f),255,true);}
        else if(v==2){for(int i=0;i<3;i++)sk_line(k,9,-6+i*2.6f,-13,-5.5f+i*2.6f,club?.6f:.4f,club?.6f:.4f,mix(k->acc,k->body,.5f));}
        else if(v==3)theme_creature_pattern(k,-2,-3,5);
        sk_tri(k,-14,0,-20,1.5f,-14,3,skin,255);sk_tri(k,-11,2,-14,3,-18,7.5f,skin,255);
        sk_dot(k,12,.5f,3,2.4f,skin,255,false);sk_dot(k,16.5f,-.3f,4*round,3.4f*round,skin,255,false);
        sk_tri(k,20,-1.2f,22,.2f,20,1.4f,dark,255);
        sk_eye(k,17.3f,-1.4f,eye,false);
        turtle_flipper(k,6,2,st,1,skin,255);turtle_gulp(k,21.5f,-1);break;
    }
    case SP_CRAB:
    case SP_LOBSTER: {
        bool lobster=species==SP_LOBSTER;float raise=clamp01(k->f->puff)*8;
        if(lobster)for(int i=5;i>=0;i--)sk_dot(k,-8-i*3,1+sinf(t*2-i*.5f),4,3-i*.25f,i%2?k->fin:k->body,255,false);
        for(int side=-1;side<=1;side+=2){
            for(int i=0;i<3;i++) {
                float y=side*(4+i*2),w=sinf(t*4+i)*1.5f;
                sk_line(k,-4+i*4,y,-9+i*6,y+side*6+w,1.1f,.6f,k->fin);
            }
            float cy=side*(lobster?6:12)-raise;
            sk_line(k,6,side*3,15,cy,1.8f,1.3f,k->body);
            sk_dot(k,17,cy,club?5:4,club?4:3,k->body,255,false);
            sk_line(k,19,cy-1,23,cy-3-beat*.5f,1.2f,.5f,k->acc);
            sk_line(k,19,cy+1,23,cy+3+beat*.5f,1.2f,.5f,k->acc);
        }
        sk_dot(k,0,0,lobster?10:9,lobster?5:8*round,k->body,255,false);
        theme_creature_pattern(k,0,0,3);
        sk_line(k,5,-4,8,-7,1,.7f,k->fin);sk_eye(k,8,-7,eye,true);
        if(!lobster)sk_eye(k,8,4,eye,true);
        break;
    }
    default:break;
    }
}
/* Blackwater (2026-10-12, the third design - Alvin: "redesign it to be better than the rest of the
 * themes ... aesthetically pleasing like a polished commercial game"): the ABYSS. The realistic
 * anatomy of 2026-10-10 (the same skeleton, polygons and tubes as the lagoon's shaded art: a
 * fusiform fish with rayed, translucent fins, a hammerhead's cephalofoil, an octopus's suckers)
 * lit as deep-sea footage is: a cold moonlight from above rims every back in pale cyan, the
 * flank keeps the keeper's colour, the belly fades into the dark, and every creature is BIO-
 * LUMINESCENT - a soft aura of its own accent colour around it and a row of photophores along
 * the flank that pulse slowly, both drawn undimmed so the tank is a field of drifting lights at
 * night. Cost as the lagoon's: a light ramp once a shape, nine spans a row, four to six alpha
 * ellipses for the aura, a handful of dots; no per-pixel trig, no allocation.
 * THE SWIM (2026-10-12, Alvin: "movement not smooth and natural"): every beat runs on a phase that
 * tank.c integrates (f->jet) or on the plain clock - never clock x (rate + speed), which jumped
 * the phase at every change of speed - and the fish, the shark and the swordfish bend: a lateral
 * wave runs down the body (bw_wave), nothing at the head, the trunk's rear swinging a little, the
 * caudal whipping a beat behind it, the fins on the body riding the wave with it. */
#define BW_DARK 0x06121c   /* the shade: the water's own dark */
#define BW_SUN  0xe9f6ff   /* the light: cool, from above */
#define BW_PALE 0xdfe1d6   /* a belly's silver */
#define BW_MOON 0xcdf3ff   /* the cold light from above: the rim along every back */
#define BW_JELLY_CENTRE 6.0f   /* as the jellyfish's JELLY_CENTRE (defined below) */

/* a countershaded, rim-darkened fill of a convex polygon in screen space: 16 tones top to bottom
   (dorsal rim, the shine, the flank, the silver belly, the ventral shadow), the outer spans of
   each row pulled toward the dark so the silhouette rounds off */
static void bw_poly(ctx_t *c,const float *x,const float *y,int n,uint32_t rgb) {
    float top=1e6f,bottom=-1e6f;
    for(int i=0;i<n;i++){top=fminf(top,y[i]);bottom=fmaxf(bottom,y[i]);}
    src_t lit[16],edge[16],rim[16];
    for(int i=0;i<16;i++) {
        float q=i/15.f;uint32_t t;
        if(q<.08f)t=mix(rgb,BW_MOON,.62f*(1-q/.08f)+.1f);                 /* the moonlit rim along the back */
        else if(q<.3f)t=mix(rgb,BW_SUN,.34f*(q-.08f)/.22f);
        else if(q<.55f)t=mix(rgb,BW_SUN,.34f*(1-(q-.3f)/.25f));
        else if(q<.85f)t=mix(rgb,BW_DARK,.42f*(q-.55f)/.3f);                /* the belly falls into the dark */
        else t=mix(rgb,BW_DARK,.42f+.33f*(q-.85f)/.15f);
        lit[i]=src_color(t,c->dim);
        edge[i]=src_color(mix(t,BW_DARK,.22f),c->dim);
        rim[i]=src_color(mix(t,BW_DARK,.5f),c->dim);
    }
    int lo=(int)top<c->oy?c->oy:(int)top,hi=(int)bottom;
    if(hi>=c->oy+c->h)hi=c->oy+c->h-1;
    for(int yy=lo;yy<=hi;yy++) {
        float left=1e6f,right=-1e6f;
        for(int i=0;i<n;i++){int j=(i+1)%n;
            if((y[i]<=yy&&y[j]>yy)||(y[j]<=yy&&y[i]>yy)) {
                float xx=x[i]+(x[j]-x[i])*(yy-y[i])/(y[j]-y[i]);
                left=fminf(left,xx);right=fmaxf(right,xx);
            }
        }
        if(left>right)continue;
        float v=(yy-top)/fmaxf(1,bottom-top),mid=(left+right)*.5f;
        int start=(int)left,end=(int)right,width=end-start+1;
        if(width<=3){int s=(int)(clamp01(v)*15);span(c,start,end,yy,&edge[s],255);continue;}
        for(int j=0;j<9;j++) {
            int x0=start+width*j/9,x1=start+width*(j+1)/9-1;
            if(x1<x0)continue;
            float u=((x0+x1)*.5f-mid)/fmaxf(1,(right-left)*.5f);
            int s=(int)(clamp01(v+u*u*.08f)*15);
            const src_t *tone=(j==0||j==8)?&rim[s]:(j==1||j==7)?&edge[s]:&lit[s];
            span(c,x0,x1,yy,tone,255);
        }
    }
}
static void bw_shape(const sk_t *k,const float *x,const float *y,int n,uint32_t rgb) {
    float X[20],Y[20];
    for(int i=0;i<n;i++)sk_pt(k,x[i],y[i],&X[i],&Y[i]);
    bw_poly(k->c,X,Y,n,rgb);
}
/* the body's thickness: a shaded cross-section that widens as the creature turns to the glass */
static void bw_volume(const sk_t *k,float x,float y,float depth,float ry,uint32_t rgb) {
    float w=fabsf(sk_yaw(k,x)),f=sqrtf(fmaxf(0,1-w*w));
    float RX=depth*f*k->s,RY=ry*k->s;if(RX<1)return;
    float X,Y;sk_pt(k,x,y,&X,&Y);
    float px[20],py[20];for(int i=0;i<20;i++){float a=i*TAU/20;px[i]=X+RX*fast_cos(a);py[i]=Y+RY*fast_sin(a);}
    bw_poly(k->c,px,py,20,rgb);
}
static void bw_body(const sk_t *k,float x,float y,float rx,float ry,uint32_t rgb) {
    float X[20],Y[20];
    for(int i=0;i<20;i++){float a=i*TAU/20;X[i]=x+rx*fast_cos(a);Y[i]=y+ry*fast_sin(a);}
    bw_shape(k,X,Y,20,rgb);
    bw_volume(k,x,y,fminf(rx,ry)*.8f,ry*.95f,rgb);
}
/* a tube with an alpha (translucent fin rays, a jelly's tentacles) */
static void bw_tube_a(const sk_t *k,const float *lx,const float *ly,const float *r,int n,uint32_t rgb,int a) {
    float X[SK_TUBE_MAX],Y[SK_TUBE_MAX],R[SK_TUBE_MAX];
    for(int i=0;i<n;i++){sk_pt(k,lx[i],ly[i],&X[i],&Y[i]);R[i]=r[i]*k->s;if(R[i]<.5f)R[i]=.5f;}
    src_t s=src_color(rgb,k->c->dim);
    tube_s(k->c,X,Y,R,n,&s,a);
}
static void bw_line_a(const sk_t *k,float x0,float y0,float x1,float y1,float r0,float r1,uint32_t rgb,int a) {
    float lx[2]={x0,x1},ly[2]={y0,y1},r[2]={r0,r1};
    bw_tube_a(k,lx,ly,r,2,rgb,a);
}
/* a limb: a tube in the shade with a lit core along its upper side (an arm, a tail, a leg) */
static void bw_limb(const sk_t *k,const float *x,const float *y,const float *r,int n,uint32_t rgb) {
    sk_tube(k,x,y,r,n,mix(rgb,BW_DARK,.35f));
    float yy[SK_TUBE_MAX],rr[SK_TUBE_MAX];
    for(int i=0;i<n;i++){yy[i]=y[i]-r[i]*.3f;rr[i]=r[i]*.6f;}
    sk_tube(k,x,yy,rr,n,mix(rgb,BW_SUN,.22f));
}
/* a fin: a translucent membrane between its base (b0 -> b1) and its outer edge (o0 -> o1), the
   rays between them and a lighter edge. A base of one point is a fan (a tail's lobe). */
static void bw_fin(const sk_t *k,float bx0,float by0,float bx1,float by1,float ox0,float oy0,float ox1,float oy1,int rays) {
    float lx[4]={bx0,bx1,ox1,ox0},ly[4]={by0,by1,oy1,oy0};
    sk_poly(k,lx,ly,4,mix(k->fin,BW_SUN,.12f),150);
    uint32_t ray=mix(k->fin,BW_DARK,.4f);
    for(int j=0;j<rays;j++) {
        float u=rays>1?(float)j/(rays-1):.5f;
        bw_line_a(k,bx0+(bx1-bx0)*u,by0+(by1-by0)*u,ox0+(ox1-ox0)*u,oy0+(oy1-oy0)*u,.3f,.18f,ray,125);
    }
    bw_line_a(k,ox0,oy0,ox1,oy1,.25f,.25f,mix(k->fin,BW_SUN,.5f),95);
}
/* a real eye: the orbit's dark ring, a silver sclera, the iris, the pupil, the sun's glint;
   asleep, a lid */
static void bw_eye(const sk_t *k,float lx,float ly,float r,uint32_t iris) {
    float X,Y;sk_pt(k,lx,ly,&X,&Y);float R=r*k->s;if(R<1.4f)R=1.4f;
    if(k->asleep){fill_ellipse(k->c,X,Y,R,R*.3f+.4f,mix(k->body,BW_DARK,.45f),230);return;}
    float dir=sk_yaw(k,lx)<0?-1:1,g=fmaxf(.55f,R*.22f);
    fill_ellipse(k->c,X,Y,R,R,0x0a0c10,255);
    fill_ellipse(k->c,X,Y,R*.82f,R*.82f,0xe6e4d6,255);
    fill_ellipse(k->c,X+dir*R*.12f,Y,R*.62f,R*.62f,iris,255);
    fill_ellipse(k->c,X+dir*R*.2f,Y,R*.36f,R*.36f,0x05070a,255);
    fill_ellipse(k->c,X-R*.3f,Y-R*.32f,g,g,0xffffff,230);
}
/* a small all-dark eye with a glint (sharks, crustaceans), at a screen point */
static void bw_bead(const sk_t *k,float X,float Y,float r) {
    float R=r*k->s;if(R<1)R=1;
    if(k->asleep){fill_ellipse(k->c,X,Y,R,R*.4f,0x202830,220);return;}
    fill_ellipse(k->c,X,Y,R,R,0x05080b,255);
    fill_ellipse(k->c,X-R*.3f,Y-R*.3f,fmaxf(.5f,R*.3f),fmaxf(.5f,R*.3f),0xffffff,210);
}
/* a light of the creature's own (an esca, a firefly squid's photophores): drawn undimmed, so it
   still glows at night */
static void bw_glow(const sk_t *k,float lx,float ly,float r,uint32_t rgb,int a) {
    float X,Y;sk_pt(k,lx,ly,&X,&Y);float R=r*k->s;if(R<.6f)R=.6f;
    src_t s=src_color(rgb,1);
    fill_ellipse_s(k->c,X,Y,R,R,&s,a);
}
/* the creature's AURA: its accent colour as a soft light around it, three alpha ellipses widening
   outward, undimmed - by day a faint halo that lifts it off the dark water, at night the light
   it swims in. The glow breathes with the creature's own clock. */
static void bw_aura(const sk_t *k,float lx,float ly,float rx,float ry) {
    float X,Y;sk_pt(k,lx,ly,&X,&Y);
    uint32_t g=mix(k->body,0x8af0ff,.35f);
    float br=1+.1f*fast_sin(k->t*.9f+k->f->wander);
    float RX=rx*k->s*br,RY=ry*k->s*br;if(RX<4)return;
    bool night=k->c->dim<.8f;int n=night?6:4,a=night?14:9;   /* by day four rings (a third the cost of six: the bench's 25-creature frame), at night six */
    src_t s=src_color(g,1);
    for(int i=0;i<n;i++){float q=night?1.4f-i*.18f:1.3f-i*.25f;fill_ellipse_s(k->c,X,Y,RX*q,RY*q,&s,a);}
}
/* a photophore: one of the lights along the flank, a pale core in a coloured halo, pulsing on
   its own phase; undimmed */
static void bw_photo(const sk_t *k,float lx,float ly,float r,int i) {
    float p=.55f+.45f*fast_sin(k->t*1.7f+i*1.9f+k->f->wander);
    uint32_t g=mix(k->acc,0xffffff,.35f);
    bw_glow(k,lx,ly,r*2.6f,g,(int)(70*p)+25);
    bw_glow(k,lx,ly,r*1.4f,mix(g,0xffffff,.5f),(int)(120*p)+60);
    bw_glow(k,lx,ly,r*.7f,0xf4ffff,(int)(90*p)+150);
}
/* a row of them between two points */
static void bw_photos(const sk_t *k,float x0,float y0,float x1,float y1,int n,float r) {
    if(!k->marked)return;
    for(int i=0;i<n;i++){float u=n>1?(float)i/(n-1):.5f;bw_photo(k,x0+(x1-x0)*u,y0+(y1-y0)*u,r*1.6f,i);}
}
/* the swimming wave: a lateral offset at body point lx - nothing ahead of x0, growing as the square
   of the way back from it (amp px at the body's end xe, up to 2.56 amp past it: the tail's whip), its
   phase lagging along the body by `lag` a px so the tail follows the trunk */
typedef struct{float ph,x0,xe,amp,lag;} bw_wave_t;
static inline float bw_wave(const bw_wave_t *w,float lx) {
    float u=(w->x0-lx)/(w->x0-w->xe);if(u<=0)return 0;if(u>1.6f)u=1.6f;
    return w->amp*u*u*fast_sin(w->ph+lx*w->lag+TAU*4);
}
/* a scale lattice over the flank: staggered, faint, narrowing with the turn */
static void bw_scales(const sk_t *k,float x0,float x1,float y0,float y1,int cols,int rows,uint32_t rgb) {
    src_t s=src_color(rgb,k->c->dim);
    for(int r=0;r<rows;r++)for(int c=0;c<cols;c++) {
        float x=x0+(x1-x0)*(c+.5f+(r&1)*.5f)/cols,y=y0+(y1-y0)*(r+.5f)/rows;
        sk_dot_s(k,x,y,.95f,.8f,&s,55,true);
    }
}

static void draw_blackwater_species(sk_t *k,int species) {
    const fish_t *f=k->f;float t=k->t;int v=f->variant&3;
    bool grown=f->stage>=STAGE_ADULT,elder=f->stage==STAGE_ELDER;
    uint32_t body=k->body,dark=mix(k->body,BW_DARK,.5f),pale=mix(k->body,BW_PALE,.55f);
    if(species==SP_OCTOPUS&&f->camo>0) {
        body=mix(body,f->camo_rgb,clamp01(f->camo));
        dark=mix(body,BW_DARK,.5f);
        k->fade=1-clamp01(f->camo)*.8f;
    }
    switch(species) {
    case SP_FISH: {
        /* the swim: the beat's phase from tank.c (f->jet), its amplitude with the speed; the wave runs
           down the body from behind the gill, the trunk's rear swinging up to ~1.5 px, the caudal's tips
           whipping ~4 px a beat behind; asleep, a slow breath of it */
        bw_wave_t W={f->jet*TAU+f->wander,4,-12,(k->asleep?.5f:1.6f+f->speed*.035f),.09f};
        float tl=elder?1.2f:1;
        bw_aura(k,-2,0,22,12);
        /* the forked caudal: two lobes, rayed, riding the wave at the body's end and whipping past it */
        float wb=bw_wave(&W,-12),wt=bw_wave(&W,-27*tl),wm=bw_wave(&W,-21*tl);
        bw_fin(k,-12,-1.6f+wb,-12,wb,-27*tl,-10+wt,-21*tl,-1+wm,4);
        bw_fin(k,-12,wb,-12,1.6f+wb,-21*tl,1+wm,-27*tl,10+wt,4);
        /* the dorsal along the back, taller with age; the anal and the pelvic underneath - each on the wave */
        float dh=elder?-15.5f:grown?-12.5f:-10;
        bw_fin(k,5,-6.3f+bw_wave(&W,5),-8,-6.8f+bw_wave(&W,-8),1,dh+bw_wave(&W,1),-11,dh+1.2f+bw_wave(&W,-11),5);
        bw_fin(k,-5,5.8f+bw_wave(&W,-5),-10,4.5f+bw_wave(&W,-10),-7,10+bw_wave(&W,-7),-12,8.5f+wb,3);
        bw_fin(k,3,6.5f+bw_wave(&W,3),0,7+bw_wave(&W,0),1,10.5f+bw_wave(&W,1),-3,10+bw_wave(&W,-3),2);
        /* the body: fusiform, countershaded, bent by the wave */
        float x[14]={17,15,11,5,-2,-8,-12,-14,-12,-8,-2,5,11,15};
        float y[14]={0,-3,-5.5f,-7.3f,-7.6f,-6.5f,-4,0,4,6.5f,7.6f,7.3f,5.5f,3};
        for(int i=0;i<14;i++)y[i]+=bw_wave(&W,x[i]);
        bw_shape(k,x,y,14,body);bw_volume(k,0,0,5.5f,7.4f,body);
        bw_scales(k,-10,10,-5,5,5,3,dark);
        /* the lateral line, the operculum's curve, the mouth */
        float lx[4]={11,4,-4,-12},ly[4]={-1.5f,-.5f+bw_wave(&W,4),bw_wave(&W,-4),-.5f+wb},lr[4]={.3f,.3f,.3f,.3f};
        bw_tube_a(k,lx,ly,lr,4,dark,90);
        float gx[3]={10,8,10},gy[3]={-5,0,5},gr[3]={.35f,.35f,.35f};
        bw_tube_a(k,gx,gy,gr,3,dark,120);
        bw_line_a(k,17,.8f,14,1.5f,.3f,.3f,dark,170);
        /* the design's pattern (a juvenile on): bars, blotches or spots */
        for(int i=0;i<3;i++) {
            float px=-6+i*5;
            if(v==1)sk_mark(k,px,0,.9f,4.5f,k->acc,130);
            else if(v==2)sk_mark(k,px,-2,1.8f,1,k->acc,130);
            else sk_mark(k,px,(i%2)*3-1.5f,1.2f,1.2f,k->acc,130);
        }
        /* the pectoral behind the gill, sculling - a slow stroke of its own, deeper when the fish hangs still - then the eye */
        float sc=k->asleep?0:(.9f+1.1f*(1-clamp01(f->speed/30)))*fast_sin(t*2.4f+f->wander+TAU);
        bw_fin(k,6,1.5f,5,4,-1+sc*.8f,.5f+sc*.5f,-3+sc,6+sc*.4f,3);
        bw_eye(k,11.5f,-2.3f,1.6f,mix(0xc8a048,k->acc,.25f));
        bw_photos(k,9,3.2f,-10,3.6f+bw_wave(&W,-10),5,.55f);                 /* the lights along the flank */
        break;
    }
    case SP_SEAHORSE: {
        float sw=k->asleep?0:.8f*fast_sin(t*1.3f+f->wander);
        float fl=k->asleep?0:1.5f*fast_sin(t*9+f->jet*TAU);
        /* the prehensile tail, ringed; it grips a frond when anchored */
        static const float TLX[10]={-1.5f,-2.6f,-2.9f,-2.1f,0,3,5.2f,5.3f,3.6f,2.3f};
        static const float TLY[10]={6,10,14,17.5f,20,21,19.5f,17,16,17.2f};
        static const float ALX[10]={-1.5f,-2.6f,-2,.2f,3,5.2f,7.4f,7.2f,5.2f,3.4f};
        static const float ALY[10]={6,9.5f,12.8f,14.4f,13.4f,13,11,8.4f,7.5f,8.6f};
        static const float TLR[10]={3.1f,2.7f,2.3f,1.9f,1.6f,1.4f,1.2f,1,.85f,.7f};
        bool held=f->anchor>=0&&f->speed<6;
        bw_aura(k,0,-3,11,19);
        float tx[10],ty[10];
        for(int i=0;i<10;i++){tx[i]=(held?ALX:TLX)[i]+(held?0:sw)*i/9.f;ty[i]=(held?ALY:TLY)[i];}
        bw_limb(k,tx,ty,TLR,10,body);
        for(int i=1;i<8;i++)sk_dot(k,tx[i],ty[i],TLR[i]*.95f,.4f,dark,120,true);
        /* the dorsal fan, fluttering; the little pectoral behind the head */
        bw_fin(k,-4.5f,-3,-4.8f,5,-9.5f+fl,-2,-10-fl,6,4);
        bw_fin(k,-2,-11.5f,-2.5f,-9,-5-fl*.5f,-12.5f,-5.5f+fl*.5f,-9,2);
        /* the trunk: a pot belly in bony rings, their ridges knobbed */
        float bx[10]={2.5f,5,5.8f,4.6f,2,-2,-4.8f,-5.6f,-5,-3.5f},by[10]={-9,-4,1,5.5f,8.6f,8.8f,6,1,-4,-9};
        bw_shape(k,bx,by,10,body);bw_volume(k,0,0,4,8.5f,body);
        for(int j=0;j<5;j++) {
            float yy=-6+j*3.4f;
            bw_line_a(k,-4.6f,yy,4.6f,yy+.4f,.3f,.3f,dark,110);
            sk_dot(k,-4.9f,yy,.7f,.55f,mix(body,BW_SUN,.5f),210,false);
            sk_dot(k,4.9f,yy+.4f,.6f,.5f,mix(body,BW_SUN,.5f),210,false);
        }
        /* the neck, the head, the tubular snout, the coronet */
        float nx[2]={-.5f,1},ny[2]={-8,-12.5f},nr[2]={3.1f,2.7f};
        bw_limb(k,nx,ny,nr,2,body);
        bw_body(k,1.2f,-14.5f,4.2f,3.6f,body);
        float sx[2]={4,11},sy[2]={-14.6f,-13.4f},sr[2]={1.6f,1};
        bw_limb(k,sx,sy,sr,2,body);
        sk_dot(k,11.3f,-13.4f,.6f,.9f,dark,220,false);
        sk_tri(k,-1.5f,-17.5f,-.5f,-21,.8f,-17.8f,k->fin,255);
        sk_tri(k,1,-17.8f,2.6f,-20.5f,3.4f,-17.2f,k->fin,255);
        sk_tri(k,-3,-13,-5.5f,-15,-3.5f,-11.5f,k->fin,255);
        /* the design: bands, speckles, white spots or spines */
        if(v==0)for(int j=0;j<3;j++)sk_mark(k,0,-5+j*5,4,.9f,k->acc,130);
        else if(v==1)for(int j=0;j<6;j++)sk_mark(k,-3+(j%3)*3,-6+(j/3)*6+(j%2),.5f,.5f,k->acc,170);
        else if(v==2)for(int j=0;j<5;j++)sk_mark(k,-2+(j%2)*4,-7+j*3.5f,.8f,.8f,0xf4f0e0,200);
        else for(int j=0;j<4;j++)sk_tri(k,-5.5f,-7+j*4,-8,-8.5f+j*4,-5,-5+j*4,mix(k->acc,BW_SUN,.3f),200);
        bw_eye(k,2.2f,-15,1.25f,mix(0x8a6a30,k->acc,.3f));
        bw_photos(k,-3.6f,-6,-3.2f,6,4,.45f);
        break;
    }
    case SP_OCTOPUS: {
        float jk=k->pose;draw_ink(k);
        bw_aura(k,2,2,18,13);
        /* the head and the mantle's axis: up-back crawling, straight back jetting (the arms lead) */
        float hx=2.5f-1.5f*jk,hy=-2.5f+2.5f*jk;
        float ang=-2.3f-(TAU*.5f-2.3f)*jk,ax=fast_cos(ang+TAU),ay=fast_sin(ang+TAU);
        float mrx=7.8f+2.7f*jk,mry=6.8f-1.3f*jk;
        float jp=f->jet-floorf(f->jet),pulse=jp<.3f?fast_sin(jp*(TAU*.5f/.3f)):0;
        mry*=1-.12f*pulse*jk;
        float mcx=hx+ax*mrx*.85f,mcy=hy+ay*mrx*.85f;
        float rx0=hx+3*jk,ry0=hy+3.2f*(1-jk)+.5f*jk,floor_y=9+40*jk;
        /* eight arms: the near four in front, the far four behind, lying along the ground */
        static const float AC[8]={.15f,.95f,1.75f,2.65f,.55f,1.35f,2.2f,3};
        float AX[8][6],AY[8][6],AR[6];
        for(int j=0;j<6;j++)AR[j]=2.1f-1.65f*j/5.f;
        float open=jp<.3f?.1f:.1f+.9f*fast_sin((jp-.3f)*(TAU*.5f/.7f));
        float wob=k->asleep?0:1;
        for(int i=0;i<8;i++) {
            float aj=((i&3)-1.5f+(i>=4?.5f:0))*.07f*(.5f+1.5f*open);
            float ac=AC[i]+.22f*wob*fast_sin(t*1.8f+i*1.9f);
            float a=ac+(aj-ac)*jk;
            float cc=(AC[i]<1.57f?-.3f:.3f)*(1-jk)+.08f*fast_sin(t*5+i*1.1f)*jk;
            float L=(19+5*jk)*(1+.08f*wob*fast_sin(t*1.7f+i*2.3f)),sl=L/5;
            float x=rx0+(i>=4?.8f:0),y=ry0-(i>=4?.6f:0);
            for(int j=0;j<6;j++) {
                AX[i][j]=x;AY[i][j]=y;
                x+=sl*fast_cos(a+TAU);y+=sl*fast_sin(a+TAU);
                if(y>floor_y){y=floor_y;a=fast_cos(a+TAU)>=0?.05f:TAU*.5f-.05f;}
                a+=cc*(.4f+j*.36f);
            }
        }
        for(int i=4;i<8;i++) {
            float fx[3]={AX[i][0],AX[i][2],AX[i][5]},fy[3]={AY[i][0],AY[i][2],AY[i][5]},fr[3]={AR[0],AR[2],AR[5]};
            sk_tube(k,fx,fy,fr,3,dark);
        }
        /* the mantle: an egg, fuller at its apex, its skin in papillae */
        float lx[16],ly[16];
        for(int i=0;i<16;i++) {
            float u=mrx*g_u16c[i],w=mry*g_u16s[i]*(1+.15f*g_u16c[i]);
            lx[i]=mcx+ax*u-ay*w;ly[i]=mcy+ay*u+ax*w;
        }
        bw_shape(k,lx,ly,16,body);
        {float X,Y;sk_pt(k,mcx,mcy,&X,&Y);if(k->zq*mry*k->s>=1){float px[20],py[20];for(int i=0;i<20;i++){float a=i*TAU/20;px[i]=X+k->zq*mry*k->s*fast_cos(a);py[i]=Y+mry*k->s*fast_sin(a);}bw_poly(k->c,px,py,20,body);}}
        for(int i=0;i<6;i++) {
            float u=(-.6f+i*.25f)*mrx,w=(i%2?-.45f:.2f)*mry;
            sk_dot(k,mcx+ax*u-ay*w,mcy+ay*u+ax*w,.55f,.45f,dark,70,false);
        }
        for(int i=0;i<3;i++){float u=(-.3f+i*.3f)*mrx,w=-.95f*mry;sk_dot(k,mcx+ax*u-ay*w,mcy+ay*u+ax*w,.5f,.5f,mix(body,BW_SUN,.5f),150,false);}
        /* the design: mottling, blue rings, a mimic's stripes, spots */
        for(int i=0;i<6;i++) {
            float u=(-.55f+(i%3)*.5f)*mrx,w=(i<3?-.35f:.35f)*mry,px=mcx+ax*u-ay*w,py=mcy+ay*u+ax*w;
            if(v==0)sk_mark(k,px,py,1.3f,1,mix(k->acc,dark,.5f),110);
            else if(v==1){sk_mark(k,px,py,1.3f,1.3f,k->acc,220);sk_mark(k,px,py,.6f,.6f,body,255);}
            else if(v==2)sk_mark(k,px,py,.7f,2.4f,mix(k->acc,0xf0ead8,.4f),170);
            else sk_mark(k,px,py,.7f,.7f,k->acc,180);
        }
        bw_body(k,hx,hy,5-.8f*jk,4.4f-.3f*jk,body);                    /* the head */
        sk_dot(k,rx0,ry0-.5f,4.2f,2.6f,mix(body,BW_DARK,.15f),255,false); /* the web */
        for(int i=0;i<4;i++)bw_limb(k,AX[i],AY[i],AR,6,body);           /* the near arms */
        if(jk<.6f) {                                                      /* a row of suckers along each */
            int a=(int)(200*(1-jk));uint32_t sc=mix(body,0xfff4e4,.6f);
            for(int i=0;i<4;i++)for(int j=1;j<5;j++) {
                sk_dot(k,AX[i][j],AY[i][j]+AR[j]*.55f,.45f,.4f,sc,a,false);
                sk_dot(k,(AX[i][j]+AX[i][j+1])*.5f,(AY[i][j]+AY[i][j+1])*.5f+AR[j]*.5f,.4f,.35f,sc,a,false);
            }
        }
        /* the eye: a bulge, a gold iris, the horizontal pupil */
        {
            float ex=hx+1.8f-1.6f*jk,ey=hy-2.8f;
            sk_dot(k,ex,ey,2.6f,2.3f,body,255,false);
            float X,Y;sk_pt(k,ex,ey,&X,&Y);float R=2*k->s;
            if(k->asleep)fill_ellipse(k->c,X,Y,R,R*.3f+.4f,dark,230);
            else{fill_ellipse(k->c,X,Y,R,R*.9f,0x0a0c10,255);fill_ellipse(k->c,X,Y,R*.8f,R*.72f,mix(0xd8b45c,k->acc,.2f),255);
                fill_ellipse(k->c,X,Y,R*.72f,R*.26f,0x05070a,255);fill_ellipse(k->c,X-R*.35f,Y-R*.4f,fmaxf(.55f,R*.2f),fmaxf(.55f,R*.2f),0xffffff,220);}
        }
        break;
    }
    case SP_SQUID: {
        float jk=k->pose,jp=f->jet-floorf(f->jet);draw_ink(k);
        bw_aura(k,-4,0,24,10);
        float pulse=jp<.3f?fast_sin(jp*(TAU*.5f/.3f)):0,my=1-.12f*pulse*jk;
        float big=v==2?1.6f:1,wob=k->asleep?0:1;
        /* the fins: across the glass, triangular at the rear */
        for(int side=-1;side<=1;side+=2) {
            float rp=1.3f*wob*fast_sin(jp*TAU*2+t*3+side+TAU);
            float lz[5]={3.4f*my,(8.5f+rp)*big*(1-.45f*jk),(9.5f-rp)*big*(1-.45f*jk),1.2f,0};
            float lxx[5]={v==2?-4.f:-8.f,-11.5f+rp*.3f,-18,-22.5f,-23};
            float xs[5],ys[5];
            for(int i=0;i<5;i++)sk_pt3(k,lxx[i],0,side*lz[i],1,&xs[i],&ys[i]);
            src_t s=src_color(mix(k->fin,BW_SUN,.1f),k->c->dim);
            fill_poly_s(k->c,xs,ys,5,&s,200);
            for(int i=1;i<3;i++){float ex,ey;sk_pt3(k,lxx[i]*.9f-1,0,side*lz[i]*.75f,1,&ex,&ey);float X[2]={xs[0],ex},Y[2]={ys[0],ey},R[2]={.3f*k->s,.2f*k->s};src_t d=src_color(mix(k->fin,BW_DARK,.4f),k->c->dim);tube_s(k->c,X,Y,R,2,&d,120);}
        }
        /* the far arms and the far tentacle, behind the head */
        float ar[4]={1.2f,.95f,.7f,.45f};
        for(int i=0;i<6;i++) {
            float a=((i-2.5f)*.16f+.14f*wob*fast_sin(t*3+i*1.3f))*(1-jk*.85f);
            float x=9.5f,y=(-2.5f+i)*my*(1-.4f*jk),L=3.2f+.4f*(i&1);
            float px[4],py[4];
            for(int j=0;j<4;j++){px[j]=x;py[j]=y;x+=L*fast_cos(a+TAU);y+=L*fast_sin(a+TAU);a+=(i<3?-.08f:.08f)*(1-jk);}
            if(i==0||i==5)sk_tube(k,px,py,ar,4,dark);
            else bw_limb(k,px,py,ar,4,body);
            if(i>0&&i<5&&jk<.6f)for(int j=1;j<3;j++)sk_dot(k,px[j],py[j]+ar[j]*.5f,.35f,.3f,mix(body,0xfff4e4,.6f),150,false);
        }
        for(int q=0;q<2;q++) {                                            /* the two long tentacles, clubbed */
            float a=(q?.12f:-.1f)+.18f*wob*fast_sin(t*2.2f+q*2)*(1-jk),x=9.5f,y=(q?.8f:-.8f)*my,L=(20-11*jk)/5;
            float px[6],py[6],pr[6]={.7f,.6f,.5f,.45f,.4f,.4f};
            for(int j=0;j<6;j++){px[j]=x;py[j]=y;x+=L*fast_cos(a+TAU);y+=L*fast_sin(a+TAU);a+=.04f*(q?1:-1);}
            sk_tube(k,px,py,pr,6,mix(body,BW_DARK,.25f));
            sk_dot(k,px[5],py[5],1.3f,.75f,mix(body,BW_SUN,.15f),255,false);
        }
        /* the mantle: a torpedo, its tip at the rear; the siphon under the head */
        float x[12]={6,4,-2,-9,-16,-21,-23,-21,-16,-9,-2,4};
        float y[12]={0,-3.6f*my,-5*my,-5*my,-4*my,-2*my,0,2*my,4*my,5*my,5*my,3.6f*my};
        bw_shape(k,x,y,12,body);bw_volume(k,-6,0,4.5f,5*my,body);
        bw_line_a(k,4,3.5f*my,1,5.5f*my,.9f,.6f,dark,255);
        /* chromatophores: the design's dots over the mantle (a firefly's glow in the dark) */
        for(int i=0;i<8;i++) {
            float px=2-i*2.8f,py=(i%2?-2.2f:1.8f)*my;
            if(v==1&&k->marked){bw_glow(k,px,py,1.1f,0x9af4ff,k->c->dim<.8f?110:40);bw_glow(k,px,py,.5f,0xd8fcff,230);}
            else if(v==2)sk_mark(k,px,py,.5f,.5f,mix(k->acc,BW_PALE,.4f),140);
            else sk_mark(k,px,py,.7f,.6f,k->acc,170);
        }
        bw_body(k,7.5f,0,3.4f,3.6f*my,body);                             /* the head */
        if(v!=1)bw_photos(k,2,3.6f*my,-18,3.2f*my,5,.45f);
        bw_eye(k,6.5f,-.6f,2.1f,mix(0x1c2330,k->acc,.3f));
        break;
    }
    case SP_PUFFER: {
        float puff=clamp01(f->puff),rx=11+4*puff,ry=8.5f+6.5f*puff;
        float beat=k->asleep?0:fast_sin(t*7+f->wander),fl=k->asleep?0:2*fast_sin(t*10+f->wander);
        bw_aura(k,0,0,rx+6,ry+5);
        bw_fin(k,-rx+1.5f,-2.2f,-rx+1.5f,2.2f,-rx-7,-5.5f+beat*1.5f,-rx-7,5.5f+beat*1.5f,5);
        bw_fin(k,-3,-ry+1,-7,-ry+1.5f,-5,-ry-4+beat*.5f,-9,-ry-3,3);
        bw_fin(k,-3,ry-1,-7,ry-1.5f,-5,ry+4-beat*.5f,-9,ry+3,3);
        if(puff>.05f)for(int i=0;i<16;i++) {                              /* the spines stand out */
            float a=i*TAU/16,ca=fast_cos(a),sa=fast_sin(a);
            bw_line_a(k,ca*rx*.93f,sa*ry*.93f,ca*(rx+1+3.5f*puff),sa*(ry+1+3.5f*puff),.45f,.12f,mix(body,BW_SUN,.5f),235);
        }
        bw_body(k,0,0,rx,ry,body);
        sk_dot(k,1,ry*.5f,rx*.78f,ry*.45f,mix(body,0xf0ead8,.6f),110,false);   /* the pale belly */
        if(v==0)for(int i=0;i<7;i++)sk_mark(k,-8+(i%4)*4.5f,-ry*.55f+(i/4)*4,.9f,.8f,dark,180);
        else if(v==1)sk_mark(k,rx*.55f,-ry*.3f,3.6f,3.2f,mix(body,BW_DARK,.65f),190);
        else if(v==2)for(int i=0;i<2;i++)sk_mark(k,-6+i*7,-ry*.55f,1.7f,ry*.4f,mix(body,BW_DARK,.7f),200);
        for(int i=0;i<5;i++)sk_dot(k,-7+i*3.5f,-ry*.25f+(i%2),.5f,.4f,dark,60,true);
        bw_fin(k,3,0,3,3.5f,-2+fl*.3f,-1.5f,-3-fl*.3f,5,3);               /* the pectoral, sculling */
        sk_dot(k,rx-.4f,1.4f,1.5f,1.1f,0xf3ecd8,235,false);              /* the beak */
        bw_photos(k,rx*.5f,ry*.55f,-rx*.6f,ry*.5f,4,.5f);
        bw_line_a(k,rx+.8f,1.4f,rx-2,1.5f,.3f,.3f,dark,210);
        bw_eye(k,rx*.55f,-ry*.33f,2.1f,mix(0x6a8a3a,k->acc,.3f));
        break;
    }
    case SP_ANGLER: {
        float beat=k->asleep?0:fast_sin(t*3+f->wander),gape=.3f+(f->sp_mode==SPM_LUNGE?.7f:0);
        uint32_t lure=v==0?0x8ff6ff:v==3?0xfff07a:mix(k->acc,0xffffff,.4f);
        bw_aura(k,-2,0,24,14);
        bw_fin(k,-19,-2,-19,2,-25,-6.5f+beat,-25,6.5f+beat,4);
        bw_fin(k,-7,-7,-14,-4.5f,-9,-11.5f+beat*.4f,-16,-8.5f,3);
        bw_fin(k,-7,7,-14,4.5f,-9,11.5f,-16,8.5f,3);
        /* the head is most of it: the body tapers from a huge jaw to a small tail */
        float x[14]={18,15,9,1,-8,-15,-20,-20,-15,-8,-1,5,10,15};
        float y[14]={0,-5,-9.5f,-10.5f,-7.5f,-4,-2,2,4.5f,7,8.5f,8,5,2};
        bw_shape(k,x,y,14,body);bw_volume(k,2,-1,6,8.5f,body);
        /* the mouth: the upper jaw's line runs up to the nose; the cavity, the hinged lower jaw
           (dropped in a lunge), needle teeth on both */
        float mx[6]={5,10,16,17,10,5},myy[6]={8,5,2,3.5f+gape*3,7.5f+gape*2,9+gape};
        sk_poly(k,mx,myy,6,0x1a0a0e,235);
        float jx[6]={4,10,17,16.5f,9,3},jy[6]={8.5f,7+gape*2,3.5f+gape*4,6+gape*5,10.5f+gape*2,10.5f};
        sk_poly(k,jx,jy,6,mix(body,BW_DARK,.15f),255);
        for(int i=0;i<5;i++) {
            float tx=14.5f-i*2.1f,ty=2.3f+(15-tx)*.6f;
            sk_tri(k,tx-.35f,ty-.3f,tx+.35f,ty-.3f,tx-.2f,ty+2.3f,0xf6f2e4,240);
        }
        for(int i=0;i<4;i++) {
            float u=i/3.f,tx=11+u*5.5f,ty=7+gape*2+u*(gape*2-3.5f);
            sk_tri(k,tx-.35f,ty+.3f,tx+.35f,ty+.3f,tx+.2f,ty-2.2f,0xf6f2e4,240);
        }
        /* warts, blotches: the design's skin */
        if(v==1)for(int i=0;i<6;i++)sk_mark(k,-6+(i%3)*6,-6+(i/3)*5,.8f,.7f,mix(body,BW_SUN,.45f),170);
        else if(v==2)for(int i=0;i<5;i++)sk_mark(k,-10+i*5,(i%2?-3:3),1.8f,1.4f,mix(k->acc,dark,.4f),140);
        else for(int i=0;i<4;i++)sk_mark(k,-8+i*5,(i%2?-4:2),.6f,.6f,mix(body,BW_SUN,.3f),120);
        /* the pectoral lobe it walks on, the illicium and the esca's glow */
        float px[3]={0,-2,-3.5f},py[3]={6,9,11.5f},pr[3]={1.7f,1.4f,1.1f};
        bw_limb(k,px,py,pr,3,body);
        float ix[5]={3,3,5,8.5f,11},iy[5]={-10,-14,-17,-18.5f,-17+beat},ir[5]={.7f,.6f,.5f,.4f,.3f};
        bw_tube_a(k,ix,iy,ir,5,mix(k->fin,BW_DARK,.3f),255);
        bw_glow(k,11,-17+beat,4,lure,20+(int)(110*f->lure));
        bw_glow(k,11,-17+beat,2,lure,60+(int)(150*f->lure));
        bw_glow(k,11,-17+beat,1.1f,0xfff8dc,255);
        bw_eye(k,8,-4.5f,1.35f,mix(0x9a7a2a,k->acc,.3f));
        bw_photos(k,-3,-7,-16,-3,4,.45f);
        break;
    }
    case SP_EEL: {
        float x[16],y[16],r[16],uy[16],ur[16],ly[16],lr[16];
        for(int i=0;i<16;i++) {
            float u=i/15.f;x[i]=23-u*60;y[i]=fast_sin(u*7-f->jet*TAU+TAU*4)*u*(k->asleep?2:4.5f+1.5f*clamp01(f->speed/20));r[i]=4.1f*(1-u)+.4f;   /* the wave on tank.c's phase (quicker with speed), deeper the faster it goes */
            uy[i]=y[i]-r[i]*1.05f;ur[i]=r[i]*.55f+.4f;ly[i]=y[i]+r[i]*.95f;lr[i]=r[i]*.45f+.3f;
        }
        /* the fin ridge the length of the back and the belly, rayed */
        bw_aura(k,-7,0,32,11);
        bw_tube_a(k,x,uy,ur,16,k->fin,215);
        bw_tube_a(k,x,ly,lr,16,k->fin,190);
        for(int i=1;i<15;i+=2)bw_line_a(k,x[i],y[i]-r[i]*.5f,x[i],y[i]-r[i]*1.35f,.2f,.15f,mix(k->fin,BW_DARK,.4f),120);
        bw_limb(k,x,y,r,16,body);
        float by[16],br[16];for(int i=0;i<16;i++){by[i]=y[i]+r[i]*.4f;br[i]=r[i]*.42f;}
        bw_tube_a(k,x,by,br,16,v==2?mix(body,0xf0d8a8,.4f):mix(k->acc,BW_PALE,.35f),130);   /* the belly's colour */
        if(v==3)for(int i=1;i<13;i+=2)sk_mark(k,x[i],y[i]-r[i]*.2f,.7f,.6f,mix(k->acc,BW_PALE,.3f),170);
        else if(v==0||v==1)for(int i=2;i<13;i+=3)sk_mark(k,x[i],y[i]-r[i]*.3f,1.2f,.5f,dark,110);
        /* the blunt head: a jaw line, the gill opening, a small eye */
        bw_body(k,21,0,6.5f,3.9f,body);
        bw_line_a(k,27,1,19,1.6f,.3f,.3f,dark,180);
        bw_line_a(k,16.5f,-1.5f,16,2.5f,.3f,.3f,dark,150);
        sk_dot(k,26,-1.2f,.4f,.4f,dark,180,false);
        bw_eye(k,24,-1.2f,1,0x3a3222);
        bw_photos(k,x[2],y[2]+r[2]*.1f,x[13],y[13],6,.4f);
        if(f->spark>0)for(int j=0;j<4;j++) {
            float xx=-25+j*12,sgn=j%2?1:-1,w=fast_sin(t*18+j)*2;
            sk_dot(k,xx+1.5f,sgn*9,4,4,0x9ad8ff,50,false);
            bw_line_a(k,xx,sgn*5,xx+3+w,sgn*9,.5f,.35f,0xd8f8ff,255);
            bw_line_a(k,xx+3+w,sgn*9,xx+1,sgn*13,.35f,.2f,0xeefcff,255);
        }
        break;
    }
    case SP_SHARK: {
        /* the swim: the whole rear third sweeps on tank.c's beat, the caudal a beat behind it */
        bw_wave_t W={f->jet*TAU,0,-21,k->asleep?.4f:1.4f+.8f*clamp01(f->speed/25),.07f};
        uint32_t belly=v==3?mix(body,0xf4f6f0,.7f):0xe4e8e4;
        bw_aura(k,-4,0,30,13);
        /* the heterocercal tail: the upper lobe the long one; the far pectoral behind */
        float wb=bw_wave(&W,-21),w27=bw_wave(&W,-27),w34=bw_wave(&W,-34);
        bw_fin(k,-21,-1.5f+wb,-21,.5f+wb,-34,-13+w34,-27,-2+w27,4);
        bw_fin(k,-21,.5f+wb,-21,2+wb,-27,w27,-29,6+bw_wave(&W,-29),3);
        sk_tri(k,5,2,-2,3,-6,8,mix(k->fin,BW_DARK,.4f),140);
        float x[14]={17,14,8,0,-8,-15,-20,-22,-20,-15,-8,0,8,14};
        float y[14]={-1,-3,-4.6f,-5.2f,-4.6f,-3.2f,-2,0,1.6f,2.8f,4,4.8f,4.4f,2.6f};
        for(int i=0;i<14;i++)y[i]+=bw_wave(&W,x[i]);
        bw_shape(k,x,y,14,body);bw_volume(k,-2,0,4.5f,4.8f,body);
        sk_dot(k,-2,3,13,1.8f,belly,150,true);                            /* the white belly */
        /* the first dorsal, tall and raked; the second; the pectoral; the pelvic - the rear ones on the wave */
        bw_fin(k,2,-4.8f,-8,-4.6f+bw_wave(&W,-8),-2,-15,-9,-8+bw_wave(&W,-9),4);
        bw_fin(k,-14,-3.2f+bw_wave(&W,-14),-18,-2.2f+bw_wave(&W,-18),-16,-6.5f+bw_wave(&W,-16),-19,-4.2f+bw_wave(&W,-19),2);
        bw_fin(k,6,3,1,4.2f,-3,10.5f,-8,8.5f,4);
        bw_fin(k,-10,4.2f+bw_wave(&W,-10),-14,3.2f+bw_wave(&W,-14),-12,7+bw_wave(&W,-12),-16,6+bw_wave(&W,-16),2);
        for(int i=0;i<5;i++)bw_line_a(k,9-i*1.3f,-2.2f,8.4f-i*1.3f,2.2f,.25f,.25f,dark,170);   /* five gill slits */
        bw_line_a(k,15,2.3f,11,2.8f,.3f,.3f,dark,160);                     /* the mouth, underslung */
        bw_photos(k,4,.8f,-16,.6f+bw_wave(&W,-16),5,.4f);
        /* the cephalofoil: a flat lens across the glass, its lobes shown obliquely, an eye at each end */
        {
            static const float HX[9]={18.5f,18,16,13.5f,12,12,13.5f,16,18},HZ[9]={0,-7,-10,-9,-4,4,9,10,7};
            float X[9],Y[9];
            for(int i=0;i<9;i++)sk_pt3(k,HX[i],0,HZ[i],.3f,&X[i],&Y[i]);
            bw_poly(k->c,X,Y,9,body);
            float ex,ey;
            sk_pt3(k,16.5f,0,-9.5f,.3f,&ex,&ey);bw_bead(k,ex,ey,1);
            sk_pt3(k,16.5f,0,9.5f,.3f,&ex,&ey);bw_bead(k,ex,ey,.85f);
        }
        break;
    }
    case SP_SWORDFISH: {
        /* the swim: a stiff fast body - only the last third sweeps, the lunate tail a beat behind */
        bw_wave_t W={f->jet*TAU,-2,-19,k->asleep?.4f:1.2f+.8f*clamp01(f->speed/40),.08f};
        uint32_t belly=mix(k->acc,0xf4f6f8,.5f),bill=mix(k->fin,BW_DARK,.45f);
        bw_aura(k,-2,0,34,13);
        float wb=bw_wave(&W,-19),wt=bw_wave(&W,-31);
        bw_fin(k,-19,-1+wb,-19,1+wb,-31,-12+wt,-31,12+wt*.8f,5);                      /* the lunate tail */
        sk_tri(k,6,2.5f,.5f,8.5f,3.5f,3,mix(k->fin,BW_DARK,.4f),140);               /* the far pectoral */
        float x[14]={16,13,7,0,-7,-14,-20,-21.5f,-20,-14,-7,0,7,13};
        float y[14]={-1.6f,-3.8f,-5.2f,-5.6f,-5,-3.6f,-1.8f,0,1.8f,3.4f,4.6f,4.9f,4.4f,3};
        for(int i=0;i<14;i++)y[i]+=bw_wave(&W,x[i]);
        bw_shape(k,x,y,14,body);bw_volume(k,0,0,4.4f,5.2f,body);
        sk_dot(k,-2,3.2f,13,1.6f,belly,150,true);                                    /* the silver belly */
        bw_fin(k,5,-4.6f,-10,-4.2f+bw_wave(&W,-10),-1,-18.5f,-8,-9+bw_wave(&W,-8),4);   /* the tall sickle dorsal */
        bw_fin(k,-13.5f,-3+bw_wave(&W,-13.5f),-18,-2.2f+bw_wave(&W,-18),-15.5f,-7+bw_wave(&W,-15.5f),-19,-4.5f+wb,2);
        bw_fin(k,-13,3+bw_wave(&W,-13),-17,2.2f+bw_wave(&W,-17),-15,6.2f+bw_wave(&W,-15),-18.5f,4.2f+bw_wave(&W,-18.5f),2);
        bw_fin(k,6,3,1,4.2f,-3,10,-7,8,4);                                           /* the near pectoral */
        {float llx[3]={12,-8,-17},lly[3]={-.5f,bw_wave(&W,-8),.5f+bw_wave(&W,-17)},llr[3]={.3f,.3f,.3f};bw_tube_a(k,llx,lly,llr,3,mix(body,BW_DARK,.5f),150);}   /* the lateral line, on the wave */
        if(v==1||v==2)for(int i=0;i<5;i++)bw_line_a(k,9-i*5.5f,-4,8.5f-i*5.5f,2.5f,.5f,.4f,mix(k->acc,k->fin,.5f),90);   /* faint bars */
        sk_tri(k,-19,-1.3f+wb,-22.5f,-1.3f+bw_wave(&W,-22.5f),-19,1.3f+wb,mix(k->fin,BW_DARK,.4f),255);   /* the keel */
        bw_line_a(k,15.5f,-1,33.5f,-.3f,1.2f,.2f,bill,255);                           /* the bill */
        bw_line_a(k,16,-1.7f,25,-1.3f,.35f,.15f,mix(k->acc,0xffffff,.5f),200);
        bw_line_a(k,15,2.3f,11.5f,2.6f,.3f,.3f,mix(body,BW_DARK,.55f),160);          /* the mouth */
        bw_eye(k,10.5f,-1.8f,1.6f,0x1a2a44);
        bw_photos(k,8,1.2f,-15,1.4f+bw_wave(&W,-15),6,.4f);
        break;
    }
    case SP_TURTLE: {   /* 2026-10-11: a countershaded carapace with its scutes, leathery flippers, a scaled head, a real eye */
        float st=k->asleep?.6f:fast_sin(f->jet*TAU);
        uint32_t skin=mix(k->fin,BW_DARK,.15f),fdark=mix(k->fin,BW_DARK,.5f),seam=mix(body,BW_DARK,.6f);
        bw_aura(k,0,0,24,11);
        turtle_flipper(k,6.5f,-.5f,-st*.7f,1,fdark,230);sk_tri(k,-12,-1,-15,0,-19,-4,fdark,230);
        bw_shape(k,TURTLE_CX,TURTLE_CY,10,body);
        bw_line_a(k,10.5f,-1.3f,-14.5f,-3.3f,.35f,.35f,seam,200);                              /* the rim */
        sk_dot(k,-2,3.2f,12,2.1f,mix(k->acc,BW_PALE,.4f),240,true);                            /* the plastron */
        bw_line_a(k,9,3,-13,3.2f,.25f,.25f,seam,120);
        turtle_volume(k,body,mix(k->acc,BW_PALE,.4f),skin,seam,BW_SUN);
        if(fabsf(sk_yaw(k,-2))<.45f)v=-1;
        if(v==0){                                                                               /* the green's scutes: five vertebrals, the costals either side */
            for(int i=0;i<5;i++){float x=8-i*5.2f;bw_line_a(k,x,-7.5f,x-.5f,-.5f,.3f,.3f,seam,170);}
            bw_line_a(k,9.5f,-4.5f,-13.5f,-5.2f,.3f,.3f,seam,170);bw_line_a(k,10,-1.2f,-14,-2.2f,.25f,.25f,seam,120);
            for(int i=0;i<4;i++)sk_dot(k,5.5f-i*5.2f,-5.6f,1.6f,1.1f,mix(body,BW_SUN,.18f),80,false);
        } else if(v==1){                                                                        /* the hawksbill's amber streaks */
            for(int i=0;i<6;i++)sk_mark(k,7-i*4.2f,-4.8f+(i&1)*1.8f,1.9f,.55f,mix(k->fin,body,.25f),190);
            for(int i=0;i<4;i++)bw_line_a(k,6-i*5.2f,-7.5f,5.5f-i*5.2f,-.5f,.25f,.25f,seam,120);
        } else if(v==2){                                                                        /* the leatherback's seven ridges, pale-flecked */
            for(int i=0;i<3;i++)bw_line_a(k,9.5f,-6.8f+i*2.4f,-13.5f,-6.2f+i*2.4f,.3f,.3f,mix(k->acc,body,.5f),200);
            for(int i=0;i<7;i++)sk_dot(k,7-i*3.6f,-5.8f+(i%3)*1.9f,.5f,.4f,k->acc,120,false);
        } else if(v==3)for(int i=0;i<7;i++)sk_dot(k,7.5f-i*3.9f,-5.2f+(i&1)*2.6f,.95f,.85f,mix(k->acc,BW_SUN,.2f),210,false);   /* pale spots */
        sk_tri(k,-14,0,-20,1.5f,-14,2.5f,skin,255);sk_tri(k,-11,2,-14,3,-18,7.5f,skin,255);
        bw_body(k,12,.5f,3,2.2f,skin);bw_body(k,16.5f,-.3f,3.6f,3,skin);
        for(int i=0;i<4;i++)bw_line_a(k,14.5f+(i&1)*1.6f,-2.5f+i*1.2f,15.5f+(i&1)*1.6f,-1.7f+i*1.2f,.2f,.2f,fdark,80);   /* the head's scales */
        sk_tri(k,19.5f,-1.2f,21.6f,.2f,19.5f,1.4f,fdark,255);
        bw_line_a(k,19.8f,.3f,17.8f,.9f,.25f,.25f,fdark,180);                                  /* the mouth */
        bw_eye(k,17.3f,-1.3f,1.5f,mix(0x3a2a14,k->acc,.25f));
        turtle_flipper(k,6.5f,1.5f,st,1,skin,255);
        {float ang=.35f+.8f*st;bw_line_a(k,6.5f,1.5f,6.5f+fast_cos(ang+.25f)*8,1.5f+fast_sin(ang+.25f)*8,.2f,.2f,fdark,110);}                         /* the flipper's bone line */
        turtle_gulp(k,21,-1);break;
    }
    case SP_CRAB: {
        /* face-on: the carapace keeps its width whichever way it sidles */
        sk_t crab=*k;crab.yh=crab.yh<0?-1:1;crab.yt=crab.yh;k=&crab;
        float ph=f->jet*TAU,mv=clamp01(f->speed/6),threat=clamp01(f->puff),raise=threat*6;
        float snap=k->asleep?0:.6f*fmaxf(0,fast_sin(t*2.5f+f->wander));
        uint32_t tip=v==1?k->acc:mix(body,BW_DARK,.3f);
        bw_aura(k,0,1,20,12);
        /* eight jointed legs, stepping */
        for(int side=-1;side<=1;side+=2)for(int i=0;i<4;i++) {
            float st=mv*1.6f*fast_sin(ph+i*1.6f+(side>0?0:2.5f)+TAU),lift=mv*fmaxf(0,fast_sin(ph+i*1.6f+1.5f+(side>0?0:2.5f)+TAU))*2;
            float lx[3]={side*(4+i*2.f),side*(10+i*2.2f)+st*.3f,side*(12+i*2.4f)+st},ly[3]={2+i*.5f,-2+i*2.5f-lift,8+i*.8f-lift*.4f},lr[3]={.9f,.65f,.3f};
            bw_limb(k,lx,ly,lr,3,k->fin);
            sk_dot(k,lx[1],ly[1],.7f,.7f,mix(k->fin,BW_DARK,.3f),255,false);
        }
        /* the claws, the near one the bigger; the threat raises them */
        for(int side=-1;side<=1;side+=2) {
            float big=side>0?1.15f:.9f,open=threat*1.5f+snap;
            float ax[3]={side*5.f,side*10.f,side*13.f},ay[3]={1,-1-raise*.5f,-3-raise},ar[3]={1.3f,1.1f,1};
            bw_limb(k,ax,ay,ar,3,body);
            bw_body(k,side*15.f,-4-raise,3.6f*big,2.5f*big,body);
            sk_tri(k,side*16.f,-5-raise,side*21.f,-6-raise,side*17.f,-3-raise,tip,255);
            sk_tri(k,side*16.f,-6-raise,side*20.f,-8.5f-raise-open,side*17.5f,-5.5f-raise,tip,255);
            sk_dot(k,side*15.f,-5-raise,.5f,.5f,dark,120,false);
        }
        /* the carapace, toothed along its front edge */
        float cx[11]={-11,-9,-5,0,5,9,11,9,4,-4,-9},cy[11]={0,-3.5f,-5.5f,-6,-5.5f,-3.5f,0,4,6.5f,6.5f,4};
        bw_shape(k,cx,cy,11,body);
        for(int i=0;i<3;i++)for(int side=-1;side<=1;side+=2){float tx=side*(5.5f+i*1.8f),ty=-5.2f+i*1.1f;sk_tri(k,tx-.6f,ty,tx+.6f,ty,tx+side*.3f,ty-1.6f,mix(body,BW_SUN,.3f),255);}
        bw_line_a(k,-6,-1.5f,6,-1.5f,.3f,.3f,mix(body,BW_SUN,.4f),80);
        for(int i=0;i<5;i++)sk_dot(k,-6+i*3,(i%2?2:-3),.5f,.45f,dark,60,false);
        if(v==2)for(int i=0;i<6;i++)sk_mark(k,-7+(i%3)*7,(i<3?-3:2.5f),.6f,.6f,k->acc,190);
        else if(v==3)for(int i=0;i<3;i++)sk_mark(k,-5+i*5,-2,1.2f,.7f,mix(k->acc,dark,.4f),110);
        /* the eye stalks, the mouthparts */
        for(int side=-1;side<=1;side+=2) {
            bw_line_a(k,side*3.f,-4.5f,side*4.f,-8,.6f,.55f,body,255);
            float X,Y;sk_pt(k,side*4.f,-8.6f,&X,&Y);bw_bead(k,X,Y,.95f);
        }
        sk_dot(k,-1.3f,5.8f,.7f,.5f,pale,200,false);sk_dot(k,1.3f,5.8f,.7f,.5f,pale,200,false);
        break;
    }
    case SP_LOBSTER: {
        float curl=f->sp_mode==SPM_FLIP&&f->jet>.5f?clamp01((f->jet-.5f)*8):0;
        float ph=t*5,mv=clamp01(f->speed/6),wob=k->asleep?0:1;   /* the legs' step at a fixed rate (the stride's length follows the speed, mv) */
        uint32_t ant=v==0?k->acc:mix(body,k->fin,.5f),shade=mix(body,BW_DARK,.4f);
        bw_aura(k,-2,1,26,10);
        /* the long antennae, sweeping back; the antennules */
        for(int a=0;a<2;a++) {
            float lx[7],ly[7],lr[7],ang=-.5f-a*.3f+.2f*wob*fast_sin(t*1.3f+a*2+f->wander),x=13,y=-3.5f;
            for(int j=0;j<7;j++){lx[j]=x;ly[j]=y;lr[j]=.55f-.04f*j;x+=4.2f*fast_cos(ang+TAU);y+=4.2f*fast_sin(ang+TAU);ang+=.16f+.03f*wob*fast_sin(t*2+j);}
            sk_tube(k,lx,ly,lr,7,a?shade:ant);
        }
        for(int a=0;a<2;a++)bw_line_a(k,14,-2.5f,19,-4+a*1.5f,.4f,.3f,k->fin,255);
        /* the far claw (the cutter) and the far legs, behind */
        {
            float ax[3]={8,14,19},ay[3]={-1,-3,-3.5f},ar[3]={1,.9f,.8f};
            sk_tube(k,ax,ay,ar,3,shade);
            sk_dot(k,22,-3.5f,3.2f,2,shade,255,false);
            sk_tri(k,24,-4,28.5f,-4.5f,24.5f,-2.5f,shade,255);sk_tri(k,24,-4.5f,28,-6.5f,25,-3.5f,shade,255);
        }
        for(int j=0;j<4;j++) {
            float st=mv*1.8f*fast_sin(ph+j*1.6f+2.4f+TAU),lift=mv*fmaxf(0,fast_sin(ph+j*1.6f+4+TAU))*1.4f,bx=9.5f-j*3.2f;
            float lx[3]={bx+.8f,bx+2.5f,bx+1.2f+st},ly[3]={2.5f,5-lift,7-lift*.6f},lr[3]={.7f,.55f,.4f};
            sk_tube(k,lx,ly,lr,3,shade);
        }
        /* the abdomen: six plates, each overlapping the one behind, curling under in a flip; the fan */
        float sx[6],sy[6];
        for(int i=0;i<6;i++){sx[i]=-6-i*3.2f*(1-.45f*curl);sy[i]=1+curl*i*i*.3f;}
        float fx=sx[5]-3.2f*(1-.45f*curl),fy=sy[5]+curl*6;
        for(int j=0;j<5;j++)sk_dot(k,fx-(j==2?1:0),fy+(j-2)*1.5f,3.2f,1.2f,mix(k->fin,BW_SUN,.15f),225,false);
        for(int i=5;i>=0;i--) {
            bw_body(k,sx[i],sy[i],3.8f,3.3f-i*.25f,body);
            bw_line_a(k,sx[i]+1.9f,sy[i]-2.6f+i*.2f,sx[i]+1.9f,sy[i]+2.4f-i*.2f,.3f,.3f,dark,150);
            if(i>0)sk_dot(k,sx[i],sy[i]+3-i*.25f,.6f,.5f,mix(body,BW_PALE,.4f),150,false);   /* a swimmeret */
            if(v==2)sk_mark(k,sx[i],sy[i]-1,.6f,.6f,mix(k->acc,BW_SUN,.3f),200);
            if(v==3&&i%2)sk_mark(k,sx[i],sy[i]+.5f,1.4f,1.6f,mix(k->acc,BW_PALE,.5f),160);
        }
        /* the cephalothorax with its rostrum and groove; the near legs */
        float cx[11]={12,11,6,0,-5,-6,-5,0,6,11,12.5f},cy[11]={-1.5f,-4.5f,-5.5f,-5.5f,-4.5f,0,4,5.5f,5.5f,4,1.5f};
        bw_shape(k,cx,cy,11,body);bw_volume(k,3,0,4,5,body);
        sk_tri(k,11.5f,-2.5f,17,-4.5f,12,-1,body,255);
        bw_line_a(k,2,-5,1,5,.3f,.3f,dark,120);
        for(int i=0;i<4;i++)sk_dot(k,8-i*3,-3.5f,.45f,.4f,mix(body,BW_SUN,.4f),140,false);
        if(v==3)sk_mark(k,3,0,2.2f,2,mix(k->acc,BW_PALE,.5f),150);
        for(int j=0;j<4;j++) {
            float st=mv*1.8f*fast_sin(ph+j*1.6f+.8f+TAU),lift=mv*fmaxf(0,fast_sin(ph+j*1.6f+2.4f+TAU))*1.4f,bx=9-j*3.2f;
            float lx[3]={bx,bx+1.5f+st*.5f,bx-.5f+st},ly[3]={3.5f,6.5f-lift,9.5f-lift*.6f},lr[3]={.75f,.55f,.4f};
            bw_limb(k,lx,ly,lr,3,k->fin);
        }
        /* the near claw: the crusher, knobbed, its dactyl opening and closing */
        {
            float snap=k->asleep?0:1.2f*fmaxf(0,fast_sin(t*2+f->wander));
            float ax[3]={8,14,18},ay[3]={2.5f,4.5f,4},ar[3]={1.3f,1.2f,1.1f};
            bw_limb(k,ax,ay,ar,3,body);
            bw_body(k,21.5f,3.2f,4.3f,2.8f,body);
            sk_tri(k,24,2.5f,29,2,25,4.8f,body,255);
            sk_tri(k,24,1.5f,28.5f,-1-snap,25.5f,2.8f,body,255);
            for(int i=0;i<3;i++)sk_dot(k,19.5f+i*1.6f,1.6f,.45f,.4f,dark,150,false);
        }
        bw_line_a(k,11,-3,13,-4.5f,.5f,.45f,body,255);
        {float X,Y;sk_pt(k,13.3f,-5,&X,&Y);bw_bead(k,X,Y,.9f);}
        break;
    }
    case SP_JELLYFISH: {
        sk_t local=*k;k=&local;
        k->yh=k->yt=f->facing<0?-1:1;
        k->y-=BW_JELLY_CENTRE*k->s;
        bool fry=f->stage==STAGE_FRY;
        float pulse=.5f+.5f*fast_sin(f->jet*TAU),sx=1-.17f*pulse,sy=1+.1f*pulse;
        float trail=.5f+.5f*fast_sin(f->jet*TAU-1.1f+TAU),length=elder?1.16f:fry?.74f:1,wob=k->asleep?0:1;
        uint32_t rim=mix(body,0xffffff,.55f),arm=k->fin;
        bw_aura(k,0,4,20,22);
        /* the four frilled oral arms (two on a fry) */
        int arms=fry?2:4;
        for(int i=0;i<arms;i++) {
            float x=(-7.5f+i*5)*(arms==2?1.6f:1)+(arms==2?1.25f:0),w=wob*fast_sin(t*1.8f+i*.8f)*2.3f;
            float end=(22+(i%2)*3)*length,spread=1+(.5f-trail)*.3f;
            float X[5]={x*sx,x+w,(x-w*.7f)*spread,(x+w*.9f)*spread,(x+w*.3f)*spread};
            float Y[5]={2,8,14,end-3,end},R[5]={1.3f,1.1f,.9f,.6f,.3f};
            bw_tube_a(k,X,Y,R,5,arm,190);
            for(int j=1;j<4;j++)sk_dot(k,X[j]+(j%2?1.3f:-1.3f),Y[j]+2,.9f,.6f,mix(arm,BW_SUN,.35f),150,false);
        }
        /* the tentacle fringe from the hem */
        int fringe=fry?6:12;
        for(int i=0;i<fringe;i++) {
            float x=(-15+i*30.f/(fringe-1))*sx,w=wob*fast_sin(t*1.4f+i*1.1f)*1.5f,len=(10+(i%3)*3)*length*(elder?1.3f:1);
            bw_line_a(k,x,4.5f,x+w,4.5f+len,.25f,.15f,mix(arm,0xffffff,.4f),115);
        }
        /* the bell: a translucent dome, its canals, the gonads, a scalloped hem, the lit rim */
        float X[16],Y[16];int n=0;
        for(int i=0;i<=12;i++){float a=TAU*.5f+i*TAU*.5f/12;X[n]=17*sx*fast_cos(a);Y[n++]=2+17*sy*fast_sin(a);}
        X[n]=11*sx;Y[n++]=5;X[n]=-11*sx;Y[n++]=5;
        sk_poly(k,X,Y,n,body,150);
        sk_dot(k,0,-4,12*sx,9*sy,mix(body,0xffffff,.25f),70,false);
        sk_dot(k,-4*sx,-8,5*sx,3.5f*sy,0xffffff,45,false);
        for(int s=-1;s<=1;s+=2){bw_line_a(k,s*3*sx,-10*sy,s*9*sx,3,.25f,.25f,rim,80);bw_line_a(k,s*1.5f*sx,-12.5f*sy,s*14*sx,1.5f,.25f,.25f,rim,80);}
        if(!fry){sk_dot(k,-4.5f*sx,-5,1.7f,1.2f,k->acc,120,false);sk_dot(k,4.5f*sx,-5,1.7f,1.2f,k->acc,120,false);
            sk_dot(k,-1.6f*sx,-8.5f,1.3f,1.5f,k->acc,120,false);sk_dot(k,1.6f*sx,-8.5f,1.3f,1.5f,k->acc,120,false);}
        for(int i=0;i<8;i++)sk_dot(k,(-14+i*4)*sx,4.6f,2.1f,1.4f,rim,200,false);
        {float xx[5]={-16*sx,-9*sx,0,9*sx,16*sx},yy[5]={2,4,5,4,2},rr[5]={.5f,.65f,.7f,.65f,.5f};bw_tube_a(k,xx,yy,rr,5,rim,230);}
        sk_dot(k,0,2.5f,15*sx,4,mix(body,0xffffff,.6f),35,false);          /* a faint glow inside the rim */
        break;
    }
    }
}
static void draw_blackwater_fish(ctx_t *c,const fish_t *f,float clock,bool asleep) {
    sk_t k;sk_setup(&k,c,f,clock,asleep,-27,17,.61f,1);
    draw_blackwater_species(&k,SP_FISH);
}

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

/* Approved jellyfish study (docs/design/jellyfish): an upright pulsing bell,
 * no bitmap in the live tank. f->jet is shared with the native locomotion so
 * the contraction, the rise and the thrust agree (tank.c, the jellyfish branch).
 * The creature's own saved colours are the inputs in every theme: body = the
 * bell, fin = the arms, accent = the markings. The themes change the material
 * (flat / translucent / scalloped), never the design the keeper bought. */
#define JELLY_CENTRE 6.0f   /* bell + arms span ~ -16..+28: drawn about f->y's middle (rings, cards, rows) */
static void draw_jellyfish(sk_t *source) {
    if(theme_active()==THEME_BLACKWATER){draw_blackwater_species(source,SP_JELLYFISH);return;}   /* the Blackwater look centres it itself */
    sk_t local=*source,*k=&local;
    bool quiet=theme_active()==THEME_QUIET_LAGOON,club=theme_active()==THEME_TIDEPOOL_CLUB;
    bool fry=k->f->stage==STAGE_FRY,elder=k->f->stage==STAGE_ELDER;
    /* Radial bell: a horizontal turn flips the eye's side, it never thins to a sliver. */
    k->yh=k->yt=k->f->facing<0?-1:1;
    k->y-=JELLY_CENTRE*k->s;
    float pulse=.5f+.5f*fast_sin(k->f->jet*TAU),sx=1-.17f*pulse,sy=1+.10f*pulse;
    float trail=.5f+.5f*fast_sin(k->f->jet*TAU-1.1f);         /* the arms follow the bell with a lag */
    float length=elder?1.16f:fry?.74f:1;
    uint32_t body=k->body,fin=k->fin,accent=k->acc;
    /* the theme's material, lightly: the design's hue stays readable (a rose jelly is rose) */
    if(quiet){body=mix(body,0xa9c6b8,.22f);fin=mix(fin,0xc4dccd,.3f);}
    if(club){body=mix(body,0xf1c6a4,.2f);}
    uint32_t rim=quiet?mix(body,0xeef1dc,.6f):club?mix(body,0xfff0d0,.62f):mix(body,0xffffff,.5f);
    int arms=quiet?(fry?6:10):club?3:fry?3:4;
    for(int i=0;i<arms;i++) {
        float x=-12+i*24.f/(arms-1),w=fast_sin(k->t*(k->asleep?.5f:1.8f)+i*.8f)*2.3f;
        float end=((quiet?29:club?25:20)+(i%3)*2)*length;
        float spread=1+(.5f-trail)*.3f;                        /* relaxed bell: the arms drift apart */
        float X[5]={x*sx,x+w,(x-w*.7f)*spread,(x+w*.9f)*spread,(x+w*.3f)*spread};
        float Y[5]={1,8,14,end-3,end};
        float R[5]={club?1.4f:quiet?.3f:1.15f,club?1.2f:quiet?.25f:.9f,club?1:quiet?.22f:.6f,.4f,.15f};
        if(quiet){for(int j=0;j<4;j++){
            float ax,ay,bx,by;sk_pt(k,X[j],Y[j],&ax,&ay);sk_pt(k,X[j+1],Y[j+1],&bx,&by);
            int n=(int)(fabsf(bx-ax)+fabsf(by-ay))+1;
            for(int q=0;q<=n;q++)px_blend(k->c,(int)(ax+(bx-ax)*q/n),(int)(ay+(by-ay)*q/n),fin,135);
        }}else if(club)lagoon_limb(k,X,Y,R,5,fin);
        else sk_tube(k,X,Y,R,5,fin);
    }
    if(quiet&&!fry)for(int side=-1;side<=1;side+=2){              /* the moon jelly's two folded oral arms */
        float w=fast_sin(k->t*1.7f+side)*2;
        float X[6]={side*4,side*5+w,side*2,side*4-w,side*3,side*5};
        float Y[6]={1,7,13,18,22,25};float R[6]={1.1f,.9f,.8f,.7f,.45f,.2f};
        sk_tube(k,X,Y,R,6,mix(fin,0xe4dec0,.45f));
    }
    float X[16],Y[16];int n=0;
    /* Smooth sampled dome for the modern themes; a deliberately simple polygon for Original. */
    int steps=quiet||club?12:8;
    for(int i=0;i<=steps;i++){float a=3.14159f+i*3.14159f/steps;X[n]=17*sx*fast_cos(a);Y[n++]=2+17*sy*fast_sin(a);}
    X[n]=11*sx;Y[n++]=5;X[n]=-11*sx;Y[n++]=5;
    if(quiet){
        sk_poly(k,X,Y,n,body,155);
        /* Nested, translucent highlights preserve water behind the moon jelly. */
        sk_dot(k,-3,-6,10*sx,7*sy,mix(body,0xe8efd8,.6f),55,true);
        sk_dot(k,-5,-8,5*sx,3*sy,0xf0e8ce,55,true);
    }else if(club){
        lagoon_shape(k,X,Y,n,body);
        /* the scalloped hem: a cream crescent under each lobe of the bell */
        for(int i=0;i<6;i++){float x=(-14+i*5.6f)*sx;sk_dot(k,x,5.2f,3.1f,2.5f,rim,255,false);sk_dot(k,x,3.8f,2.9f,2.1f,body,255,false);}
    }else sk_poly(k,X,Y,n,body,255);
    if(!club){float xx[5]={-16*sx,-9*sx,0,9*sx,16*sx},yy[5]={2,4,5,4,2},rr[5]={.5f,.65f,.7f,.65f,.5f};sk_tube(k,xx,yy,rr,5,rim);}
    if(quiet){
        if(!fry)for(int j=0;j<4;j++){                             /* the four petal gonads */
            float xx[9],yy[9],rr[9];
            for(int i=0;i<9;i++){float a=i*TAU/8,dx=2+2*fast_cos(a),dy=1.5f*fast_sin(a),angle=j*TAU/4;
                xx[i]=dx*fast_cos(angle)-dy*fast_sin(angle);yy[i]=-4+dx*fast_sin(angle)+dy*fast_cos(angle);rr[i]=.22f;}
            sk_tube(k,xx,yy,rr,9,mix(rim,accent,.3f));
        }
        sk_line(k,-10,-5,-7,-10,.4f,.35f,0xe5e7cf);
        if(elder)for(int i=0;i<7;i++)sk_dot(k,-12+i*4,3+.5f*(i%2),.4f,.4f,rim,150,false);
    }else if(club){
        if(!fry)for(int i=0;i<3;i++)sk_dot(k,-7+i*7,-8-(i==1?3:0),1.5f,1.3f,mix(accent,0x995333,.5f),230,true);
        sk_line(k,-12,-9,-5,-14,.9f,.7f,rim);                     /* the parasol's shine */
        sk_dot(k,-4,-2,1.1f,1.1f,0x36584b,255,false);sk_dot(k,4,-2,1.1f,1.1f,0x36584b,255,false);
    }else{
        if(!fry)for(int i=0;i<3;i++)sk_mark(k,-7+i*4,-4,1.3f,2.7f,accent,240);
        sk_eye(k,11,-4,1.9f,false);
    }
}

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
