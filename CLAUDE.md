# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Aqua Pets is a virtual aquarium in which a 14M-parameter 4-bit transformer, running on an ESP32-S3, picks each fish's goal. The same C code in `common/` is compiled unchanged into a PC simulator (`sim/`, LVGL v9 + SDL2) and the ESP-IDF firmware (`firmware/`). The project was called pocket-tank until 0.4. The save format and NVS keys still use the old layout on purpose.

## Commands

All simulator work runs from `sim/`. One-time setup: `git clone --depth 1 --branch v9.2.2 https://github.com/lvgl/lvgl.git sim/lvgl` plus SDL2.

- **Windows:** build in the MSYS2 **UCRT64** shell, not PowerShell. `make check` also needs `C:/tmp /tmp ntfs binary,posix=0,noacl 0 0` in `C:\msys64\etc\fstab`, because the selftests save to `/tmp` and restore with `cp`. `sim/win_compat.h` is force-included on Windows to supply missing POSIX calls.
- **macOS:** the Makefile builds x86_64 by default. Use `make ARCH=` for a native build.

```bash
make                 # ./fishsim (the 1.8" rectangle, the one board for now)
make check           # every selftest
make check-all       # every selftest on every board (one for now; the required gate for any change to common/)
make NOMODEL=1 check-all   # skip --selftest-llm when model/out/model_q4.bin is absent
```

**Run a single test:** run one selftest flag on the built binary, e.g. `./fishsim --selftest-shop`. The full list is `SELFTESTS` in [sim/Makefile](sim/Makefile): `--selftest-themes`, `--selftest`, `--selftest-hunger`, `--selftest-shop`, `--selftest-battery`, `--selftest-pop`, `--selftest-card`, `--selftest-menu`, `--selftest-sleep`, `--selftest-tend`, `--selftest-llm`, `--selftest-saves`, `--selftest-update`, `--selftest-species`, `--selftest-encoder`, `--selftest-decor`, `--selftest-helpers`, `--selftest-creatures`.

`--golden-decor [prefix]` (not a selftest) prints a hash per frame for a fixed tank with every decor piece and helper, every depth mix, each helper's poses and cards, every theme (one for now), cache on and off, plus a save. Run it on two builds to prove a refactor changed no pixel. `--golden-creatures [prefix]` does the same for the creatures: every kind (the classic fish too) in every design, stage, pose and theme, the pages' previews, then a run of all thirteen kinds with a hash of what the model hears, a night asleep and a save.

Useful sim flags: `--fresh`, `--fast N`, `--greedy`, `--narrate` (prints every model decision), `--snapshot <prefix>` (writes PPM frames of every page), `--bench`.

Other checks that CI runs ([.github/workflows/checks.yml](.github/workflows/checks.yml)):
- Encoder agreement: `python3 model/train_tokenizer.py --schema 5 --out tok5.bin && cd sim && ./fishsim --selftest-encoder tok5.bin | python3 ../model/encoder_agree.py`
- Batched vs sequential inference: `firmware/host_test/q4_host.c` against `model/out/model_q4.bin`
- `python3 -m compileall -q tools model`

Firmware (ESP-IDF v5.4): `cd firmware && idf.py build` builds the 1.8. `firmware/run_qemu.sh` boots the app in QEMU. The first build needs a signing key from `tools/ota_key.sh`, written to `firmware/keys/`, which is never committed.

## Architecture

There are three strictly separated layers, all in `common/`:

