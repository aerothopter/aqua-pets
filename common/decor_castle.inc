/* ---- the castle (2026-09-16, from Strato's castle-v2 mockup) ----
 * A swim-through decoration drawn the way the fish and the fronds are -
 * spans and fills from a little geometry, no bitmap - so it sits in the same
 * water as everything else instead of a pixel-art sprite clashing with it.
 * Two layers: the KEEP (towers, battlements, the base, the dark courtyard
 * behind the arch) is static and bakes into the scene cache with the floor;
 * the GATE WALL (the curtain wall round the arch, its brick trim and jambs)
 * draws after the fish every frame, so a fish crossing the arch is tucked
 * behind the jambs and seen through the opening - it swims THROUGH. Stone
 * is a brick-course pattern from a position hash (the pebbled floor's
 * trick), golden-lit from the upper left like the mockup, mossed toward the
 * base, and pre-tinted with the water of its row like the fronds - the gate
 * a touch less than the keep, so the keep reads farther back. Geometry is
 * in px about the centre x on the floor line; ~176 x 150 px, the arch
 * opening 52 x 50 (an adult fish is ~37 x 22). */
/* CASTLE_FY (the floor line the keep stands on) and CASTLE_ROWS (rows above
 * it the palette covers) are defined with veg_span above */
#define CASTLE_ARCH_R 26                     /* the opening's half-width (and the vault's radius) */
#define CASTLE_ARCH_S 24                     /* the spring line: straight jambs below, the vault above */
#define CASTLE_TRIM   5                      /* the brick trim's width */
enum { CT_LIGHT, CT_MID, CT_MIDDK, CT_DARK, CT_MORTAR, CT_MOSS_A, CT_MOSS_B,
       CT_BRICK_L, CT_BRICK, CT_BRICK_D, CT_ROOF_L, CT_ROOF, CT_ROOF_D, CT_INSIDE, CT_N };
static const uint32_t CASTLE_RGB[CT_N] = {
    0xd9c58c, 0xa99b7b, 0x87795f, 0x5d5446, 0x6a6151, 0x7aa33c, 0x527f2e,
    0xe08a58, 0xc25f38, 0x8b3d24, 0xe4cd8a, 0xc4a95e, 0x8f7a42, 0x061219 };
/* [keep | gate][tone][row], row 0 = CASTLE_FY - CASTLE_ROWS */
static uint16_t g_castle_row[2][CT_N][CASTLE_ROWS + 12];
static float    g_castle_dim = -1;
/* the castle's silhouette, one bit per pixel in local coords (x - cx + 112,
 * 0..223; row 0 = CASTLE_FY - CASTLE_ROWS): every pixel castle_put writes
 * sets its bit, so the first full draw fills it. While the castle stands IN
 * FRONT of the grass, frond spans skip the pixels inside it (veg_span). */
