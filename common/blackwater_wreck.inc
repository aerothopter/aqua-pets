/* Blackwater (2026-10-12, the abyss): the wreck slot is a SUNKEN GALLEON. A rounded oak hull
   settled on the sand, bow to the right and a little up, a high stern castle at the left with
   three windows lit amber from within, a quarterdeck rail, the main mast leaning aft with its
   top gone, the foremast still carrying a yard and a rag of sail, a bowsprit. Planks 4 px with
   faint seams; the hull rounded by a light across it (brighter at the turn of the bilge, dark
   under the counter and at the keel), barnacles and dark weed below the waterline. Two HOLES
   the fish pass behind: a breach in the side amidships and a gap in the stern quarter. The
   LANTERNS - one on the stern, one at the main mast's foot - and the windows are drawn undimmed
   and light the wood around them. Same layers as every hull: -1 = all of it, 0 = only the dark
   inside (baked, never in the front mask), 1 = the solid pieces (the mask). Local coordinates:
   x from the centre, h up from WK_FY. */
static inline float wkg_bottom(int x) { return x < -40 ? (-40 - x) * .55f : x > 44 ? (x - 44) * .9f : 0; }
static inline float wkg_deck(int x) { return 26 + (x < -22 ? (( -22 - x) / 36.0f) * ((-22 - x) / 36.0f) * 12 : x > 30 ? ((x - 30) / 32.0f) * ((x - 30) / 32.0f) * 9 : 0); }
static inline bool wkg_hull(int x, float h) { return x >= -58 && x < 62 && h >= wkg_bottom(x) && h < wkg_deck(x); }
static inline bool wkg_castle(int x, float h) { float d = wkg_deck(x); return x >= -58 && x < -28 && h >= d && h < d + 16 - (x + 58) * .1f; }
static inline bool wkg_roof(int x, float h) { float d = wkg_deck(x); return x >= -60 && x < -26 && h >= d + 16 - (x + 58) * .1f && h < d + 19 - (x + 58) * .1f; }
static inline int  wkg_window(int x, float h) { float d = wkg_deck(-44); int i = (x + 54) / 9; return x >= -54 && x < -27 && (x + 54) % 9 < 6 && h >= d + 5 && h < d + 11 ? i : -1; }
static inline bool wkg_rail(int x, float h) { float d = wkg_deck(x); return x >= -28 && x < 60 && h >= d && h < d + 5 && (((x + 64) % 7) == 0 || h >= d + 4); }
static inline bool wkg_main(int x, float h) { float cx = -6 - (h - 26) * .22f; return h >= 20 && h < 78 && fabsf(x - cx) <= 2.2f - (h - 20) * .012f; }
static inline bool wkg_fore(int x, float h) { float cx = 30 + (h - 30) * .08f; return h >= 24 && h < 72 && fabsf(x - cx) <= 1.8f - (h - 24) * .01f; }
static inline bool wkg_yard(int x, float h) { return h >= 56 && h < 58.5f && x >= 14 && x < 52; }
static inline bool wkg_sail(int x, float h) {
    if (x < 18 || x >= 48 || h >= 56 || h < 34) return false;
    int tear = (int)(chash((x - 18) / 3, 5) % 5u) * 2 + (int)(chash(x, 9) % 3u);
    float foot = 36 + tear + (x > 36 ? (x - 36) * .9f : 0);
    return h >= foot && !(x >= 30 && x < 36 && h >= 42 && h < 47 + (int)(chash(x, 2) % 3u));   /* a rag with a rent in it */
}
static inline bool wkg_sprit(int x, float h) { float cy = wkg_deck(58) - 2 + (x - 56) * .35f; return x >= 56 && x < 64 && fabsf(h - cy) <= 1.2f; }
static inline bool wkg_breach(int x, float h) {
    if (x < 8 || x >= 30 || h < 6 || h >= 21) return false;
    int jl = (int)(chash((int)h >> 1, 3) % 4u), jr = (int)(chash((int)h >> 1, 4) % 4u), jt = (int)(chash(x >> 1, 6) % 3u);
    return x >= 8 + jl && x < 30 - jr && h >= 6 + jt && h < 21 - jt;
}
static inline bool wkg_gap(int x, float h) {
    if (x < -42 || x >= -26 || h < 9 || h >= 20) return false;
    int jl = (int)(chash((int)h, 13) % 3u), jr = (int)(chash((int)h, 14) % 3u), jt = (int)(chash(x, 16) % 3u);
    return x >= -42 + jl && x < -26 - jr && h >= 9 + jt && h < 20 - jt;
}
static inline float wkg_light(float x, float h, float lx, float lh, float reach) {
    float dx = (x - lx) / reach, dy = (h - lh) / reach, d = dx * dx + dy * dy;
    return d < 1 ? (1 - d) * (1 - d) : 0;
}
#define WKG_LAMP_X  -57.0f
#define WKG_LAMP_H  (wkg_deck(-57) + 9)
#define WKG_LAMP2_X -3.0f
#define WKG_LAMP2_H 30.0f
static void draw_wreck_blackwater(ctx_t *c, int cx, bool final, int layer) {
    const uint32_t OAK = 0x6a4a30, OAK_LIT = 0xa8865c, OAK_DK = 0x2e1e12, SEAM = 0x1e1208, DECK = 0x8a6a48, AMBER = 0xffb648, WEED = 0x1f6a60;
    for (int h = -2; h <= WK_H; h++) {
        int y = WK_FY - h;
        if (y < c->oy || y >= c->oy + c->h) continue;
        for (int x = -WK_W / 2; x < WK_W / 2; x++) {
            int X = cx + x;
            if (X < c->ox || X >= c->ox + c->w) continue;
            float fh = h + .5f;
            uint32_t inside = mix(water_rgb(y), 0x050a10, 0.82f);
            bool hull = wkg_hull(x, fh), castle = wkg_castle(x, fh);
            if (layer == 0) { if (hull || castle) wk_put(c, X, y, inside, final); continue; }
            bool lamp1 = (x - WKG_LAMP_X) * (x - WKG_LAMP_X) + (fh - WKG_LAMP_H) * (fh - WKG_LAMP_H) < 6;
            bool lamp2 = (x - WKG_LAMP2_X) * (x - WKG_LAMP2_X) + (fh - WKG_LAMP2_H) * (fh - WKG_LAMP2_H) < 5;
            bool roof = wkg_roof(x, fh), rail = wkg_rail(x, fh), main = wkg_main(x, fh), fore = wkg_fore(x, fh), yard = wkg_yard(x, fh), sail = wkg_sail(x, fh), sprit = wkg_sprit(x, fh);
            if (!(hull || castle || roof || rail || main || fore || yard || sail || sprit || lamp1 || lamp2)) continue;
            bool hole = (hull && (wkg_breach(x, fh) || wkg_gap(x, fh)));
            if (hole) { if (layer < 0) wk_put(c, X, y, inside, final); continue; }
            uint32_t rgb; float dim = c->dim;
            int win = castle ? wkg_window(x, fh) : -1;
            if (lamp1 || lamp2) { float lx = lamp1 ? WKG_LAMP_X : WKG_LAMP2_X, lh = lamp1 ? WKG_LAMP_H : WKG_LAMP2_H, d = ((x - lx) * (x - lx) + (fh - lh) * (fh - lh)) / 6; rgb = mix(0xfff6d8, AMBER, d); dim = 1; }
            else if (win >= 0) { float d = wkg_deck(-44); rgb = mix(0xffe6a8, AMBER, (fh - d - 5) / 6); if (((x + 54) % 9) == 3) rgb = mix(rgb, OAK_DK, .6f); dim = 1; }
            else if (sail) { uint32_t w = chash(x >> 1, h >> 1); rgb = (w % 9) == 0 ? 0x8a9298 : ((x + h) & 7) == 0 ? 0xa8b0b4 : 0xc8ccc8; if (h < 40) rgb = mix(rgb, 0x5a6a68, .4f); }
            else if (main || fore || yard || sprit) { float cxm = main ? -6 - (fh - 26) * .22f : fore ? 30 + (fh - 30) * .08f : x, u = main ? (x - cxm) / 2.2f : fore ? (x - cxm) / 1.8f : 0; rgb = mix(OAK_DK, OAK_LIT, .25f + .45f * (1 - fabsf(u + .3f))); if (yard) rgb = (h == 57) ? OAK_LIT : OAK; if ((h % 11) == 0 && (main || fore)) rgb = mix(rgb, SEAM, .5f); }
            else if (rail) rgb = (h >= (int)wkg_deck(x) + 4) ? mix(OAK, OAK_LIT, .5f) : OAK_DK;
            else if (roof) rgb = (fh >= wkg_deck(x) + 18 - (x + 58) * .1f) ? mix(OAK, OAK_LIT, .55f) : OAK_DK;
            else {
                /* the planks: 4 px courses, the butts offset, four tones by hash; the hull rounded by its light */
                int row = h >> 2, px = x + 64 + ((row & 1) ? 7 : 0);
                uint32_t hs = chash(row, px / 14);
                float tone = .3f + .14f * (hs % 4);
                bool seam = (h & 3) == 0 || (px % 14) == 0;
                float b = wkg_bottom(x), d = wkg_deck(x), v = (fh - b) / fmaxf(1, d - b);
                float lit = 1 - (v - .62f) * (v - .62f) * 3.2f;                                     /* the turn of the bilge catches the light */
                if (castle) { v = (fh - d) / 16; lit = .75f - v * .2f; }
                rgb = mix(OAK_DK, OAK_LIT, clamp01(tone * lit + .08f));
                if (seam) rgb = mix(rgb, SEAM, .55f);
                if (hull && fh >= d - 1.5f) rgb = mix(DECK, OAK_LIT, .3f);                              /* the deck's edge */
                if (hull && fh < b + 2.5f) rgb = mix(rgb, SEAM, .5f);                                   /* the keel */
                if (hull && x > 44 && fh < b + 4) rgb = mix(rgb, OAK_LIT, .2f);                          /* the stem */
                if (hull && v < .42f) {                                                                 /* below the waterline: barnacles and weed */
                    uint32_t w = chash(x >> 1, h >> 1);
                    if ((w % 11) == 0) rgb = mix(rgb, 0xd0d8d0, .55f);
                    else if ((w % 7) == 0) rgb = mix(rgb, WEED, .7f);
                    rgb = mix(rgb, 0x0c2a30, (.42f - v) * .9f);
                }
                if (castle && win < 0 && ((x + 54) % 9) == 6 && fh >= wkg_deck(-44) + 4 && fh < wkg_deck(-44) + 12) rgb = mix(rgb, OAK_LIT, .3f);   /* the mullions */
                if (wkg_breach(x - 1, fh) || wkg_breach(x + 1, fh) || wkg_breach(x, fh - 1) || wkg_breach(x, fh + 1) || wkg_gap(x - 1, fh) || wkg_gap(x + 1, fh) || wkg_gap(x, fh - 1) || wkg_gap(x, fh + 1)) rgb = SEAM;   /* the splintered edges */
            }
            if (dim == c->dim) {
                float g = fmaxf(wkg_light(x, fh, WKG_LAMP_X, WKG_LAMP_H, 24), wkg_light(x, fh, WKG_LAMP2_X, WKG_LAMP2_H, 20));
                g = fmaxf(g, wkg_light(x, fh, -40, wkg_deck(-44) + 8, 16) * .8f);
                if (g > .02f) { rgb = mix(rgb, AMBER, g * .55f); dim = c->dim + (1 - c->dim) * clamp01(g * 1.4f); }
            }
            wk_put_d(c, X, y, rgb, dim, final);
        }
    }
    draw_floor_mound(c, cx, WRECK_HALF_W - 8, 5, final);
}
