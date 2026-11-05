#!/usr/bin/env python3
"""Widen an MLP student's training states: take real v5 states, re-roll a few
fields of each to another valid value, and have the shipped model label the
result (label_q4.c). Stdlib only.

The sim's sampler spends most states on hungry creatures, so calm situations
and unusual trait/landmark mixes are thin; a student copying the shipped model
learns those only from states that cover them. The labels cost nothing, so
the states can cover the space more widely than the sim visits it.

  python3 augment_states.py --data out/mlp_student_clean.jsonl --count 20000 --seed 31 \
      --out out/mlp_aug_31.jsonl
"""
import argparse
import json
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_traces as gt  # noqa: E402

DIGITS = [str(d) for d in range(10)]
CLOCK = [str(h) for h in range(1, 13)]


def reroll(field, rng):
    if field == "zone":
        return str(rng.randint(1, 6))
    if field in ("hunger", "energy", "stress", "curiosity", "bold", "social", "trust", "bored"):
        return rng.choice(DIGITS)
    if field == "stage":
        return rng.choice(gt.STAGES)
    if field == "species":
        return rng.choice(gt.SPECIES_TOKENS)
    if field in ("food", "friend", "bubble", "reef"):
        d = rng.choice(["none", "near", "mid", "far"] if field in ("food", "friend") else ["near", "mid", "far"])
        return d if d == "none" else f"{d} {rng.choice(CLOCK)}"
    if field == "wall":
        d = rng.choice(["clear", "near", "mid"])
        return d if d == "clear" else f"{d} {rng.choice(CLOCK)}"
    if field == "last":
        return rng.choice(gt.GOALS_V4)
    if field == "time":
        return rng.choice(["day", "night"])
    raise ValueError(field)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True, help="jsonl of real v5 states to start from")
    ap.add_argument("--count", type=int, required=True)
    ap.add_argument("--max-fields", type=int, default=4, help="re-roll 1..N fields of each state")
    ap.add_argument("--seed", type=int, default=31)
    ap.add_argument("--out", required=True)
    ap.add_argument("--label-bin", default=os.path.join(here, "label_q4"))
    ap.add_argument("--label-model", default=os.path.join(here, "out", "model_q4.bin"))
    ap.add_argument("--label-tok", default=os.path.join(here, "out", "tokenizer.bin"))
    args = ap.parse_args()

    rng = random.Random(args.seed)
    with open(args.data) as fh:
        base = [json.loads(line)["state"] for line in fh if line.strip()]
    label = subprocess.Popen([args.label_bin, args.label_model, args.label_tok], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, text=True, bufsize=1)
    with open(args.out, "a") as out:
        for i in range(args.count):
            d = gt.parse_v5(rng.choice(base))
            for f in rng.sample(gt.V5_FIELDS, rng.randint(1, args.max_fields)):
                d[f] = reroll(f, rng)
            state = gt.render_v5(d)
            label.stdin.write(state + "\n")
            label.stdin.flush()
            w = label.stdout.readline().split()
            out.write(json.dumps({"state": state, "goal": " ".join(w[:3]), "probs": [float(x) for x in w[3:]]}) + "\n")
            if (i + 1) % 2000 == 0:
                out.flush()
                print(f"{i + 1}/{args.count}", file=sys.stderr)
    print(f"done: {args.count} states -> {args.out}")


if __name__ == "__main__":
    main()
