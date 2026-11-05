#!/usr/bin/env python3
"""Check the C featurizer (common/llm/mlp_model.c, via runmlp --features) against
features.py on sampled v5 states: the vectors must be identical. Stdlib only.

  python3 features_agree.py --run-bin ./runmlp --model-bin out/mlp.bin [--count 2000]

The states come from gen_traces.py's headless tank (every species, perturbed
edge cases), the same sampler the training data comes from. Exits 1 on any
difference.
"""
import argparse
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import features as feat  # noqa: E402
import gen_traces as gt  # noqa: E402


def sample_states(n, seed):
    gt.SCHEMA = 5
    rng = random.Random(seed)
    tank, out = None, []
    for i in range(n):
        if i % 60 == 0:
            tank = gt.Tank(rng)
            for _ in range(200):
                tank.tick()
        for _ in range(10):
            tank.tick()
        f = tank.fish[i % len(tank.fish)]
        if rng.random() < 0.45:
            tank.perturb(f)
        out.append(gt.encode(tank, f))
        f.set_goal(rng.choice(gt.goals()))
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--run-bin", default=os.path.join(here, "runmlp"))
    ap.add_argument("--model-bin", default=os.path.join(here, "out", "mlp.bin"))
    ap.add_argument("--count", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    states = sample_states(args.count, args.seed)
    c_out = subprocess.run([args.run_bin, args.model_bin, "--features"], input="\n".join(states) + "\n",
                           capture_output=True, text=True, check=True).stdout.split()
    bad = 0
    for i, s in enumerate(states):
        py = "".join(map(str, feat.featurize(s)))
        c = c_out[i] if i < len(c_out) else "<missing>"
        if py != c:
            bad += 1
            if bad <= 5:
                print(f"DIFF on: {s}\n  py {py}\n  c  {c}")
    species = len({s.split(' species ')[1].split()[0] for s in states})
    print(f"features_agree: {len(states)} states, {species} species, {feat.N_FEATURES} features: "
          f"{'OK' if not bad else f'{bad} differ'}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
