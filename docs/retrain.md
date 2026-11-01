# Retraining the model

A runbook. The pipeline's steps and tools are in [pipeline.md](pipeline.md), the state
line in [model/schema.md](../model/schema.md), the shipped model's numbers in
[stats.md](stats.md).

The shipped model is **v5j**: schema 5, vocab 66, 14.19M parameters, 7.56 MB at 4 bits.

## When a retrain is needed

Anything that changes what the model SEES (the state line) or what it may CHOOSE (the
goals). The model was trained on an exact format, and an unseen token gets unpredictable
answers, so none of this can ship as a firmware-only change. Batch such changes into one
schema bump.

Nothing else needs one. The reflex layer can change how a goal is carried out, and new
decor or helpers reach the creatures through cover and the film.

## What is queued

- **The swordfish and the turtle** have no species word: the encoder sends them as
  `species fish` (`advisor_core_species_word`), so they are decided as classic fish with
  their traits. [One species on a shipped model](#one-species-on-a-shipped-model) adds them.
- **Shrimp to watch.** A short `shrimp <N> out` field near the sightings (3 tokens: 49 of
  the 64 KV positions) so a bored creature can choose to watch the school. Undecided: a new
  goal (a schema change to the goal list, the C enum and the reflex layer) or an existing
  one used near the school; what "out" means (`shrimp_t` has no hiding state); and the
  acceptance numbers. The goal: P(watching | bored high, shrimp out) up, everything else
  roughly unchanged, no flattening of personality.
- **Solo mode**, if it is built, needs `friend none` probed on the shipped model first
  ([progression.md](progression.md)).

## The rules

- **The prompt is the model's DNA.** Most of what goes wrong is one sentence in the
  teacher's prompt (lessons below). If a check fails, edit ONE sentence of
  `SPECIES_PROMPT_V5` and re-run. Never touch the part before it, and never fix a model's
  behaviour in the sim: **the model owns its decisions.**
- **The encoder and the training must agree.** The C encoder has to write exactly what
  `model/gen_traces.py` trains on; `--selftest-encoder` plus `encoder_agree.py` enforce it
  (and CI).
- **The tokenizer and the model go together.** New words are APPENDED, so ids never move. A
  newer tokenizer with an older model runs as the older schema (it reads only the model's
  ids), so the firmware's tokenizer may go out first. An older tokenizer with a newer model
  is refused ("bad model/tokenizer", advisor off), so never after.
- **Keep the teacher free.** A cycle keeps an Ollama host busy for hours; point `--host` /
  `HOST` at a machine you can monopolize.

## 0. Preflight (about 20 min of teacher time; the required gate)

```bash
cd model
python3 gen_traces.py --count 10 --dry-run --schema 5 --seed 1          # state lines print, no network
python3 train_tokenizer.py --schema 5                                   # out/tokenizer_v5.bin
(cd ../sim && make && ./fishsim --selftest-encoder ../model/out/tokenizer_v5.bin) | python3 encoder_agree.py
python3 prompt_check.py --schema 5 --workers 2 | tee out/prompt_check_v5.log
```

Watch stderr for `[warn] the prompt used N of num_ctx 2048 tokens`. It must not appear: a
prompt that fills the window has its middle cut, and the labels are made from part of it.
Raise `num_ctx()` in gen_traces.py if it does. `prompt_check` prints `teacher prompt: up to N
tokens evaluated`; expect ~1,300-1,500.

The panel's checks for the classic fish (social and bold cliffs, starving, lonely, boredom,
night, flee 0, the content attractor), and per species:

