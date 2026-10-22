/* ---- the reef cluster, procedural (2026-09-24; Strato's coral-cluster.png:
 * a branching coral, three purple tube sponges, a cyan brain coral and green
 * weed on a pile of grey stones). The same idiom as the coral, bigger: a
 * CL_CW x CL_CH grid of 2 px cells, five ELEMENTS drawn in painter's order
 * (rock, weed, the coral, the tubes, the brain, weed in front), each classed
 * by its own signed distance - rim below / right, lit above / left, a deep
 * variant per element (moss on the rock, the coral's tips, the tube mouths,
 * the brain's grooves, the weed's tips) - into one tone sprite. The size is
 * the growth's first phase: CLUSTER_SIZE_MIN of full on the day it is bought
 * (it must look mature at once), full at 1. The look is a scheme (three
 * presets: the coral / the tubes / the brain each), tinted per row. */
#define CL_CELL 2
#define CL_W    (CL_CW * CL_CELL)
#define CL_FY   (TANK_BOT - 14 + DECOR_SINK)
#define CL_Q    64                                           /* sprite rebuilds over the size phase */
enum { CLE_ROCK, CLE_WEED, CLE_CORAL, CLE_TUBE, CLE_BRAIN, CLE_N };
enum { CLV_BODY, CLV_LIT, CLV_RIM, CLV_SHADE, CLV_DEEP, CLV_N };
#define CL_TONE(e, v) (1 + (e) * CLV_N + (v))
static int      g_cl_q = -1, g_cl_cells;
static int      g_cl_scheme = -1; static float g_cl_dim = -1;
/* geometry in unscaled cells: x from the left, y UP from the base row; the
 * size scales it about (36, 0) */
#define CL_CX 36.0f
typedef struct { float x0, y0, x1, y1, r; } cl_cap_t;
static const cl_cap_t CL_ROCKS[] = {                         /* stones, as capsules (a circle = a zero-length one) */
    { 8, 3, 8, 3, 6 }, { 20, 4, 20, 4, 7 }, { 33, 3, 33, 3, 6 }, { 46, 4, 46, 4, 7 }, { 58, 3, 58, 3, 6 }, { 66, 4, 66, 4, 5 },
    { 14, 7, 14, 7, 4 }, { 40, 7, 40, 7, 4 }, { 52, 8, 52, 8, 4 }, { 4, 6, 70, 6, 2.5f },
};
static const cl_cap_t CL_TUBES[] = {                         /* back to front */
    { 45, 5, 44, 22, 4.5f }, { 61, 4, 63, 27, 5.0f }, { 52, 4, 53, 34, 5.5f }, { 58, 3, 59, 13, 3.5f },
};
static const cl_cap_t CL_BRAIN[] = {
    { 35, 9, 35, 9, 7 }, { 28, 7, 28, 7, 5.5f }, { 42, 7, 42, 7, 5.5f }, { 35, 14, 35, 14, 5 }, { 31, 12, 31, 12, 4 }, { 39, 12, 39, 12, 4 },
};
static const cl_cap_t CL_WEED_BACK[] = {
    { 13, 3, 11, 11, 1.1f }, { 14, 3, 15, 12, 1.1f }, { 12, 3, 9, 9, 1.0f },
    { 30, 3, 29, 10, 1.1f }, { 31, 3, 33, 11, 1.0f },
    { 47, 3, 48, 12, 1.1f }, { 46, 3, 44, 10, 1.0f },
    { 66, 3, 68, 11, 1.1f }, { 65, 3, 63, 9, 1.0f }, { 67, 3, 67, 12, 1.0f },
};
static const cl_cap_t CL_WEED_FRONT[] = {
    { 21, 2, 19, 9, 1.1f }, { 22, 2, 24, 8, 1.0f }, { 41, 2, 43, 8, 1.0f }, { 40, 2, 39, 7, 1.0f }, { 56, 2, 57, 8, 1.0f },
};
/* Blackwater's reef (redrawn 2026-10-10 so each piece reads at tank scale against the near-black
 * water; the first cut reused the shared geometry and came out a few small blobs): the pieces are
 * spread across the rock and kept apart - a mesh SEA FAN at the far left behind, four TUBE sponges
 * in front of it, a big pale BRANCHING CORAL in the middle reaching 45 cells up, an ANEMONE (the
 * brain's slot: a column with nine tentacles) on the rock at the right. Same unscaled cells. */
