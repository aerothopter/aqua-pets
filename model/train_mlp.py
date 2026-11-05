#!/usr/bin/env python3
"""Train the MLP student: a small fully-connected network from the state
line's features (features.py) to the goal and the urgency.

Two kinds of training rows, both JSONL from gen_traces.py (folded by fold_traces.py):
  --data          teacher labels: {"state", "goal"}; the goal is the target
  --student-data  the shipped model's labels: {"state", "goal", "probs"}; its
                  8 goal probabilities are the target, so the network learns
                  where the shipped model is torn as well as what it picks

  python3 train_mlp.py --student-data "out/mlp_student_clean.jsonl"           # copy v5j
  python3 train_mlp.py --data "out/mlp_teacher_clean.jsonl" \
      --student-data "out/mlp_student_clean.jsonl" --student-weight 0.5       # teacher + v5j

Each epoch prints validation agreement (the network's top goal == the label's)
overall and per species, and for student rows the total-variation distance
from the shipped model's probabilities (0 = the same distribution). The
checkpoint (--out) keeps the epoch with the lowest validation loss; export
it with export_mlp.py. Requires torch.
"""
import argparse
import glob
import json
import math
import os
import random
import sys
import time

import torch
import torch.nn as nn
import torch.nn.functional as F

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import features as feat  # noqa: E402

N_GOALS, N_URGENCY = len(feat.GOALS), 10


class MLP(nn.Module):
    def __init__(self, n_in, hidden, layers, dropout):
        super().__init__()
        dims = [n_in] + [hidden] * layers
        self.body = nn.ModuleList(nn.Linear(a, b) for a, b in zip(dims, dims[1:]))
        self.drop = nn.Dropout(dropout)
        self.goal = nn.Linear(hidden, N_GOALS)
        self.urgency = nn.Linear(hidden, N_URGENCY)

    def forward(self, x):
        for layer in self.body:
            x = self.drop(F.relu(layer(x)))
        return self.goal(x), self.urgency(x)


def load_rows(pattern, soft):
    rows = []
    for path in sorted(glob.glob(pattern)) if pattern else []:
        with open(path) as fh:
            for line in fh:
                if not line.strip():
                    continue
                o = json.loads(line)
                goal, _, urg = o["goal"].split()
                if soft:
                    target = o["probs"]
                else:
                    target = [0.0] * N_GOALS
                    target[feat.GOALS.index(goal)] = 1.0
                sp = o["state"].split(" species ", 1)[1].split()[0]
                rows.append((feat.featurize(o["state"]), target, int(urg), sp, soft))
    if pattern and not rows:
        sys.exit(f"no rows matched {pattern}")
    return rows


def tensors(rows):
    return (torch.tensor([r[0] for r in rows], dtype=torch.float32),
            torch.tensor([r[1] for r in rows], dtype=torch.float32),
            torch.tensor([r[2] for r in rows], dtype=torch.long))


def split(rows, frac, rng):
    rng.shuffle(rows)
    n = max(1, int(len(rows) * frac)) if rows else 0
    return rows[n:], rows[:n]


@torch.no_grad()
def evaluate(model, rows, label):
    """agreement (top goal == the label's top goal), overall and per species;
    total variation from the target distribution; urgency within 1"""
    if not rows:
        return None
    model.eval()
    x, t, u = tensors(rows)
    g, ul = model(x)
    p = F.softmax(g, -1)
    loss = -(t * F.log_softmax(g, -1)).sum(-1).mean().item()
    hit = (p.argmax(-1) == t.argmax(-1))
    tv = 0.5 * (p - t).abs().sum(-1)
    urg1 = ((ul.argmax(-1) - u).abs() <= 1).float().mean().item()
    per = {}
    for i, r in enumerate(rows):
        per.setdefault(r[3], [0, 0])
        per[r[3]][0] += 1
        per[r[3]][1] += int(hit[i])
    model.train()
    return dict(label=label, n=len(rows), loss=loss, agree=hit.float().mean().item(),
                tv=tv.mean().item(), urg1=urg1, per=per)


