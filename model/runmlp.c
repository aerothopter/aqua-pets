/* runmlp.c — run the MLP student (common/llm/mlp_model.c, the engine the tank
 * runs) from the command line.
 *
 *   runmlp mlp.bin [-z tok] [-t temp] -i "<state line> ->"
 *       runw / runq4's command line (eval.py --run-bin): prints
 *       "<state line> -> <goal> urgency <d>". -z is accepted and unused.
 *   runmlp mlp.bin --label < states.txt
 *       label_q4's output, a line per state: <goal> urgency <d> <8 probabilities>
 *   runmlp mlp.bin --features < states.txt
 *       features.py --dump's output, the 0/1 vector (features_agree.py)
 *   runmlp mlp.bin --bench
 *       time per decision
 * Build from model/:
 *   cc -O3 -I../common/llm -o runmlp runmlp.c ../common/llm/mlp_model.c -lm */
#include "mlp_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *GOALS[MLP_N_GOALS] = { "seek_food", "flee_shadow", "visit_bubbles", "follow_friend",
                                          "explore", "rest", "dart_play", "inspect_reef" };

static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n); if (b && fread(b, 1, *n, f) != *n) { free(b); b = NULL; }
    fclose(f); return b;
}
static void *xalloc(size_t n) { return calloc(1, n); }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: runmlp mlp.bin [-t temp] -i \"<state> ->\" | --label | --features | --bench\n"); return 1; }
    size_t ml; uint8_t *mb = slurp(argv[1], &ml);
    mlp_model_t *m = mb ? mlp_model_open(mb, ml, xalloc) : NULL;
    if (!m) { fprintf(stderr, "bad model %s\n", argv[1]); return 1; }
    const char *prompt = NULL; float temp = 1.0f; int mode = 0;   /* 0 prompt, 1 label, 2 features, 3 bench */
    for (int a = 2; a < argc; a++) {
        if (!strcmp(argv[a], "-i") && a + 1 < argc) prompt = argv[++a];
        else if (!strcmp(argv[a], "-t") && a + 1 < argc) temp = (float)atof(argv[++a]);
        else if (!strcmp(argv[a], "-z") && a + 1 < argc) a++;
        else if (!strcmp(argv[a], "--label")) mode = 1;
        else if (!strcmp(argv[a], "--features")) mode = 2;
        else if (!strcmp(argv[a], "--bench")) mode = 3;
    }
    uint32_t rng = (uint32_t)time(NULL) | 1u;
    char line[512];
    if (mode == 0) {
        if (!prompt) { fprintf(stderr, "no -i prompt\n"); return 1; }
        snprintf(line, sizeof line, "%s", prompt);
        char *arrow = strstr(line, " ->"); if (arrow) *arrow = 0;
        int urg, g = mlp_model_decide(m, line, temp, &rng, NULL, &urg);
        if (g < 0) { printf("%s -> <unparsed state>\n", line); return 1; }
        printf("%s -> %s urgency %d\n", line, GOALS[g], urg);
        return 0;
    }
    if (mode == 3) {
        const char *s = "zone 5 hunger 2 energy 7 stress 0 curiosity 3 bold 6 social 1 stage adult species angler "
                        "trust 5 bored 1 food none friend mid 3 bubble far 10 reef mid 7 wall near 6 last rest time day";
        int n = 100000, urg; clock_t t0 = clock();
        for (int i = 0; i < n; i++) mlp_model_decide(m, s, 1.0f, &rng, NULL, &urg);
        double us = 1e6 * (double)(clock() - t0) / CLOCKS_PER_SEC / n;
        printf("%d decisions, %.2f us each (featurize + forward + sample); %d -> %d x %d\n",
               n, us, mlp_model_n_features(m), mlp_model_hidden(m), mlp_model_layers(m));
        return 0;
    }
    float *x = malloc(sizeof(float) * (size_t)mlp_model_n_features(m));
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        if (mode == 2) {
            if (!mlp_featurize(m, line, x)) { printf("<unparsed>\n"); continue; }
            for (int i = 0; i < mlp_model_n_features(m); i++) putchar(x[i] != 0 ? '1' : '0');
            putchar('\n');
        } else {
            float p[MLP_N_GOALS]; int urg, g = mlp_model_decide(m, line, 0, NULL, p, &urg);
            if (g < 0) { printf("<unparsed>\n"); fflush(stdout); continue; }
            printf("%s urgency %d", GOALS[g], urg);
            for (int i = 0; i < MLP_N_GOALS; i++) printf(" %.5f", p[i]);
            printf("\n");
        }
        fflush(stdout);
    }
    return 0;
}