static const cl_cap_t CL_BW_ROCKS[] = {                      /* fewer, chunkier stones */
    { 10, 3, 10, 3, 7 }, { 26, 3, 26, 3, 8 }, { 44, 3, 44, 3, 8 }, { 61, 3, 61, 3, 7 },
    { 18, 7, 18, 7, 4.5f }, { 36, 7, 36, 7, 4.5f }, { 54, 7, 54, 7, 4.5f }, { 4, 5, 69, 5, 3 },
};
static const cl_cap_t CL_BW_TUBES[] = {                      /* back to front */
    { 22, 4, 21, 22, 3.4f }, { 27, 4, 28, 30, 4.0f }, { 32, 3, 33, 16, 2.8f }, { 18, 3, 17, 14, 2.6f },
};
static const cl_cap_t CL_BW_CORAL[] = {                      /* the staghorn: a trunk, three limbs, seven tips */
    { 44, 4, 43, 18, 2.6f }, { 43, 14, 35, 26, 2.0f }, { 35, 26, 30, 38, 1.6f }, { 35, 24, 38, 35, 1.4f },
    { 43, 18, 46, 32, 2.2f }, { 46, 32, 42, 45, 1.6f }, { 46, 30, 52, 40, 1.5f }, { 43, 15, 53, 23, 1.9f },
    { 53, 23, 59, 31, 1.5f }, { 52, 22, 50, 29, 1.2f }, { 45, 9, 50, 13, 1.3f },
};
static const uint8_t CL_BW_CORAL_TIP[] = { 0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1 };
#define CL_BW_ANEM_X 61.0f                                   /* the anemone's column, and its tentacles from the top of it */
#define CL_BW_ANEM_Y 9.0f
static const cl_cap_t CL_BW_ANEM[] = {
    { 61, 12, 51.3f, 15.6f, 1.3f }, { 61, 12, 52.0f, 19.3f, 1.3f }, { 61, 12, 54.1f, 22.8f, 1.3f }, { 61, 12, 57.9f, 24.6f, 1.3f },
    { 61, 12, 61.0f, 26.0f, 1.3f }, { 61, 12, 64.1f, 24.6f, 1.3f }, { 61, 12, 67.9f, 22.8f, 1.3f }, { 61, 12, 70.0f, 19.3f, 1.3f },
    { 61, 12, 70.7f, 15.6f, 1.3f },
};
#define CL_BW_FAN_X 9.0f                                     /* the sea fan's root and radius */
#define CL_BW_FAN_Y 6.0f
#define CL_BW_FAN_R 15.0f
#define CL_CORAL_X  24.0f                                    /* the branching coral's base, and its scale */
#define CL_CORAL_Y  5.0f
#define CL_CORAL_SX 0.95f
#define CL_CORAL_SY 0.85f
static inline float cl_cap_sd(float px, float py, const cl_cap_t *k, float rs) {
    float dx = k->x1 - k->x0, dy = k->y1 - k->y0, len2 = dx * dx + dy * dy;
    float u = len2 > 1e-6f ? ((px - k->x0) * dx + (py - k->y0) * dy) / len2 : 0;
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    float ex = px - (k->x0 + dx * u), ey = py - (k->y0 + dy * u);
    return sqrtf(ex * ex + ey * ey) - k->r * rs;
}
/* an element's signed distance at an unscaled point; *deep = the element's
 * deep variant here (a tube's mouth, a brain groove, the coral's tip cap) */
