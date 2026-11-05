/* mlp_model.h — the MLP student (model/train_mlp.py, model/export_mlp.py): a
 * small fully-connected network from a v5 state line to the goal and the
 * urgency, the alternative to the 4-bit transformer (q4_model.h).
 *
 * The file (export_mlp.py): a header (magic "AMLP", version, sizes, the 16
 * species slot names), then each layer as fp32 row scales, fp32 biases and
 * int8 weights (row-major, padded to 4 bytes). The weights are read in place
 * from the file's memory: nothing is copied. Activations are fp32.
 *
 * mlp_featurize writes the same 0/1 vector as model/features.py
 * (model/features_agree.py checks them against each other). */
#ifndef MLP_MODEL_H
#define MLP_MODEL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MLP_MAGIC      0x504c4d41u   /* "AMLP" */
#define MLP_N_GOALS    8             /* common/tank.h goal_id_t order */
#define MLP_N_URGENCY  10

typedef struct mlp_model mlp_model_t;

bool mlp_model_is(const uint8_t *bin, size_t len);      /* the magic only */
/* NULL on a bad header or a file shorter than its layers; alloc holds the
 * struct and the activation buffers (a few KB) */
mlp_model_t *mlp_model_open(const uint8_t *bin, size_t len, void *(*alloc)(size_t));
int  mlp_model_n_features(const mlp_model_t *m);
int  mlp_model_hidden(const mlp_model_t *m);
int  mlp_model_layers(const mlp_model_t *m);
const char *mlp_model_species_slot(const mlp_model_t *m, int i);   /* 0..15; "" = free */

/* a v5 state line (no trailing " ->") -> x[mlp_model_n_features]; false if the
 * line is not a v5 line or names a species no slot holds */
bool mlp_featurize(const mlp_model_t *m, const char *line, float *x);
/* the forward pass: goal_logits[MLP_N_GOALS], urgency_logits[MLP_N_URGENCY] */
void mlp_model_forward(mlp_model_t *m, const float *x, float *goal_logits, float *urgency_logits);
/* decide, as q4_model_decide does: the goal sampled from the softmax at temp
 * with the xorshift32 state *rng (temp <= 0 or rng == NULL: greedy), the goal
 * probabilities in probs_out (may be NULL), the greedy urgency. Returns the
 * goal index, or -1 if the line could not be featurized. */
int  mlp_model_decide(mlp_model_t *m, const char *line, float temp, uint32_t *rng,
                      float *probs_out, int *urgency_out);
#endif
