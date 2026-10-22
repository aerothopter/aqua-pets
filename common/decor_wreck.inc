/* ---- the shipwreck (2026-10-10, item 7) ---- a small boat lying on the sand, its deck tilted
 * up toward the bow on the right: planked hull with two big holes, a gunwale and rail posts,
 * a stub of a cabin with a porthole, a broken mast with a yard and a rag of sail, an anchor
 * on the sand before the bow with its chain up to the prow. Geometry in local coordinates:
 * x from the centre, h up from WK_FY. layer -1 = all of it, 0 = only the dark inside (what
 * shows through the holes: baked, never in the front mask), 1 = the solid pieces (the mask). */
static inline void wk_put(ctx_t *c, int x, int y, uint32_t rgb, bool final) {
    if (!CTX_IN(c, x, y)) return;
    uint16_t *p = &CTX_PX(c, x, y);
    *p = rgb565(rgb, c->dim);
    rec_px(x, y);
    if (final) {
        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
        if (a) px_darken(p, a);
        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
    } else dirty_px(x, y);
}
/* the same with its own dim (Blackwater's lanterns and lit planks, 2026-10-12: they stay lit at night) */
static inline void wk_put_d(ctx_t *c, int x, int y, uint32_t rgb, float dim, bool final) {
    if (!CTX_IN(c, x, y)) return;
    uint16_t *p = &CTX_PX(c, x, y);
    *p = rgb565(rgb, dim);
    rec_px(x, y);
    if (final) {
        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
        if (a) px_darken(p, a);
        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
    } else dirty_px(x, y);
}
static inline float wk_deck(int x) { return 30 + x * 0.12f; }                    /* the deck line, tilted up to the bow */
static inline float wk_keel(int x) { return (x * x) / (50.0f * 50.0f) * 12; }     /* the keel's curve up to either end */
static inline bool wk_hole(int x, int h) {                                       /* the two big holes in the hull side */
    int ax = x + 22, ah = h - 14, bx = x - 18, bh = h - 12;
    return ax * ax * 64 + ah * ah * 100 < 100 * 64 || bx * bx * 64 + bh * bh * 81 < 64 * 81;
}
static uint32_t wk_wood(uint32_t rgb) {                                          /* the themes' weathering */
    return theme_active() == THEME_QUIET_LAGOON ? mix(rgb, 0x5d6f62, 0.35f) : theme_active() == THEME_TIDEPOOL_CLUB ? mix(rgb, 0xb08a5c, 0.4f) : rgb;
}
static void draw_wreck_original(ctx_t *c, int cx, bool final, int layer) {
    for (int h = -2; h <= WK_H; h++) {
        int y = WK_FY - h;
        if (y < c->oy || y >= c->oy + c->h) continue;
        for (int x = -WK_W / 2; x < WK_W / 2; x++) {
            int X = cx + x;
            if (X < c->ox || X >= c->ox + c->w) continue;
            unsigned noise = ((unsigned)(x + 100) * 1237u + (unsigned)(h + 40) * 719u) ^ ((unsigned)(x + 130) * (unsigned)(h + 17));
            float deck = wk_deck(x), keel = wk_keel(x);
            bool body = x > -50 && x < 50 && h >= keel && h < deck;
            bool stern = x >= -54 && x <= -46 && h >= wk_keel(-50) - 2 && h < wk_deck(-50) + 4;
            bool prow = x >= 46 && x <= 54 && h >= wk_keel(50) - 2 && h < wk_deck(50) + 10;
            bool hull = body || stern || prow;
            if (layer == 0) { if (body && h < deck - 1) wk_put(c, X, y, mix(water_rgb(y), 0x0b1517, 0.78f), final); continue; }
            bool hole = body && wk_hole(x, h);
            if (hull && hole) { if (layer < 0) wk_put(c, X, y, mix(water_rgb(y), 0x0b1517, 0.78f), final); continue; }
            uint32_t rgb = 0; bool on = false;
            if (hull) {
                on = true;
                float down = deck - h;                                           /* planks counted from the deck down */
                bool seam = ((int)down % 6) == 0, joint = ((x + 64 + ((int)(down / 6) & 1) * 9) % 18) == 0;
                rgb = (noise % 7 < 2) ? 0x7d5f42 : 0x6f5238;
                if (down < 2.5f) rgb = 0x4a3524;                                  /* the gunwale */
                else if (seam || joint) rgb = 0x3e2d1e;
                if (h < keel + 9) rgb = mix(rgb, 0x4a6b45, 0.45f);                  /* algae along the keel */
                if (stern || prow) rgb = mix(rgb, 0x2f2218, 0.35f);
                /* a ring of broken plank ends round each hole */
                if (body && (wk_hole(x - 1, h) || wk_hole(x + 1, h) || wk_hole(x, h - 1) || wk_hole(x, h + 1))) rgb = 0x2a1c12;
                rgb = wk_wood(rgb);
            }
            /* the rail: posts every 16 px and the rail they carry */
            bool post = x > -44 && x < 44 && ((x + 40) % 16 == 0 || (x + 40) % 16 == 1) && h >= deck && h < deck + 7;
            bool rail = x > -45 && x < 45 && h >= deck + 6 && h < deck + 8;
            if (post || rail) { on = true; rgb = wk_wood(0x4a3524); }
            /* the cabin, aft of the mast, with a porthole */
            bool cabin = x >= 8 && x <= 30 && h >= deck && h < deck + 11;
            if (cabin) { on = true; int px = x - 19, ph = (int)(h - deck) - 5;
                rgb = px * px + ph * ph < 7 ? 0x9fd8e2 : px * px + ph * ph < 12 ? 0x3e2d1e : (((int)(h - deck)) % 5 == 0 ? 0x3e2d1e : 0x6a4d35);
                rgb = px * px + ph * ph < 7 ? rgb : wk_wood(rgb); }
            /* the mast, broken jagged at its top, its yard, the rag of sail */
            float top = deck + 44 + ((x + 3) * 5 % 4);
            bool mast = x >= -3 && x <= 1 && h >= deck && h < top;
            bool yard = x >= -17 && x <= 13 && h >= deck + 30 && h < deck + 32;
            bool sail = x >= -15 && x <= -3 && h < deck + 30 && h >= deck + 30 - (x + 15) * 1.1f - ((noise % 5) > 3 ? 3 : 0) && ((noise % 23) != 0);
            if (mast || yard) { on = true; rgb = wk_wood(mast && ((h - (int)deck) % 9 == 0) ? 0x3e2d1e : 0x5a4330); }
            else if (sail) { on = true; rgb = (noise % 11 < 2) ? 0xb3a68a : 0xcfc4a6; }
            /* the anchor on the sand before the bow, its chain up to the prow */
            int axl = x - 58;
            bool shank = axl >= -1 && axl <= 1 && h >= 2 && h < 27;
            bool stock = h >= 22 && h < 25 && axl >= -6 && axl <= 6;
            bool arms = h >= 1 && h < 6 && axl >= -7 && axl <= 7 && !(h >= 4 && axl >= -3 && axl <= 3);
            bool fluke = (axl <= -6 || axl >= 6) && axl >= -7 && axl <= 7 && h >= 5 && h < 11;
            bool ring = h >= 27 && h < 33 && axl >= -3 && axl <= 3 && !(h >= 28 && h < 32 && axl >= -2 && axl <= 2);
            if (shank || stock || arms || fluke || ring) { on = true; rgb = (noise % 5 == 0) ? 0x6f5a46 : (axl < 0 ? 0x565a5d : 0x3a3d40); }
            /* the chain: from the ring (58, 33) to the prow's top (50, 46), a link every third pixel */
            { float u = (h - 33) / 13.0f; int lx = (int)(58 - 8 * u + 0.5f);
              if (h >= 33 && h <= 46 && (x == lx) && (h % 3) != 2) { on = true; rgb = 0x5a5d5f; } }
            if (!on) continue;
            if (noise % 29 == 0) rgb = mix(rgb, 0xffffff, 0.12f);
            wk_put(c, X, y, rgb, final);
        }
    }
    draw_floor_mound(c, cx, WRECK_HALF_W - 8, 5, final);
}
/* Quiet Lagoon's wreck (redesigned 2026-10-10, the capsized dinghy "didn't look like a
 * shipwreck"): an old sailing ship LISTING on the sand - the stern sunk low, the bow risen,
 * the planks gone from the stern quarter so its RIBS show (the water behind them: the fish
 * pass behind those gaps too), two big holes in the side, a tilted mast broken off short
 * with its yard and a rag of sail, a stay down to the bowsprit, a little deckhouse aft of the
 * mast with a porthole, an anchor on the sand off the bow with its chain up to the stem.
 * Weathered grey-green wood, moss along the keel. Same layers as the Original. */
