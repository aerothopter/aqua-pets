/* Blackwater (2026-10-12): the castle slot is a BLOCK CASTLE - Alvin: "make the blackwater theme
   ... decorations like minecraft. redesign the castle to look like a castle" (the step pyramid
   of 2026-10-10 is gone). Built of 4 px blocks on a grid: a curtain wall of stone bricks with a
   crenellated top, a square tower of cobblestone at each end rising above it with its own
   battlements and arrow slits, a tall keep behind the middle with lit windows and a banner on
   a pole, the vaulted gate at the bottom centre (the swim-through every castle shares: the
   opening |x|<26, vault radius 26) in an arch of pale stone, a torch either side of it whose
   flame is drawn undimmed so it glows at night, mossy bricks along the foot (nothing past CASTLE_HALF_W
   92: the round board's per-rect repaint stops there). Stone bricks are 8 x 4 px with a dark seam on the left and the bottom,
   the courses offset, three greys by hash and the odd cracked one; cobblestone is 4 x 4 lumps
   with a dark edge. No shading but the block faces: a flat picture, as the game's. Same rule as
   the other castles: every solid pixel is painted in both passes, the holes' dark only behind
   the fish (!front). */
#define BWC_WALL_HW   68    /* the curtain wall's half-width */
#define BWC_WALL_H    52    /* its height to the walk; the merlons on top */
#define BWC_TOWER_X   68    /* the towers from here out to 92 */
#define BWC_TOWER_H   84
#define BWC_KEEP_HW   32
#define BWC_KEEP_H    100
static inline uint32_t bwc_brick(int x, int h, uint32_t a, uint32_t b, uint32_t c, uint32_t seam, bool mossy) {
    int course = h >> 2, bx = x + 400 + ((course & 1) ? 4 : 0);
    bool edge = (h & 3) == 0 || (bx & 7) == 0;
    uint32_t hs = chash(course, bx >> 3), rgb = edge ? seam : (hs % 5) < 2 ? a : (hs % 5) < 4 ? b : c;
    if (!edge && (hs % 17) == 0 && ((x + h) & 3) == 1) rgb = seam;                 /* a cracked brick */
    if (mossy && !edge && (hs % 3) == 0) rgb = (hs >> 6) & 1 ? 0x5e8a46 : 0x4a7238;   /* a mossy one */
    return rgb;
}
static inline uint32_t bwc_cobble(int x, int h, uint32_t seam) {
    int bx = (x + 400) >> 2, by = h >> 2; uint32_t hs = chash(bx, by + 77);
    bool edge = (x & 3) == 0 || (h & 3) == 0;
    static const uint32_t L[4] = { 0x7e7e7e, 0x8e8e8e, 0x6c6c6c, 0x989898 };
    uint32_t rgb = L[hs & 3];
    if (edge) rgb = ((hs >> 2) & 3) ? seam : rgb;
    return rgb;
}
static void draw_blackwater_castle(ctx_t *c, int cx, int front, bool final) {
    cst_t k = { c, cx, front, final };
    const uint32_t SA = 0x8d8d8d, SB = 0x7e7e7e, SC = 0x9a9a9a, SEAM = 0x4e4e4e, DARKW = 0x0b1118, GLOW = 0xffc04a;
    for (int y = CASTLE_FY - 148; y <= CASTLE_FY + 3; y++) {
        if (y < c->oy || y >= c->oy + c->h) continue;
        int h = CASTLE_FY - y;
        int left = c->ox - cx; if (left < -92) left = -92;
        int right = c->ox + c->w - 1 - cx; if (right > 92) right = 92;
        for (int x = left; x <= right; x++) {
            int ax = abs(x), bx = (x + 400) >> 2;
            /* the gate: the opening every castle shares, its arch of pale stone */
            bool opening = ax < 26 && h >= 0 && (h < 24 || x * x + (h - 24) * (h - 24) < 26 * 26);
            bool ring = !opening && ax < 30 && h >= 0 && (h < 24 || x * x + (h - 24) * (h - 24) < 30 * 30);
            /* the towers, their battlements (a merlon every other block) and arrow slits */
            bool tower = ax >= BWC_TOWER_X && ax < 92 && h >= 0 && h < BWC_TOWER_H;
            bool tmerlon = ax >= BWC_TOWER_X && ax < 92 && h >= BWC_TOWER_H && h < BWC_TOWER_H + 8 && (bx & 1) == 0;
            bool tslit = tower && ax >= 78 && ax < 82 && ((h >= 32 && h < 44) || (h >= 60 && h < 72));
            /* the curtain wall between them, its walk and merlons */
            bool wall = ax < BWC_TOWER_X && h >= 0 && h < BWC_WALL_H;
            bool wmerlon = ax < BWC_TOWER_X && h >= BWC_WALL_H && h < BWC_WALL_H + 8 && (bx & 1) == 1;
            /* the keep behind: its windows, battlements, the banner */
            bool keep = ax < BWC_KEEP_HW && h >= 0 && h < BWC_KEEP_H;
            bool kmerlon = ax < BWC_KEEP_HW && h >= BWC_KEEP_H && h < BWC_KEEP_H + 8 && (bx & 1) == 0;
            bool kwin = keep && ((ax >= 14 && ax < 22 && h >= 62 && h < 74) || (ax < 4 && h >= 80 && h < 92));
            bool pole = x >= 0 && x < 3 && h >= BWC_KEEP_H + 8 && h < BWC_KEEP_H + 40;
            bool banner = x >= 3 && x < 19 && h >= BWC_KEEP_H + 26 && h < BWC_KEEP_H + 38 && !(x >= 15 && h >= BWC_KEEP_H + 30 && h < BWC_KEEP_H + 34);
            /* the torches either side of the gate: a stick, a flame */
            bool stick = ax >= 33 && ax < 36 && h >= 30 && h < 38;
            bool flame = ax >= 32 && ax < 37 && h >= 38 && h < 43;
            /* the footing, two tumbled blocks on the sand */
            bool footing = h >= -3 && h < 0 && ax < 92;
            bool hole = opening;
            if (hole) { if (!front) castle_put(&k, cx + x, y, rgb565(mix(water_rgb(y), 0x05090c, .55f), c->dim)); continue; }
            if (!(ring || tower || tmerlon || wall || wmerlon || keep || kmerlon || pole || banner || stick || flame || footing)) continue;
            uint32_t rgb; float dim = c->dim;
            if (flame) { rgb = (h >= 41 || ax == 32 || ax == 36) ? 0xff8a1c : (h == 38) ? 0xffe27a : GLOW; if (h == 42 && (ax == 32 || ax == 36)) continue; dim = 1; }
            else if (stick) rgb = (ax == 33) ? 0x4a2e14 : 0x6e4620;
            else if (pole) rgb = x == 0 ? 0x3a2a18 : 0x5a4028;
            else if (banner) { int row = h - (BWC_KEEP_H + 26); rgb = row < 2 || row >= 10 ? 0x2aa8b8 : (x >= 7 && x < 15 && row >= 4 && row < 8) ? 0xfff2c8 : 0x3fd8e6; if (((x + h) & 7) == 0) rgb = mix(rgb, 0xffffff, .12f); }
            else if (ring) { bool voussoir = ((int)(x + 40 + (h >> 1)) & 7) < 4; rgb = (h < 24 && ax >= 28) || (h >= 24 && x * x + (h - 24) * (h - 24) >= 28 * 28) ? (voussoir ? 0xbdbdbd : 0xa8a8a8) : (voussoir ? 0xd2d2d2 : 0xbdbdbd); if ((h & 3) == 0 && h < 24) rgb = SEAM; }
            else if (tslit) rgb = DARKW;
            else if (tower || tmerlon) { rgb = bwc_cobble(x, h, SEAM); if (tmerlon && h >= BWC_TOWER_H + 7) rgb = 0xa4a4a4; if (tower && h >= BWC_TOWER_H - 1) rgb = 0x5a5a5a; }
            else if (kwin) { rgb = ((x + h) & 3) == 0 ? 0xffd890 : GLOW; dim = 1; if (ax >= 14 && ax < 22 && (ax == 14 || ax == 21 || h == 62)) { rgb = SEAM; dim = c->dim; } }
            else if (wall || wmerlon) { rgb = bwc_brick(x, h, SA, SB, SC, SEAM, h < 20); if (h >= BWC_WALL_H - 4 && h < BWC_WALL_H) rgb = (h == BWC_WALL_H - 1) ? 0xaaaaaa : 0x6a6a6a; if (wmerlon && h == BWC_WALL_H + 7) rgb = 0xaaaaaa; }
            else if (keep || kmerlon) { rgb = bwc_brick(x + 2, h + 2, mix(SA, 0x000000, .12f), mix(SB, 0x000000, .12f), mix(SC, 0x000000, .12f), SEAM, false); if (kmerlon && h == BWC_KEEP_H + 7) rgb = 0xa4a4a4; if (keep && h >= BWC_KEEP_H - 2) rgb = 0x5a5a5a; }
            else rgb = (chash(x, h) % 3) ? 0x2a2824 : 0x3a3832;                           /* the footing */
            castle_put(&k, cx + x, y, rgb565(rgb, dim));
        }
    }
}