static float cl_sd(int e, int pass, float px, float py, bool *deep) {
    float best = 1e9f; *deep = false;
    bool bw = theme_active() == THEME_BLACKWATER;
    if (e == CLE_ROCK) {
        const cl_cap_t *rk = bw ? CL_BW_ROCKS : CL_ROCKS; int n = bw ? (int)(sizeof CL_BW_ROCKS / sizeof rk[0]) : (int)(sizeof CL_ROCKS / sizeof rk[0]);
        for (int i = 0; i < n; i++) { float d = cl_cap_sd(px, py, &rk[i], 1); if (d < best) best = d; }
        *deep = best < -1.5f && py > 4 && (chash((int)(px * 0.7f), (int)(py * 0.7f)) % 100) < 22;   /* moss between the stones */
    } else if (e == CLE_WEED && bw) {
        if (pass) return best;                                   /* no tufts in front: the pieces stay apart */
        /* the sea fan: a half-disc of radial ribs and arcs on a short stalk; the holes of the mesh are outside */
        float dx = px - CL_BW_FAN_X, dy = py - CL_BW_FAN_Y, rr = sqrtf(dx * dx + dy * dy);
        float fan = fmaxf(rr - CL_BW_FAN_R, fmaxf(fabsf(dx) - 0.95f * dy, 2.5f - rr));
        float k = dy > 0.5f ? dx / dy * 4.0f : 0, arc = rr / 3.6f;                 /* radial ribs a quarter of slope apart, arcs every 3.6 cells */
        bool rib = fabsf(k - floorf(k + 0.5f)) * dy < 2.2f || fabsf(arc - floorf(arc + 0.5f)) < 0.16f;
        if (fan < 0 && !rib) fan = 0.5f;
        static const cl_cap_t stalk = { CL_BW_FAN_X, 2, CL_BW_FAN_X, CL_BW_FAN_Y + 1, 1.4f };
        float st = cl_cap_sd(px, py, &stalk, 1);
        *deep = st < 0 || (fan < 0 && rr > CL_BW_FAN_R - 1.6f);   /* the stalk and the fan's outer edge, brighter */
        best = fminf(fan, st);
    } else if (e == CLE_WEED) {
        const cl_cap_t *w = pass ? CL_WEED_FRONT : CL_WEED_BACK; int n = pass ? (int)(sizeof CL_WEED_FRONT / sizeof w[0]) : (int)(sizeof CL_WEED_BACK / sizeof w[0]);
        for (int i = 0; i < n; i++) { float d = cl_cap_sd(px, py, &w[i], 1); if (d < best) best = d;
            float tx = px - w[i].x1, ty = py - w[i].y1; if (tx * tx + ty * ty < 2.2f) *deep = true; }
    } else if (e == CLE_CORAL && bw) {
        for (int i = 0; i < (int)(sizeof CL_BW_CORAL / sizeof CL_BW_CORAL[0]); i++) {
            const cl_cap_t *k = &CL_BW_CORAL[i];
            float d = cl_cap_sd(px, py, k, 1); if (d < best) best = d;
            float tx = px - k->x1, ty = py - k->y1;
            if (CL_BW_CORAL_TIP[i] && tx * tx + ty * ty < (k->r + 0.8f) * (k->r + 0.8f)) *deep = true;   /* the lit tip caps */
        }
    } else if (e == CLE_CORAL) {
        float qx = (px - CL_CORAL_X) / CL_CORAL_SX + 16.0f, qy = (py - CL_CORAL_Y) / CL_CORAL_SY;   /* into the coral's own cells */
        bool tip; best = coral_sd(qx, qy, 1.0f, &tip) * CL_CORAL_SY; *deep = tip;
    } else if (e == CLE_BRAIN && bw) {
        /* the anemone: a squat column, nine tentacles fanned from its top, their tips lit */
        float ax = px - CL_BW_ANEM_X, ay = (py - CL_BW_ANEM_Y) / 0.8f;
        best = sqrtf(ax * ax + ay * ay) - 5.5f;
        for (int i = 0; i < (int)(sizeof CL_BW_ANEM / sizeof CL_BW_ANEM[0]); i++) {
            const cl_cap_t *k = &CL_BW_ANEM[i];
            float d = cl_cap_sd(px, py, k, 1); if (d < best) best = d;
            float tx = px - k->x1, ty = py - k->y1;
            if (tx * tx + ty * ty < (k->r + 0.9f) * (k->r + 0.9f)) *deep = true;
        }
    } else if (e == CLE_TUBE) {
        const cl_cap_t *k = bw ? &CL_BW_TUBES[pass] : &CL_TUBES[pass];
        best = cl_cap_sd(px, py, k, 1);
        if(theme_active()) {
            float height=(py-k->y0)/(k->y1-k->y0), center=k->x0+(k->x1-k->x0)*fminf(1,fmaxf(0,height));
            float width=theme_active()==THEME_TIDEPOOL_CLUB ? k->r*(.75f+.25f*cosf(height*TAU*3)) : k->r*(.45f+.55f*fmaxf(0,height));
            best=fmaxf(fabsf(px-center)-width,fmaxf(k->y0-py,py-k->y1-1));
        }
        float mx = (px - k->x1) / (k->r - 1.3f), my = (py - k->y1) / 1.7f;
        *deep = mx * mx + my * my < 1;                           /* the mouth */
    } else if (e == CLE_BRAIN) {
        float second = 1e9f;                                     /* the lobes: the seam where two meet is a groove */
        for (int i = 0; i < (int)(sizeof CL_BRAIN / sizeof CL_BRAIN[0]); i++) {
            float d = cl_cap_sd(px, py, &CL_BRAIN[i], 1);
            if (d < best) { second = best; best = d; } else if (d < second) second = d;
        }
        float v = fast_sin(px * 1.3f + py * 0.9f) + 0.6f * fast_sin(py * 2.1f - px * 0.5f);
        if(theme_active()) {
            float ax=(px-35)/12,ay=(py-9)/9;best=(sqrtf(ax*ax+ay*ay)-1)*9;
            *deep=theme_active()==THEME_TIDEPOOL_CLUB ? ((int)px%5==0&&(int)py%5<2) : (v>.8f&&best<-.5f);
            return best;
        }
        *deep = best < -0.8f && ((second < 0.5f && second > -1.2f) || v > 0.95f);   /* the seams, and a few winding grooves */
    }
    return best;
}
/* one element pass over the sprite: classed by its own distance field and
 * painted over what is there */
