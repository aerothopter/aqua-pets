# State & Goal Schema (v5)

The encoding the shipped model, **v5j**, reads and writes ([retrain.md](../docs/retrain.md), [stats.md](../docs/stats.md)). The
tokenizer and all training data depend on it: do not change field order, vocabulary or value ranges without regenerating the traces
and retraining.

Three things implement it and must agree:

- **Training:** `gen_traces.py --schema 5` (`render_v5` / `parse_v5`, the field list `V5_FIELDS`).
- **The tank:** `common/llm/advisor_core.c`, which picks v5 when the loaded tokenizer contains ` species`.
- **The vocabulary:** `train_tokenizer.py --schema 5`.

`./fishsim --selftest-encoder <v5 tokenizer> | model/encoder_agree.py` checks the C and Python lines against each other, in CI.

## State line (model input)

One lowercase, space-delimited line, in this fixed order. The advisor is asked one creature at a time, and the line is egocentric to
that creature.

```
zone <1-6> hunger <0-9> energy <0-9> stress <0-9> curiosity <0-9>
bold <0-9> social <0-9> stage fry|juv|adult|elder species <species> trust <0-9> bored <0-9>
food <dist> <clock>|none friend <dist> <clock>|none
bubble <dist> <clock>|none reef <dist> <clock>|none wall <dist> <clock>|clear
last <goal> time day|night
```

(The line breaks are for reading; the real encoding is one line.)

Example:

```
zone 5 hunger 2 energy 7 stress 0 curiosity 3 bold 6 social 1 stage adult species angler trust 5 bored 1 food none friend mid 3 bubble far 10 reef mid 7 wall near 6 last rest time day
```

**Values**

- **`zone`:** a 3-wide × 2-tall grid over the tank, numbered left to right, top row first:

  ```
  1 2 3
  4 5 6
  ```

- **`<dist>`:** `near`, `mid` or `far`. A sighting is `<dist> <clock>`, or `none` when absent or out of range.
- **`<clock>`:** a clock position `1`–`12` relative to the creature's heading: `12` is dead ahead, `6` behind.
- **Drives and traits:** single digits, so every value is one short token.

**Fields**

- **`hunger`, `energy`, `stress`, `curiosity`:** the drives, clamped to 0–9.
- **`bold`, `social`:** slow-drifting personality traits. **`stage`:** the life stage. The model conditions on them, so one model
  expresses every personality and age.
- **`species`:** `SPECIES[f->species].token`, one of `fish` (the classic fish) `seahorse octopus puffer angler eel shark squid crab
  lobster jellyfish`. Each has a real animal's temperament (the anglerfish lies in wait, the hammerhead never stops, the lobster
  wakes at night) that no trait number carries. A species whose word the loaded vocabulary lacks is sent as `species fish`
  (`advisor_core_species_word`); the swordfish and the turtle go this way against v5j ([species.md](../docs/species.md)).
- **`trust`:** the keeper relationship, `fish_t.trust` 0–10 clamped to one digit.
- **`bored`:** how stale the current pastime is, `fish_t.bored` 0–10 clamped to one digit.
  - It rises while the creature keeps a leisure goal, 0 to 9 in a minute.
  - A goal that is not the one just left relieves it (`BORED_NEW_GOAL`), and so does entering a zone unseen for 20 s
    (`BORED_NEW_ZONE`).
  - Eating and resting at night ease it, and a night's sleep resets it.
  - Its band is in the re-ask signature.
- **`food`:** the nearest pellet.
- **`friend`:** the nearest other creature, of any species.
- **`bubble`:** the bubble column. **`reef`:** the reef cluster once the keeper owns one, else the grass corner's spot.
- **`wall`:** `clear`, or the nearest wall when it is `near` or `mid` (`wall near 9`).
- **`last`:** the creature's current goal, a hysteresis signal.
- **`time`:** the day/night cycle.

A line is at most 41 words: a prompt of 43 tokens with `->` and BOS, 46 with the reply, of the 64-position KV cache.

## Goal output (model output)

```
<goal> urgency <0-9>
```

where `<goal>` is one of:

```
seek_food  flee_shadow  visit_bubbles  follow_friend
explore    rest         dart_play      inspect_reef
```

Example: `seek_food urgency 8`

- A goal fuses intent and target; the reflex layer resolves the concrete target (the nearest pellet, the nearest friend).
- Urgency scales cruise speed and steering.
- The first token is sampled from the goals alone. A missing urgency reads as 5.
- `flee_shadow` is never a label, and the teacher's JSON enum excludes it. It stays in the vocabulary because `GOAL_FLEE_SHADOW` stays
  in `GOAL_NAMES` and `advisor_core_init` wants a token id for every goal: one idle embedding row beats a C special case.

## Traces (JSONL, one object per line)

```json
{"state": "zone 5 ... time day", "goal": "rest urgency 3"}
```

Ollama is queried with structured output constraining the reply to `{"goal": <enum>, "urgency": 0-9}`, which `gen_traces.py` renders
into the goal line. Older datasets convert mechanically with `convert_to_v5.py`: every older label was about the classic fish, so it
gains `species fish`.

## Training serialization

Each trace becomes the document `<BOS>state -> goal`, with documents concatenated. At inference the prompt is `state ->` and the model
completes the goal.

The tokenizer is word-level: one token per whitespace word of the closed vocabulary, **66** ids with the specials
(`train_tokenizer.py`). New words are appended, so a model built on a shorter vocabulary reads a longer tokenizer's first ids as its
own, and a tokenizer may go out before its model. Encoding is split-and-lookup in C (`model/runw.c`, `common/llm/word_tok.c`).