#define CASTLE_MASK_W 224
static void castle_tint_fill(float dim) {
    if (g_castle_dim == dim) return;
    for (int r = 0; r < CASTLE_ROWS + 12; r++) {
        int y = CASTLE_FY - CASTLE_ROWS + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 0; k < CT_N; k++) {
            g_castle_row[0][k][r] = rgb565(mix(w, CASTLE_RGB[k], k == CT_INSIDE ? 0.6f : 0.70f), dim);
            g_castle_row[1][k][r] = rgb565(mix(w, CASTLE_RGB[k], k == CT_INSIDE ? 0.6f : 0.82f), dim);
        }
    }
    g_castle_dim = dim;
}
static inline uint32_t chash(int a, int b) {
    uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
/* moss creeps up from the base: 4 px cells, a chance that fades with height,
 * feathered per pixel so the patches have ragged edges */
static inline int castle_moss(int lx, int y, int extra_pct) {
    int d = CASTLE_FY - y;
    int pct = (d < 36 ? 26 - d * 26 / 36 : 0) + extra_pct;
    if (pct <= 0) return -1;
    uint32_t h = chash((lx + 4096) >> 2, y >> 2);
    if ((int)(h % 100) >= pct) return -1;
    if ((chash(lx, y) & 7) == 0) return -1;
    return (h >> 8) & 1 ? CT_MOSS_A : CT_MOSS_B;
}
/* a pixel's tone. mode 0 = WALL (5 px brick courses, 9 px bricks), 1 = ROOF
 * (4 px tile courses), 2 = RUBBLE (the base: no courses, mossy), 3 = the
 * brick JAMBS (courses), 4 = INSIDE. u = 0..1 across the element: the left
 * fifth catches the light, the right fifth falls into shadow. */
static int castle_tone(int mode, int lx, int y, float u) {
    if (mode == 4) return CT_INSIDE;
    int d = CASTLE_FY - y;
    int lit = u < 0.22f ? -1 : u > 0.78f ? 1 : 0;
    if (mode == 2) {
        int m = castle_moss(lx, y, 18); if (m >= 0) return m;
        static const int8_t pick[8] = { CT_MIDDK, CT_DARK, CT_DARK, CT_MIDDK, CT_MID, CT_DARK, CT_MIDDK, CT_DARK };
        int t = pick[chash(lx >> 1, y) & 7] + (lit < 0 ? -1 : 0);
        return t < CT_MID ? CT_MID : t;
    }
    int pitch = mode == 1 ? 4 : 5, len = mode == 1 ? 6 : 9;
    int course = d / pitch;
    if (d % pitch == 0) return mode == 3 ? CT_BRICK_D : mode == 1 ? CT_ROOF_D : CT_MORTAR;
    int bx = lx + 4096 + ((course & 1) ? len / 2 : 0);
    if (bx % len == 0) return mode == 3 ? CT_BRICK_D : mode == 1 ? CT_ROOF_D : CT_MORTAR;
    if (mode == 3) {
        static const int8_t pick[4] = { CT_BRICK, CT_BRICK, CT_BRICK_L, CT_BRICK_D };
        int t = pick[chash(course, bx / len) & 3] + lit;
        return t < CT_BRICK_L ? CT_BRICK_L : t > CT_BRICK_D ? CT_BRICK_D : t;
    }
    if (mode == 1) {
        static const int8_t pick[4] = { CT_ROOF, CT_ROOF, CT_ROOF_L, CT_ROOF_D };
        int t = pick[chash(course, bx / len) & 3] + lit;
        return t < CT_ROOF_L ? CT_ROOF_L : t > CT_ROOF_D ? CT_ROOF_D : t;
    }
    int m = castle_moss(lx, y, 0); if (m >= 0) return m;
    static const int8_t pick[8] = { CT_MID, CT_MID, CT_MID, CT_MIDDK, CT_MID, CT_MIDDK, CT_LIGHT, CT_MIDDK };
    int t = pick[chash(course, bx / len) & 7] + lit;
    return t < CT_LIGHT ? CT_LIGHT : t > CT_DARK ? CT_DARK : t;
}
typedef struct { ctx_t *c; int cx; int layer; bool final; } cst_t;
static inline void rec_px(int x, int y);
static inline void castle_put(const cst_t *k, int x, int y, uint16_t v) {
    ctx_t *c = k->c;
    if (!CTX_IN(c, x, y)) return;
    rec_px(x, y);                            /* a themed castle baked IN FRONT: its mask (2026-10-10) */
    { int lx = x - k->cx + CASTLE_MASK_W / 2, r = y - (CASTLE_FY - CASTLE_ROWS);
      if ((unsigned)lx < CASTLE_MASK_W && (unsigned)r < CASTLE_ROWS + 12) g_castle_mask[r][lx >> 5] |= 1u << (lx & 31); }
    if (g_front_n > 0 && front_px(x, y)) return;   /* a piece baked IN FRONT stands over the castle: the front row's repaint leaves it */
    uint16_t *p = &CTX_PX(c, x, y);
    *p = v;
    if (k->final) {                          /* vignetted here, and untagged: the sweep must not darken it again */
        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
        if (a) px_darken(p, a);
        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
    } else dirty_px(x, y);
}
/* one row of an element: local x lx0..lx1 on tank row y; ux0..ux1 = the
 * element's full width on that row, for the lighting */
static void castle_span(const cst_t *k, int lx0, int lx1, int y, int mode, float ux0, float ux1) {
    int r = y - (CASTLE_FY - CASTLE_ROWS);
    if (r < 0 || r >= CASTLE_ROWS + 12) return;
    const ctx_t *c = k->c;                       /* clip to the window first: tones cost */
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (k->cx + lx0 < c->ox) lx0 = c->ox - k->cx;
    if (k->cx + lx1 > c->ox + c->w - 1) lx1 = c->ox + c->w - 1 - k->cx;
    float uw = ux1 > ux0 ? ux1 - ux0 : 1;
    for (int lx = lx0; lx <= lx1; lx++)
        castle_put(k, k->cx + lx, y, g_castle_row[k->layer][castle_tone(mode, lx, y, (lx - ux0) / uw)][r]);
}
static void castle_rect(const cst_t *k, int lx0, int lx1, int ytop, int ybot, int mode) {
    for (int y = ytop; y <= ybot; y++) castle_span(k, lx0, lx1, y, mode, (float)lx0, (float)lx1);
}
/* a conical roof: half_base wide at ybase, a point at yapex, plus an eave
 * a pixel wider than the tower */
static void castle_cone(const cst_t *k, int lxc, int half_base, int ybase, int yapex) {
    for (int y = yapex; y <= ybase; y++) {
        float t = (y - yapex) / (float)(ybase - yapex);
        int half = (int)(half_base * t + 0.5f);
        castle_span(k, lxc - half, lxc + half, y, 1, (float)(lxc - half), (float)(lxc + half));
    }
}
/* merlons along a top edge: 7 wide, 7 tall, on a 12 px pitch */
static void castle_merlons(const cst_t *k, int lx0, int lx1, int ytop) {
    for (int x = lx0; x + 6 <= lx1; x += 12) castle_rect(k, x, x + 6, ytop - 7, ytop - 1, 0);
}
/* an arched window: w wide, from ybot up to ytop, the vault a half-disc */
static void castle_window(const cst_t *k, int lxc, int w, int ytop, int ybot) {
    float r = w / 2.0f;
    for (int y = ytop; y <= ybot; y++) {
        float dy = (ytop + r) - y;
        float half = dy > 0 ? r * ell_half(dy / r) : r;
        castle_span(k, (int)(lxc - half + 0.5f), (int)(lxc + half - 0.5f), y, 4, 0, 1);
    }
}
/* the gate: the arch's brick trim (a ring of voussoirs over the vault, jambs
 * down the sides) and the opening. A static tone map for the ring: the
 * voussoir index needs an angle, and the gate redraws every frame. */
static int8_t g_ring[CASTLE_ARCH_R + CASTLE_TRIM + 1][2 * (CASTLE_ARCH_R + CASTLE_TRIM) + 1];
static bool   g_ring_filled = false;
static void castle_ring_fill(void) {
    if (g_ring_filled) return;
    int R = CASTLE_ARCH_R + CASTLE_TRIM;
    for (int j = 0; j <= R; j++)
        for (int i = 0; i <= 2 * R; i++) {
            float dx = i - R, dy = (float)j;                 /* dy up from the spring line */
            float d = sqrtf(dx * dx + dy * dy);
            int8_t t = -1;
            if (d >= CASTLE_ARCH_R && d < R + 0.5f) {
                float ang = atan2f(dy, dx) / 3.14159265f * 9;   /* 9 voussoirs across the half-turn */
                int v = (int)ang; float fr = ang - v;
                t = fr < 0.14f ? CT_BRICK_D : (chash(v, 3) & 3) == 0 ? CT_BRICK_L : (chash(v, 3) & 3) == 1 ? CT_BRICK_D : CT_BRICK;
                if (v < 3 && t == CT_BRICK) t = CT_BRICK_L;    /* the lit side */
                if (v > 5 && t == CT_BRICK) t = CT_BRICK_D;
            }
            g_ring[j][i] = t;
        }
    g_ring_filled = true;
}
static void castle_gate_trim(const cst_t *k) {
    int R = CASTLE_ARCH_R + CASTLE_TRIM, ys = CASTLE_FY - CASTLE_ARCH_S;
    castle_ring_fill();
    for (int j = 0; j <= R; j++) {
        int y = ys - j, r = y - (CASTLE_FY - CASTLE_ROWS);
        if (r < 0) continue;
        for (int i = 0; i <= 2 * R; i++)
            if (g_ring[j][i] >= 0) castle_put(k, k->cx + i - R, y, g_castle_row[k->layer][g_ring[j][i]][r]);
    }
    castle_rect(k, -R, -CASTLE_ARCH_R - 1, ys + 1, CASTLE_FY, 3);
    castle_rect(k,  CASTLE_ARCH_R + 1, R,  ys + 1, CASTLE_FY, 3);
}
static void castle_opening(const cst_t *k) {
    int ys = CASTLE_FY - CASTLE_ARCH_S;
    for (int y = CASTLE_FY; y >= ys - CASTLE_ARCH_R; y--) {
        float half = y > ys ? CASTLE_ARCH_R : CASTLE_ARCH_R * ell_half((ys - y) / (float)CASTLE_ARCH_R);
        if (half < 1) continue;
        castle_span(k, (int)(-half + 0.5f), (int)(half - 0.5f), y, 4, 0, 1);
    }
}
/* the gate wall: the curtain between the left tower and the right, minus the
 * opening. Drawn in the keep pass (baked) and again after the fish. */
#define CASTLE_GATE_X0 (-34)
#define CASTLE_GATE_X1  55
#define CASTLE_WALL_TOP (CASTLE_FY - 58)
static void castle_gate_wall(const cst_t *k) {
    int ys = CASTLE_FY - CASTLE_ARCH_S, R = CASTLE_ARCH_R + CASTLE_TRIM;
    for (int y = CASTLE_WALL_TOP; y <= CASTLE_FY; y++) {
        float half = y > ys ? R : (ys - y) < R ? R * ell_half((ys - y) / (float)R) : 0;
        int h = (int)half;
        if (h < 1) castle_span(k, CASTLE_GATE_X0, CASTLE_GATE_X1, y, 0, -44, 44);
        else {
            castle_span(k, CASTLE_GATE_X0, -h - 1, y, 0, -44, 44);
            castle_span(k, h + 1, CASTLE_GATE_X1, y, 0, -44, 44);
        }
    }
    castle_merlons(k, -44, 44, CASTLE_WALL_TOP);
    castle_gate_trim(k);
}
/* the FRONT ROW: the gate wall and the three towers flush with it. Drawn in
 * the keep pass (baked) and again over the fish, clipped to their rects. */
static void castle_front_row(const cst_t *k) {
    int FY = CASTLE_FY;
    castle_gate_wall(k);
    /* the left tower, pointed */
    castle_rect(k, -62, -34, FY - 78, FY, 0);
    castle_cone(k, -48, 18, FY - 78, FY - 108);
    castle_window(k, -48, 7, FY - 62, FY - 48);
    /* the short far-left tower */
    castle_rect(k, -86, -60, FY - 46, FY, 0);
    castle_cone(k, -73, 17, FY - 46, FY - 72);
    castle_window(k, -73, 6, FY - 30, FY - 19);
    /* the open far-right tower, crenellated */
    castle_rect(k, 56, 86, FY - 70, FY, 0);
    castle_merlons(k, 56, 86, FY - 70);
    castle_window(k, 71, 7, FY - 44, FY - 30);
}
/* the whole castle (front = 0: the keep, then the front row over it - the
 * scene bake), or the front row alone (front = 1: over the fish) */
/* A quiet rounded ruin / a sculpted sandcastle. Same footprint and swim-through arch. */
/* Limestone ruin: relief, joints and moss are baked through the same depth mask. */
/* the Quiet Lagoon's castle (2026-10-10, the third design: "a chateau, a French castle"): a
   symmetrical limestone facade - a plinth, two round pepper-pot corner towers with conical
   slate roofs and finials, a main block with two rows of tall windows and a balustrade, a
   taller central pavilion with quoins, a steep hipped slate roof and a spire, dormers in the
   mansard, chimneys, the grand arched entrance (the same opening every castle has, the fish
   swim through it) with a tall window and an oculus above it. Pale stone greened a little by
   the water, lit from the left. The same geometry rule as the other castles: every solid
   pixel is painted in both passes, the opening's dark only behind the fish (!front). */
static void draw_lagoon_castle(ctx_t *c,int cx,int front,bool final) {
    cst_t k={c,cx,front,final};
    for(int y=CASTLE_FY-160;y<=CASTLE_FY+3;y++) {
        if(y<c->oy||y>=c->oy+c->h)continue;
        int h=CASTLE_FY-y;
        int left=c->ox-cx;if(left< -90)left=-90;
        int right=c->ox+c->w-1-cx;if(right>90)right=90;
        for(int x=left;x<=right;x++) {
            unsigned noise=((unsigned)(x+100)*1237u+(unsigned)(h+40)*719u)^((unsigned)(x+130)*(unsigned)(h+17));
            int ax=abs(x);
            bool plinth=h>=-3&&h<6&&ax<90;
            /* the corner towers: cylinders, conical roofs, finials */
            int tx=ax-74; bool tower=abs(tx)<15&&h>=6&&h<92;
            int troof_w=15-(h-92)*15/38; bool troof=h>=92&&h<130&&abs(tx)<troof_w;
            bool tfinial=h>=130&&h<137&&abs(tx)<=(h<134?1:0);
            bool tslit=tower&&abs(tx)<=1&&((h>=30&&h<44)||(h>=60&&h<74));
            /* the main block, its mansard, balustrade, dormers and chimneys */
            bool block=ax<62&&h>=6&&h<84;
            bool balus=ax<62&&ax>=30&&h>=84&&h<89&&(h>=88||h<85||((ax/4)&1));
            int mroof_w=62-(h-89)*18/16; bool mansard=h>=89&&h<105&&ax<mroof_w&&ax>=30;
            bool ridge=h>=105&&h<108&&ax<44&&ax>=30;
            bool dormer=((ax>=38&&ax<48))&&h>=89&&h<100, dormer_roof=((ax>=36&&ax<50))&&h>=100&&h<104&&(ax-36)<=(104-h)*7/2&&(49-ax)<=(104-h)*7/2;
            bool dormer_win=ax>=40&&ax<46&&h>=91&&h<98;
            bool chimney=ax>=52&&ax<58&&h>=100&&h<116, chimney_cap=ax>=51&&ax<59&&h>=116&&h<118;
            /* the central pavilion: quoins, a hipped roof, a spire */
            bool pav=ax<31&&h>=6&&h<104;
            bool quoin=pav&&ax>=27&&((h/6)&1)==((ax>=29)?0:1);
            int proof_w=31-(h-104)*28/36; bool proof=h>=104&&h<140&&ax<proof_w;
            bool spire=h>=140&&h<152&&ax<=(h<148?1:0), spire_ball=h>=147&&h<150&&ax<=1;
            /* the windows: two rows on the block, a tall one and an oculus on the pavilion */
            bool win=false, sill=false;
            { int wx=ax; bool col=(wx>=36&&wx<44)||(wx>=48&&wx<56); if(block&&col&&((h>=20&&h<38)||(h>=52&&h<70)))win=true; if(block&&col&&(h==19||h==51))sill=true; }
            if(pav&&ax<6&&h>=58&&h<80)win=true;
            if(pav&&ax<=6&&h==57)sill=true;
            { int oy=h-92; if(pav&&ax*ax+oy*oy<5*5)win=true; }
            bool win_bar=win&&((h>=20&&h<38&&(h==29||ax==40||ax==52))||(h>=52&&h<70&&(h==61||ax==40||ax==52))||(h>=58&&h<80&&ax<6&&(h==69||ax==0)));
            /* the entrance: the opening, its stone ring and keystone */
            bool opening=ax<26&&h>=0&&(h<24||x*x+(h-24)*(h-24)<26*26);
            bool ring=!opening&&ax<30&&h>=0&&(h<24||x*x+(h-24)*(h-24)<30*30);
            bool keystone=ring&&ax<3&&h>=47;
            if(opening){if(!front)castle_put(&k,cx+x,y,rgb565(mix(water_rgb(y),0x173e3e,.5f),c->dim));continue;}
            if(!(plinth||tower||troof||tfinial||block||balus||mansard||ridge||dormer||dormer_roof||chimney||chimney_cap||pav||proof||spire))continue;
            uint32_t rgb;
            float lit=x<0?1:0.55f;                                              /* the sun from the left */
            uint32_t stone=mix(0xd9d3b6,0x9fa78c,lit<1?0.45f:0.f), slate=mix(0x5c6a74,0x3b474f,lit<1?0.5f:0.f);
            if(win) rgb=win_bar?0xc9c4a4:mix(0x1f3a40,0x6f9aa0,((noise%5)/6.f)+0.1f);
            else if(sill) rgb=0xb9b398;
            else if(tfinial||spire_ball) rgb=0xd6b286;
            else if(spire) rgb=0x4b575f;
            else if(troof||proof||dormer_roof) { rgb=slate; if(((x+h)%6==0)||((x-h)%6==0)) rgb=mix(rgb,0x2c353b,0.6f); if(troof&&abs(tx)>=troof_w-1) rgb=0x2c353b; if(proof&&ax>=proof_w-1) rgb=0x2c353b; }
            else if(mansard||ridge) { rgb=slate; if((h%4)==0) rgb=mix(rgb,0x2c353b,0.5f); if(ridge) rgb=0x3b474f; }
            else if(chimney||chimney_cap) rgb=chimney_cap?0x8c8470:mix(0xb9a98a,0x8c7a5c,(noise%3)/3.f);
            else if(balus) rgb=0xc9c4a4;
            else if(keystone) rgb=0xb9b398;
            else if(ring) rgb=((h/5)&1)?0xc4bea0:0xb0aa8c;
            else if(quoin) rgb=0xc9c4a4;
            else if(dormer) rgb=dormer_win?0x2a4448:0xcfc9ab;
            else if(plinth) rgb=mix(0x8f9a84,0x6f7a66,(noise%3)/3.f);
            else {                                                              /* the walls: stone courses, a little moss low down */
                rgb=stone;
                if((h%8)==0||(((x+100+(h/8%2)*7)%14)==0)) rgb=mix(rgb,0x8c907a,0.5f);
                if(noise%17==0) rgb=mix(rgb,0xf0ead0,0.4f);
                if(h<16&&noise%5<2) rgb=mix(rgb,0x4f7757,0.5f);
                if(tower&&abs(tx)>=13) rgb=mix(rgb,0x7a826c,0.5f);                /* the towers' rounded shade */
                if(tslit) rgb=0x1f3a40;
            }
            castle_put(&k,cx+x,y,rgb565(rgb,c->dim));
        }
    }
}
/* the Tidepool Club's castle (2026-10-10, the sixth design: "a fat single pineapple castle"):
   ONE big, fat pineapple - a wide fruit in a diamond skin with an eye in every cell, a swept
   crown of eleven leaves with a pennant on a stick in the middle of it; three pineapple-slice
   rings for windows (yellow rings, the water through their holes - the fish pass behind);
   the same arched opening every castle has for the gate, a wooden drawbridge on two chains
   lying on the sand before it; a starfish on the mound. */
static inline bool pa_window(int x, int h) {
    int a = x + 36, ah = h - 66, b = x - 36, bh = h - 66, t = x, th = h - 98;
    return a * a + ah * ah < 49 || b * b + bh * bh < 49 || t * t + th * th < 36;
}
static inline bool pa_ring(int x, int h) {
    int a = x + 36, ah = h - 66, b = x - 36, bh = h - 66, t = x, th = h - 98;
    int ra = a * a + ah * ah, rb = b * b + bh * bh, rt = t * t + th * th;
    return (ra >= 49 && ra < 121) || (rb >= 49 && rb < 121) || (rt >= 36 && rt < 100);
}
static bool pa_leaf(int x, int h, int px, int top, int n, int tall, int *which) {
    for (int i = 0; i < n; i++) { int o = i - n / 2, lx = px + o * (tall / 5); int ao = o < 0 ? -o : o;
        int lh = top + tall - ao * (tall / (n / 2 + 1)); int w = 5 - (h - top) * 5 / (lh - top + 1);
        int sway = o * ((h - top) / 6);
        if (h >= top && h < lh && abs(x - lx - sway) <= w) { *which = i; return true; } }
    return false;
}
#include "blackwater_castle.inc"
static void draw_theme_castle(ctx_t *c,int cx,int front,bool final) {
    if(theme_active()==THEME_QUIET_LAGOON){draw_lagoon_castle(c,cx,front,final);return;}
    if(theme_active()==THEME_BLACKWATER){draw_blackwater_castle(c,cx,front,final);return;}
    cst_t k={c,cx,front,final};
    for(int y=CASTLE_FY-162;y<=CASTLE_FY+5;y++) {
        if(y<c->oy || y>=c->oy+c->h)continue;
        int h=CASTLE_FY-y;
        int left=c->ox-cx;if(left< -92)left=-92;                               /* only the clip's columns (a drag, the live path) */
        int right=c->ox+c->w-1-cx;if(right>92)right=92;
        for(int x=left;x<=right;x++) {
            unsigned noise=((unsigned)(x+100)*1237u+(unsigned)(h+40)*719u)^((unsigned)(x+130)*(unsigned)(h+17));
            int ax=abs(x);
            bool mound=h>=-3&&h<9&&x*x+(h-2)*(h-2)*25<8464;
            /* the fruit: fat - 150 wide, 118 tall */
            float ex=x/75.f, ey=(h-60)/59.f; float e2=ex*ex+ey*ey;
            bool fruit=h>=3&&e2<1.0f, fedge=fruit&&e2>0.84f;
            int li=0; bool leaf=h>=112&&pa_leaf(x,h,0,112,11,50,&li);
            bool stick=ax<=0&&h>=140&&h<160, flag=x>0&&x<=14&&h>=150&&h<160&&(x<=(160-h)*2+2);
            /* the gate, the windows (holes), their rings */
            bool opening=ax<26&&h>=0&&(h<24 || x*x+(h-24)*(h-24)<26*26);
            bool gate_frame=!opening&&ax<30&&h>=0&&(h<24 || x*x+(h-24)*(h-24)<30*30)&&fruit;
            bool hole=opening||(fruit&&pa_window(x,h));
            bool ring=fruit&&!hole&&pa_ring(x,h);
            /* the drawbridge on the sand before the gate, its two chains up to the fruit */
            bool bridge=x>=-62&&x<-24&&h>=0&&h<4, bridge_slat=bridge&&((x+62)%6)==0;
            bool chain=false;
            for(int cxi=0;cxi<2;cxi++){ int x0=cxi?-30:-58, y0=4, x1=cxi?-32:-52, y1=cxi?54:40; float u=(h-y0)/(float)(y1-y0);
                if(h>=y0&&h<=y1&&x==(int)(x0+(x1-x0)*u+0.5f)&&(h%3)!=2)chain=true; }
            bool star=false;
            if(h>=0&&h<14&&ax>=76) { float sx=x-84, sh=h-6; float r=sqrtf(sx*sx+sh*sh); if(r<1||r<3.2f+3.8f*fabsf(cosf(atan2f(sh,sx)*2.5f)))star=r<7.5f; }
            if(hole){ if(!front)castle_put(&k,cx+x,y,rgb565(mix(water_rgb(y),0x4a3a26,.55f),c->dim)); continue; }
            if(!(mound||fruit||leaf||bridge||chain||stick||flag||star))continue;
            uint32_t rgb;
            if(flag) rgb=((h-150)/3)&1?0xe0473a:0xfff6d9;
            else if(stick) rgb=0x8a5a3a;
            else if(leaf) rgb=(li+(h/7))%2 ? 0x55a862 : 0x3b8048;
            else if(star) rgb=(noise%5==0)?0xf0a050:0xe8702e;
            else if(chain) rgb=0x565a5d;
            else if(bridge) rgb=bridge_slat?0x6a4428:((h==3)?0xa8865a:0x8a5a3a);
            else if(fruit) {
                if(ring) { int band=((x+h)&3); rgb=band==0?0xf0c040:band==1?0xffe27a:0xf6cf55; }   /* a pineapple slice: a yellow ring */
                else if(gate_frame) rgb=((h/4)%2)?0x8a5a3a:0x9c6a44;
                else {
                    int u=(x+h)/10, v=(x-h)/10; bool seam=((x+h)%10==0)||((x-h)%10==0);
                    rgb = seam ? 0xc0702e : ((u+v)&1) ? 0xf2b45a : 0xe69a44;
                    if(!seam&&((x+h)%10==5)&&((x-h)%10==5)) rgb=0x8a4a20;                 /* the eye in every cell */
                    if(((u*7+v*13)&3)==0&&!seam) rgb=mix(rgb,0xfff0b0,.3f);
                    if(x<-20&&!seam) rgb=mix(rgb,0xfff0b0,.12f);                           /* lit from the left */
                    if(fedge) rgb=mix(rgb,0x8a4a20,.45f);
                }
            }
            else rgb=mix(0xdcc39b,0xc0a67c,(noise%4)/4.f);
            castle_put(&k,cx+x,y,rgb565(rgb,c->dim));
        }
    }
}
static void draw_castle(ctx_t *c, int cx, int front, bool final) {
    if(theme_active()){draw_theme_castle(c,cx,front,final);return;}
    castle_tint_fill(c->dim);
    cst_t k = { c, cx, 1, final };
    if (front) { castle_front_row(&k); return; }
    k.layer = 0;
    int FY = CASTLE_FY;
    /* the base: a low rubble mound the towers stand on */
    for (int y = FY - 9; y <= FY + 6; y++) {
        float w = ell_half((y - (FY - 1)) / 8.0f);
        if (w <= 0) continue;
        int half = (int)(92 * w);
        castle_span(&k, -half, half, y, 2, (float)-half, (float)half);
    }
    /* the rear tower, tallest, right of centre: behind the wall */
    castle_rect(&k, 24, 54, FY - 108, FY, 0);
    castle_cone(&k, 39, 19, FY - 108, FY - 142);
    castle_window(&k, 39, 7, FY - 92, FY - 78);
    /* the curtain wall behind the gate's opening, then the courtyard's dark */
    castle_rect(&k, -44, 44, CASTLE_WALL_TOP, FY, 0);
    castle_opening(&k);
    k.layer = 1;
    castle_front_row(&k);
}
/* the front row over a rect of the frame (a fish's box): only what the rect
 * covers is recomputed */
static void draw_castle_front_rect(ctx_t *c, int cx, int x0, int y0, int x1, int y1) {
    if (x1 < cx - 92 || x0 > cx + 92 || y1 < CASTLE_FY - CASTLE_ROWS || y0 > CASTLE_FY) return;
    if (x0 < c->ox) x0 = c->ox;
    if (y0 < c->oy) y0 = c->oy;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (y1 > c->oy + c->h - 1) y1 = c->oy + c->h - 1;
    if (x0 > x1 || y0 > y1) return;
    ctx_t cc = { c->fb + (y0 - c->oy) * c->stride + (x0 - c->ox), c->stride, c->dim, x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
    draw_castle(&cc, cx, 1, true);
}