- **Reflex layer** (`tank.c`/`tank.h`, with `creature.c`, `decor.c`, `helper.c`, and `glass.c` for the glass's shape): physics, steering, touch gestures, feeding, hunger, grass and algae. It runs every frame and never blocks on the model. It calls a plugged-in `advisor_fn` on a slow cadence. `tank_internal.h` holds what tank.c and creature.c share (not for the platforms).
- **Advisor** (`advisor.c`, `llm/`): `advisor_rules` is a non-LLM stub (sim key **L** toggles it). The LLM advisor encodes a fish's situation as one text line ([model/schema.md](model/schema.md)), runs the 4-bit llama2.c-style engine (`llm/q4_model.c`, `llm/word_tok.c`), and parses back a goal and an urgency. `llm/advisor_core.c` holds the shared encoder. Each platform supplies its own thread wrapper: `sim/advisor_llm.c` and `firmware/main/advisor_llm_esp.c` (second core, weights mmapped from flash). A fish is re-asked only when its `state_signature` bands change.
- **Progression** (`progression.c`): growth, arrivals and breeding, trust, milestones, sand dollars, the shop, and persistence.

Tank entities come in three families (a refactor in phases since 2026-10-21; the plan and what each phase became are in docs/refactor-plan.md):
- **Decor** (`decor.c`/`decor.h`): static pieces, one row each in `DECOR[]` (where it stands, the depths it offers, its den/camouflage/ground/reef parts, buy/sell/grow hooks), instances in `tank_t.decor[]`. render.c draws them through `DECOR_DRAW[]` and the `PAINT_*` orders; each piece's art is in `decor_<name>.inc`. A depth is BACK, FRONT or MIDDLE, and MIDDLE is a split: half the piece behind the creatures and half in front, each piece defining its halves. A new piece is a row, its draw hooks and its art.
- **Helpers** (`helper.c`/`helper.h`): the snail, urchin, shrimp, frogman and sub move by their own rules, are bought (and sold back from the shop's page), and are never bred or placed. One row each in `HELPER[]` (its rules' hooks: arrive, tick, sleep, startle, tap, save/load), its state in `tank_t` (`t->snail`, `t->urchin`, `t->school`, `t->frogman`, `t->sub`). The table's order is the RNG's order, so a new helper goes at the end. render.c paints them through `HELPER_DRAW[]` in four slots (under the creatures, over them, over the decor IN FRONT, on the glass); each one's art is in `helper_<name>.inc`. A tap reaches them through `render_helper_tap`.
- **Creatures** (`creature.c`/`creature.h`): what the model decides for, `t->fish[]` (also the encoder's `friend` set). Helpers must never join it. One row each in `SPECIES[]`, the classic fish included (`SP_FISH`): what it is, how it moves (`mo`, the `SPF_*` flags) and its hooks (`state`, `startle`, `flee`, `target`, `move`, `dance`, `swim`, ...); a new species is a row and its hooks. creature.c also makes, breeds and removes them and runs each one's frame (`update_fish`). Every creature's art, the classic fish and every species, is in `creatures.inc`, which render.c includes. In code they keep the fish's names (`fish_t`, `t->fish[]`); docs call them creatures.

Rendering is a software RGB565 renderer in `render.c`, which draws the tank, the cards, milestones, the shop, settings, and the pixel font. `theme.c` holds the themes (one for now, the Original: the other three are at git tag `multi-board-multi-theme`, docs/THEMES.md). `setup.c` handles first run and the birth flow, `update.c` the OTA pages, `notice.c` the announcements, and `audio.c`/`sounds.c` the mixer and the cue bank.

Platform-specific code is limited to the **ports**: `sim/*_port_sim.c` and `firmware/main/*_port_*.c` (display, touch, IMU, battery, audio, net, persistence).

**The model owns its decisions.** Never add fallback heuristics that override the goal the model picked. Show uncertainty as visible hesitation. The reflex layer only performs the chosen goal, or stages presentations the model's state has earned.

**Encoder and training must agree.** The C encoder has to write exactly what `model/gen_traces.py` trains on, and `--selftest-encoder` plus `encoder_agree.py` enforce this. A schema change means a retrain (see `docs/retrain-v*.md`). The firmware's `tokenizer.bin` and the model partition must be flashed together.

## Rules for changes (from docs/BOARDS.md; read it before a feature)

- **Write a feature once, in `common/`.** Pages return what was tapped (`MS_TAP_*`, `SHOP_TAP_*`, ...). The routing is duplicated in `sim/main.c` and in the firmware's `touch_port_ft3168.c` + `main.c`, so keep it to one or two calls. Sounds go through `tank_emit`, not the platform.
- **One board for now (since 2026-10-26).** The round 1.75C and the 2.06 watch were taken out while features are built; everything they needed is at git tag `multi-board-multi-theme` (docs/BOARDS.md says what and how to bring one back). Keep the seams below intact: write features against the device's names, never against the 1.8's pixels.
- **A board is a device header.** `common/device.h` picks one of `common/devices/<board>.h` (only `amoled18.h` for now) (the frame, the glass's shape, the floor, the film grid, the grass beds, `PT_BOARD`, the flag `DEVICE_WORN`), and that header names its layout header, `devices/<board>_layout.h` (`DEVICE_LAYOUT`): every page number that differs between boards. render.h and update.h hold the shared ones. **Board `#ifdef`s (`TANK_ROUND`, `TANK_WATCH`) belong only in** `device.h`'s selector and the ports. Every glass shape's code is in `glass.c` (by `DEVICE_GLASS`). Feature code asks the header's names and flags (`GLASS_CURVED` where it only needs to know the glass curves). If feature logic needs a board name, it was written against a pixel instead of a name.
- **There are two coordinate spaces.** Fish live in TANK coordinates (`TANK_W`/`TANK_H`/`TANK_BOT`/`TANK_FX0`/`TANK_FX1`). Pages draw in a 448×368 PAGE space centered at `PAGE_X`/`PAGE_Y`. Anything crossing between them must subtract `PAGE_X`/`PAGE_Y`. New page elements go inside PAGE x 56..392, y 60..300, the area every board shows whole.
- **Tests ask the layout where things are** (`render_milestones_card`, the `MSP_*`/`SHP_*`/`SET_*`/`UPD_*` constants). A literal pixel in a test only tests one board.
- **The save layout is locked** (`SAVE LAYOUT LOCK` in [common/progression.c](common/progression.c)). The save is layout 3 ("PTK3", since 2026-10-21). New fields only go at the end of `save_t`, and the `_Static_assert` offsets must hold. Decor and helpers are records keyed by kind, so a new kind needs no new field. Every layout 3 save must still load. PTK2 saves (before 2026-10-21) start a fresh tank: the keeper chose that break for the decor refactor. `sim/testdata/saves/` holds both kinds for `--selftest-saves`.
- **A change is not done until every board passes** (one for now): `make check-all`, the picture (`tools/board_sheet.py --selftest-card`), and the firmware build (`tools/flash.sh`). Watch `.bss`: internal RAM has only ~23 KB headroom.

## Generated files: do not hand-edit

`common/icons.c`/`icons.h` (`tools/gen_icons.py` from `assets/icons/*.png`), `common/sounds.c`/`sounds.h` (`tools/make_sounds.py`), and `common/{frogman,sub,wreck,jellyfish}_assets.c` (the matching `tools/gen_*_assets.py`; `gen_theme_assets.py`'s output is out of the build for now). Change the source or the script and regenerate.

## Handoff log (docs/HANDOFF.md)

- **At the start of a session,** read the newest entries in `docs/HANDOFF.md` before starting work. Create the file if it doesn't exist.
- **When you finish a piece of work** (a feature, fix, investigation or measurement), add a dated entry at the top. Cover what was done, where each board stands (checked / pictured / built / flashed / tried on the glass; "not flashed" is fine, "unknown" is not), what's still open, and why key decisions were made.
- HANDOFF.md is the narrative. Current state still belongs in the doc that owns it: DEVICE.md's in-flight table, measured numbers in docs/stats.md.

## Device safety (docs/DEVICE.md)

- Flash a real board **only** through `tools/flash.sh` (`--model` to include the model partition). It runs `tools/preflight.py`, which archives the battery log and state to `docs/batlog/<date_time>.txt`. Commit that archive with the flash.
- Never run raw `esptool`/`idf.py flash` or reset a connected tank without the preflight. Check the "In flight on the device" table in DEVICE.md first.

## Repo notes

- This is a fork. "Strato" in the docs and code comments is the original owner (@mediacutlet). Their HANDOFF.md was never committed, so any older doc that cites a "HANDOFF <date>" entry refers to notes that aren't available here.
- `sim/lvgl/` and `model/llama2.c/` are separate clones and are not tracked.
- The dev repo may not track `model/out/`. The public repo ships `model_q4.bin` and `tokenizer.bin`.
- Releases and installer publishing only run from `fiatminimalist/aqua-pets` ([.github/workflows/](.github/workflows/)). Each board gets its own signed image, identified by `PT_BOARD` in `common/version.h`.