static void cl_paint(int e, int pass, float s) {
    int8_t  (*sd)[CL_CW] = ds()->cl_sd;                         /* eighths of a cell; the deep flags as bits (see sd_q) */
    uint8_t (*dp)[(CL_CW + 7) / 8] = ds()->cl_dp;
    for (int j = 0; j < CL_CH; j++)
        for (int i = 0; i < CL_CW; i++) {
            float px = CL_CX + (i + 0.5f - CL_CX) / s, py = (CL_CH - 1 - j + 0.5f) / s;   /* unscale the query */
            bool deep; sd[j][i] = sd_q(cl_sd(e, pass, px, py, &deep) * s);
            if (deep) dp[j][i >> 3] |= (uint8_t)(1 << (i & 7)); else dp[j][i >> 3] &= (uint8_t)~(1 << (i & 7));
        }
    for (int j = 0; j < CL_CH; j++)
        for (int i = 0; i < CL_CW; i++) {
            int d = sd[j][i];
            if (d >= 0) continue;
            bool deep = (dp[j][i >> 3] >> (i & 7)) & 1;
            bool out_l = i == 0 || sd[j][i - 1] >= 0, out_u = j == 0 || sd[j - 1][i] >= 0;
            bool out_r = i == CL_CW - 1 || sd[j][i + 1] >= 0, out_d = j == CL_CH - 1 || sd[j + 1][i] >= 0;
            int v;
            bool bw = theme_active() == THEME_BLACKWATER;
            if (bw && e == CLE_WEED) v = deep ? CLV_LIT : CLV_BODY;   /* the sea fan's mesh: no rims, the stalk and the outer edge lit */
            else if (deep && e != CLE_ROCK) v = CLV_DEEP;
            else if (out_r || out_d) v = CLV_RIM;
            else if (out_l || out_u) v = CLV_LIT;
            else if (deep) v = CLV_DEEP;                        /* the rock's moss: never on its edge */
            else if (bw) {                                      /* Blackwater's surfaces: the anemone's dark column, ribbed tubes, a plain pale coral */
                uint32_t h = chash(i + 97 * e, j) % 100;
                float py = (CL_CH - 1 - j + 0.5f) / s;
                v = e == CLE_BRAIN ? (py < 13.5f ? CLV_SHADE : CLV_BODY)
                  : e == CLE_TUBE  ? (i % 4 == 0 ? CLV_SHADE : h < 8 ? CLV_LIT : CLV_BODY)
                  : e == CLE_CORAL ? (h < 8 ? CLV_LIT : CLV_BODY)
                  : h < 18 ? CLV_SHADE : h < 26 ? CLV_LIT : CLV_BODY;
            }
            else if (theme_active()==THEME_QUIET_LAGOON) {   /* the lagoon's surfaces (2026-10-10): the brain's ridges, the tubes' streaks, the rock's speckle */
                uint32_t h = chash(i + 97 * e, j) % 100;
                v = e == CLE_BRAIN ? (((i * 2 + j * 3 + (i * j) % 4) % 7) < 2 ? CLV_SHADE : h < 12 ? CLV_LIT : CLV_BODY)
                  : e == CLE_TUBE  ? (i % 3 == 1 ? CLV_SHADE : h < 10 ? CLV_LIT : CLV_BODY)
                  : e == CLE_CORAL ? (h < 10 ? CLV_LIT : CLV_BODY)
                  : h < 22 ? CLV_SHADE : h < 30 ? CLV_LIT : CLV_BODY;
            }
            else { uint32_t h = chash(i + 97 * e, j) % 100; v = theme_active()==THEME_TIDEPOOL_CLUB?CLV_BODY:h < 20 ? CLV_SHADE : h < 26 ? CLV_LIT : CLV_BODY; }
            if (e == CLE_TUBE && deep && (out_u || out_l)) v = bw ? CLV_LIT : CLV_RIM;   /* the mouth's far lip (Blackwater: a bright rim) */
            ds()->cl_sprite[j][i] = (uint8_t)CL_TONE(e, v);
        }
}
static void cl_build(float g) {
    if (g > 1) g = 1;
    if (g < 0) g = 0;
    float s = CLUSTER_SIZE_MIN + (1.0f - CLUSTER_SIZE_MIN) * g;
    memset(ds()->cl_sprite, 0, sizeof ds()->cl_sprite);
    cl_paint(CLE_ROCK, 0, s);
    cl_paint(CLE_WEED, 0, s);
    cl_paint(CLE_CORAL, 0, s);
    for (int k = 0; k < 4; k++) cl_paint(CLE_TUBE, k, s);
    cl_paint(CLE_BRAIN, 0, s);
    cl_paint(CLE_WEED, 1, s);
    g_cl_cells = 0;
    for (int j = 0; j < CL_CH; j++) for (int i = 0; i < CL_CW; i++) g_cl_cells += ds()->cl_sprite[j][i] != 0;
}
int render_cluster_cells(float growth) { cl_build(growth); g_cl_q = -1; return g_cl_cells; }
static void cl_tint_fill(int scheme, float dim) {
    if (g_cl_scheme == scheme && g_cl_dim == dim) return;
    const cluster_scheme_t *sc = &CLUSTER_SCHEMES[scheme];
    uint32_t base[CLE_N] = { 0x9a9284, 0x8fc63a, sc->coral, sc->tube, sc->brain };
    uint32_t tone[CL_TONES]; tone[0] = 0;
    for (int e = 0; e < CLE_N; e++) {
        uint32_t c = base[e];
        if(theme_active()==THEME_QUIET_LAGOON) {
            /* the lagoon's reef (redesigned 2026-10-10; it was every element pulled 92 % toward tones a hair
               from the water and sand, and read as a bare twig and grey slabs): a dark basalt rock with a
               mossy crown, the keeper's coral / tube / brain hues kept (softened a quarter toward the
               lagoon's green-grey, no more), deep shaded rims so each piece has a silhouette */
            static const uint32_t quiet[CLE_N]={0x4e5a52,0x5f9a62,0x9fb6a0,0x9fb6a0,0x9fb6a0};
            c = e==CLE_ROCK||e==CLE_WEED ? quiet[e] : mix(c,quiet[e],.25f);
            tone[CL_TONE(e, CLV_BODY)]  = c;
            tone[CL_TONE(e, CLV_LIT)]   = mix(c, 0xf4f0d0, e == CLE_ROCK ? 0.26f : 0.40f);
            tone[CL_TONE(e, CLV_RIM)]   = mix(c, 0x0e1a18, 0.62f);
            tone[CL_TONE(e, CLV_SHADE)] = mix(c, 0x0e1a18, 0.32f);
            tone[CL_TONE(e, CLV_DEEP)]  = e == CLE_ROCK ? 0x5f8a4a : e == CLE_CORAL ? mix(c, 0xfff0c0, 0.55f)
                                        : e == CLE_TUBE ? mix(c, 0x0a1012, 0.72f) : e == CLE_BRAIN ? mix(c, 0x0e1a18, 0.50f) : mix(c, 0xe8ff80, 0.40f);
            continue;
        }
        if(theme_active()==THEME_BLACKWATER) {
            /* Blackwater's reef (redrawn 2026-10-10): a mid-grey basalt rock with a lit crown and moss, a deep red
               sea fan (the weed's slot), the keeper's coral hue paled more than half toward ivory so the big
               branching coral stands out against the dark water, the tubes vivid with bright mouth rims and
               near-black mouths, the anemone (the brain's slot) vivid with pale tentacle tips over a dark column */
            static const uint32_t bw[CLE_N]={0x4e5258,0xc23c30,0,0,0};
            c = e==CLE_ROCK||e==CLE_WEED ? bw[e] : e==CLE_CORAL ? mix(theme_creature_color(c,0),0xfff4e6,0.55f) : theme_creature_color(c,0);
            tone[CL_TONE(e, CLV_BODY)]  = c;
            tone[CL_TONE(e, CLV_LIT)]   = mix(c, 0xffffff, e == CLE_ROCK ? 0.42f : e == CLE_TUBE ? 0.65f : 0.5f);
            tone[CL_TONE(e, CLV_RIM)]   = mix(c, 0x050608, e == CLE_CORAL ? 0.6f : 0.68f);
            tone[CL_TONE(e, CLV_SHADE)] = e == CLE_BRAIN ? mix(c, 0x2a1c1c, 0.6f) : mix(c, 0x050608, 0.36f);
            tone[CL_TONE(e, CLV_DEEP)]  = e == CLE_ROCK ? 0x3f7a30 : e == CLE_CORAL ? 0xfff8f0
                                        : e == CLE_TUBE ? mix(c, 0x050608, 0.82f) : mix(c, 0xffffff, 0.6f);
            continue;
        }
        if(theme_active()) {
            static const uint32_t club[CLE_N]={0xe5c89b,0x79a86a,0xe99d7f,0xbb9fb2,0xe4bc70};
            c=mix(c,club[e],.7f);
        }
        tone[CL_TONE(e, CLV_BODY)]  = c;
        tone[CL_TONE(e, CLV_LIT)]   = mix(c, 0xfff0b0, e == CLE_ROCK ? 0.22f : 0.36f);
        tone[CL_TONE(e, CLV_RIM)]   = mix(c, e == CLE_ROCK ? 0x2a2620 : 0x20060e, 0.55f);
        tone[CL_TONE(e, CLV_SHADE)] = mix(c, 0x000000, 0.24f);
        tone[CL_TONE(e, CLV_DEEP)]  = e == CLE_ROCK ? 0x6f9a3a : e == CLE_CORAL ? mix(c, 0xffe8a0, 0.58f)
                                    : e == CLE_TUBE ? mix(c, 0x100418, 0.65f) : e == CLE_BRAIN ? mix(c, 0x0a3040, 0.45f) : mix(c, 0xe8ff80, 0.40f);
    }
    for (int r = 0; r < CL_H; r++) {
        int y = CL_FY - CL_H + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 1; k < CL_TONES; k++) ds()->cl_row[k][r] = rgb565(mix(w, tone[k], 0.90f), dim);
    }
    g_cl_scheme = scheme; g_cl_dim = dim;
}
static void draw_cluster(ctx_t *c, int cx, int scheme, float growth, bool final) {
    if (growth > 1) growth = 1;
    int q = (int)(growth * (CL_Q - 0.01f));
    if (q != g_cl_q) { cl_build(growth); g_cl_q = q; }
    cl_tint_fill(scheme, c->dim);
    int x0 = cx - CL_W / 2, y0 = CL_FY - CL_H;
    for (int j = 0; j < CL_CH; j++)
        for (int i = 0; i < CL_CW; i++) {
            uint8_t t = ds()->cl_sprite[j][i];
            if (!t) continue;
            for (int dy = 0; dy < CL_CELL; dy++) {
                int y = y0 + j * CL_CELL + dy;
                uint16_t v = ds()->cl_row[t][j * CL_CELL + dy];
                for (int dx = 0; dx < CL_CELL; dx++) {
                    int x = x0 + i * CL_CELL + dx;
                    if (!CTX_IN(c, x, y)) continue;
                    uint16_t *p = &CTX_PX(c, x, y);
                    *p = v;
                    rec_px(x, y);
                    if (final) {
                        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                        if (a) px_darken(p, a);
                        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
                    }
                }
            }
        }
    draw_floor_mound(c, cx, CLUSTER_HALF_W - 2, 6, final);            /* anchored: the rock sits in a low hump of the floor's stones */
}
/* the bloom (growth 1 -> CLUSTER_FULL): more and more tentacles, dealt one
 * at a time round the hosts - the four tube mouths, the coral's seven tips,
 * the brain's top - up to CL_TENT_MAX, each host's fan widening as it fills.
 * Per frame, swaying (draw_crown); the bounding box for the caller's rect. */
