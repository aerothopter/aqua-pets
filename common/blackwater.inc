/* Blackwater (2026-10-12): the BLOCK creatures. Alvin: "make the blackwater theme characters
 * and decorations like minecraft ... give the creatures more colors and attractive". Every
 * creature is built of square blocks on a grid that rides with it (so the blocks never
 * shimmer as it swims): the same skeleton, local coordinates, yaw, pitch and live state as
 * the other themes' drawers, but each shape is rasterised to cells - a cell is in when its
 * centre is inside the polygon - and painted as voxels: a cell with open water above is a
 * lit top face, one with water below the dark underside, the ends a shaded side, and a
 * three-tone texture noise over all of it, as a block texture has. Fins and tails are flat
 * planes (no face light). A body turning to the glass folds to its cross-section, drawn as
 * a BOX of cells that widens with the turn, so head-on it is a cube with a face. Eyes are
 * the two-cell kind (a white cell, a black cell ahead of it). The colours are the keeper's
 * three (body, fin, accent) and bold mixes of them: the belly a pale wash of the fin colour,
 * the stripes and spots the accent, the fin tips the accent lifted toward white, so every
 * creature wears three or four strong colours. Cost: a 64 x 64 bit mask on the stack, a
 * polygon test a cell (a fish is ~150 cells), twelve dimmed tones a shape, spans a row. */
#define BW_DARK 0x06121c   /* the shade: the water's own dark */
#define BW_SUN  0xfff1d2   /* the sunlight */
#define BW_PALE 0xdfe1d6   /* a belly's silver */
#define BW_JELLY_CENTRE 6.0f   /* as jellyfish.inc's JELLY_CENTRE (defined after this file) */
#define MC_N 64
static inline uint32_t mc_hash(int a, int b) { uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u; h ^= h >> 13; h *= 0x5bd1e995u; return h ^ (h >> 15); }
typedef struct { const sk_t *k; int B; float gx, gy; int i0, i1, j0, j1; uint64_t row[MC_N]; } mc_t;
static void mc_begin(mc_t *m, const sk_t *k) {
    m->k = k;
    m->B = (int)(k->s * 2.2f + .5f);
    if (m->B < 2) m->B = 2;
    if (m->B > 5) m->B = 5;
    m->gx = k->x - (MC_N / 2) * m->B; m->gy = k->y - (MC_N / 2) * m->B;
    m->i0 = m->j0 = MC_N; m->i1 = m->j1 = -1;
    memset(m->row, 0, sizeof m->row);
}
static inline void mc_set(mc_t *m, int i, int j) {
    if ((unsigned)i >= MC_N || (unsigned)j >= MC_N) return;
    m->row[j] |= 1ull << i;
    if (i < m->i0) m->i0 = i;
    if (i > m->i1) m->i1 = i;
    if (j < m->j0) m->j0 = j;
    if (j > m->j1) m->j1 = j;
}
/* a polygon in screen space: every cell whose centre is inside (even-odd, so concave shapes
   work); a sliver smaller than a cell still marks the cell under its centre */
static void mc_add_screen_poly(mc_t *m, const float *X, const float *Y, int n) {
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) { x0 = fminf(x0, X[i]); x1 = fmaxf(x1, X[i]); y0 = fminf(y0, Y[i]); y1 = fmaxf(y1, Y[i]); }
    int ia = (int)floorf((x0 - m->gx) / m->B), ib = (int)floorf((x1 - m->gx) / m->B);
    int ja = (int)floorf((y0 - m->gy) / m->B), jb = (int)floorf((y1 - m->gy) / m->B);
    if (ia < 0) ia = 0;
    if (ja < 0) ja = 0;
    if (ib >= MC_N) ib = MC_N - 1;
    if (jb >= MC_N) jb = MC_N - 1;
    bool any = false;
    for (int j = ja; j <= jb; j++) {
        float cy = m->gy + (j + .5f) * m->B;
        for (int i = ia; i <= ib; i++) {
            float cx = m->gx + (i + .5f) * m->B; bool in = false;
            for (int a = 0, b = n - 1; a < n; b = a++)
                if ((Y[a] > cy) != (Y[b] > cy) && cx < X[a] + (X[b] - X[a]) * (cy - Y[a]) / (Y[b] - Y[a])) in = !in;
            if (in) { mc_set(m, i, j); any = true; }
        }
    }
    if (!any) mc_set(m, (int)floorf(((x0 + x1) * .5f - m->gx) / m->B), (int)floorf(((y0 + y1) * .5f - m->gy) / m->B));
}
static void mc_add_poly(mc_t *m, const float *lx, const float *ly, int n) {
    float X[24], Y[24];
    for (int i = 0; i < n; i++) sk_pt(m->k, lx[i], ly[i], &X[i], &Y[i]);
    mc_add_screen_poly(m, X, Y, n);
}
/* a block: an axis-aligned rectangle in local space (half sizes) */
static void mc_add_rect(mc_t *m, float x, float y, float hw, float hh) {
    float lx[4] = { x - hw, x + hw, x + hw, x - hw }, ly[4] = { y - hh, y - hh, y + hh, y + hh };
    mc_add_poly(m, lx, ly, 4);
}
/* the cross-section: a box of the body's depth across the glass, widening as it turns to face it */
static void mc_add_volume(mc_t *m, float x, float y, float depth, float ry) {
    const sk_t *k = m->k;
    float w = fabsf(sk_yaw(k, x)), f = sqrtf(fmaxf(0, 1 - w * w)), RX = depth * f * k->s, RY = ry * k->s;
    if (RX < m->B * .5f) return;
    float X, Y; sk_pt(k, x, y, &X, &Y);
    float px[4] = { X - RX, X + RX, X + RX, X - RX }, py[4] = { Y - RY, Y - RY, Y + RY, Y + RY };
    mc_add_screen_poly(m, px, py, 4);
}
/* a limb: a chain of quads along local points, r in local units (an arm, a tail, a leg) */
static void mc_add_tube(mc_t *m, const float *lx, const float *ly, const float *r, int n) {
    const sk_t *k = m->k; float X[SK_TUBE_MAX], Y[SK_TUBE_MAX];
    for (int i = 0; i < n; i++) sk_pt(k, lx[i], ly[i], &X[i], &Y[i]);
    for (int i = 0; i + 1 < n; i++) {
        float dx = X[i + 1] - X[i], dy = Y[i + 1] - Y[i], l = sqrtf(dx * dx + dy * dy) + 1e-3f;
        float nx = -dy / l, ny = dx / l, ra = fmaxf(r[i] * k->s, m->B * .5f), rb = fmaxf(r[i + 1] * k->s, m->B * .5f);
        float px[4] = { X[i] + nx * ra, X[i + 1] + nx * rb, X[i + 1] - nx * rb, X[i] - nx * ra };
        float py[4] = { Y[i] + ny * ra, Y[i + 1] + ny * rb, Y[i + 1] - ny * rb, Y[i] - ny * ra };
        mc_add_screen_poly(m, px, py, 4);
    }
}
/* a line of single cells between two local points (a seam, an antenna, a tentacle) */
static void mc_add_line(mc_t *m, float x0, float y0, float x1, float y1) {
    const sk_t *k = m->k; float X0, Y0, X1, Y1; sk_pt(k, x0, y0, &X0, &Y0); sk_pt(k, x1, y1, &X1, &Y1);
    float dx = X1 - X0, dy = Y1 - Y0, l = sqrtf(dx * dx + dy * dy); int n = (int)(l / (m->B * .5f)) + 1;
    for (int i = 0; i <= n; i++) { float u = (float)i / n; mc_set(m, (int)floorf((X0 + dx * u - m->gx) / m->B), (int)floorf((Y0 + dy * u - m->gy) / m->B)); }
}
static void mc_add_cell(mc_t *m, float lx, float ly) {
    float X, Y; sk_pt(m->k, lx, ly, &X, &Y);
    mc_set(m, (int)floorf((X - m->gx) / m->B), (int)floorf((Y - m->gy) / m->B));
}
/* paint the mask and clear it: voxel faces (flat = a plane: no faces), a three-tone texture */
static void mc_paint_d(mc_t *m, uint32_t rgb, bool flat, int a, float dim) {
    if (m->j1 < 0) return;
    ctx_t *c = m->k->c; src_t pal[4][3]; int seed = (m->k->f->variant & 3) * 17 + 3;
    for (int f = 0; f < 4; f++) {
        uint32_t t = f == 1 ? mix(rgb, 0xffffff, .26f) : f == 2 ? mix(rgb, 0x000000, .38f) : f == 3 ? mix(rgb, 0x000000, .18f) : rgb;
        pal[f][0] = src_color(mix(t, 0x000000, .09f), dim); pal[f][1] = src_color(t, dim); pal[f][2] = src_color(mix(t, 0xffffff, .07f), dim);
    }
    int B = m->B;
    for (int j = m->j0; j <= m->j1; j++) {
        uint64_t r = m->row[j]; if (!r) continue;
        uint64_t up = j ? m->row[j - 1] : 0, dn = j + 1 < MC_N ? m->row[j + 1] : 0;
        int y0 = (int)floorf(m->gy + j * B), y1 = (int)floorf(m->gy + (j + 1) * B) - 1;
        for (int i = m->i0; i <= m->i1; i++) {
            if (!((r >> i) & 1)) continue;
            int face = 0;
            if (!flat) face = !((up >> i) & 1) ? 1 : !((dn >> i) & 1) ? 2 : (!((r >> (i + 1)) & 1) || (i && !((r >> (i - 1)) & 1))) ? 3 : 0;
            const src_t *s = &pal[face][mc_hash(i + seed, j) % 3];
            int x0 = (int)floorf(m->gx + i * B), x1 = (int)floorf(m->gx + (i + 1) * B) - 1;
            for (int y = y0; y <= y1; y++) span(c, x0, x1, y, s, a);
        }
        m->row[j] = 0;
    }
    m->i0 = m->j0 = MC_N; m->i1 = m->j1 = -1;
}
static inline void mc_paint(mc_t *m, uint32_t rgb, bool flat, int a) { mc_paint_d(m, rgb, flat, a, m->k->c->dim); }
/* one flat cell at a local point */
static void mc_cell(mc_t *m, float lx, float ly, uint32_t rgb, int a) { mc_add_cell(m, lx, ly); mc_paint(m, rgb, true, a); }
/* a light of the creature's own (an esca, a glow squid's spots): undimmed, so it glows at night */
static void mc_glow(mc_t *m, float lx, float ly, uint32_t rgb, int a) { mc_add_cell(m, lx, ly); mc_paint_d(m, rgb, true, a, 1); }
/* the two-cell eye: white, and a black cell ahead of it; asleep, one dark cell */
static void mc_eye(mc_t *m, float lx, float ly) {
    const sk_t *k = m->k; float u = m->B / k->s;
    if (k->asleep) { mc_cell(m, lx, ly, mix(k->body, 0x000000, .55f), 255); return; }
    float dir = sk_yaw(k, lx) < 0 ? -1 : 1;
    mc_cell(m, lx, ly, 0xf6f6f6, 255);
    mc_cell(m, lx + dir * u * .999f, ly, 0x0a0a0c, 255);
}
/* a pattern cell on the flank: a juvenile or older, hidden through the turn */
static void mc_mark(mc_t *m, float lx, float ly, uint32_t rgb, int a) {
    const sk_t *k = m->k;
    if (!k->marked || fabsf(sk_yaw(k, lx)) < .3f) return;
    mc_cell(m, lx, ly, rgb, (int)(a * k->fade));
}
/* a run of pattern cells along a local line */
static void mc_mark_line(mc_t *m, float x0, float y0, float x1, float y1, uint32_t rgb, int a) {
    const sk_t *k = m->k;
    if (!k->marked || fabsf(sk_yaw(k, (x0 + x1) * .5f)) < .3f) return;
    mc_add_line(m, x0, y0, x1, y1); mc_paint(m, rgb, true, (int)(a * k->fade));
}
/* a flat plane off the side (a squid's fin, a hammer's lobe): local x,y with a depth z each */
static void mc_add_plane(mc_t *m, const float *lx, const float *ly, const float *lz, int n, float obl) {
    float X[12], Y[12];
    for (int i = 0; i < n; i++) sk_pt3(m->k, lx[i], ly[i], lz[i], obl, &X[i], &Y[i]);
    mc_add_screen_poly(m, X, Y, n);
}