def show(m, per_species):
    s = (f"  {m['label']:7s} n {m['n']:6d}  loss {m['loss']:.4f}  agree {100 * m['agree']:5.1f}%  "
         f"TV {m['tv']:.3f}  urgency+-1 {100 * m['urg1']:5.1f}%")
    if per_species:
        s += "\n          " + "  ".join(f"{sp} {100 * a / n:.0f}%" for sp, (n, a) in sorted(
            m["per"].items(), key=lambda kv: feat.SPECIES_SLOTS.index(kv[0])))
    print(s)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", default=None, help="teacher-labelled jsonl (glob)")
    ap.add_argument("--student-data", default=None, help="shipped-model-labelled jsonl with probs (glob)")
    ap.add_argument("--student-weight", type=float, default=0.5, help="a student row's weight in the loss vs a teacher row's 1")
    ap.add_argument("--out", default=os.path.join(here, "out", "mlp.pt"))
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--layers", type=int, default=2)
    ap.add_argument("--dropout", type=float, default=0.1)
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--batch", type=int, default=256)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--weight-decay", type=float, default=1e-4)
    ap.add_argument("--urgency-weight", type=float, default=0.3, help="the urgency loss's weight vs the goal's 1")
    ap.add_argument("--val-frac", type=float, default=0.05)
    ap.add_argument("--limit", type=int, default=0, help="use only the first N teacher rows (a data-size sweep)")
    ap.add_argument("--seed", type=int, default=1337)
    args = ap.parse_args()
    if not args.data and not args.student_data:
        sys.exit("give --data and/or --student-data")

    rng = random.Random(args.seed)
    torch.manual_seed(args.seed)
    teacher = load_rows(args.data, soft=False)
    if args.limit:
        rng.shuffle(teacher)
        teacher = teacher[:args.limit]
    student = load_rows(args.student_data, soft=True)
    t_train, t_val = split(teacher, args.val_frac, rng)
    s_train, s_val = split(student, args.val_frac, rng)
    train = t_train + s_train
    x, t, u = tensors(train)
    w = torch.tensor([args.student_weight if r[4] else 1.0 for r in train])
    print(f"rows: teacher {len(t_train)} train / {len(t_val)} val, student {len(s_train)} train / {len(s_val)} val")

    model = MLP(feat.N_FEATURES, args.hidden, args.layers, args.dropout)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"model: {feat.N_FEATURES} -> {' -> '.join([str(args.hidden)] * args.layers)} -> {N_GOALS} + {N_URGENCY}, "
          f"{n_params:,} parameters")
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    steps = args.epochs * math.ceil(len(train) / args.batch)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=args.lr, total_steps=steps, pct_start=0.05)

    best = float("inf")
    t0 = time.time()
    for epoch in range(1, args.epochs + 1):
        perm = torch.randperm(len(train))
        total = 0.0
        for i in range(0, len(train), args.batch):
            b = perm[i:i + args.batch]
            g, ul = model(x[b])
            goal_loss = -(t[b] * F.log_softmax(g, -1)).sum(-1)
            urg_loss = F.cross_entropy(ul, u[b], reduction="none")
            loss = ((goal_loss + args.urgency_weight * urg_loss) * w[b]).sum() / w[b].sum()
            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()
            sched.step()
            total += loss.item() * len(b)
        vals = [m for m in (evaluate(model, t_val, "teacher"), evaluate(model, s_val, "student")) if m]
        val_loss = sum(m["loss"] * m["n"] for m in vals) / sum(m["n"] for m in vals)
        mark = ""
        if val_loss < best:
            best = val_loss
            torch.save({"args": vars(args), "model": model.state_dict(), "n_features": feat.N_FEATURES,
                        "species_slots": feat.SPECIES_SLOTS, "epoch": epoch,
                        "val": [{k: v for k, v in m.items()} for m in vals]}, args.out)
            mark = f"  -> saved {os.path.basename(args.out)}"
        print(f"epoch {epoch:3d}/{args.epochs}  train loss {total / len(train):.4f}  val loss {val_loss:.4f}"
              f"  ({time.time() - t0:.0f}s){mark}")
        last = epoch == args.epochs
        for m in vals:
            show(m, per_species=last or epoch % 10 == 0)

    ck = torch.load(args.out, weights_only=False)
    print(f"\ndone: best val loss {best:.4f} at epoch {ck['epoch']} -> {args.out}")
    for m in ck["val"]:
        show(m, per_species=True)


if __name__ == "__main__":
    main()