static inline float wkl_deck(int x) { return 20 + x * 0.20f; }
static inline float wkl_keel(int x) { float u = (x + 4) / 56.0f; u *= u; return u * u * 10; }
static inline bool wkl_hole(int x, int h) {
    int ax = x + 24, ah = h - 8, bx = x - 14, bh = h - 12;
    return ax * ax * 25 + ah * ah * 121 < 121 * 25 || bx * bx * 36 + bh * bh * 81 < 81 * 36;
}
static inline bool wkl_ribgap(int x, int h) {                                      /* the stern quarter: planks gone between the ribs */
    return x > -50 && x < -34 && h >= wkl_keel(x) + 4 && h < wkl_deck(x) - 2 && ((x + 50) % 5) >= 2;
}
static void draw_wreck_lagoon(ctx_t *c, int cx, bool final, int layer) {
    for (int h = -2; h <= WK_H; h++) {
        int y = WK_FY - h;
        if (y < c->oy || y >= c->oy + c->h) continue;
        for (int x = -WK_W / 2; x < WK_W / 2; x++) {
            int X = cx + x;
            if (X < c->ox || X >= c->ox + c->w) continue;
            unsigned noise = ((unsigned)(x + 100) * 1237u + (unsigned)(h + 40) * 719u) ^ ((unsigned)(x + 130) * (unsigned)(h + 17));
            float deck = wkl_deck(x), keel = wkl_keel(x);
            bool body = x > -56 && x < 54 && h >= keel && h < deck;
            bool stern = x >= -60 && x <= -54 && h >= wkl_keel(-56) - 1 && h < wkl_deck(-56) + 5;
            float dbow = wkl_deck(54);
            bool stem = x >= 50 && x <= 54 + (h > dbow ? (int)((h - dbow) / 3) : 0) && h >= wkl_keel(50) - 1 && h < dbow + 14;
            bool sprit = x >= 54 && x < 64 && h >= dbow + 11 + (x - 54) * 0.6f && h < dbow + 13 + (x - 54) * 0.6f;
            bool hull = body || stern || stem;
            if (layer == 0) { if (body && h < deck - 1) wk_put(c, X, y, mix(water_rgb(y), 0x0b1517, 0.78f), final); continue; }
            bool gap = body && (wkl_hole(x, h) || wkl_ribgap(x, h));
            if (gap) { if (layer < 0) wk_put(c, X, y, mix(water_rgb(y), 0x0b1517, 0.78f), final); continue; }
            uint32_t rgb = 0; bool on = false;
            if (hull) {
                on = true;
                float down = deck - h;
                int plank = (int)(down / 5);
                bool seam = ((int)down % 5) == 0, joint = ((x + 64 + (plank & 1) * 9) % 18) == 0;
                rgb = (plank & 1) ? 0x6f7d6a : 0x5f6d5c; if (noise % 7 == 0) rgb = 0x7a8874;
                if (down < 2.5f) rgb = 0x3a4a3d;                                   /* the gunwale */
                else if (seam || joint) rgb = 0x3c4a3f;
                if (h < keel + 8) rgb = mix(rgb, 0x4f7a4a, 0.5f);                    /* moss along the keel */
                if (noise % 23 == 0) rgb = mix(rgb, 0x6a9a62, 0.5f);                 /* algae streaks */
                if (stern || stem) rgb = mix(rgb, 0x2f3a31, 0.35f);
                if (body && x > -50 && x < -34 && !wkl_ribgap(x, h) && h >= keel + 4 && h < deck - 2) rgb = (noise % 5 == 0) ? 0x55634f : 0x4a5648;   /* the ribs */
                if (body && (wkl_hole(x - 1, h) || wkl_hole(x + 1, h) || wkl_hole(x, h - 1) || wkl_hole(x, h + 1))) rgb = 0x2f3a31;
            }
            if (sprit) { on = true; rgb = 0x5c5a48; }
            /* the rail posts along the deck */
            bool post = x > -50 && x < 48 && ((x + 50) % 14 == 0 || (x + 50) % 14 == 1) && h >= deck && h < deck + 5;
            if (post) { on = true; rgb = 0x3a4a3d; }
            /* the deckhouse aft of the mast, a porthole in it */
            bool house = x >= 18 && x <= 38 && h >= deck && h < deck + 9;
            if (house) { on = true; int px = x - 28, ph = (int)(h - deck) - 4;
                rgb = px * px + ph * ph < 5 ? 0xc4e6d1 : px * px + ph * ph < 9 ? 0x3c4a3f : ((int)(h - deck) % 4 == 0 ? 0x3c4a3f : 0x66755f); }
            /* the mast, leaning aft, broken off short; its yard; the rag of sail hanging from it */
            float d0 = wkl_deck(0), mx = -2 - (h - d0) * 0.22f; int dm = x - (int)(mx + 0.5f);
            float top = d0 + 48 + ((x + 3) * 5 % 4);
            bool mast = dm >= -1 && dm <= 1 && h >= d0 && h < top;
            bool yard = h >= d0 + 30 && h < d0 + 32 && dm >= -17 && dm <= 11;
            bool sail = dm >= -16 && dm <= -2 && h < d0 + 30 && h >= d0 + 30 - (dm + 16) * 1.3f - ((noise % 5) > 3 ? 3 : 0) && ((noise % 19) != 0) && h > d0 + 8;
            if (mast || yard) { on = true; rgb = mast && ((h - (int)d0) % 9 == 0) ? 0x3f3d30 : 0x5c5a48; }
            else if (sail) { on = true; rgb = (noise % 11 < 2) ? 0xa3a28c : 0xb9b7a0; }
            /* the stay: a rope from the mast's top down to the bowsprit's end */
            { float x0 = -2 - 46 * 0.22f, y0 = d0 + 47, x1 = 62, y1 = dbow + 17; float u = (x - x0) / (x1 - x0);
              if (u >= 0 && u <= 1 && h == (int)(y0 + (y1 - y0) * u + 0.5f) && (x % 2) == 0) { on = true; rgb = 0x8a7a5a; } }
            /* the anchor on the sand off the bow, its chain up to the stem */
            int axl = x - 59;
            bool shank = axl >= -1 && axl <= 1 && h >= 2 && h < 24;
            bool stock = h >= 19 && h < 22 && axl >= -5 && axl <= 5;
            bool arms = h >= 1 && h < 6 && axl >= -5 && axl <= 5 && !(h >= 4 && axl >= -2 && axl <= 2);
            bool fluke = (axl <= -4 || axl >= 4) && axl >= -5 && axl <= 5 && h >= 5 && h < 10;
            bool ring = h >= 24 && h < 29 && axl >= -2 && axl <= 2 && !(h >= 25 && h < 28 && axl >= -1 && axl <= 1);
            if (shank || stock || arms || fluke || ring) { on = true; rgb = (noise % 5 == 0) ? 0x6f6a5a : (axl < 0 ? 0x565a5d : 0x3a3d40); }
            { float u = (h - 29) / (dbow + 12 - 29); int lx = (int)(59 - 5 * u + 0.5f);
              if (h >= 29 && h <= dbow + 12 && x == lx && (h % 3) != 2) { on = true; rgb = 0x5a5d5f; } }
            if (!on) continue;
            if (noise % 29 == 0) rgb = mix(rgb, 0xffffff, 0.10f);
            wk_put(c, X, y, rgb, final);
        }
    }
    draw_floor_mound(c, cx, WRECK_HALF_W - 8, 5, final);
}
/* Tidepool Club's wreck (2026-10-10): a cheerful little TUGBOAT sitting upright on the sand -
 * a beamy cream hull with a red band and a teal bottom, two big brass-rimmed portholes (the
 * holes), a wheelhouse with lit windows and a red roof, an orange funnel with a white stripe,
 * a mast flying a cyan pennant, a life ring on the side, an anchor hung off the bow. */
