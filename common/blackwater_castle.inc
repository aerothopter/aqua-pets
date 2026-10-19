/* Blackwater (2026-10-12, the abyss - Alvin: "redesign it to be better than the rest of the themes
   ... like a polished commercial game"): the castle slot is a DROWNED TEMPLE. Pale marble gone
   blue in the deep: a crypt at the base with the vaulted gate every castle shares (the opening
   |x|<26, vault radius 26) in a ring of voussoirs, a stylobate over it carrying four fluted
   columns - the right-hand pair broken short - an architrave and a frieze with a band of glyphs,
   a pediment missing its right corner with a round window at its heart. Steps run out either
   side onto the sand. The light: the moon from above (every top face brighter, the columns
   rounded by a cosine across them, a shade under each ledge), the GLYPHS, the WINDOW and the
   CRYSTALS - clusters of cyan shards grown from the steps and the broken column - drawn undimmed
   so they glow at night, and each crystal and the two hanging amber LAMPS light the stone around
   them. Weed and dark algae low on the crypt. Nothing past |x| 92 (the round board's per-rect
   repaint stops there). Same rule as the other castles: every solid pixel is painted in both
   passes, the hole's dark only behind the fish (!front). */
#define BWC_CRYPT_HW  58
#define BWC_CRYPT_H   56
#define BWC_STYLO_H   62     /* the stylobate's top */
#define BWC_COL_R     6.0f
#define BWC_COL_TOP   118    /* where the columns meet the architrave */
#define BWC_ENT_TOP   130    /* the entablature's top; the pediment above */
#define BWC_APEX      150
static const float bwc_col_x[4] = { -46, -18, 18, 46 };
static const int   bwc_col_h[4] = { BWC_COL_TOP, BWC_COL_TOP, BWC_COL_TOP, 92 };   /* the fourth stands broken */
/* the crystal shards: base x0..x1 on row hb, the apex at (xa, ha) */
typedef struct { float x0, x1, hb, xa, ha; } bwc_shard_t;
static const bwc_shard_t bwc_shards[9] = {
    { -90, -80, 6, -88, 34 }, { -84, -74, 6, -76, 24 }, { -80, -72, 6, -78, 16 },
    {  70,  80, 6,  78, 30 }, {  76,  86, 6,  84, 20 },
    {  40,  50, 92,  42, 112 }, { 44, 52, 92, 50, 104 },
    { -14, -6, 62, -11, 76 }, { 6, 12, 62, 10, 72 },
};
static inline bool bwc_in_tri(float x, float h, const bwc_shard_t *s, float *core) {
    /* inside the shard, and how close to its axis (1 at the axis, 0 at its edge) */
    if (h < s->hb || h > s->ha) return false;
    float u = (h - s->hb) / (s->ha - s->hb);
    float l = s->x0 + (s->xa - s->x0) * u, r = s->x1 + (s->xa - s->x1) * u;
    if (x < l || x > r) return false;
    float w = (r - l) * .5f, c = (l + r) * .5f;
    *core = w > .01f ? 1 - fabsf(x - c) / w : 1;
    return true;
}
static inline float bwc_light(float x, float h, float lx, float lh, float reach) {
    float dx = (x - lx) / reach, dy = (h - lh) / reach, d = dx * dx + dy * dy;
    return d < 1 ? (1 - d) * (1 - d) : 0;
}
static void draw_blackwater_castle(ctx_t *c, int cx, int front, bool final) {
    cst_t k = { c, cx, front, final };
    const uint32_t MARBLE = 0x8e9cae, MARBLE_LIT = 0xc9d6e2, MARBLE_DK = 0x4a5668, JOINT = 0x3a4454, CYAN = 0x3fd8e6, AMBER = 0xffb648;
    for (int y = CASTLE_FY - BWC_APEX; y <= CASTLE_FY + 3; y++) {
        if (y < c->oy || y >= c->oy + c->h) continue;
        int h = CASTLE_FY - y;
        int left = c->ox - cx; if (left < -92) left = -92;
        int right = c->ox + c->w - 1 - cx; if (right > 92) right = 92;
        for (int x = left; x <= right; x++) {
            int ax = abs(x);
            float fx = x, fh = h;
            /* the gate: the opening every castle shares, its ring of voussoirs */
            bool opening = ax < 26 && h >= 0 && (h < 24 || x * x + (h - 24) * (h - 24) < 26 * 26);
            bool ring = !opening && ax < 31 && h >= 0 && (h < 24 || x * x + (h - 24) * (h - 24) < 31 * 31);
            if (opening) { if (!front) castle_put(&k, cx + x, y, rgb565(mix(water_rgb(y), 0x04070c, .6f), c->dim)); continue; }
            /* the crystals first: they stand in front of everything */
            float core; int shard = -1;
            for (int i = 0; i < 9; i++) if (bwc_in_tri(fx, fh, &bwc_shards[i], &core)) { shard = i; break; }
            if (shard >= 0) {
                const bwc_shard_t *s = &bwc_shards[shard];
                float u = (fh - s->hb) / (s->ha - s->hb);
                uint32_t rgb = mix(mix(0x1a7a90, CYAN, core), 0xe8ffff, core * core * (.3f + .5f * u));
                if (core < .18f) rgb = mix(rgb, 0x0e4a5a, .5f);                    /* the dark edge facets */
                castle_put(&k, cx + x, y, rgb565(rgb, 1));
                continue;
            }
            /* the stone: which piece owns the pixel */
            bool crypt = ax < BWC_CRYPT_HW && h >= 0 && h < BWC_CRYPT_H;
            bool step1 = ax < 92 && h >= 0 && h < 6, step2 = ax < 76 && h >= 6 && h < 12;
            bool stylo = ax < 66 && h >= BWC_CRYPT_H && h < BWC_STYLO_H;
            int col = -1; float cu = 0;
            for (int i = 0; i < 4; i++) {
                float d = fx - bwc_col_x[i];
                if (fabsf(d) <= BWC_COL_R && h >= BWC_STYLO_H && h < bwc_col_h[i]) { col = i; cu = d / BWC_COL_R; break; }
            }
            bool capital = col < 0 && h >= BWC_COL_TOP - 6 && h < BWC_COL_TOP && ((fabsf(fx - bwc_col_x[0]) <= 8) || (fabsf(fx - bwc_col_x[1]) <= 8) || (fabsf(fx - bwc_col_x[2]) <= 8));
            bool broken_top = col == 3 && h >= bwc_col_h[3] - 4 && ((x * 7 + h * 3) & 7) < 3;   /* the fracture, ragged */
            bool entab = ax < 68 && h >= BWC_COL_TOP && h < BWC_ENT_TOP;
            /* the pediment: a gable from the entablature to the apex, its right corner fallen */
            float gable = BWC_ENT_TOP + (BWC_APEX - BWC_ENT_TOP) * (1 - ax / 70.0f);
            bool ped = ax < 70 && h >= BWC_ENT_TOP && fh < gable;
            if (ped && x > 18 && fh > BWC_ENT_TOP + 1 + (x - 18) * .22f + (int)(chash(x, 5) % 4)) ped = false;   /* the fallen corner: a ragged break sloping down to the right */
            bool window = ped && x * x + (h - 139) * (h - 139) < 7 * 7;
            bool rim = ped && !window && x * x + (h - 139) * (h - 139) < 9 * 9;
            bool lamp_a = (fx + 32) * (fx + 32) + (fh - 100) * (fh - 100) < 9, lamp_b = (fx - 32) * (fx - 32) + (fh - 100) * (fh - 100) < 9;
            bool chain = (ax == 32) && h >= 103 && h < BWC_COL_TOP && (h & 1);
            if (!(ring || crypt || step1 || step2 || stylo || col >= 0 || capital || entab || ped || lamp_a || lamp_b || chain)) continue;
            if (broken_top) continue;
            uint32_t rgb; float dim = c->dim;
            if (window) { float d = sqrtf((float)(x * x + (h - 139) * (h - 139))) / 7; rgb = mix(0xf0ffff, CYAN, d * d); dim = 1; }
            else if (lamp_a || lamp_b) { float lx = lamp_a ? -32 : 32, d = ((fx - lx) * (fx - lx) + (fh - 100) * (fh - 100)) / 9; rgb = mix(0xfff4d0, AMBER, d); dim = 1; }
            else if (chain) rgb = 0x6a6a60;
            else {
                /* the marble: blocks of 14 x 7 with faint joints, three tones by hash, lit from above */
                int course = h / 7, bx = x + 400 + ((course & 1) ? 7 : 0);
                uint32_t hs = chash(course, bx / 14);
                float tone = .42f + .12f * (hs % 5) / 4.0f;
                bool joint = (h % 7) == 0 || (bx % 14) == 0;
                rgb = mix(MARBLE_DK, MARBLE_LIT, tone);
                if (col >= 0) {
                    /* a fluted column: rounded by a cosine across it, five flutes */
                    float lit = .5f + .5f * fast_cos((cu + .25f) * 1.6f), flute = fabsf(fast_sin(cu * 7.8f)) * .12f;
                    rgb = mix(MARBLE_DK, MARBLE_LIT, .22f + .66f * lit - flute);
                    if (h < BWC_STYLO_H + 3) rgb = mix(rgb, MARBLE_DK, .4f);                               /* the base's shade */
                    if ((h & 15) == 0) rgb = mix(rgb, JOINT, .35f);                                        /* the drums */
                } else if (capital) rgb = mix(MARBLE, MARBLE_LIT, h >= BWC_COL_TOP - 2 ? .7f : .3f);
                else if (ring) {
                    bool vous = ((int)(x + 40 + (h >> 1)) & 7) < 4;
                    rgb = vous ? mix(MARBLE, MARBLE_LIT, .55f) : mix(MARBLE, MARBLE_LIT, .25f);
                    if (((int)(x + 40 + (h >> 1)) & 7) == 2 && h >= 10 && ((h >> 1) & 1)) { rgb = mix(CYAN, 0xe8ffff, .3f); dim = 1; }   /* a glyph on each stone */
                } else if (joint) rgb = mix(rgb, JOINT, .45f);
                /* the top faces catch the moon; a shade under every ledge */
                if ((step1 && h == 5) || (step2 && h == 11) || (stylo && h == BWC_STYLO_H - 1) || (crypt && h == BWC_CRYPT_H - 1) || (entab && h == BWC_ENT_TOP - 1)) rgb = mix(rgb, 0xe8f2fa, .55f);
                if ((step2 && h == 6) || (stylo && h == BWC_CRYPT_H) || (crypt && h < 2) || (entab && h == BWC_COL_TOP)) rgb = mix(rgb, JOINT, .5f);
                if (ped && fh > gable - 1.5f) rgb = mix(rgb, 0xe8f2fa, .5f);                                /* the raking cornice */
                if (rim) rgb = mix(rgb, 0xd8e6f0, .4f);
                if (entab && h >= 122 && h < 127) {                                                       /* the frieze: the band of glyphs */
                    uint32_t g = chash(x >> 2, 11);
                    bool glyph = ((g >> 3) & 3) == 0 && (((x & 3) == 1) || ((h == 124) && (x & 3) != 3));
                    if (glyph) { rgb = mix(CYAN, 0xe8ffff, .25f); dim = 1; }
                    else rgb = mix(rgb, JOINT, .3f);
                }
                if (crypt && h < 26 && ax >= 31 && (chash(x >> 2, h >> 2) % 5) == 0) rgb = mix(rgb, 0x1a6a62, .6f);   /* the algae low on the crypt */
                if (crypt && h < 10 && (chash(x >> 1, h) % 7) == 0) rgb = mix(rgb, 0x0e3a3a, .5f);
            }
            if (dim == c->dim) {
                /* the stone lit by the crystals and the lamps */
                float glow = 0;
                for (int i = 0; i < 9; i++) glow = fmaxf(glow, bwc_light(fx, fh, (bwc_shards[i].x0 + bwc_shards[i].x1) * .5f, (bwc_shards[i].hb + bwc_shards[i].ha) * .5f, 22));
                float wg = bwc_light(fx, fh, 0, 139, 26);
                float lg = fmaxf(bwc_light(fx, fh, -32, 100, 30), bwc_light(fx, fh, 32, 100, 30));
                rgb = mix(rgb, CYAN, fmaxf(glow, wg) * .55f);
                rgb = mix(rgb, AMBER, lg * .5f);
                if (glow + wg + lg > .05f) dim = c->dim + (1 - c->dim) * clamp01(fmaxf(fmaxf(glow, wg), lg) * 1.4f);   /* the lit stone stays lit at night */
            }
            castle_put(&k, cx + x, y, rgb565(rgb, dim));
        }
    }
}
