/* label_q4.c — label state lines with the shipped 4-bit model, the engine the
 * tank runs (common/llm/q4_model.c), for training a smaller student on its
 * distribution (gen_traces.py --teacher student).
 *
 * One state line per stdin line (no trailing " ->"); one line out per state:
 *   <goal> urgency <d> <p seek_food> <p flee_shadow> ... <p inspect_reef>
 * the greedy goal, the 8 goal probabilities in GOAL_NAMES order (T = 1), and
 * the greedy urgency (5 when the model wrote none, as schema.md reads it).
 * Build from model/:
 *   cc -O3 -I../common/llm -o label_q4 label_q4.c \
 *     ../common/llm/q4_model.c ../common/llm/word_tok.c -lm */
#include "q4_model.h"
#include "word_tok.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *GOALS[8] = { "seek_food", "flee_shadow", "visit_bubbles", "follow_friend",
                                "explore", "rest", "dart_play", "inspect_reef" };   /* common/tank.h goal_id_t */

static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n); if (b && fread(b, 1, *n, f) != *n) { free(b); b = NULL; }
    fclose(f); return b;
}
static void *xalloc(size_t n) { return calloc(1, n); }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: label_q4 model_q4.bin tokenizer.bin < states.txt\n"); return 1; }
    size_t ml, tl; uint8_t *mb = slurp(argv[1], &ml), *tb = slurp(argv[2], &tl);
    if (!mb || !tb) { fprintf(stderr, "could not read model/tokenizer\n"); return 1; }
    q4_model_t *m = q4_model_open(mb, ml, xalloc); if (!m) { fprintf(stderr, "bad model\n"); return 1; }
    word_tok_t tok;
    if (word_tok_init(&tok, tb, tl, q4_model_config(m)->vocab_size) != 0) { fprintf(stderr, "bad tokenizer\n"); return 1; }
    int goal_ids[8];
    for (int g = 0; g < 8; g++) {
        goal_ids[g] = -1;
        for (int id = 0; id < tok.vocab_size; id++) {
            const char *p = word_tok_piece(&tok, id);
            if (p[0] == ' ' && strcmp(p + 1, GOALS[g]) == 0) goal_ids[g] = id;
        }
        if (goal_ids[g] < 0) { fprintf(stderr, "tokenizer has no `%s`\n", GOALS[g]); return 1; }
    }
    char line[512], prompt[520];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        snprintf(prompt, sizeof prompt, "%s ->", line);
        int toks[64], out[4]; float p[8];
        int n = word_tok_encode(&tok, prompt, toks, 60);
        int nout = q4_model_decide(m, toks, n, goal_ids, 8, 0, NULL, p, out, 3);
        int g = 0, urg = 5;
        for (int i = 1; i < 8; i++) if (p[i] > p[g]) g = i;
        if (nout >= 3 && strcmp(word_tok_piece(&tok, out[1]), " urgency") == 0) {
            const char *d = word_tok_piece(&tok, out[2]);
            if (d[0] == ' ' && d[1] >= '0' && d[1] <= '9' && d[2] == 0) urg = d[1] - '0';
        }
        printf("%s urgency %d", GOALS[g], urg);
        for (int i = 0; i < 8; i++) printf(" %.5f", p[i]);
        printf("\n");
        fflush(stdout);
    }
    return 0;
}
