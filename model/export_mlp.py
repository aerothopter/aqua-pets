#!/usr/bin/env python3
"""Export a train_mlp.py checkpoint to the tank's MLP file (common/llm/mlp_model.h).

  python3 export_mlp.py --ckpt out/mlp.pt --out out/mlp.bin --check "out/mlp_student_clean.jsonl"

Layout, little-endian:
  header  uint32 magic "AMLP", int32 version 1, n_in, hidden, layers, 8 goals, 10 urgencies,
          16 species slot names of 12 bytes (NUL-padded)
  layers  the hidden layers, then the goal head, then the urgency head; each is
          fp32 scale[rows], fp32 bias[rows], int8 weight[rows x cols] padded to 4 bytes
A row's weights are symmetric int8: w ~= scale * q, scale = max|w| / 127.

--check compares the int8 network with the fp32 one on up to 2,000 states of the
given jsonl: the share with the same top goal, and the mean total variation.
"""
import argparse
import glob
import json
import os
import struct
import sys

import torch
import torch.nn.functional as F

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import features as feat  # noqa: E402
from train_mlp import MLP  # noqa: E402

MAGIC, VERSION = 0x504C4D41, 1


def quantize(w):
    scale = w.abs().amax(dim=1).clamp(min=1e-12) / 127.0
    q = torch.round(w / scale[:, None]).clamp(-127, 127).to(torch.int8)
    return q, scale


def load(ckpt_path):
    ck = torch.load(ckpt_path, weights_only=False)
    a = ck["args"]
    model = MLP(ck["n_features"], a["hidden"], a["layers"], 0.0)
    model.load_state_dict(ck["model"])
    model.eval()
    return model, ck


def layers_of(model):
    return list(model.body) + [model.goal, model.urgency]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ckpt", default=os.path.join(here, "out", "mlp.pt"))
    ap.add_argument("--out", default=os.path.join(here, "out", "mlp.bin"))
    ap.add_argument("--check", default=None, help="jsonl (glob) of states to compare int8 with fp32 on")
    args = ap.parse_args()

    model, ck = load(args.ckpt)
    a = ck["args"]
    slots = ck["species_slots"]
    blob = struct.pack("<I6i", MAGIC, VERSION, ck["n_features"], a["hidden"], a["layers"], 8, 10)
    for s in slots:
        blob += s.encode().ljust(12, b"\0")[:12]
    quant = []
    for layer in layers_of(model):
        q, scale = quantize(layer.weight.detach())
        quant.append((q, scale))
        qb = q.contiguous().numpy().tobytes()          # row-major, as mlp_model.c reads it
        blob += struct.pack(f"<{len(scale)}f", *scale.tolist())
        blob += struct.pack(f"<{len(scale)}f", *layer.bias.detach().tolist())
        blob += qb + b"\0" * (-len(qb) % 4)
    with open(args.out, "wb") as fh:
        fh.write(blob)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"{args.out}: {len(blob):,} bytes ({n_params:,} parameters, {ck['n_features']} -> "
          f"{' -> '.join([str(a['hidden'])] * a['layers'])} -> 8 + 10)")

    if args.check:
        states = []
        for path in sorted(glob.glob(args.check)):
            with open(path) as fh:
                states += [json.loads(line)["state"] for line in fh if line.strip()]
        states = states[:2000]
        x = torch.tensor([feat.featurize(s) for s in states], dtype=torch.float32)
        with torch.no_grad():
            p32 = F.softmax(model(x)[0], -1)
            h = x
            for (q, scale), layer in zip(quant[:-2], layers_of(model)[:-2]):
                h = F.relu((h @ q.float().T) * scale + layer.bias)
            q, scale = quant[-2]
            p8 = F.softmax((h @ q.float().T) * scale + model.goal.bias, -1)
        same = (p32.argmax(-1) == p8.argmax(-1)).float().mean().item()
        tv = (0.5 * (p32 - p8).abs().sum(-1)).mean().item()
        print(f"int8 vs fp32 on {len(states)} states: same goal {100 * same:.1f}%, mean TV {tv:.4f}")


if __name__ == "__main__":
    main()
