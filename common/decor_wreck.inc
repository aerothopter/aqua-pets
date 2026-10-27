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
static inline float wk_deck(int x) { return 30 + x * 0.12f; }                    /* the deck line, tilted up to the bow */
static inline float wk_keel(int x) { return (x * x) / (50.0f * 50.0f) * 12; }     /* the keel's curve up to either end */
static inline bool wk_hole(int x, int h) {                                       /* the two big holes in the hull side */
    int ax = x + 22, ah = h - 14, bx = x - 18, bh = h - 12;
    return ax * ax * 64 + ah * ah * 100 < 100 * 64 || bx * bx * 64 + bh * bh * 81 < 64 * 81;
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
            }
            /* the rail: posts every 16 px and the rail they carry */
            bool post = x > -44 && x < 44 && ((x + 40) % 16 == 0 || (x + 40) % 16 == 1) && h >= deck && h < deck + 7;
            bool rail = x > -45 && x < 45 && h >= deck + 6 && h < deck + 8;
            if (post || rail) { on = true; rgb = 0x4a3524; }
            /* the cabin, aft of the mast, with a porthole */
            bool cabin = x >= 8 && x <= 30 && h >= deck && h < deck + 11;
            if (cabin) { on = true; int px = x - 19, ph = (int)(h - deck) - 5;
                rgb = px * px + ph * ph < 7 ? 0x9fd8e2 : px * px + ph * ph < 12 ? 0x3e2d1e : (((int)(h - deck)) % 5 == 0 ? 0x3e2d1e : 0x6a4d35); }
            /* the mast, broken jagged at its top, its yard, the rag of sail */
            float top = deck + 44 + ((x + 3) * 5 % 4);
            bool mast = x >= -3 && x <= 1 && h >= deck && h < top;
            bool yard = x >= -17 && x <= 13 && h >= deck + 30 && h < deck + 32;
            bool sail = x >= -15 && x <= -3 && h < deck + 30 && h >= deck + 30 - (x + 15) * 1.1f - ((noise % 5) > 3 ? 3 : 0) && ((noise % 23) != 0);
            if (mast || yard) { on = true; rgb = mast && ((h - (int)deck) % 9 == 0) ? 0x3e2d1e : 0x5a4330; }
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
static void draw_wreck(ctx_t *c, int cx, bool final, int layer) {
    draw_wreck_original(c, cx, final, layer);   /* (each theme had its own boat: git tag multi-board-multi-theme) */
}
