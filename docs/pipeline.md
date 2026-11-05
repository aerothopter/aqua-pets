# Training pipeline: from teacher to flashable binary

The distillation loop end to end. The teacher is any Ollama-served model (the shipped weights were
taught by gemma4:26b); set `--host` / `HOST` to your Ollama server (default
`http://localhost:11434`). To run a whole data cycle unattended and ship the result, follow the
runbook, [retrain.md](retrain.md).

Training needs PyTorch, so use a virtualenv:

```bash
python3 -m venv .venv && .venv/bin/pip install torch
```

`model/llama2.c/` is a clone of [karpathy/llama2.c](https://github.com/karpathy/llama2.c) (training
script and reference inference):

```bash
git clone https://github.com/karpathy/llama2.c model/llama2.c
```

## The steps

```bash
cd model/llama2.c && make run && cd ../..                # 1. native llama2.c build

python3 model/gen_traces.py --count 10 --dry-run --schema 5           # 2a. sample encoded states, no network
python3 model/gen_traces.py --schema 5 --count 3000 --max-minutes 45 --seed 1 \
    --out model/out/traces_x1.jsonl                      # 2b. generate teacher-labeled traces
                                                         #     (run several workers with distinct seeds/outs)

python3 model/fold_traces.py --schema 5                  #     dedup + vocabulary check + label distribution
python3 model/train_tokenizer.py --schema 5              # 3. word-level tokenizer, no deps

.venv/bin/python model/train.py --data model/out/clean.jsonl \
    --dim 384 --n-layers 8 --n-heads 8 --max-seq-len 64 \
    --batch 64 --iters 7000 --lr 6e-4                    # 4. train (torch)

.venv/bin/python model/llama2.c/export.py model/out/model.bin \
    --checkpoint model/out/ckpt.pt                       # 5. export fp32
python3 model/export_q4.py                               #    → 4-bit model_q4.bin

cc -O3 -o model/runw model/runw.c -lm                    # 6a. word-tokenizing runner
./model/runw model/out/model.bin -z model/out/tokenizer.bin \
    -t 0 -i "<state line> ->"                            # 6b. test a decision

python3 model/eval.py --schema 5 --count 60 --teacher    # 7. goal agreement vs the teacher
```

The prompt in step 6 is any [schema.md](../model/schema.md) state line with ` ->` appended; the model
completes it with `<goal> urgency <0-9>` and stops.

## Tools

- `model/prompt_check.py`: the teacher and prompt sanity check. **Run this before any long generation
  run**; a subtly broken prompt wastes a night.
- `model/probe_dist.py`: the student's full goal *distribution* on real and synthetic states.
- `model/eval.py`: agreement with the teacher, overall and per species.
- `model/relabel_subset.py`: relabel only the states one prompt sentence is about.
- `model/teacher_vs_student.py`: side-by-side decisions for the same states.
- `model/encoder_agree.py`: the C encoder's lines against `gen_traces.py`'s (in CI).
- `model/runq4.c`: runs the 4-bit file that ships; `firmware/host_test/q4_host.c` checks batched against
  sequential inference (in CI).

## The MLP student

A small fully-connected network that reads the v5 state line as 181 0/1 features (`model/features.py`)
instead of as tokens, and writes the goal and the urgency: ~120K parameters, ~125 KB at int8, and a
decision in microseconds instead of seconds. `common/llm/mlp_model.c` runs it, and `advisor_core_init`
picks it when the model file starts with the `AMLP` magic, so the sim and the firmware load either
engine the same way. The tokenizer still ships alongside: it says the schema and which species words
the encoder sends.

It trains from two kinds of label, kept apart because they mean different things:

- **The teacher's** (`gen_traces.py` as in step 2b): one goal per state.
- **The shipped model's** (`gen_traces.py --teacher student`): its 8 goal probabilities per state, so
  the network learns where the transformer is torn as well as what it picks. No GPU or teacher needed.

```bash
cd model
cc -O3 -I../common/llm -o label_q4 label_q4.c ../common/llm/q4_model.c ../common/llm/word_tok.c -lm
cc -O3 -I../common/llm -o runmlp runmlp.c ../common/llm/mlp_model.c -lm

python3 gen_traces.py --schema 5 --teacher student --count 20000 --seed 21 \
    --out out/mlp_student_21.jsonl                       # the shipped model's labels (several seeds)
python3 fold_traces.py --schema 5 "out/mlp_student_*.jsonl" --out out/mlp_student_clean.jsonl

.venv/bin/python train_mlp.py --data out/mlp_teacher_clean.jsonl \
    --student-data out/mlp_student_clean.jsonl           # either or both; ~minutes on a CPU
.venv/bin/python export_mlp.py --check out/mlp_student_clean.jsonl   # out/mlp.pt -> out/mlp.bin

python3 features_agree.py                                # C features == Python features
python3 probe_dist.py --schema 5 --mlp out/mlp.bin --data out/mlp_student_clean.jsonl
python3 probe_dist.py --schema 5 --q4 --data out/mlp_student_clean.jsonl            # v5j, the same panel
python3 eval.py --schema 5 --teacher --count 400 --min-per-species 30 --seed 5 \
    --run-bin ./runmlp --model-bin out/mlp.bin --tok-bin out/tokenizer.bin         # the acceptance number
./runmlp out/mlp.bin --bench                             # time per decision
```

In the sim: `AQUA_PETS_MODEL=../model/out/mlp.bin ./fishsim` (or `--selftest-llm`).