static void draw_blackwater_species(sk_t *k, int species) {
    const fish_t *f = k->f; float t = k->t; int v = f->variant & 3;
    bool grown = f->stage >= STAGE_ADULT, elder = f->stage == STAGE_ELDER;
    uint32_t body = k->body, fin = k->fin, acc = k->acc;
    uint32_t dark = mix(body, 0x000000, .45f), belly = mix(fin, 0xffffff, .5f), tip = mix(acc, 0xffffff, .4f), pop = mix(acc, fin, .5f);
    if (species == SP_OCTOPUS && f->camo > 0) {
        body = mix(body, f->camo_rgb, clamp01(f->camo)); dark = mix(body, 0x000000, .45f);
        k->fade = 1 - clamp01(f->camo) * .8f;
    }
    mc_t M; mc_begin(&M, k); mc_t *m = &M; float U = M.B / k->s;   /* U: one cell in local units */
    switch (species) {
    case SP_FISH: {   /* a tropical fish: a slab body, a flared tail, a tall dorsal, bars / a band / spots / two-tone */
        float tail = k->asleep ? 0 : fast_sin(t * (8 + f->speed * .055f) + f->wander) * (.32f + f->speed * .007f);
        float tl = elder ? 1.2f : 1, wag = tail * 7;
        { float x[4] = { -12, -12, -25 * tl, -25 * tl }, y[4] = { -2.5f, 2.5f, 9 + wag, -9 + wag }; mc_add_poly(m, x, y, 4); mc_paint(m, fin, true, 255); }
        { float x[4] = { -12.5f, -12.5f, -17 * tl, -17 * tl }, y[4] = { -1.5f, 1.5f, 4 + wag * .5f, -4 + wag * .5f }; mc_add_poly(m, x, y, 4); mc_paint(m, tip, true, 255); }
        float dh = elder ? 6 : grown ? 4.5f : 3;
        mc_add_rect(m, -2, -6.5f - dh * .5f, 7, dh * .5f); mc_paint(m, fin, true, 255);                 /* the dorsal */
        mc_add_rect(m, -5, 7.8f, 4, 1.6f); mc_paint(m, fin, true, 255);                                /* the anal */
        { float x[7] = { 17, 14, -10, -13, -13, -10, 14 }, y[7] = { 0, -5.5f, -6.5f, -3.5f, 3.5f, 6.5f, 5.5f };
          mc_add_poly(m, x, y, 7); mc_add_volume(m, 0, 0, 5, 6.3f); mc_paint(m, body, false, 255); }
        mc_add_rect(m, 0, 5, 12, 1.4f); mc_paint(m, belly, true, 255);                                 /* the pale belly */
        if (fabsf(sk_yaw(k, 0)) >= .3f && k->marked) {
            if (v == 0) for (int i = 0; i < 3; i++) { mc_add_rect(m, -6 + i * 6, -.5f, .9f, 4.6f); mc_paint(m, acc, true, (int)(255 * k->fade)); }
            else if (v == 1) { mc_add_rect(m, 0, -1.5f, 12, 1.4f); mc_paint(m, acc, true, (int)(255 * k->fade)); mc_add_rect(m, 0, -4.5f, 11, .9f); mc_paint(m, tip, true, (int)(200 * k->fade)); }
            else if (v == 2) for (int i = 0; i < 6; i++) mc_mark(m, -8 + i * 4, (i & 1) ? -3 : 1, i % 3 ? acc : tip, 255);
            else { mc_add_rect(m, 0, -3.5f, 12, 2.6f); mc_paint(m, pop, true, (int)(255 * k->fade)); mc_add_rect(m, 12, -1, 2, 3); mc_paint(m, acc, true, (int)(255 * k->fade)); }
        }
        mc_add_rect(m, 3, 2.5f, 3, 1.3f); mc_paint(m, acc, true, 255);                                 /* the pectoral */
        mc_cell(m, 16.5f, 1.5f, dark, 255);                                                            /* the mouth */
        mc_eye(m, 11, -2.5f);
        break;
    }
    case SP_SEAHORSE: {
        float sw = k->asleep ? 0 : .8f * fast_sin(t * 1.3f + f->wander), fl = k->asleep ? 0 : 1.5f * fast_sin(t * 9 + f->jet * TAU);
        static const float TLX[10] = { -1.5f, -2.6f, -2.9f, -2.1f, 0, 3, 5.2f, 5.3f, 3.6f, 2.3f }, TLY[10] = { 6, 10, 14, 17.5f, 20, 21, 19.5f, 17, 16, 17.2f };
        static const float ALX[10] = { -1.5f, -2.6f, -2, .2f, 3, 5.2f, 7.4f, 7.2f, 5.2f, 3.4f }, ALY[10] = { 6, 9.5f, 12.8f, 14.4f, 13.4f, 13, 11, 8.4f, 7.5f, 8.6f };
        static const float TLR[10] = { 3.1f, 2.7f, 2.3f, 1.9f, 1.6f, 1.4f, 1.2f, 1, .85f, .7f };
        bool held = f->anchor >= 0 && f->speed < 6; float tx[10], ty[10];
        for (int i = 0; i < 10; i++) { tx[i] = (held ? ALX : TLX)[i] + (held ? 0 : sw) * i / 9.f; ty[i] = (held ? ALY : TLY)[i]; }
        mc_add_tube(m, tx, ty, TLR, 10); mc_paint(m, body, false, 255);                                /* the curled tail */
        for (int i = 1; i < 8; i += 2) mc_cell(m, tx[i], ty[i], acc, 220);                             /* its rings */
        { float x[4] = { -4.5f, -4.8f, -10 - fl, -9.5f + fl }, y[4] = { -3, 5, 6, -2 }; mc_add_poly(m, x, y, 4); mc_paint(m, fin, true, 255); }   /* the dorsal fan */
        mc_add_rect(m, -3.5f, -10.5f, 2, 1.5f); mc_paint(m, tip, true, 255);                             /* the little pectoral */
        { float bx[8] = { 2.5f, 5.5f, 5, 2, -2, -5.5f, -5.5f, -3.5f }, by[8] = { -9, -3, 5.5f, 9, 9, 5.5f, -3, -9 };
          mc_add_poly(m, bx, by, 8); mc_add_volume(m, 0, 0, 4, 8.5f); mc_paint(m, body, false, 255); }
        for (int j = 0; j < 5; j++) mc_mark_line(m, -5, -6 + j * 3.4f, 5, -6 + j * 3.4f, j & 1 ? acc : dark, 200);   /* the bony rings */
        mc_add_rect(m, 0, 2, 2, 6); mc_paint(m, belly, true, 200);                                     /* the pale belly */
        mc_add_rect(m, .3f, -11, 2.2f, 2.5f); mc_paint(m, body, false, 255);                           /* the neck */
        mc_add_rect(m, 1.2f, -14.5f, 4.2f, 3.3f); mc_add_volume(m, 1.2f, -14.5f, 3.5f, 3.3f); mc_paint(m, body, false, 255);   /* the head */
        mc_add_rect(m, 7.5f, -14, 3.5f, 1.2f); mc_paint(m, body, false, 255);                          /* the snout */
        mc_cell(m, 11, -14, dark, 255);
        mc_cell(m, -.5f, -18.5f, acc, 255); mc_cell(m, 2, -18.5f, acc, 255); mc_cell(m, .8f, -18.5f - U, tip, 255);   /* the coronet */
        if (v == 1) for (int j = 0; j < 6; j++) mc_mark(m, -3 + (j % 3) * 3, -6 + (j / 3) * 6, tip, 255);
        else if (v == 2) for (int j = 0; j < 4; j++) mc_mark(m, -2 + (j % 2) * 4, -7 + j * 3.5f, 0xffffff, 230);
        else if (v == 3) for (int j = 0; j < 4; j++) mc_cell(m, -6.5f, -7 + j * 4, acc, 255);
        mc_eye(m, 2.5f, -15);
        break;
    }
    case SP_OCTOPUS: {
        float jk = k->pose; draw_ink(k);
        float hx = 2.5f - 1.5f * jk, hy = -2.5f + 2.5f * jk;
        float ang = -2.3f - (TAU * .5f - 2.3f) * jk, ax = fast_cos(ang + TAU), ay = fast_sin(ang + TAU);
        float mrx = 7.8f + 2.7f * jk, mry = 6.8f - 1.3f * jk;
        float jp = f->jet - floorf(f->jet), pulse = jp < .3f ? fast_sin(jp * (TAU * .5f / .3f)) : 0;
        mry *= 1 - .12f * pulse * jk;
        float mcx = hx + ax * mrx * .85f, mcy = hy + ay * mrx * .85f;
        float rx0 = hx + 3 * jk, ry0 = hy + 3.2f * (1 - jk) + .5f * jk, floor_y = 9 + 40 * jk;
        static const float AC[8] = { .15f, .95f, 1.75f, 2.65f, .55f, 1.35f, 2.2f, 3 };
        float AX[8][6], AY[8][6], AR[6];
        for (int j = 0; j < 6; j++) AR[j] = 2.1f - 1.65f * j / 5.f;
        float open = jp < .3f ? .1f : .1f + .9f * fast_sin((jp - .3f) * (TAU * .5f / .7f)), wob = k->asleep ? 0 : 1;
        for (int i = 0; i < 8; i++) {
            float aj = ((i & 3) - 1.5f + (i >= 4 ? .5f : 0)) * .07f * (.5f + 1.5f * open);
            float ac = AC[i] + .22f * wob * fast_sin(t * (1.6f + f->speed * .04f) + i * 1.9f), a = ac + (aj - ac) * jk;
            float cc = (AC[i] < 1.57f ? -.3f : .3f) * (1 - jk) + .08f * fast_sin(t * 5 + i * 1.1f) * jk;
            float L = (19 + 5 * jk) * (1 + .08f * wob * fast_sin(t * 1.7f + i * 2.3f)), sl = L / 5;
            float x = rx0 + (i >= 4 ? .8f : 0), y = ry0 - (i >= 4 ? .6f : 0);
            for (int j = 0; j < 6; j++) {
                AX[i][j] = x; AY[i][j] = y;
                x += sl * fast_cos(a + TAU); y += sl * fast_sin(a + TAU);
                if (y > floor_y) { y = floor_y; a = fast_cos(a + TAU) >= 0 ? .05f : TAU * .5f - .05f; }
                a += cc * (.4f + j * .36f);
            }
        }
        for (int i = 4; i < 8; i++) mc_add_tube(m, AX[i], AY[i], AR, 6);
        mc_paint(m, dark, false, 255);                                                                 /* the far arms */
        { float lx[12], ly[12];                                                                        /* the mantle */
          for (int i = 0; i < 12; i++) { float a = i * TAU / 12, u = mrx * fast_cos(a), w = mry * fast_sin(a) * (1 + .15f * fast_cos(a)); lx[i] = mcx + ax * u - ay * w; ly[i] = mcy + ay * u + ax * w; }
          mc_add_poly(m, lx, ly, 12);
          { float X, Y; sk_pt(k, mcx, mcy, &X, &Y); float RX = k->zq * mry * k->s, RY = mry * k->s; if (RX >= M.B * .5f) { float px[4] = { X - RX, X + RX, X + RX, X - RX }, py[4] = { Y - RY, Y - RY, Y + RY, Y + RY }; mc_add_screen_poly(m, px, py, 4); } }
          mc_paint(m, body, false, 255); }
        for (int i = 0; i < 6; i++) {                                                                  /* the design over the mantle */
            float u = (-.55f + (i % 3) * .5f) * mrx, w = (i < 3 ? -.35f : .35f) * mry, px = mcx + ax * u - ay * w, py = mcy + ay * u + ax * w;
            if (v == 0) mc_mark(m, px, py, i & 1 ? acc : tip, 200);
            else if (v == 1) { mc_mark(m, px, py, acc, 255); mc_mark(m, px + U, py, tip, 255); }
            else if (v == 2) mc_mark_line(m, px, py - 2, px, py + 2, i & 1 ? acc : belly, 230);
            else mc_mark(m, px, py, acc, 255);
        }
        mc_add_rect(m, hx, hy, 5 - .8f * jk, 4.2f - .3f * jk); mc_add_volume(m, hx, hy, 4.2f, 4.2f - .3f * jk); mc_paint(m, body, false, 255);   /* the head */
        mc_add_rect(m, rx0, ry0 - .5f, 4.2f, 2.3f); mc_paint(m, mix(body, fin, .5f), false, 255);        /* the web */
        for (int i = 0; i < 4; i++) mc_add_tube(m, AX[i], AY[i], AR, 6);
        mc_paint(m, body, false, 255);                                                                 /* the near arms */
        if (jk < .6f) { int a = (int)(230 * (1 - jk));                                                 /* the suckers: pale cells along each */
            for (int i = 0; i < 4; i++) for (int j = 1; j < 5; j++) mc_cell(m, AX[i][j], AY[i][j] + AR[j] * .6f, j & 1 ? belly : tip, a); }
        { float ex = hx + 1.8f - 1.6f * jk, ey = hy - 2.8f;                                            /* the eye on its bulge */
          mc_add_rect(m, ex, ey, 2.6f, 2.3f); mc_paint(m, body, false, 255);
          if (k->asleep) mc_cell(m, ex, ey, dark, 255);
          else { mc_cell(m, ex - U * .5f, ey, 0xf6f6f6, 255); mc_cell(m, ex + U * .5f, ey, 0xf6f6f6, 255); mc_cell(m, ex, ey, 0x0a0a0c, 255); } }
        break;
    }
    case SP_SQUID: {   /* the squid: a box mantle, flat fins, hanging arms; the firefly design glows */
        float jk = k->pose, jp = f->jet - floorf(f->jet); draw_ink(k);
        float pulse = jp < .3f ? fast_sin(jp * (TAU * .5f / .3f)) : 0, my = 1 - .12f * pulse * jk;
        float big = v == 2 ? 1.6f : 1, wob = k->asleep ? 0 : 1;
        for (int side = -1; side <= 1; side += 2) {                                                    /* the fins */
            float rp = 1.3f * wob * fast_sin(jp * TAU * 2 + t * 3 + side + TAU);
            float lz[5] = { 3.4f * my, (8.5f + rp) * big * (1 - .45f * jk), (9.5f - rp) * big * (1 - .45f * jk), 1.2f, 0 };
            float lx[5] = { v == 2 ? -4.f : -8.f, -11.5f + rp * .3f, -18, -22.5f, -23 }, ly[5] = { 0, 0, 0, 0, 0 };
            for (int i = 0; i < 5; i++) lz[i] *= side;
            mc_add_plane(m, lx, ly, lz, 5, 1); mc_paint(m, fin, true, 255);
        }
        float ar[4] = { 1.2f, .95f, .7f, .45f };
        for (int i = 0; i < 6; i++) {                                                                  /* the arms */
            float a = ((i - 2.5f) * .16f + .14f * wob * fast_sin(t * 3 + i * 1.3f)) * (1 - jk * .85f);
            float x = 9.5f, y = (-2.5f + i) * my * (1 - .4f * jk), L = 3.2f + .4f * (i & 1), px[4], py[4];
            for (int j = 0; j < 4; j++) { px[j] = x; py[j] = y; x += L * fast_cos(a + TAU); y += L * fast_sin(a + TAU); a += (i < 3 ? -.08f : .08f) * (1 - jk); }
            mc_add_tube(m, px, py, ar, 4); mc_paint(m, (i == 0 || i == 5) ? dark : body, false, 255);
            if (i > 0 && i < 5 && jk < .6f) mc_cell(m, px[2], py[2] + ar[2] * .5f, belly, 180);
        }
        for (int q = 0; q < 2; q++) {                                                                  /* the two long tentacles, clubbed */
            float a = (q ? .12f : -.1f) + .18f * wob * fast_sin(t * 2.2f + q * 2) * (1 - jk), x = 9.5f, y = (q ? .8f : -.8f) * my, L = (20 - 11 * jk) / 5;
            float px[6], py[6], pr[6] = { .7f, .6f, .5f, .45f, .4f, .4f };
            for (int j = 0; j < 6; j++) { px[j] = x; py[j] = y; x += L * fast_cos(a + TAU); y += L * fast_sin(a + TAU); a += .04f * (q ? 1 : -1); }
            mc_add_tube(m, px, py, pr, 6); mc_paint(m, mix(body, acc, .4f), false, 255);
            mc_cell(m, px[5], py[5], tip, 255);
        }
        { float x[8] = { 6, 4, -9, -21, -23, -21, -9, 4 }, y[8] = { 0, -5 * my, -5 * my, -2.5f * my, 0, 2.5f * my, 5 * my, 5 * my };
          mc_add_poly(m, x, y, 8); mc_add_volume(m, -6, 0, 4.5f, 5 * my); mc_paint(m, body, false, 255); }
        mc_add_rect(m, -8, 3.6f * my, 12, 1); mc_paint(m, belly, true, 220);                           /* the pale underside */
        mc_cell(m, 3, 4.5f * my, dark, 255);                                                           /* the siphon */
        for (int i = 0; i < 8; i++) {                                                                  /* chromatophores, or the firefly's lights */
            float px = 2 - i * 2.8f, py = (i % 2 ? -2.2f : 1.8f) * my;
            if (v == 1 && k->marked) mc_glow(m, px, py, i & 1 ? 0x7af6ff : 0xd8fcff, k->c->dim < .8f ? 255 : 200);
            else mc_mark(m, px, py, i & 1 ? acc : tip, 230);
        }
        mc_add_rect(m, 7.5f, 0, 3.4f, 3.6f * my); mc_add_volume(m, 7.5f, 0, 3.2f, 3.6f * my); mc_paint(m, body, false, 255);   /* the head */
        mc_eye(m, 6.5f, -.8f);
        break;
    }
    case SP_PUFFER: {   /* the pufferfish: a cube that swells; its spikes stand out as single cells */
        float puff = clamp01(f->puff), rx = 11 + 4 * puff, ry = 8.5f + 6.5f * puff;
        float beat = k->asleep ? 0 : fast_sin(t * 7 + f->wander);
        { float x[4] = { -rx + 1, -rx + 1, -rx - 7, -rx - 7 }, y[4] = { -2.5f, 2.5f, 6 + beat * 1.5f, -6 + beat * 1.5f }; mc_add_poly(m, x, y, 4); mc_paint(m, fin, true, 255); }
        mc_add_rect(m, -5, -ry - 2, 3.5f, 2); mc_paint(m, fin, true, 255);
        mc_add_rect(m, -5, ry + 2, 3.5f, 2); mc_paint(m, fin, true, 255);
        if (puff > .05f) for (int i = 0; i < 14; i++) {
            float a = i * TAU / 14, ca = fast_cos(a), sa = fast_sin(a);
            mc_add_line(m, ca * rx * .9f, sa * ry * .9f, ca * (rx + 1 + 3.5f * puff), sa * (ry + 1 + 3.5f * puff)); mc_paint(m, i & 1 ? tip : acc, true, 255);
        }
        { float x[8] = { rx, rx * .7f, -rx * .7f, -rx, -rx, -rx * .7f, rx * .7f, rx }, y[8] = { -ry * .55f, -ry, -ry, -ry * .55f, ry * .55f, ry, ry, ry * .55f };
          mc_add_poly(m, x, y, 8); mc_add_volume(m, 0, 0, rx * .8f, ry); mc_paint(m, body, false, 255); }
        mc_add_rect(m, 1, ry * .6f, rx * .8f, ry * .38f); mc_paint(m, belly, true, 255);                 /* the pale belly */
        if (v == 0) for (int i = 0; i < 8; i++) mc_mark(m, -8 + (i % 4) * 5, -ry * .55f + (i / 4) * 4, i & 1 ? dark : acc, 255);
        else if (v == 1) { if (fabsf(sk_yaw(k, 0)) >= .3f && k->marked) { mc_add_rect(m, rx * .5f, -ry * .3f, 3.6f, 3.2f); mc_paint(m, acc, true, (int)(255 * k->fade)); } }
        else if (v == 2) for (int i = 0; i < 3; i++) mc_mark_line(m, -7 + i * 7, -ry * .9f, -7 + i * 7, -ry * .15f, i & 1 ? tip : acc, 255);
        else for (int i = 0; i < 5; i++) mc_mark(m, -8 + i * 4, -ry * .3f, acc, 255);
        mc_add_rect(m, 1, 1.5f, 2.5f, 1.3f); mc_paint(m, acc, true, 255);                               /* the pectoral */
        mc_cell(m, rx - .5f, 1.5f, 0xf3ecd8, 255); mc_cell(m, rx - .5f, 1.5f + U, dark, 255);            /* the beak and the mouth */
        mc_eye(m, rx * .55f, -ry * .35f);
        break;
    }
    case SP_ANGLER: {
        float beat = k->asleep ? 0 : fast_sin(t * 3 + f->wander), gape = .3f + (f->sp_mode == SPM_LUNGE ? .7f : 0);
        uint32_t lure = v == 0 ? 0x8ff6ff : v == 3 ? 0xfff07a : mix(acc, 0xffffff, .4f);
        { float x[4] = { -19, -19, -26, -26 }, y[4] = { -2.5f, 2.5f, 7 + beat, -7 + beat }; mc_add_poly(m, x, y, 4); mc_paint(m, fin, true, 255); }
        mc_add_rect(m, -10, -9, 4, 2); mc_paint(m, fin, true, 255); mc_add_rect(m, -10, 9, 4, 2); mc_paint(m, fin, true, 255);
        { float x[8] = { 18, 12, -2, -14, -20, -20, -14, 10 }, y[8] = { 0, -9, -10.5f, -6, -2, 2, 7, 8.5f };
          mc_add_poly(m, x, y, 8); mc_add_volume(m, 2, -1, 6, 8.5f); mc_paint(m, body, false, 255); }
        { float x[5] = { 5, 10, 17, 10, 5 }, y[5] = { 8, 4.5f, 2.5f, 7.5f + gape * 2, 9 + gape }; mc_add_poly(m, x, y, 5); mc_paint(m, 0x1a0a0e, true, 255); }   /* the mouth */
        { float x[4] = { 3, 10, 17, 9 }, y[4] = { 8.5f, 7 + gape * 2, 4 + gape * 4, 10.5f + gape * 2 }; mc_add_poly(m, x, y, 4); mc_paint(m, mix(body, acc, .3f), false, 255); }   /* the jaw */
        for (int i = 0; i < 4; i++) mc_cell(m, 14 - i * 2.4f, 3.4f + (14 - i * 2.4f < 10 ? 2 : 0) + (15 - (14 - i * 2.4f)) * .4f, 0xf6f2e4, 255);   /* teeth */
        for (int i = 0; i < 3; i++) mc_cell(m, 11 + i * 2.2f, 6.5f + gape * 2.5f - i * .5f, 0xf6f2e4, 255);
        if (v == 1) for (int i = 0; i < 6; i++) mc_mark(m, -6 + (i % 3) * 6, -6 + (i / 3) * 5, i & 1 ? tip : acc, 230);
        else if (v == 2) for (int i = 0; i < 4; i++) mc_mark_line(m, -10 + i * 5, -5, -10 + i * 5, 4, i & 1 ? acc : pop, 200);
        else for (int i = 0; i < 5; i++) mc_mark(m, -9 + i * 5, (i % 2 ? -4 : 2), acc, 230);
        mc_add_rect(m, 1, 5.5f, 4.5f, 1.4f); mc_paint(m, belly, true, 220);
        { float px[3] = { 0, -2, -3.5f }, py[3] = { 6, 9, 11.5f }, pr[3] = { 1.7f, 1.4f, 1.1f }; mc_add_tube(m, px, py, pr, 3); mc_paint(m, fin, false, 255); }   /* the lobe it walks on */
        mc_add_line(m, 3, -10, 3, -14); mc_add_line(m, 3, -14, 8.5f, -18.5f); mc_add_line(m, 8.5f, -18.5f, 11, -17 + beat); mc_paint(m, fin, true, 255);   /* the illicium */
        mc_glow(m, 11, -17 + beat, lure, 150 + (int)(105 * f->lure));
        mc_glow(m, 11 - U, -17 + beat, lure, 60 + (int)(120 * f->lure)); mc_glow(m, 11 + U, -17 + beat, lure, 60 + (int)(120 * f->lure));
        mc_glow(m, 11, -17 + beat - U, lure, 60 + (int)(120 * f->lure)); mc_glow(m, 11, -17 + beat + U, lure, 60 + (int)(120 * f->lure));
        mc_eye(m, 8, -4.5f);
        break;
    }
    case SP_EEL: {
        float x[16], y[16], r[16], uy[16], ur[16];
        for (int i = 0; i < 16; i++) {
            float u = i / 15.f; x[i] = 23 - u * 60; y[i] = fast_sin(u * 7 - t * (k->asleep ? .5f : 3) + TAU * 64) * u * 6; r[i] = 4.1f * (1 - u) + .4f;
            uy[i] = y[i] - r[i] * 1.1f; ur[i] = r[i] * .5f + .4f;
        }
        mc_add_tube(m, x, uy, ur, 16); mc_paint(m, fin, true, 255);                                    /* the fin ridge along the back */
        mc_add_tube(m, x, y, r, 16); mc_paint(m, body, false, 255);
        { float by[16], br[16]; for (int i = 0; i < 16; i++) { by[i] = y[i] + r[i] * .5f; br[i] = r[i] * .35f; } mc_add_tube(m, x, by, br, 16); mc_paint(m, v == 2 ? mix(body, 0xf0d8a8, .5f) : belly, true, 230); }   /* the belly's colour */
        if (v == 3) for (int i = 1; i < 13; i += 2) mc_mark(m, x[i], y[i] - r[i] * .2f, i & 2 ? acc : tip, 255);
        else if (v == 0 || v == 1) for (int i = 2; i < 13; i += 3) mc_mark_line(m, x[i], y[i] - r[i] * .9f, x[i], y[i] + r[i] * .6f, acc, 230);
        else for (int i = 2; i < 14; i += 2) mc_mark(m, x[i], y[i] - r[i] * .3f, acc, 200);
        mc_add_rect(m, 21, 0, 6.5f, 3.9f); mc_add_volume(m, 21, 0, 3.5f, 3.9f); mc_paint(m, body, false, 255);   /* the blunt head */
        mc_add_line(m, 27, 1.5f, 19, 1.5f); mc_paint(m, dark, true, 255);                               /* the jaw */
        mc_cell(m, 16.5f, 0, dark, 200);                                                               /* the gill */
        mc_eye(m, 24, -1.5f);
        if (f->spark > 0) for (int j = 0; j < 4; j++) {                                                /* the electric arcs */
            float xx = -25 + j * 12, sgn = j % 2 ? 1 : -1, w = fast_sin(t * 18 + j) * 2;
            mc_add_line(m, xx, sgn * 5, xx + 3 + w, sgn * 9); mc_add_line(m, xx + 3 + w, sgn * 9, xx + 1, sgn * 13); mc_paint_d(m, 0xd8f8ff, true, 255, 1);
            mc_glow(m, xx + 3 + w, sgn * 9, 0x9ad8ff, 160);
        }
        break;
    }
    case SP_SHARK: {
        float beat = k->asleep ? 0 : fast_sin(f->jet * TAU);
        uint32_t white = v == 3 ? mix(body, 0xf4f6f0, .7f) : 0xe4e8e4;
        { float x[4] = { -21, -21, -33, -33 }, y[4] = { -2, 1, -13 + beat * 2, -3 + beat }; mc_add_poly(m, x, y, 4); mc_paint(m, fin, true, 255); }   /* the tail, the upper lobe long */
        { float x[4] = { -21, -21, -28, -28 }, y[4] = { 0, 2.5f, 6 + beat * 1.5f, 1 + beat }; mc_add_poly(m, x, y, 4); mc_paint(m, fin, true, 255); }
        mc_add_rect(m, -2, 5.5f, 4, 2); mc_paint(m, mix(fin, 0x000000, .4f), true, 255);                 /* the far pectoral */
        { float x[8] = { 17, 13, -8, -20, -22, -20, -8, 13 }, y[8] = { -1, -5, -5.2f, -2.2f, 0, 1.8f, 4.8f, 3 };
          mc_add_poly(m, x, y, 8); mc_add_volume(m, -2, 0, 4.5f, 4.8f); mc_paint(m, body, false, 255); }
        mc_add_rect(m, -2, 3.2f, 13, 1.5f); mc_paint(m, white, true, 255);                             /* the white belly */
        { float x[4] = { 2, -8, -9, -2 }, y[4] = { -5, -4.8f, -8, -15 }; mc_add_poly(m, x, y, 4); mc_paint(m, fin, true, 255); }   /* the first dorsal */
        mc_add_rect(m, -16, -4.5f, 2, 2); mc_paint(m, fin, true, 255);                                 /* the second */
        { float x[4] = { 6, 1, -8, -3 }, y[4] = { 3, 4.2f, 8.5f, 10.5f }; mc_add_poly(m, x, y, 4); mc_paint(m, acc, true, 255); }   /* the near pectoral, in the accent */
        mc_add_rect(m, -12, 5.5f, 2.5f, 1.5f); mc_paint(m, acc, true, 255);
        for (int i = 0; i < 4; i++) mc_add_line(m, 9 - i * 1.5f * fmaxf(U, 1), -2, 9 - i * 1.5f * fmaxf(U, 1), 2);
        mc_paint(m, dark, true, 230);                                                                  /* the gill slits */
        if (v == 1 || v == 2) for (int i = 0; i < 4; i++) mc_mark(m, 4 - i * 6, -3.5f, i & 1 ? acc : tip, 230);
        mc_add_line(m, 15, 2.5f, 11, 2.8f); mc_paint(m, dark, true, 255);                               /* the mouth, underslung */
        { static const float HX[8] = { 18.5f, 18, 14, 12.5f, 12.5f, 14, 18, 18.5f }, HZ[8] = { 0, -8, -10, -6, 6, 10, 8, 0 }; float HY[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
          mc_add_plane(m, HX, HY, HZ, 8, .3f); mc_paint(m, body, false, 255);                           /* the hammer */
          float ex, ey; sk_pt3(k, 16, 0, -9, .3f, &ex, &ey); mc_set(m, (int)floorf((ex - M.gx) / M.B), (int)floorf((ey - M.gy) / M.B));
          sk_pt3(k, 16, 0, 9, .3f, &ex, &ey); mc_set(m, (int)floorf((ex - M.gx) / M.B), (int)floorf((ey - M.gy) / M.B)); mc_paint(m, k->asleep ? dark : 0x0a0a0c, true, 255); }
        break;
    }
    case SP_SWORDFISH: {
        float beat = k->asleep ? 0 : fast_sin(f->jet * TAU);
        uint32_t bill = mix(fin, 0x000000, .45f);
        { float x[4] = { -19, -19, -30, -30 }, y[4] = { -1.5f, 1.5f, 12 + beat * 1.6f, -12 + beat * 2.2f }; mc_add_poly(m, x, y, 4); mc_paint(m, fin, true, 255); }   /* the lunate tail */
        mc_add_rect(m, 3, 5, 3, 2); mc_paint(m, mix(fin, 0x000000, .4f), true, 255);
        { float x[8] = { 16, 12, -7, -19, -21.5f, -19, -7, 12 }, y[8] = { -1.6f, -5.3f, -5.6f, -2, 0, 2, 4.8f, 3.5f };
          mc_add_poly(m, x, y, 8); mc_add_volume(m, 0, 0, 4.4f, 5.2f); mc_paint(m, body, false, 255); }
        mc_add_rect(m, -3, 3.4f, 13, 1.4f); mc_paint(m, mix(acc, 0xf4f6f8, .5f), true, 255);           /* the silver belly */
        { float x[4] = { 5, -10, -8, -1 }, y[4] = { -4.6f, -4.2f, -9, -18.5f }; mc_add_poly(m, x, y, 4); mc_paint(m, acc, true, 255); }   /* the tall sickle dorsal, in the accent */
        mc_add_rect(m, -16, -4, 2, 1.8f); mc_paint(m, fin, true, 255); mc_add_rect(m, -15.5f, 4.2f, 2, 1.5f); mc_paint(m, fin, true, 255);
        { float x[4] = { 6, 1, -5, -1 }, y[4] = { 3, 4.2f, 9, 10 }; mc_add_poly(m, x, y, 4); mc_paint(m, fin, true, 255); }   /* the near pectoral */
        mc_mark_line(m, 12, -.5f, -17, .3f, mix(body, 0x000000, .4f), 200);                             /* the lateral line */
        if (v == 1 || v == 2) for (int i = 0; i < 5; i++) mc_mark_line(m, 9 - i * 5.5f, -4.5f, 9 - i * 5.5f, 2.5f, i & 1 ? tip : acc, 200);   /* bars */
        mc_add_rect(m, -21, 0, 1.5f, 1.3f); mc_paint(m, fin, true, 255);                                 /* the keel */
        mc_add_line(m, 15.5f, -1, 34, -.3f); mc_paint(m, bill, true, 255);                               /* the bill */
        mc_add_line(m, 16, -1.8f, 24, -1.4f); mc_paint(m, tip, true, 255);
        mc_cell(m, 14, 2.5f, dark, 255);
        mc_eye(m, 10.5f, -1.8f);
        break;
    }
    case SP_TURTLE: {   /* a flat shell of scute blocks over a pale plastron, a head block, paddle flippers */
        float st = k->asleep ? .6f : fast_sin(f->jet * TAU);
        uint32_t skin = fin, fdark = mix(fin, 0x000000, .45f), seam = mix(body, 0x000000, .55f);
        { float ang = .35f + .8f * -st * .7f, cx = 6.5f, cy = -.5f; float lx[4] = { cx - 1.5f, cx + 1.5f, cx + 1.5f + fast_cos(ang) * 9, cx - 2 + fast_cos(ang) * 9 }, ly[4] = { cy, cy, cy + fast_sin(ang) * 9, cy + fast_sin(ang) * 9 }, lz[4] = { 5, 5, 7, 7 };
          mc_add_plane(m, lx, ly, lz, 4, .3f); mc_paint(m, fdark, true, 255); }                           /* the far flipper */
        { float lx[3] = { -12, -15, -19 }, ly[3] = { -1, 0, -4 }, r[3] = { 1.4f, 1.2f, .9f }; mc_add_tube(m, lx, ly, r, 3); mc_paint(m, fdark, false, 255); }   /* the far rear flipper */
        mc_add_poly(m, TURTLE_CX, TURTLE_CY, 10); mc_add_volume(m, -2, -1.8f, 8.5f, 7.2f); mc_paint(m, body, false, 255);   /* the carapace */
        mc_add_rect(m, -2, 3.2f, 12, 1.6f); mc_paint(m, mix(acc, 0xffffff, .45f), true, 255);           /* the plastron */
        mc_mark_line(m, 10.5f, -1.3f, -14.5f, -3.3f, seam, 220);                                         /* the rim */
        if (fabsf(sk_yaw(k, -2)) >= .45f && k->marked) {
            if (v == 0) { for (int i = 0; i < 5; i++) mc_mark_line(m, 8 - i * 5.2f, -5.5f, 7.5f - i * 5.2f, -.5f, seam, 220); mc_mark_line(m, 9.5f, -4.5f, -13.5f, -5.2f, seam, 220);
                          for (int i = 0; i < 4; i++) mc_mark(m, 5.5f - i * 5.2f, -5.6f, i & 1 ? acc : tip, 230); }
            else if (v == 1) for (int i = 0; i < 6; i++) mc_mark(m, 7 - i * 4.2f, -4.8f + (i & 1) * 1.8f, i & 1 ? acc : fin, 230);
            else if (v == 2) for (int i = 0; i < 3; i++) mc_mark_line(m, 9.5f, -6.8f + i * 2.4f, -13.5f, -6.2f + i * 2.4f, i & 1 ? acc : tip, 230);
            else for (int i = 0; i < 7; i++) mc_mark(m, 7.5f - i * 3.9f, -5.2f + (i & 1) * 2.6f, i % 3 ? tip : acc, 230);
        }
        { float lx[3] = { -11, -14, -18 }, ly[3] = { 2, 3, 7.5f }, r[3] = { 1.6f, 1.3f, 1 }; mc_add_tube(m, lx, ly, r, 3); mc_paint(m, skin, false, 255); }   /* the near rear flipper */
        mc_add_rect(m, -15, 1.2f, 3, 1); mc_paint(m, skin, false, 255);                                 /* the tail */
        mc_add_rect(m, 12, .5f, 2.5f, 2.2f); mc_paint(m, skin, false, 255);                             /* the neck */
        mc_add_rect(m, 17, -.3f, 4, 3); mc_add_volume(m, 17, -.3f, 3.6f, 3); mc_paint(m, skin, false, 255);   /* the head */
        mc_cell(m, 20.5f, .8f, fdark, 255);                                                            /* the mouth */
        mc_eye(m, 17.5f, -1.5f);
        { float ang = .35f + .8f * st, cx = 6.5f, cy = 1.5f; float lx[4] = { cx - 1.5f, cx + 1.5f, cx + 1.5f + fast_cos(ang) * 9.5f, cx - 2 + fast_cos(ang) * 9.5f }, ly[4] = { cy, cy, cy + fast_sin(ang) * 9.5f, cy + fast_sin(ang) * 9.5f }, lz[4] = { -5, -5, -7, -7 };
          mc_add_plane(m, lx, ly, lz, 4, .3f); mc_paint(m, skin, false, 255);
          mc_cell(m, cx + fast_cos(ang) * 8, cy + fast_sin(ang) * 8 + -5 * .3f + .5f, fdark, 200); }      /* the near flipper, a dark cell on its paddle */
        turtle_gulp(k, 21, -1);
        break;
    }
    case SP_CRAB: {
        sk_t crab = *k; crab.yh = crab.yh < 0 ? -1 : 1; crab.yt = crab.yh; k = &crab; M.k = k;   /* face-on: the carapace keeps its width */
        float ph = f->jet * TAU, mv = clamp01(f->speed / 6), threat = clamp01(f->puff), raise = threat * 6;
        float snap = k->asleep ? 0 : .6f * fmaxf(0, fast_sin(t * 2.5f + f->wander));
        for (int side = -1; side <= 1; side += 2) for (int i = 0; i < 4; i++) {                        /* eight jointed legs */
            float stp = mv * 1.6f * fast_sin(ph + i * 1.6f + (side > 0 ? 0 : 2.5f) + TAU), lift = mv * fmaxf(0, fast_sin(ph + i * 1.6f + 1.5f + (side > 0 ? 0 : 2.5f) + TAU)) * 2;
            float lx[3] = { side * (4 + i * 2.f), side * (10 + i * 2.2f) + stp * .3f, side * (12 + i * 2.4f) + stp }, ly[3] = { 2 + i * .5f, -2 + i * 2.5f - lift, 8 + i * .8f - lift * .4f }, lr[3] = { .9f, .7f, .5f };
            mc_add_tube(m, lx, ly, lr, 3); mc_paint(m, fin, false, 255);
            mc_cell(m, lx[1], ly[1], acc, 255);                                                        /* the knee */
        }
        for (int side = -1; side <= 1; side += 2) {                                                    /* the claws, raised in a threat */
            float big = side > 0 ? 1.15f : .9f, open = threat * 1.5f + snap;
            float ax[3] = { side * 5.f, side * 10.f, side * 13.f }, ay[3] = { 1, -1 - raise * .5f, -3 - raise }, ar[3] = { 1.3f, 1.1f, 1 };
            mc_add_tube(m, ax, ay, ar, 3); mc_paint(m, body, false, 255);
            mc_add_rect(m, side * 15.5f, -4.5f - raise, 3.6f * big, 2.6f * big); mc_paint(m, body, false, 255);
            mc_add_rect(m, side * 20.f, -3.5f - raise, 2, 1); mc_paint(m, acc, false, 255);
            mc_add_rect(m, side * 19.5f, -6.5f - raise - open, 2, 1); mc_paint(m, acc, false, 255);
            mc_cell(m, side * 15.5f, -5 - raise, dark, 200);
        }
        { float cx[8] = { -11, -8, 0, 8, 11, 8, 0, -8 }, cy[8] = { 0, -5.5f, -6.3f, -5.5f, 0, 5.5f, 6.5f, 5.5f }; mc_add_poly(m, cx, cy, 8); mc_paint(m, body, false, 255); }   /* the carapace */
        for (int i = 0; i < 3; i++) for (int side = -1; side <= 1; side += 2) mc_cell(m, side * (5.5f + i * 2), -6.5f + i * 1.1f, tip, 255);   /* its teeth */
        mc_add_rect(m, 0, 4.5f, 8, 1); mc_paint(m, belly, true, 220);
        if (v == 2) for (int i = 0; i < 6; i++) mc_mark(m, -7 + (i % 3) * 7, (i < 3 ? -3 : 1.5f), i & 1 ? acc : tip, 255);
        else if (v == 3) for (int i = 0; i < 3; i++) mc_mark(m, -5 + i * 5, -2, acc, 255);
        else for (int i = 0; i < 4; i++) mc_mark(m, -6 + i * 4, -2.5f, i & 1 ? acc : dark, 200);
        for (int side = -1; side <= 1; side += 2) {                                                    /* the eye stalks */
            mc_add_line(m, side * 3.f, -5, side * 4.f, -8.5f); mc_paint(m, body, true, 255);
            mc_cell(m, side * 4.f, -9.5f, k->asleep ? dark : 0x0a0a0c, 255);
            if (!k->asleep) mc_cell(m, side * 4.f + side * U, -9.5f, 0xf6f6f6, 255);
        }
        mc_cell(m, -1.3f, 6.2f, belly, 230); mc_cell(m, 1.3f, 6.2f, belly, 230);                       /* the mouthparts */
        break;
    }
    case SP_LOBSTER: {
        float curl = f->sp_mode == SPM_FLIP && f->jet > .5f ? clamp01((f->jet - .5f) * 8) : 0;
        float ph = t * (2 + clamp01(f->speed / 20) * 7), mv = clamp01(f->speed / 6), wob = k->asleep ? 0 : 1;
        uint32_t ant = v == 0 ? acc : mix(body, fin, .5f), shade = mix(body, 0x000000, .4f);
        for (int a = 0; a < 2; a++) {                                                                  /* the antennae */
            float ang = -.5f - a * .3f + .2f * wob * fast_sin(t * 1.3f + a * 2 + f->wander), x = 13, y = -3.5f;
            for (int j = 0; j < 6; j++) { float nx = x + 4.2f * fast_cos(ang + TAU), ny = y + 4.2f * fast_sin(ang + TAU); mc_add_line(m, x, y, nx, ny); x = nx; y = ny; ang += .16f + .03f * wob * fast_sin(t * 2 + j); }
            mc_paint(m, a ? shade : ant, true, 255);
        }
        { float ax[3] = { 8, 14, 19 }, ay[3] = { -1, -3, -3.5f }, ar[3] = { 1, .9f, .8f }; mc_add_tube(m, ax, ay, ar, 3); mc_add_rect(m, 23, -3.5f, 3.5f, 2); mc_add_rect(m, 27.5f, -4.5f, 1.5f, 1); mc_paint(m, shade, false, 255); }   /* the far claw */
        for (int j = 0; j < 4; j++) {                                                                  /* the far legs */
            float stp = mv * 1.8f * fast_sin(ph + j * 1.6f + 2.4f + TAU), lift = mv * fmaxf(0, fast_sin(ph + j * 1.6f + 4 + TAU)) * 1.4f, bx = 9.5f - j * 3.2f;
            float lx[3] = { bx + .8f, bx + 2.5f, bx + 1.2f + stp }, ly[3] = { 2.5f, 5 - lift, 7 - lift * .6f }, lr[3] = { .7f, .55f, .45f };
            mc_add_tube(m, lx, ly, lr, 3); mc_paint(m, shade, false, 255);
        }
        float sx[6], sy[6];
        for (int i = 0; i < 6; i++) { sx[i] = -6 - i * 3.2f * (1 - .45f * curl); sy[i] = 1 + curl * i * i * .3f; }
        float fx = sx[5] - 3.2f * (1 - .45f * curl), fy = sy[5] + curl * 6;
        for (int j = 0; j < 3; j++) { mc_add_rect(m, fx - 1, fy + (j - 1) * 2.4f, 3.2f, 1.1f); mc_paint(m, j & 1 ? tip : fin, true, 255); }   /* the tail fan */
        for (int i = 5; i >= 0; i--) {                                                                 /* the abdomen: six plates */
            mc_add_rect(m, sx[i], sy[i], 1.9f, 3.3f - i * .25f); mc_add_volume(m, sx[i], sy[i], 3, 3.3f - i * .25f); mc_paint(m, i & 1 ? body : mix(body, acc, .35f), false, 255);
            if (i > 0) mc_cell(m, sx[i], sy[i] + 3.2f - i * .25f, belly, 200);                        /* a swimmeret */
            if (v == 2) mc_mark(m, sx[i], sy[i] - 1, tip, 230);
            if (v == 3 && i % 2) mc_mark(m, sx[i], sy[i] + .5f, acc, 230);
        }
        { float cx[8] = { 12, 10, 0, -5, -6, -5, 0, 10 }, cy[8] = { -1.5f, -5.5f, -5.8f, -4, 0, 4, 5.8f, 4.5f }; mc_add_poly(m, cx, cy, 8); mc_add_volume(m, 3, 0, 4, 5); mc_paint(m, body, false, 255); }   /* the cephalothorax */
        mc_add_line(m, 12, -2.5f, 17, -4.5f); mc_paint(m, body, true, 255);                             /* the rostrum */
        for (int i = 0; i < 4; i++) mc_cell(m, 8 - i * 3, -3.8f, i & 1 ? acc : tip, 200);
        if (v == 3) mc_mark(m, 3, 0, acc, 230);
        for (int j = 0; j < 4; j++) {                                                                  /* the near legs */
            float stp = mv * 1.8f * fast_sin(ph + j * 1.6f + .8f + TAU), lift = mv * fmaxf(0, fast_sin(ph + j * 1.6f + 2.4f + TAU)) * 1.4f, bx = 9 - j * 3.2f;
            float lx[3] = { bx, bx + 1.5f + stp * .5f, bx - .5f + stp }, ly[3] = { 3.5f, 6.5f - lift, 9.5f - lift * .6f }, lr[3] = { .75f, .6f, .45f };
            mc_add_tube(m, lx, ly, lr, 3); mc_paint(m, fin, false, 255);
        }
        { float snapc = k->asleep ? 0 : 1.2f * fmaxf(0, fast_sin(t * 2 + f->wander));                  /* the near claw: the crusher */
          float ax[3] = { 8, 14, 18 }, ay[3] = { 2.5f, 4.5f, 4 }, ar[3] = { 1.3f, 1.2f, 1.1f }; mc_add_tube(m, ax, ay, ar, 3); mc_paint(m, body, false, 255);
          mc_add_rect(m, 21.5f, 3.2f, 4.3f, 2.8f); mc_paint(m, body, false, 255);
          mc_add_rect(m, 27, 3.5f, 2.5f, 1); mc_paint(m, acc, false, 255);
          mc_add_rect(m, 26.5f, .5f - snapc, 2.5f, 1); mc_paint(m, acc, false, 255);
          for (int i = 0; i < 3; i++) mc_cell(m, 19.5f + i * 1.6f, 1.4f, tip, 200); }
        mc_add_line(m, 11, -3, 13, -5); mc_paint(m, body, true, 255);
        mc_cell(m, 13.5f, -5.5f, k->asleep ? shade : 0x0a0a0c, 255);
        break;
    }
    case SP_JELLYFISH: {   /* a glass dome of blocks, lit at the rim, arms and a fringe of cell strings */
        sk_t local = *k; k = &local; M.k = k;
        k->yh = k->yt = f->facing < 0 ? -1 : 1;
        k->y -= BW_JELLY_CENTRE * k->s;
        bool fry = f->stage == STAGE_FRY;
        float pulse = .5f + .5f * fast_sin(f->jet * TAU), sx = 1 - .17f * pulse, sy = 1 + .1f * pulse;
        float trail = .5f + .5f * fast_sin(f->jet * TAU - 1.1f + TAU), length = elder ? 1.16f : fry ? .74f : 1, wob = k->asleep ? 0 : 1;
        uint32_t rim = mix(body, 0xffffff, .55f), arm = fin;
        int arms = fry ? 2 : 4;
        for (int i = 0; i < arms; i++) {                                                               /* the oral arms */
            float x = (-7.5f + i * 5) * (arms == 2 ? 1.6f : 1) + (arms == 2 ? 1.25f : 0), w = wob * fast_sin(t * 1.8f + i * .8f) * 2.3f;
            float end = (22 + (i % 2) * 3) * length, spread = 1 + (.5f - trail) * .3f;
            float X[5] = { x * sx, x + w, (x - w * .7f) * spread, (x + w * .9f) * spread, (x + w * .3f) * spread }, Y[5] = { 2, 8, 14, end - 3, end }, R[5] = { 1.3f, 1.1f, .9f, .7f, .5f };
            mc_add_tube(m, X, Y, R, 5); mc_paint(m, i & 1 ? arm : mix(arm, acc, .5f), true, 220);
            mc_cell(m, X[4], Y[4], tip, 255);
        }
        int fringe = fry ? 6 : 12;
        for (int i = 0; i < fringe; i++) {                                                             /* the tentacle fringe */
            float x = (-15 + i * 30.f / (fringe - 1)) * sx, w = wob * fast_sin(t * 1.4f + i * 1.1f) * 1.5f, len = (10 + (i % 3) * 3) * length * (elder ? 1.3f : 1);
            mc_add_line(m, x, 4.5f, x + w, 4.5f + len); mc_paint(m, i % 3 ? mix(arm, 0xffffff, .4f) : acc, true, 160);
        }
        { float X[16], Y[16]; int n = 0;                                                               /* the bell */
          for (int i = 0; i <= 10; i++) { float a = TAU * .5f + i * TAU * .5f / 10; X[n] = 17 * sx * fast_cos(a); Y[n++] = 2 + 17 * sy * fast_sin(a); }
          X[n] = 11 * sx; Y[n++] = 5; X[n] = -11 * sx; Y[n++] = 5;
          mc_add_poly(m, X, Y, n); mc_paint(m, body, false, 170); }
        mc_add_rect(m, -4 * sx, -8, 4 * sx, 2.5f * sy); mc_paint(m, 0xffffff, true, 60);                 /* the shine */
        if (!fry) { mc_cell(m, -4.5f * sx, -5, acc, 200); mc_cell(m, 4.5f * sx, -5, acc, 200); mc_cell(m, -1.6f * sx, -8.5f, tip, 200); mc_cell(m, 1.6f * sx, -8.5f, tip, 200); }   /* the gonads */
        for (int i = 0; i < 8; i++) mc_cell(m, (-14 + i * 4) * sx, 4.2f, i & 1 ? rim : tip, 230);          /* the scalloped hem */
        mc_add_line(m, -16 * sx, 2, -9 * sx, 4); mc_add_line(m, -9 * sx, 4, 9 * sx, 4); mc_add_line(m, 9 * sx, 4, 16 * sx, 2); mc_paint(m, rim, true, 240);
        break;
    }
    }
}
static void draw_blackwater_fish(ctx_t *c, const fish_t *f, float clock, bool asleep) {
    sk_t k; sk_setup(&k, c, f, clock, asleep, -27, 17, .61f, 1);
    draw_blackwater_species(&k, SP_FISH);
}
