/* mlp_model.c — see mlp_model.h. */
#include "mlp_model.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MLP_VERSION     1
#define MLP_MAX_LAYERS  4            /* hidden layers */
#define MLP_HEADER      (7 * 4 + 16 * 12)

typedef struct {
    int rows, cols;
    const float *scale, *bias;       /* rows each */
    const int8_t *q;                 /* rows x cols */
} mlp_layer_t;

struct mlp_model {
    int n_in, hidden, layers;
    char species[16][12];
    mlp_layer_t L[MLP_MAX_LAYERS + 2];   /* the hidden layers, then the goal and urgency heads */
    float *x, *a, *b;                /* input, two hidden buffers */
};

static const char *const GOALS[MLP_N_GOALS] = { "seek_food", "flee_shadow", "visit_bubbles", "follow_friend",
                                                "explore", "rest", "dart_play", "inspect_reef" };

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

bool mlp_model_is(const uint8_t *bin, size_t len) {
    return bin && len >= MLP_HEADER && (uint32_t)rd32(bin) == MLP_MAGIC;
}

mlp_model_t *mlp_model_open(const uint8_t *bin, size_t len, void *(*alloc)(size_t)) {
    if (!mlp_model_is(bin, len) || ((uintptr_t)bin & 3)) return NULL;   /* the fp32 blocks are read in place */
    int n_in = rd32(bin + 8), hidden = rd32(bin + 12), layers = rd32(bin + 16);
    if (rd32(bin + 4) != MLP_VERSION || rd32(bin + 20) != MLP_N_GOALS || rd32(bin + 24) != MLP_N_URGENCY ||
        n_in <= 0 || n_in > 1024 || hidden <= 0 || hidden > 1024 || layers < 1 || layers > MLP_MAX_LAYERS)
        return NULL;
    mlp_model_t *m = alloc(sizeof *m);
    if (!m) return NULL;
    m->n_in = n_in; m->hidden = hidden; m->layers = layers;
    for (int i = 0; i < 16; i++) {
        memcpy(m->species[i], bin + 28 + 12 * i, 12);
        m->species[i][11] = 0;
    }
    size_t off = MLP_HEADER;
    for (int l = 0; l < layers + 2; l++) {
        mlp_layer_t *L = &m->L[l];
        L->rows = l < layers ? hidden : l == layers ? MLP_N_GOALS : MLP_N_URGENCY;
        L->cols = l == 0 ? n_in : hidden;
        size_t nq = (size_t)L->rows * L->cols, need = 8 * (size_t)L->rows + ((nq + 3) & ~(size_t)3);
        if (off + need > len) return NULL;
        L->scale = (const float *)(bin + off);
        L->bias = L->scale + L->rows;
        L->q = (const int8_t *)(L->bias + L->rows);
        off += need;
    }
    if (off != len) return NULL;
    m->x = alloc(sizeof(float) * (size_t)(n_in + 2 * hidden));
    if (!m->x) return NULL;
    m->a = m->x + n_in; m->b = m->a + hidden;
    return m;
}

int mlp_model_n_features(const mlp_model_t *m) { return m->n_in; }
int mlp_model_hidden(const mlp_model_t *m) { return m->hidden; }
int mlp_model_layers(const mlp_model_t *m) { return m->layers; }
const char *mlp_model_species_slot(const mlp_model_t *m, int i) { return i >= 0 && i < 16 ? m->species[i] : ""; }

/* ---- the features: must match model/features.py exactly ---- */
enum { F_ZONE, F_HUNGER, F_ENERGY, F_STRESS, F_CURIOSITY, F_BOLD, F_SOCIAL, F_STAGE, F_SPECIES,
       F_TRUST, F_BORED, F_FOOD, F_FRIEND, F_BUBBLE, F_REEF, F_WALL, F_LAST, F_TIME, F_COUNT };
static const char *const FIELDS[F_COUNT] = { "zone", "hunger", "energy", "stress", "curiosity", "bold", "social",
    "stage", "species", "trust", "bored", "food", "friend", "bubble", "reef", "wall", "last", "time" };
static const int DIGIT_FIELDS[8] = { F_HUNGER, F_ENERGY, F_STRESS, F_CURIOSITY, F_BOLD, F_SOCIAL, F_TRUST, F_BORED };
static const char *const STAGES[4] = { "fry", "juv", "adult", "elder" };
static const int SIGHTINGS[4] = { F_FOOD, F_FRIEND, F_BUBBLE, F_REEF };

static int find(const char *const *names, int n, const char *w) {
    for (int i = 0; i < n; i++) if (strcmp(names[i], w) == 0) return i;
    return -1;
}
static int clock_hour(const char *w) {
    int h = 0;
    for (; *w; w++) { if (*w < '0' || *w > '9') return -1; h = h * 10 + (*w - '0'); }
    return h >= 1 && h <= 12 ? h : -1;
}
/* closeness thermometer (n_close bits) + the clock one-hot; false if malformed */
static bool sighting(float *x, int n_close, const char *const *v, int nv) {
    for (int i = 0; i < n_close + 12; i++) x[i] = 0;
    if (nv == 1) return strcmp(v[0], n_close == 3 ? "none" : "clear") == 0;
    static const char *const CLOSE[3] = { "far", "mid", "near" };
    int c = nv == 2 ? find(CLOSE, 3, v[0]) + 1 : 0, h = nv == 2 ? clock_hour(v[1]) : -1;
    int bits = c - (3 - n_close);
    if (c <= 0 || bits <= 0 || h < 0) return false;
    for (int i = 0; i < bits; i++) x[i] = 1;
    x[n_close + h - 1] = 1;
    return true;
}

