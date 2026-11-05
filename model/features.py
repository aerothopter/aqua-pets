#!/usr/bin/env python3
"""The MLP student's input: a v5 state line (schema.md) as a fixed vector of
0/1 features. Stdlib only.

  zone      one-hot, 6
  hunger energy stress curiosity bold social trust bored
            a thermometer each: bit k-1 is set when the value is >= k (k = 1..9),
            so neighbouring values share most bits; 8 x 9
  stage     one-hot, 4
  species   one-hot over SPECIES_SLOTS, 16 (11 used; a new species takes a free
            slot without changing the network's input size)
  food friend bubble reef
            closeness as a thermometer (far 100, mid 110, near 111, none 000)
            + the clock one-hot (12, all clear for none); 4 x 15
  wall      closeness (mid 10, near 11, clear 00) + the clock, 14
  last      one-hot over GOALS, 8
  time      night, 1

N_FEATURES = 181. common/llm/mlp_model.c's mlp_featurize writes the same
vector; features_agree.py checks the two against each other.

  python3 features.py --dump < states.txt     # one line of 0/1 digits per state
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_traces as gt  # noqa: E402

DIGIT_FIELDS = ["hunger", "energy", "stress", "curiosity", "bold", "social", "trust", "bored"]
STAGES = ["fry", "juv", "adult", "elder"]
SPECIES_SLOTS = (gt.SPECIES_TOKENS + [""] * 16)[:16]   # "" = a free slot
SIGHTINGS = ["food", "friend", "bubble", "reef"]
CLOSE = {"far": 1, "mid": 2, "near": 3}
GOALS = gt.GOALS
N_FEATURES = 6 + 9 * len(DIGIT_FIELDS) + 4 + 16 + 15 * len(SIGHTINGS) + 14 + 8 + 1


def _sighting(value, n_close):
    """closeness thermometer (n_close bits) + clock one-hot (12)"""
    v = [0] * (n_close + 12)
    if value in ("none", "clear"):
        return v
    dist, hour = value.split()
    for k in range(CLOSE[dist] - (3 - n_close)):
        v[k] = 1
    v[n_close + int(hour) - 1] = 1
    return v


def featurize(line):
    d = gt.parse_v5(line)
    x = [0] * 6
    x[int(d["zone"]) - 1] = 1
    for k in DIGIT_FIELDS:
        n = int(d[k])
        x += [1 if n >= i else 0 for i in range(1, 10)]
    x += [1 if d["stage"] == s else 0 for s in STAGES]
    x += [1 if d["species"] == s else 0 for s in SPECIES_SLOTS]
    for k in SIGHTINGS:
        x += _sighting(d[k], 3)
    x += _sighting(d["wall"], 2)
    x += [1 if d["last"] == g else 0 for g in GOALS]
    x += [1 if d["time"] == "night" else 0]
    assert len(x) == N_FEATURES
    return x


def main():
    if "--dump" not in sys.argv:
        sys.exit(__doc__)
    for line in sys.stdin:
        line = line.strip()
        if line:
            print("".join(map(str, featurize(line))))


if __name__ == "__main__":
    main()
