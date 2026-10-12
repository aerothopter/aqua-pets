/* Blackwater (2026-10-12): the wreck slot is a BLOCK SHIPWRECK, as the game's ocean ones (Alvin:
   "make the blackwater theme ... decorations like minecraft"; the aluminium fighter of 2026-10-10
   is gone). Built of 4 px blocks: an oak-plank hull stepped out toward the bow (the right) and
   the stern, a dark keel, a spruce deck with fence posts along it, a stern cabin with a roof, a
   door and a window the fish pass behind, a mast with a yard and a torn wool sail hanging in
   two rags, a barrel and a chest on the deck, a ragged breach in the side (the second hole),
   kelp on the lower planks. Planks are 16 x 4 px with dark seams, the ends offset a row, three
   oak tones by hash. Same layers as the other hulls: -1 = all of it, 0 = only the dark inside
   (baked, never in the front mask), 1 = the solid pieces (the mask). Local coordinates: x from
   the centre, h up from WK_FY. */
#define WKB_DECK 28
static inline bool wkb_hull(int x, int h) {
    if (h < 0 || h >= WKB_DECK) return false;
    int r = h >> 2, l = -36 - r * 4, rt = 32 + r * 4;
    if (l < -52) l = -52;
    if (rt > 52) rt = 52;
    return x >= l && x < rt;
}
static inline bool wkb_cabin(int x, int h) { return x >= -52 && x < -24 && h >= WKB_DECK && h < WKB_DECK + 20; }
static inline bool wkb_roof(int x, int h) { return x >= -54 && x < -22 && h >= WKB_DECK + 20 && h < WKB_DECK + 24; }
static inline bool wkb_window(int x, int h) { return x >= -44 && x < -36 && h >= WKB_DECK + 6 && h < WKB_DECK + 14; }
static inline bool wkb_door(int x, int h) { return x >= -30 && x < -26 && h >= WKB_DECK && h < WKB_DECK + 12; }
static inline bool wkb_mast(int x, int h) { return x >= -4 && x < 4 && h >= WKB_DECK && h < WK_H; }
static inline bool wkb_yard(int x, int h) { return x >= -28 && x < 20 && h >= WKB_DECK + 38 && h < WKB_DECK + 42; }
static inline bool wkb_sail(int x, int h) {
    if (h >= WKB_DECK + 38) return false;
    if (x >= -24 && x < -8 && h >= WKB_DECK + 14) { int col = (x + 24) >> 2; int tear = (int)(chash(col, 5) % 4u) * 4; return h >= WKB_DECK + 14 + tear; }   /* the big rag, torn along its foot */
    if (x >= 4 && x < 16 && h >= WKB_DECK + 26) { int col = (x - 4) >> 2; int tear = (int)(chash(col, 9) % 3u) * 4; return h >= WKB_DECK + 26 + tear; }
    return false;
}
static inline bool wkb_post(int x, int h) { return h >= WKB_DECK && h < WKB_DECK + 6 && x >= -20 && x < 52 && (((x + 64) >> 2) & 1) == 0 && !(x >= 8 && x < 36); }
static inline bool wkb_barrel(int x, int h) { return x >= 8 && x < 16 && h >= WKB_DECK && h < WKB_DECK + 10; }
static inline bool wkb_chest(int x, int h) { return x >= 24 && x < 36 && h >= WKB_DECK && h < WKB_DECK + 8; }
static inline bool wkb_breach(int x, int h) {
    if (x < 8 || x >= 28 || h < 6 || h >= 22) return false;
    int jl = (int)(chash(h >> 2, 3) % 3u) * 2, jr = (int)(chash(h >> 2, 4) % 3u) * 2, jt = (int)(chash(x >> 2, 6) % 2u) * 2;
    return x >= 8 + jl && x < 28 - jr && h >= 6 + jt && h < 22 - jt;
}
static inline bool wkb_solid(int x, int h) {
    return wkb_hull(x, h) || wkb_cabin(x, h) || wkb_roof(x, h) || wkb_mast(x, h) || wkb_yard(x, h) || wkb_sail(x, h) || wkb_post(x, h) || wkb_barrel(x, h) || wkb_chest(x, h);
}
static void draw_wreck_blackwater(ctx_t *c, int cx, bool final, int layer) {
    const uint32_t OAK[3] = { 0xb08a4e, 0xa07c44, 0xc09a5a }, SEAM = 0x5a4020, SPRUCE = 0x7a5a34, DARK = 0x3e2a14;
    for (int h = -2; h <= WK_H; h++) {
        int y = WK_FY - h;
        if (y < c->oy || y >= c->oy + c->h) continue;
        for (int x = -WK_W / 2; x < WK_W / 2; x++) {
            int X = cx + x;
            if (X < c->ox || X >= c->ox + c->w) continue;
            uint32_t inside = mix(water_rgb(y), 0x070c10, 0.8f);
            bool hull = wkb_hull(x, h), cabin = wkb_cabin(x, h);
            if (layer == 0) { if (hull || cabin) wk_put(c, X, y, inside, final); continue; }
            if (!wkb_solid(x, h)) continue;
            bool hole = (hull && wkb_breach(x, h)) || (cabin && wkb_window(x, h));
            if (hole) { if (layer < 0) wk_put(c, X, y, inside, final); continue; }
            int row = h >> 2, px = x + 64 + ((row & 1) ? 8 : 0);
            uint32_t hs = chash(row, px >> 4), rgb = OAK[hs % 3];
            bool seam = (h & 3) == 0 || (px & 15) == 0;
            if (hull) {
                if (h < 4) rgb = seam ? 0x2a1c0c : DARK;                                                   /* the keel */
                else if (seam) rgb = SEAM;
                else if (h < 12 && (chash(x >> 2, h >> 2) % 5) == 0) rgb = ((x + h) & 2) ? 0x3d8a55 : 0x24633c;   /* kelp on the lower planks */
                if (h >= WKB_DECK - 4) rgb = seam ? 0x4a3418 : SPRUCE;                                   /* the deck's planks */
                if (wkb_breach(x - 1, h) || wkb_breach(x + 1, h) || wkb_breach(x, h - 1) || wkb_breach(x, h + 1)) rgb = 0x2a1c0c;   /* the splintered edge */
            } else if (wkb_roof(x, h)) rgb = (h & 3) == 3 ? 0x4a3418 : ((px & 7) == 0 ? 0x2a1c0c : DARK);
            else if (cabin) {
                rgb = seam ? SEAM : OAK[(hs + 1) % 3];
                if (wkb_door(x, h)) rgb = (h == WKB_DECK + 11 || x == -30) ? 0x2a1c0c : (x == -27 && h == WKB_DECK + 5) ? 0xd8b040 : 0x241808;   /* the door, its handle */
                else if (wkb_window(x - 1, h) || wkb_window(x + 1, h) || wkb_window(x, h - 1) || wkb_window(x, h + 1)) rgb = 0x2a1c0c;
            } else if (wkb_mast(x, h)) rgb = (x == -4 || (h & 7) == 0) ? 0x2a1c0c : (x < 0 ? 0x8a6a3c : 0x6a4e2a);
            else if (wkb_yard(x, h)) rgb = ((x + 64) & 7) == 0 ? 0x2a1c0c : (h == WKB_DECK + 41 ? 0x8a6a3c : 0x6a4e2a);
            else if (wkb_sail(x, h)) { uint32_t w = chash(x >> 2, h >> 2); rgb = (w % 7) == 0 ? 0xc8c8c0 : ((x & 3) == 0 || (h & 3) == 0) ? 0xb8b8b0 : 0xe8e8e0; }
            else if (wkb_post(x, h)) rgb = (h >= WKB_DECK + 4) ? 0x8a6a3c : ((x + 64) & 3) == 0 ? 0x4a3418 : 0x6a4e2a;
            else if (wkb_barrel(x, h)) { int bh = h - WKB_DECK; rgb = (bh == 2 || bh == 3 || bh == 7 || bh == 8) ? 0x3a3a3c : (x == 8 || x == 15) ? SEAM : OAK[(x >> 1) % 3]; }
            else if (wkb_chest(x, h)) { int ch = h - WKB_DECK; rgb = (ch == 0 || ch == 7 || ch == 4 || x == 24 || x == 35) ? 0x4a3418 : (x >= 29 && x < 31 && ch >= 3 && ch < 6) ? 0xd8b040 : 0x9a6a30; }
            wk_put(c, X, y, rgb, final);
        }
    }
    draw_floor_mound(c, cx, WRECK_HALF_W - 8, 5, final);
}