bool mlp_featurize(const mlp_model_t *m, const char *line, float *x) {
    char buf[512];
    const char *w[64]; int nw = 0;
    size_t n = strlen(line);
    if (n >= sizeof buf) return false;
    memcpy(buf, line, n + 1);
    for (char *p = buf; *p; ) {                 /* split on spaces (no strtok: the advisor runs on its own thread) */
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        if (nw == 64) return false;
        w[nw++] = p;
        while (*p && *p != ' ') p++;
    }
    /* the fields in order, each with its value words (one, or two for a sighting) */
    const char *const *val[F_COUNT]; int nval[F_COUNT];
    int i = 0;
    for (int f = 0; f < F_COUNT; f++) {
        if (i >= nw || strcmp(w[i], FIELDS[f]) != 0) return false;
        int j = i + 1;
        while (j < nw && find(FIELDS, F_COUNT, w[j]) < 0) j++;
        val[f] = w + i + 1; nval[f] = j - i - 1;
        if (nval[f] < 1 || nval[f] > 2) return false;
        i = j;
    }
    if (i != nw) return false;

    float *o = x;
    int zone = nval[F_ZONE] == 1 ? clock_hour(val[F_ZONE][0]) : -1;
    if (zone < 1 || zone > 6) return false;
    for (int k = 0; k < 6; k++) *o++ = k == zone - 1;
    for (int d = 0; d < 8; d++) {
        const char *v = val[DIGIT_FIELDS[d]][0];
        if (nval[DIGIT_FIELDS[d]] != 1 || v[0] < '0' || v[0] > '9' || v[1]) return false;
        for (int k = 1; k <= 9; k++) *o++ = v[0] - '0' >= k;
    }
    int st = nval[F_STAGE] == 1 ? find(STAGES, 4, val[F_STAGE][0]) : -1;
    if (st < 0) return false;
    for (int k = 0; k < 4; k++) *o++ = k == st;
    int sp = -1;
    for (int k = 0; k < 16; k++) if (m->species[k][0] && strcmp(m->species[k], val[F_SPECIES][0]) == 0) sp = k;
    if (sp < 0 || nval[F_SPECIES] != 1) return false;
    for (int k = 0; k < 16; k++) *o++ = k == sp;
    for (int s = 0; s < 4; s++, o += 15)
        if (!sighting(o, 3, val[SIGHTINGS[s]], nval[SIGHTINGS[s]])) return false;
    if (!sighting(o, 2, val[F_WALL], nval[F_WALL])) return false;
    o += 14;
    int last = nval[F_LAST] == 1 ? find(GOALS, MLP_N_GOALS, val[F_LAST][0]) : -1;
    if (last < 0) return false;
    for (int k = 0; k < MLP_N_GOALS; k++) *o++ = k == last;
    if (nval[F_TIME] != 1 || (strcmp(val[F_TIME][0], "day") && strcmp(val[F_TIME][0], "night"))) return false;
    *o++ = strcmp(val[F_TIME][0], "night") == 0;
    return o - x == m->n_in;
}

/* ---- the forward pass ---- */
static void linear(const mlp_layer_t *L, const float *x, float *y, bool relu) {
    for (int r = 0; r < L->rows; r++) {
        const int8_t *w = L->q + (size_t)r * L->cols;
        float acc = 0;
        for (int c = 0; c < L->cols; c++) acc += (float)w[c] * x[c];
        float v = L->bias[r] + L->scale[r] * acc;
        y[r] = relu && v < 0 ? 0 : v;
    }
}

void mlp_model_forward(mlp_model_t *m, const float *x, float *goal_logits, float *urgency_logits) {
    const float *in = x;
    float *out = m->a;
    for (int l = 0; l < m->layers; l++) {
        linear(&m->L[l], in, out, true);
        in = out; out = out == m->a ? m->b : m->a;
    }
    linear(&m->L[m->layers], in, goal_logits, false);
    linear(&m->L[m->layers + 1], in, urgency_logits, false);
}

int mlp_model_decide(mlp_model_t *m, const char *line, float temp, uint32_t *rng,
                     float *probs_out, int *urgency_out) {
    if (!mlp_featurize(m, line, m->x)) return -1;
    float g[MLP_N_GOALS], u[MLP_N_URGENCY], p[MLP_N_GOALS];
    mlp_model_forward(m, m->x, g, u);
    /* the softmax at temp, and the draw, as q4_model_decide makes them */
    float inv = temp > 0 ? 1.0f / temp : 1.0f, mx = -1e30f, sum = 0;
    for (int i = 0; i < MLP_N_GOALS; i++) { p[i] = g[i] * inv; if (p[i] > mx) mx = p[i]; }
    for (int i = 0; i < MLP_N_GOALS; i++) { p[i] = expf(p[i] - mx); sum += p[i]; }
    for (int i = 0; i < MLP_N_GOALS; i++) p[i] /= sum;
    if (probs_out) for (int i = 0; i < MLP_N_GOALS; i++) probs_out[i] = p[i];
    int pick = 0;
    if (temp > 0 && rng) {
        uint32_t s = *rng; s ^= s << 13; s ^= s >> 17; s ^= s << 5; *rng = s;
        float r = (float)(s & 0xffffff) / 16777216.0f, acc = 0;
        for (int i = 0; i < MLP_N_GOALS; i++) { acc += p[i]; if (r < acc) { pick = i; break; } pick = i; }
    } else for (int i = 1; i < MLP_N_GOALS; i++) if (p[i] > p[pick]) pick = i;
    int ub = 0;
    for (int i = 1; i < MLP_N_URGENCY; i++) if (u[i] > u[ub]) ub = i;
    if (urgency_out) *urgency_out = ub;
    return pick;
}