| check | want | why |
|---|---|---|
| P(rest \| angler, content day) | >= 0.40 | it lies in wait |
| P(rest \| shark, content day) | <= 0.10 | it never stops |
| P(explore \| shark, content day) | >= 0.40 | it patrols |
| P(follow \| squid, friend near) | >= 0.40 | the school |
| P(reef + explore \| octopus) | >= 0.60 | the curious loner |
| P(follow \| octopus) | <= 0.05 | |
| P(dart \| seahorse, energetic) | <= 0.03 | a poor swimmer |
| P(explore + reef \| puffer) | >= 0.50 | it pokes at everything |
| P(seek_food \| crab, hunger 4-5, food mid) | >= 0.60 | the eager scavenger |
| P(rest \| lobster, day) | >= 0.40 | in its den |
| P(rest \| lobster, night) | <= 0.40 | nocturnal |
| P(follow \| lobster, day) | <= 0.05 | solitary |
| P(rest \| eel, night) | <= 0.50 | the night hunter |
| P(seek_food \| any species, starving) | >= 0.95 | temperament never beats hunger |

## 1-4. The night (unattended)

```bash
cd model
nohup ./run_v5_overnight.sh > /dev/null 2>&1 &          # HOST=... WORKERS=2 to change
tail -f out/v5_overnight.log
```

`run_v5_overnight.sh`: 3 workers x 8 h (`MINUTES` / `COUNT` / `WORKERS` / `FISH_SHARE`
overrides) → fold and verify (`out/v5_clean.jsonl`, with the per-species table) →
`convert_to_v5.py` re-encodes any older labels as `species fish` → fold the mix → train 14M (dim
384, 8 layers, 8 heads, seq 64, batch 64, lr 6e-4, `ITERS` 7000) → checkpoint, `model_q4` and
`model` binaries → `probe_dist.py --schema 5`.

Expect ~15-20K new pairs a night. If the morning's fold shows a species under ~1,000, run a second
night with `FISH_SHARE=0.15` and re-run the train step on the bigger fold.

Before bed, peek at the first ~200 lines of a worker:

```bash
python3 fold_traces.py --schema 5 out/v5_traces_a.jsonl --out /tmp/v5_peek.jsonl
```

No goal over 40%, every species present, and each species' top goals should look like its
paragraph. A species whose goals look exactly like the fish's is a prompt the teacher did not
take in: check for the `num_ctx` warning first.

## Acceptance (morning)

Evaluate the file that ships, not the fp32 one:

```bash
cc -O3 -o runq4 runq4.c -lm
python3 eval.py --schema 5 --teacher --count 400 --min-per-species 30 --seed 5 \
    --run-bin ./runq4 --model-bin out/model_q4_v5m.bin --tok-bin out/tokenizer_v5.bin | tee out/eval_v5.log
```

- **Teacher agreement >= 72% overall and for every species** (the script exits 1 below it). With 30
  states a species the number is +-8 points: re-run one at 65-72% with `--min-per-species 80`
  before calling it a fail.
- **The probe's species panel:** angler and lobster rest by day, the hammerhead's rest ~0 by day,
  squid follow a friend in view, lobster and eel awake at night, octopus and puffer on the reef and
  exploring, seahorse dart ~0, crab seeks food earliest; starving >= 0.85 for every species;
  P(flee_shadow) ~0.