#define WKC_DECK 26
static inline float wkc_keel(int x) { int a = x < 0 ? -x : x; return a > 38 ? ((a - 38) / 10.0f) * ((a - 38) / 10.0f) * 14 : 0; }
static inline bool wkc_hole(int x, int h) { int ax = x + 18, ah = h - 13, bx = x - 16, bh = h - 13; return ax * ax + ah * ah < 64 || bx * bx + bh * bh < 64; }
static inline bool wkc_rim(int x, int h) { int ax = x + 18, ah = h - 13, bx = x - 16, bh = h - 13; int ra = ax * ax + ah * ah, rb = bx * bx + bh * bh; return (ra >= 64 && ra < 110) || (rb >= 64 && rb < 110); }
static void draw_wreck_club(ctx_t *c, int cx, bool final, int layer) {
    for (int h = -2; h <= WKC_DECK + 44; h++) {
        int y = WK_FY - h;
        if (y < c->oy || y >= c->oy + c->h) continue;
        for (int x = -WK_W / 2; x < WK_W / 2; x++) {
            int X = cx + x;
            if (X < c->ox || X >= c->ox + c->w) continue;
            unsigned noise = ((unsigned)(x + 100) * 1237u + (unsigned)(h + 40) * 719u) ^ ((unsigned)(x + 130) * (unsigned)(h + 17));
            bool body = x > -48 && x < 48 && h >= wkc_keel(x) && h < WKC_DECK;
            if (layer == 0) { if (body && h < WKC_DECK - 1) wk_put(c, X, y, mix(water_rgb(y), 0x12141c, 0.75f), final); continue; }
            bool hole = body && wkc_hole(x, h);
            if (hole) { if (layer < 0) wk_put(c, X, y, mix(water_rgb(y), 0x12141c, 0.75f), final); continue; }
            uint32_t rgb = 0; bool on = false;
            if (body) {
                on = true;
                int down = WKC_DECK - h;
                rgb = (noise % 9 == 0) ? 0xe4d6b4 : 0xefe3c3;                       /* cream topsides */
                if (h < 11) rgb = (noise % 7 == 0) ? 0x25756d : 0x2e8f85;             /* the teal bottom */
                else if (h < 14) rgb = 0xd8402c;                                      /* the red boot stripe */
                if (down <= 2) rgb = 0x5a3b28;                                        /* the gunwale */
                if (noise % 41 == 0) rgb = mix(rgb, 0xb06a3a, 0.35f);                 /* a rust stain here and there */
                if (wkc_rim(x, h)) rgb = ((x + h) & 1) ? 0xd8c27a : 0xb89a52;         /* the brass rims */
                if (x <= -46 || x >= 46) rgb = mix(rgb, 0x5a3b28, 0.3f);
            }
            /* the wheelhouse, its windows and roof */
            bool house = x >= -8 && x <= 18 && h >= WKC_DECK && h < WKC_DECK + 20;
            if (house) { on = true; int hh = h - WKC_DECK;
                bool win = hh >= 8 && hh < 15 && ((x >= -4 && x <= 2) || (x >= 8 && x <= 14));
                rgb = win ? (hh < 11 ? 0xffe9a8 : 0xffd889) : (hh < 2 || hh >= 18 || x <= -7 || x >= 17) ? 0xd8402c : (noise % 11 == 0) ? 0xf0e4ca : 0xf8efd8;
                if (win && (x == -1 || x == 11)) rgb = 0xd8402c; }
            bool roof = x >= -11 && x <= 21 && h >= WKC_DECK + 20 && h < WKC_DECK + 23;
            if (roof) { on = true; rgb = h == WKC_DECK + 22 ? 0xe8623f : 0xb8321e; }
            /* the funnel, orange with a white stripe and a black top */
            bool funnel = x >= 24 && x <= 32 && h >= WKC_DECK && h < WKC_DECK + 28;
            if (funnel) { on = true; int fh = h - WKC_DECK; rgb = fh >= 24 ? 0x2b2b2b : (fh >= 17 && fh < 21) ? 0xfff6d9 : (x <= 25 ? 0xc98a34 : 0xe8a845); }
            /* the mast and its pennant */
            bool mast = x >= -30 && x <= -28 && h >= WKC_DECK && h < WKC_DECK + 40;
            bool flag = x > -28 && x <= -14 && h >= WKC_DECK + 33 && h < WKC_DECK + 40 && (x + 28) <= (WKC_DECK + 40 - h) * 2 + 1 && (noise % 23 != 0);
            if (mast) { on = true; rgb = (h % 7 == 0) ? 0x6a4428 : 0x8a5c3a; }
            else if (flag) { on = true; rgb = ((x + 28) / 4 % 2) ? 0x33c7e0 : 0x5fd8ea; }
            /* the life ring on the side, white and orange quarters */
            { int rx = x - 38, rh = h - 19; int r2 = rx * rx + rh * rh;
              if (r2 >= 12 && r2 < 42) { on = true; rgb = (rx * rh > 0) ? 0xfff6d9 : 0xe8702e; } }
            /* the anchor hung off the bow, its chain up to the gunwale */
            { int axl = x - 56;
              bool shank = axl >= -1 && axl <= 1 && h >= 4 && h < 26;
              bool stock = h >= 21 && h < 24 && axl >= -5 && axl <= 5;
              bool arms = h >= 3 && h < 8 && axl >= -7 && axl <= 7 && !(h >= 6 && axl >= -3 && axl <= 3);
              bool fluke = (axl <= -6 || axl >= 6) && axl >= -7 && axl <= 7 && h >= 7 && h < 12;
              bool ring = h >= 26 && h < 31 && axl >= -3 && axl <= 3 && !(h >= 27 && h < 30 && axl >= -2 && axl <= 2);
              if (shank || stock || arms || fluke || ring) { on = true; rgb = (noise % 5 == 0) ? 0x6f6a5a : (axl < 0 ? 0x565a5d : 0x3a3d40); }
              { float u = (h - 31) / 8.0f; int lx = (int)(56 - 8 * u + 0.5f);
                if (h >= 31 && h <= 39 && x == lx && (h % 3) != 2) { on = true; rgb = 0x5a5d5f; } } }
            if (!on) continue;
            if (noise % 29 == 0) rgb = mix(rgb, 0xffffff, 0.12f);
            wk_put(c, X, y, rgb, final);
        }
    }
    draw_floor_mound(c, cx, WRECK_HALF_W - 8, 5, final);
}
#include "blackwater_wreck.inc"
static void draw_wreck(ctx_t *c, int cx, bool final, int layer) {
    if (theme_active() == THEME_QUIET_LAGOON) draw_wreck_lagoon(c, cx, final, layer);
    else if (theme_active() == THEME_BLACKWATER) draw_wreck_blackwater(c, cx, final, layer);
    else if (theme_active() == THEME_TIDEPOOL_CLUB) draw_wreck_club(c, cx, final, layer);
    else draw_wreck_original(c, cx, final, layer);
}