#define CL_TENT_MAX 48
#define CL_HOSTS    12
static void draw_cluster_crown(ctx_t *c, int cx, int scheme, float growth, float clock, int *bx0, int *by0, int *bx1, int *by1) {
    *bx0 = *by0 = 1 << 20; *bx1 = *by1 = -1;
    float phase = (growth - 1.0f) / (CLUSTER_FULL - 1.0f);
    if (phase <= 0) return;
    if (phase > 1) phase = 1;
    int total = (int)(phase * CL_TENT_MAX + 0.5f);
    if (total < 1) return;
    const cluster_scheme_t *sc = &CLUSTER_SCHEMES[scheme];
    float s = 1.0f;                                          /* full size by the time it blooms */
    float x0 = cx - CL_W / 2;
    for (int k = 0; k < CL_HOSTS; k++) {
        int n = total / CL_HOSTS + (k < total % CL_HOSTS ? 1 : 0);
        if (!n) continue;
        float hx, hy, len; uint32_t rgb;
        if (theme_active() == THEME_BLACKWATER) {            /* its own hosts: the four tubes, the staghorn's seven tips, the anemone's top */
            if (k < 4) { const cl_cap_t *t = &CL_BW_TUBES[k]; hx = t->x1; hy = t->y1 + 0.5f; len = 11 + t->r; rgb = sc->tube; }
            else if (k < 11) {
                int ti = 0; const cl_cap_t *sg = NULL;
                for (int i = 0; i < (int)(sizeof CL_BW_CORAL / sizeof CL_BW_CORAL[0]) && !sg; i++) if (CL_BW_CORAL_TIP[i] && ti++ == k - 4) sg = &CL_BW_CORAL[i];
                if (!sg) continue;
                hx = sg->x1; hy = sg->y1; len = 9; rgb = sc->coral;
            } else { hx = CL_BW_ANEM_X; hy = 22; len = 8; rgb = sc->brain; }
        }
        else if (k < 4) { const cl_cap_t *t = &CL_TUBES[k]; hx = t->x1; hy = t->y1 + 0.5f; len = 11 + t->r; rgb = sc->tube; }
        else if (k < 11) {                                   /* the coral's tips, in CORAL_SEGS order */
            int ti = 0; const coral_seg_t *sg = NULL;
            for (int i = 0; i < CORAL_NSEG && !sg; i++) if (CORAL_SEGS[i].tip && ti++ == k - 4) sg = &CORAL_SEGS[i];
            if (!sg) continue;
            hx = CL_CORAL_X + (sg->x1 - 16.0f) * CL_CORAL_SX; hy = CL_CORAL_Y + sg->y1 * CORAL_Y_SCALE * CL_CORAL_SY; len = 9; rgb = sc->coral;
        } else { hx = 35; hy = 20; len = 8; rgb = sc->brain; }
        float ox = x0 + (CL_CX + (hx - CL_CX) * s) * CL_CELL + 1, oy = CL_FY - hy * s * CL_CELL - 1;
        float spread = 0.30f + 0.08f * n;                    /* radians each side of up, widening as the fan fills */
        int qx0, qy0, qx1, qy1;
        draw_crown(c, ox, oy, n, -1.5708f - spread, -1.5708f + spread, len, mix(rgb, 0xffffff, 0.55f), k * 7, clock, &qx0, &qy0, &qx1, &qy1);
        if (qx1 >= qx0) { if (qx0 < *bx0) *bx0 = qx0; if (qy0 < *by0) *by0 = qy0; if (qx1 > *bx1) *bx1 = qx1; if (qy1 > *by1) *by1 = qy1; }
    }
}