- **The classic fish did not regress:** bubbles again <= 0.15 at bored 8-9 with explore up, night +
  bored 9 rests, starving + bored 9 seeks food >= 0.85, the personality cliffs no worse. Judge
  those on real states (`model/eval.py`'s real-state rows): the probe's synthetic panel reads low.
- **The sim:** `AQUA_PETS_MODEL=../model/out/model_q4_v5m.bin AQUA_PETS_TOKENIZER=../model/out/tokenizer_v5.bin
  ./fishsim --selftest-llm` prints the schema, passes, and its census is no worse than the shipped
  model's in [stats.md](stats.md).

## 5. Ship it

```bash
cd model
cp out/model_q4.bin out/model_q4_shipped.bin                # the rollback copy
cp out/model_q4_v5m.bin out/model_q4.bin
cp out/tokenizer_v5.bin out/tokenizer.bin
cp out/tokenizer_v5.bin ../firmware/main/tokenizer.bin
cd ../sim && make check-all
```

In the same commit:

- `common/version.h`: `PT_MODEL_TAG`, `PT_MODEL_LEN` (bytes of the new `model_q4.bin`) and
  `PT_MODEL_SHA256`. `python3 tools/model_trailer.py --check` must pass: the tank heals its trailer
  from these, and an OTA manifest with a new model tag says "needs the cable" ([OTA.md](OTA.md)).
- `.github/workflows/checks.yml`: the q4_host prompt, if the line changed.
- `model/schema.md` for a schema change, [stats.md](stats.md) for the eval and the probe,
  [DEVICE.md](DEVICE.md) for what is on the tank, and this file's queue.

Then flash with `tools/flash.sh --model` (the model partition and its trailer).

## Rollback

Nothing in C needs reverting: the encoder speaks whatever the tokenizer is.

```bash
git checkout <the previous commit> -- model/out/model_q4.bin model/out/tokenizer.bin \
    firmware/main/tokenizer.bin common/version.h
```

Rebuild, then `tools/flash.sh --model`; both files go back together.

## If the night goes wrong

- **`[warn] OSError: curl failed`** filling the logs: the teacher is queueing. `WORKERS=2`.
- **`[warn] the prompt used N of num_ctx ...`:** the species paragraph is being cut. Stop, raise
  `num_ctx()`, regenerate; those labels are not species labels.
- **A worker dies:** the others keep going. Re-run it with a new seed; files append and the fold
  de-duplicates.
- **A species' labels look like the fish's, or one goal owns a species:** it is the prompt. Edit one
  sentence of `SPECIES_PROMPT_V5` and regenerate that worker's file.

## One species on a shipped model

No new schema, one word. This is how the swordfish and the turtle will go.

1. **The word:** `train_tokenizer.py` `SPECIES_WORDS` (APPENDED, so every id holds) and `gen_traces.py`
   `SPECIES` (its traits from `creature.c`), `_habitat`, `rules_goal`, and one sentence in
   `SPECIES_PROMPT_V5`. Add cases and checks to `prompt_check.py` and to `probe_dist.py` `RANGES`.
2. **The gate:** `prompt_check.py --schema 5`. The new checks pass and the old ones did not move.
3. **Its own labels:** `gen_traces.py --schema 5 --focus <word> --focus-share 0.8 --fish-share 0.25`,
   about 6,000 states.
4. **Train:** fold them onto the shipped training set, tokenizer, 7,000 iterations, export, probe, eval
   (>= 72% overall and per species, the new one included).
5. **Ship** as in step 5. The tokenizer may go out first: until the vocab has the word the encoder
   sends `species fish`.

## What moves a model: lessons from the runs

- **One sentence can move the whole distribution.** "A social fish follows friends and dislikes being
  alone" made 45% of all labels `follow_friend`; rewording it to "friends are always nearby in a small
  tank - mere proximity is never a reason" made 14%.
- **A "small" rewrite can flatten personality.** A rewritten prompt took P(follow | social >= 8) in the
  teacher's own labels from 0.50 to 0.18, and the student learned that. Mixing the older labels back in
  restored the cliffs. Keep the classic fish's prompt untouched, and compare its checks after any change.
- **The teacher wants proportions.** It reads a soft licence ("more than a fish would") as the default
  and a soft limit ("only sometimes") as never. "About a third of the time" works.
- **Check the behaviour you care about, not just the agreement score.** A student at 88% had stopped bold
  fish playing, because the teacher's own labels rarely did. One sentence, then relabelling only the
  states it is about (`relabel_subset.py`) and training those rows 3x, restored it at 84%.
- **Fix the data, not the sim.** A student that fled by stress rather than shadow distance, or a corner
  case in a probe, is a data question. Judge on real states before a synthetic panel.
