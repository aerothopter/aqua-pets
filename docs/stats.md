# Measured numbers

Every figure here was measured in this repo's pipeline. "Teacher" is gemma4:26b (Q4_K_M) via Ollama;
"agreement" is the student picking the same goal as the teacher on fresh situations it never trained
on (`model/eval.py --teacher`).

## Teacher and student

| | Teacher | Student (ships) |
|---|---|---|
| Model | gemma4:26b | Aqua Pets 14.19M (dim 384, 8 layers, 8 heads) |
| Parameters | ~26,000,000,000 | 14,190,000 (about 1,832× fewer) |
| Size | ~18 GB (Q4_K_M) | 57 MB fp32 → **7.56 MB 4-bit** (7,560,088 B) |
| Runs on | a desktop GPU or CPU | ESP32-S3, from flash, no network |
| Vocabulary | ~262K tokens | **66 tokens** (a closed schema lexicon, eleven of them species) |

The teacher agrees with **itself** 82% of the time on the same states asked twice, on the classic fish: a
practical ceiling.

## The shipped model, v5j

Schema 5. 52,461 training rows, 7,000 iterations on CPU in 65 min, best validation loss 0.717.

| | |
|---|---|
| teacher agreement (448 states, seed 777) | **83%** |
| per species | **72.3% (lobster) .. 93.3% (angler)**; classic fish 80.6%, jellyfish 91.9% |
| greedy == teacher / teacher in the top two, on 800 real states | 88% / 98% |
| survival overrides, sim, 60 s, 12 fish | **0** |

**What the model does:**

| probe | |
|---|---|
| starving, min P(seek_food) over 18 fish identities | 0.77 (one timid identity; sampled, P(not seek_food) is 0.00 at T 0.5-1.0) |
| bored 0 → 9 at the bubbles, P(bubbles) | 0.60 → 0.00 |
| social 0 → 9, P(follow), synthetic panel | 0.01 → 0.28 |
| night, classic fish, rest | 1.00 |
| lobster, rest by day / at night | 0.83 / 0.04 |
| content jellyfish, day | explore 0.83, rest 0.12, bubbles 0.03, dart 0.00 |
| jellyfish at night / starving / friend near | rest 1.00 / seek_food 0.99 / follow 0.38 |

Real states keep the full personality cliffs that the synthetic panel understates. Measured on v5m, which v5j
extends with the jellyfish (student vs teacher): bold 7-9 content, P(dart) 18% vs 15%; social 8-9 / 0-1 content,
P(follow) 86 / 18% vs 84 / 12%.

**A sim census** (`--selftest-llm 5`, five real-time minutes, 10 creatures, v5m): 28% of creature-time at a
landmark, 2.2 distinct zones per creature-minute, 1% dart share, 0 survival overrides.

**The distribution** is real signal: about 1 decision in 5 differs when sampled at T = 1 rather than taken
greedily, almost always to the teacher's own second choice (the teacher's label is in the top two 98% of the
time). The names `mira`, `bolt`, `kelp` and `nori` gave identical distributions, which is why schema 3 dropped
them.

## The training data

Teacher labels for the schema 5 set: 45,011 clean pairs from 45,021 raw, the classic fish 38.8% and each other
species 2,755-3,310, plus 5,998 jellyfish-focused states (3,805 of them jellyfish). Labelling runs ~2,350-2,700
states an hour per teacher host, up to ~45 states a minute with several workers.

Size (a 14M student, dim 384) matters: at 25,903 pairs on the first schema, a 0.9M student reached 53%, 8.8M 78%,
14.3M 87%.

## Engine

| | |
|---|---|
| 4-bit vs fp32 decisions (60 fresh states) | 97% same goal (58/60) |
| word-level vs char-level tokenizer | 46 tokens a decision vs ~200 (3.7× faster) |
| prompt, longest | 41 words + `->` + BOS = 43 tokens; 46 with the reply, of the 64-position KV cache |

## On the device (1.8, 2026-10-28)

| | |
|---|---|
| inference | 10-13 tok/s, about 3.7-4.4 s a decision |
| rendering | 25-30 fps alongside it, on the other core |
| full tank of 25 | a fresh decision per creature every ~90 s; the reflex layer moves them in between |
| signed app | 0x201000 of the 0x280000 slot (20% free) |
| `.dram0.bss` | 58,928 B (internal RAM headroom ~23 KB: watch it) |
| internal heap free | ~40 KB |
| PSRAM free | ~4.6 MB |

The model is mmapped from flash. [memory_budget.md](memory_budget.md) has the budget.

## Power

The AXP2101 has no current sense, so these are gauge numbers, off the %-scale. Compare them with each other.

| state | draw |
|---|---|
| awake, 4 creatures, 60% brightness | 72-120 mA (96% → 52% in an hour) |
| charging at 100 mA | 17% → 100% in under an hour |
| deep sleep, pads not isolated | ~15 mA: worse than the light-sleep drowse's 4.7 |
| PMIC power-off (the night mode) | at or under ~0.25 mA, the gauge's 1% step the floor of what it resolves (8 h 28 min for ~1%) |

The levers on the awake draw are the LLM's duty (`ADVISOR_MIN_INTERVAL`, `ADVISOR_IDLE_CEILING`, the
`state_signature` bands) and the brightness. [DEVICE.md](DEVICE.md) says how to measure.
