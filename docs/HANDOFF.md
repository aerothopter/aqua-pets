# Handoff log

Newest first. What was done, where each board stands, what's open, and why.

## 2026-10-23: one glass module (refactor phase 4b)

**What changed.** A pure move: `common/glass.h` / `glass.c` hold every piece
of code whose body depends on the glass's shape, one body a shape by
`DEVICE_GLASS`:
- from tank.c: `tank_glass_*`, `rand_water`, and the film's cells
  (`glass_holds_cell`, was `algae_ok`)
- from creature.c: `glass_bounce` (the swim's hard bounds and reflection),
  `glass_clamp_target`, and `glass_avoidance` (was `wall_avoidance`)

`corner_in` and `corner_clamp` are glass.c's own now. Code that needs only to
know that the glass curves asks `GLASS_CURVED` (device.h): an explore point,
the shrimp, the snail's stand, and the sim's window mask (one loop over
`tank_glass_x0` / `x1` for the bowl and the watch, the same maths as the
bowl's own loop). tank.c's last board blocks are the beds (4c).

**How it was checked.** All nine records are identical to 04b15e5's (both
oracles and the snapshot pages, on all three boards; see 4a for the build-id
pin).

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | oracles and snapshot pages identical | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | identical | yes | no | no |
| 2.06 watch | passes | identical | yes | no | no |

Firmware against 4a: `.dram0.bss`, `.dram0.data` and `.flash.rodata`
unchanged; `.flash.text` -72 B (1.8), -460 B (bowl), -236 B (watch); the app
still 0x271000; the same warnings. The functions left their callers' files,
so on the device the compiler inlines (and, with `-ffast-math`, may contract)
differently. As with 3b+, the sim's oracles can't prove the device's floats
bit for bit.

**Next:** 4c, the bed table.

## 2026-10-23: a board is a device header (refactor phase 4a)

**The keeper's calls for phase 4** (refactor-plan.md has them):
- every step is pure: nothing changes on any board
- no pages oracle: the `--snapshot` frames, hashed, stand in
- the `-DTANK_W=480` square-panel override goes

Phase 4 is on `refactor/devices`, from `refactor/creatures`.

**What changed.** A pure refactor:
- `common/device.h` picks one of `common/devices/amoled18.h`, `round175c.h`
  or `watch206.h` by `TANK_ROUND` / `TANK_WATCH` (both at once is an
  `#error`). A header holds the board's:
  - frame, floor and glass (`DEVICE_GLASS`: `GLASS_RECT` / `GLASS_BOWL` /
    `GLASS_CORNERS`, with `TANK_RAD` or `TANK_CORNER_R`)
  - film grid, `VEG_SEG_PX`, `FEED_ZONE_Y`, `DECOR_MARGIN` and
    `PLANT_X_DEFAULT`
  - `PT_BOARD` / `PT_BOARD_NAME` (version.h includes device.h for them)
  - `DEVICE_WORN` (was `TANK_WORN`) and `DEVICE_PAGE_BOWL` (was render.h's
    `PAGE_BOWL`)
- tank.h, version.h and render.h lost their board blocks, and the
  firmware's orientation (main.c) now asks `DEVICE_WORN`.
- The plan's `DEVICE_CAN_TURN` was not added: it would be `!DEVICE_WORN` on
  every board. Its "feature branches" step folded in here.
- CLAUDE.md, BOARDS.md, tools/pt_boards.py and release.yml point at the
  device headers.

**How it was checked.** Three records, each on all three boards:
- `--golden-decor`
- `--golden-creatures`
- md5 of every `--snapshot` frame (118 a board)

The baseline was recorded from 04b15e5 (the edits stashed). Every build had
`GIT_DIR` pointing nowhere and `persist_port_sim.o` rebuilt, so the settings
and update pages all print the build id "sim". Before that, those four pages
differed only by the id: a stale object had kept `26541d3-dirty`. All nine
records are identical.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | oracles and snapshot pages identical | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | identical | yes | no | no |
| 2.06 watch | passes | identical | yes | no | no |

Firmware against cd65114: every section byte for byte the same on each board
(watch `.dram0.bss` 60,168; the app 0x271000), and the same warnings.

**Next:** 4b, one glass module (refactor-plan.md).

## 2026-10-23: where things stand (end of session; phase 4 next)

**The branches.** Nothing is merged or pushed. They stack:
- `refactor/decor` (phase 1)
- `refactor/helpers`, on top of it (phase 2, and the plan's move into the repo)
- `refactor/creatures`, on top of that (phase 3: 11 commits, the 3.0 oracle
  through the draw table)

`main` is still at 5dcbee3. Phase 4 can go on a new `refactor/devices` from
`refactor/creatures`.

**The plan** is `docs/refactor-plan.md`. Phases 1-3 are written as built, and
phase 4 (device headers, one glass module, per-device layout sections) as
first planned. Its line numbers have drifted, so look the places up again.

**How each step was checked** (so phase 4 can do the same):
- Record both oracles on the commit before the change, in a sandboxed HOME
  (memory: the sim's real save). Then compare after:
  - `./fishsim --golden-decor` (363 lines a board)
  - `./fishsim --golden-creatures` (309)
  - and both again on `fishsim-round` / `fishsim-watch`
- A pure step matches line for line. Phase 4 touches geometry, so expect it
  to be the first step whose oracle *should* move on some boards. Explain
  every line that does.
- `make check-all`: 18 selftests a board, 54 runs, all passing at cd65114.
- Firmware: ESP-IDF 5.4.4 at `C:\esp\v5.4.4` (profile
  `C:\Espressif\tools\Microsoft.v5.4.4.PowerShell_profile.ps1`). Each board is
  built with `idf.py -B <dir> -DSDKCONFIG=<dir>\sdkconfig
  "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults[;sdkconfig.round|watch];<throwaway
  key overlay>" build`. The overlay sets `CONFIG_SECURE_BOOT_SIGNING_KEY` to a
  key made with `espsecure.py generate_signing_key --version 2 --scheme
  rsa3072`, kept outside the repo. Such images must never be flashed. At
  cd65114 every board builds: the app is 0x271000 of 0x280000 (2% free),
  watch `.dram0.bss` is 60,168, and there are 3 deprecation warnings.
- Pictures: no Python 3 on this machine, so `tools/board_sheet.py` has not
  run. `--snapshot` PPMs, joined side by side, have stood in for it.

**Open, gathered from the entries below:**
- Not flashed, on any board, since before phase 1. The first real flash
  (`tools/flash.sh`) is the layout 3 save test: the board's tank starts
  fresh, then a reboot should restore it. Check the watch's internal heap at
  boot (2026-10-22 entries). The phase 3b+ entry notes that the device's
  float contraction can't be proven by the sim's oracles.
- The app slot is 98% full (2026-10-22 measurement: sdkconfig trims free 127
  KB, tankcore at -Os 128 KB more). Phase 4 should not grow it.
- The keeper is drawing the menu and trait icons. `assets/icons/*.png` show
  as modified in the tree: his work in progress, never staged by these
  commits. When done, `tools/gen_icons.py` regenerates `common/icons.c`.
- The three themes' looks functions are the only species switches left in
  the drawing. A new species is a SPECIES[] row and its hooks, a
  CREATURE_DRAW[] row, and a case in each of those three.
- `docs/media/sim-stats-card.png` still shows the card's old MORE button
  (2026-10-21).

## 2026-10-23: draw_creature is a table (refactor phase 3, the last piece)

**What changed.** A pure refactor: both oracles are identical on all three
boards.
- `draw_creature`'s switch became `CREATURE_DRAW[SP_COUNT]` (creatures.inc).
  Each species' row holds:
  - its drawer
  - `sk_setup`'s body span (`l0`, `l1`) and lean (`pmax`)
  - a lean level shared between rows: none, with its pace (/25), a slower
    one's (/20), full, with its jet, or only in its tail-flip
  - `posed`: whether its look eases into a jet first (`sk_pose`, the octopus
    and the squid; called before `sk_setup`, as it was, since it keeps state
    per slot)
- A new species' art is now its drawer, a row here, and a case in each
  theme's looks function. Those three switches (`draw_lagoon_species`,
  `draw_theme_species`, `draw_blackwater_species`) are the only species
  switches left in the drawing.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | both oracles identical | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | both oracles identical | yes | no | no |
| 2.06 watch | passes | both oracles identical | yes | no | no |

Firmware against 3c: `.dram0.bss` and `.dram0.data` unchanged, `.flash.text`
-1.5 KB, `.flash.rodata` +~300 B (the table), the signed app still 0x271000,
the same three deprecation warnings.

**Next:** phase 4, the device architecture (refactor-plan.md).

## 2026-10-23: --selftest-creatures (refactor phase 3d)

**What changed.** `--selftest-creatures` (new, in `SELFTESTS`) loops over
`SPECIES[]`, the classic fish included, on each board. For every kind:
- the row is whole: a name, a token, names for its young, a hit radius, a
  body length, speeds, a size
- a pair arrives as its kind, in a design it has, named
- it draws in every theme (the frame is not the empty tank's)
- staged at a grass frond, a triple tap beside it changes it exactly when
  its row has a `startle` (stress and trust aside: every creature near a
  startle frets), and `tank_poke` changes it exactly when it has a `poke`
- it stays inside the glass every second through twenty minutes of the
  tank (pellets, a startle, a shake) and after a night asleep
- the save keeps every creature's kind and design
- eight fry of the pair are its kind; a classic fish's may hatch as another,
  by its row's `mutate_p`

It passes on all three boards: 13 kinds, 8 with a startle, 3 with a poke,
104 fry. It fails, with the right message, on a startle that does nothing,
a poke that does nothing, a classic fish allowed 60 px past the glass, and
fry of the wrong kind. A new species is tested with no new code. Sim only;
`common/` is unchanged.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | - (sim only) | as at 3c (`common/` unchanged) | no | no |
| 1.75C round | passes | - | as at 3c | no | no |
| 2.06 watch | passes | - | as at 3c | no | no |

**Phase 3 is done.** Every creature, the classic fish included, is a row of
`SPECIES[]` with its hooks (creature.c / creature.h), its art is in
`creatures.inc`, and both oracles have stayed identical through every step.

**Open.**
- Drawing still picks a species by switch (`draw_creature`, and each theme's
  looks function). A render-side table for `draw_creature` would make a new
  species' art one row too; it is small and pure if wanted.
- Phase 4, the device architecture (refactor-plan.md), is next.
- Not flashed. The first real flash is still the layout 3 save test
  (DEVICE.md).

## 2026-10-23: every creature's art is one file (refactor phase 3c)

**What changed.** A pure move, in five commits:
1. render.c's creature drawers move to `common/creatures.inc`, included
   where they stood: the fish's turn and body (`draw_fish_core`), the
   species' sketch kit (`sk_*`), every species' Original drawer,
   `draw_creature` and `draw_fish`.
2. `living_lagoon.inc` (the Quiet Lagoon looks) folds into it where it was
   included.
3. `theme_creatures.inc` (the Tidepool Club looks and the themes' dispatch)
   folds in.
4. `blackwater.inc` (the Blackwater looks, the classic fish's too) folds in.
5. `jellyfish.inc` folds in.

Each step expands, includes and all, to the same text as the one before,
apart from the new file's header and four comments that named a file now gone.
`creatures.inc` is 2,346 lines and includes nothing. It keeps the old order:
the fish, the kit, the themes' looks, each species' Original look, the
dispatch. docs/THEMES.md and CLAUDE.md point at it.

Both oracles are identical on all three boards. In the firmware every
section is the same size, byte for byte, as 3b+.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (17 selftests) | both oracles identical | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | both oracles identical | yes | no | no |
| 2.06 watch | passes | both oracles identical | yes | no | no |

**Open.**
- Drawing is still a switch: `draw_creature` picks a species' drawer and its
  `sk_setup` numbers, and each theme's look (`draw_lagoon_species`,
  `draw_theme_species`, `draw_blackwater_species`) has its own switch. A new
  species is a row and its hooks in creature.c, plus a case in each of these.
  Turning `draw_creature` into a render-side table (as `HELPER_DRAW[]`) is
  small; the themes' switches are each one function of looks and read fine
  as they are.
- 3d, `--selftest-creatures`, is next in the plan.

## 2026-10-23: the creatures' code moves to creature.c (refactor phase 3b+)

**What changed.** A pure move: `--golden-creatures` and `--golden-decor` are
identical to before on all three boards.
- **`common/creature.c`:** six ranges of tank.c, moved verbatim (1,623
  lines, the same count tank.c lost). That covers the roster and the
  keeper's look palettes, naming, looks and making a fish, the species
  functions and `SPECIES[]`, the population (place, add, breed, remove),
  the fish's courtship steer, and the motion: the species' hooks,
  `fish_swim`, `target_for_goal`, the walls, eating, the toss and
  `update_fish`.
- **`common/creature.h`:** the species types (`species_t`, `loco_t`,
  `sp_motion_t`, `SPF_*`, `target_t`, `species_def_t`), moved from tank.h,
  which includes it where they stood. The `tank_*` creature API stays
  declared in tank.h, as the `tank_decor_*` wrappers did.
- **`common/tank_internal.h`:** what tank.c and creature.c share. That's the
  small maths (`clampf`, `norm_ang`, `lerpf`, now `static inline` with the
  same bodies), the RNG core (`xr`, renamed `tank_xr`), `rand_water`, the
  watch's corners, the grass and courtship calls, eleven constants (startle,
  hold, hunger, curiosity, the canopy's slow), and the two calls back:
  `update_fish` and `sp_startle_hit`. It is not for the platforms.
- tank.c keeps the tank around the creatures: the glass, the light, food,
  bubbles, grass, film, touch, the shake, the sleep and the tick (1,186 lines,
  down from 2,809).
- CLAUDE.md's architecture names the new files.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (17 selftests) | both oracles identical | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | both oracles identical | yes | no | no |
| 2.06 watch | passes | both oracles identical | yes | no | no |

Firmware against 3b: `.dram0.bss`, `.dram0.data` and `.flash.rodata`
unchanged, `.flash.text` -0.3 to -2.9 KB (the code is inlined differently
across two files), the signed app still 0x271000, the same three deprecation
warnings. The oracles prove the sim build. On the device, different inlining
could in principle let gcc contract float expressions differently; nothing
here can check that without a board.

**Next:** 3c, every creature's art into one `creatures.inc`.

## 2026-10-23: the classic fish is a row too (refactor phase 3b)

**What changed.** A pure refactor: `--golden-creatures` and `--golden-decor`
are identical to before on all three boards.
- `update_fish` has no fish-or-species split left (`bool sp` is gone). Where
  the classic fish had its own code, its row brings it:
  - `flee`: the straight bolt at `lerp(55, 90, bold)`
  - `dance` (new hook): `court_steer`
  - `swim` (new hook): its committed U-turns, the yaw, the tail beat and the
    glass bounces, moved verbatim into `fish_swim`
  A species row leaves `dance` and `swim` empty, which is the species' way
  (`species_dance`: `sp_court`; `species_swim`: `sp_move` at its pace).
- Two flags on the classic fish's row: `SPF_ROSTER` (its names, look, size and
  temperament range are the ROSTER's, its fry the family's) and `SPF_ROAMS`
  (it holds no station: by the glass the wall still steers it, and a big
  one's berth only turns it). The fry's mutation is the row's `mutate_p`.
- The guards that did nothing for a classic fish are gone: `sp_target`,
  `sp_state` and the flourish, the startle calls, the night's reflex reset,
  eating at the mouth. `tank_make_fish` clears every species field, and the
  fish's row scales by 1.00 and has no head offset, so each runs bit for bit
  as a no-op. `tank_fish_head` now returns the middle for any creature with
  no head offset (the same arithmetic result as before, for every kind).
- The only `SP_FISH` uses left in tank.c are making a fish, the fallback for
  an out-of-range id, and the shop's "a species only" input guards.
- Coverage: neither oracle stages a classic fish's courtship (the creatures
  run courts seahorses). `fish_dance` forwards to the unchanged
  `court_steer`, and `--selftest-pop` runs a fish pair's whole courtship
  through to the fry.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (17 selftests) | both oracles identical | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | both oracles identical | yes | no | no |
| 2.06 watch | passes | both oracles identical | yes | no | no |

Firmware against 3a: `.dram0.bss` and `.dram0.data` unchanged,
`.flash.text` -0.7 to -1.5 KB, `.flash.rodata` +~150 B, the signed app still
0x271000, the same three deprecation warnings.

**Next:** 3b+, every creature's code moves from tank.c to `creature.c` (a
pure move).

## 2026-10-23: the species are rows (refactor phase 3a)

**What changed.** A pure refactor: `--golden-creatures` (309 lines) and
`--golden-decor` (363) are identical to before on all three boards. That
covers every creature frame, every state line the model hears through the
run, and the save.
- `species_def_t` (tank.h) is now the whole species. The old data, plus:
  - `mo`, the private `SP_MOTION[]` row it replaces
  - `fin_hz`/`fin_div` (the old `FIN_HZ`), the bob, the clamp's thickness, the
    den's side, the stop-and-go gait, the legs' beat
  - `SPF_*` flags for the ways some kinds share: upright, a bell, sideways,
    backs up, jets, breathes air, puffs, inks, sparks, visits the surface,
    keeps a home, stays level
  - hooks: `poke`, `state`, `startle`, `flee`, `court`, `target`, `walking`
    and `move`
- Every `switch (f->species)` in tank.c is gone, and every one-off species
  test with it. Each case body moved verbatim into a named function. Kinds
  that shared a case share the function: `inker_*` (octopus, squid),
  `breather_*` (eel, turtle), `cruiser_*` (shark, swordfish), `floor_*` (the
  floor walkers).
- `SPECIES[]` moved below the hooks it points to. `fish_t` got a tag
  (`struct fish`) so a row's hooks can take one. `target_t` is in tank.h.
- Done in place, in tank.c, instead of moving it to `creature.c` first:
  refactor-plan.md now moves the creature code once, finished, after 3b.
- Left for 3b: the fish-or-species split (`!= SP_FISH`, the roster, breeding).
  Left for 3c: render.c's `draw_creature` switch.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (17 selftests) | both oracles identical | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | both oracles identical | yes | no | no |
| 2.06 watch | passes | both oracles identical | yes | no | no |

Firmware against 2b: `.dram0.bss` and `.dram0.data` unchanged,
`.flash.rodata` +1.1 KB (the bigger table), `.flash.text` -0.3 to +0.5 KB,
the signed app still 0x271000, the same three deprecation warnings.

**Next:** 3b, the classic fish's own row (the riskiest step).

## 2026-10-23: the creatures oracle (refactor phase 3.0)

**What changed.** `--golden-creatures [prefix]` (sim only, not a selftest),
recorded before any creature code moves (refactor-plan.md, step 3.0). It
prints one hash a line, 309 lines a board:
- every kind, the classic fish first, in its four designs and four stages,
  both facings, by day and by night (the last of each row resting, so asleep
  at night)
- the species sheet's states (the ball, the jets, the camouflage, the lure,
  the claws, the flip, the sleepers), the ink cloud, the turns of all
  thirteen kinds, and the pages' previews
- all of that in every theme, with the scene cache on and off
- a run of 25 creatures (two fish, a pair of every species, one turtle) among
  the decor for three minutes on the rules: pellets, a startle, a shake, the
  pokes, a seahorse courtship, the light out. Every 15 s it hashes a frame and
  every creature's state line (the shipped `firmware/main/tokenizer.bin`
  schema: what the model hears) with its position, goal and mode
- a night asleep, then the save

Run twice, it prints the same lines on all three boards. I looked at a sample
of its frames to confirm they show the creatures. `--species-sheet`'s three
state sets moved to file scope (`SHEET_S1..3`) so both modes stage them. The
oracle is a code change in `sim/main.c` only.

**Board status:** `common/` unchanged since phase 2b, so every board is as it
was then (checked, pictured, built; not flashed). The oracle builds and runs
on all three.

**Next:** 3a, the creature table (refactor-plan.md), checked against this
oracle line for line.

## 2026-10-23: the refactor plan moves into the repo; phase 3 rewritten

No source change. The plan lived only in a Claude plans folder; it is now
`docs/refactor-plan.md`, with phases 1 and 2 as they were built (the table
fields, hooks and state they ended with differ from the first plan) and
phase 3 rewritten with the keeper's decisions:
- The classic fish's motion is refactored too: `SP_FISH` becomes a full row
  and `update_fish`'s `sp` split goes. A pure refactor is the target; a retrain
  is acceptable if behavior has to change.
- No per-species union of the live fields: they are shared across species.
- `fish_t` / `t->fish[]` keep their names; the docs say creatures.
- One creatures file, not one per species: `creature.c` (the table and every
  creature's motion) and `creatures.inc` (every creature's art, every theme).
- New first step: a creatures oracle (`--golden-creatures`). The decor oracle
  stages classic fish only, so nothing covered the species yet.

**Board status:** unchanged since phase 2b (all three checked, pictured and
built; not flashed).

## 2026-10-22: helpers sell back from the shop (refactor phase 2b)

**What changed** (the keeper's decisions, below the 2a entry):
- **Every helper sells**, the snail included. An owned helper's shop page
  says IN THE TANK and SELLS BACK FOR N, and has one SELL button where UNLOCK
  was. The first tap arms it ("+N OK?"), the second sells it, and any other
  tap disarms it, the same as the decor's SELL. The refund is SD_SELL_PCT,
  20% of the price, as for decor (I had told the keeper 60%; it is 20%, and
  the keeper confirmed 20%). The platforms' SELL routing was already generic,
  so neither port changed for the sale itself.
- **A sale takes it out of the tank**: `progression_sell` calls `helper_leave`,
  which is the row's `init`, its factory state. A rebuy starts fresh: a
  school at SHRIMP_START with its grown shrimp gone, and the snail's,
  urchin's and school's tallies at zero.
- **`render_card_is_helper(t, sel)`** now also asks whether the helper is in
  the tank. A sold helper's card drops by itself on both platforms (the touch
  port's existing cleanup line, and a new one in the sim's frame), so an
  invisible card can't swallow the next tap.
- **`tank_init` resets the shrimp school** (its new `init` hook). Before, the
  school kept the last tank's count, cooldown and pellet tally: after a reset
  and a rebuy, its card showed the old tank's pellets until the next boot.
  This is the one oracle change, made as its own step: on every board exactly
  7 frames differ, the shrimp card (98) in each stage after the first, which
  had shown the previous stage's carried-over tally. Every tank frame, every
  run, the shake, the sleep and the save are unchanged.
- **Tests:** the five asserts that helpers can't be sold now sell, check the
  refund and that the helper is gone, then buy it back. The snail's sale
  goes through its shop page (its row, SELL armed, SELL confirmed), and an
  armed SELL that is tapped off stands down. `--selftest-helpers` now sells
  each row and checks the refund, that the card goes, and that a rebuy
  starts fresh. `--snapshot` adds `_shop_helper` and `_shop_helper_sell`.
- The sim's free shrimp and urchin keys take them away with `helper_leave`.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (17 selftests) | the snail's page, SELL and armed; oracle as above | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | yes | yes (5.4.4, throwaway key) | no | no |
| 2.06 watch | passes | yes | yes (5.4.4, throwaway key) | no | no |

Firmware against 2a: `.dram0.bss` unchanged, `.flash.text` +~0.4 KB, the
signed app still 0x271000 (2% free), the same three deprecation warnings.

**Open.**
- The shop page's SELLS BACK FOR line sits 2 px above the buttons, decor's
  and helpers' alike (the 2a layout, unchanged). It reads cramped in a
  scaled-down picture, but it isn't clipped.
- The frogman and the sub still have no card; the keeper may give them one
  when they help with something.
- Phase 3: creatures. Not flashed: the first real flash is still the layout 3
  save test (DEVICE.md).

## 2026-10-22: the helpers are one table (refactor phase 2a)

**What changed.** A pure refactor: every frame and the save are the same as
before on all three boards.
- **`common/helper.h` / `helper.c`**: `HELPER[HK_COUNT]` has one row per helper
  (snail, urchin, shrimp, frogman, sub) with its shop item, its habitat and its
  hooks: init, arrive, tick, sleep (the whole span: the shrimp's cooldown),
  sleep_slice (each night slice: the snail and the urchin), startle, shake,
  hit, tap, body, drift, save, load. The table's order is the RNG's order (the
  ticks and the shake draw in it), so a new helper goes at the end.
- **State**: the ~65 loose `tank_t` fields became `t->snail`, `t->urchin`,
  `t->school` (`t->shrimp[i]` is `t->school.q[i]`), and `t->frogman` / `t->sub`,
  which share a `drift_t`. There is no union: the plan's union would have put
  the shrimp array into all five (+1.5 KB of internal RAM). The frogman carries
  the sub's 20 B of cruise fields instead: `.dram0.bss` +16 B.
- **Rules**: they moved from tank.c into helper.c unchanged. The frogman and
  the sub avoid each other through `lane_clear` / `drift_near` over every
  owned `HAB_WATER` helper, which with two drifters is the old code exactly.
  tank.c calls `helper_init`, `helper_tick_all`, `helper_sleep_all`,
  `helper_sleep_slice_all`, `helper_startle_all` and `helper_shake_all`, at the
  same points as the old per-helper calls. progression.c buys through
  `helper_arrive` and saves and loads through the hooks. The record kinds stay
  locked in progression.c (`SVH_OF[]` maps a helper to its `SVH_*`).
- **Render**: `HELPER_DRAW[]` paints each owned helper in four slots: under the
  creatures (the shrimp, the frogman, the sub), over them (the urchin, the
  snail's back lane), over the decor IN FRONT (the snail's front lane), and on
  the glass (the snail grazing). The drawers moved, unchanged, into
  `common/helper_<name>.inc`, as their own commit. Cards go through the table
  too, and each card says when it has nothing to show.
- **Taps**: `render_helper_tap(t, x, y, &sel)` and `render_card_is_helper(sel)`
  replace the three card IDs' lists and the per-helper branches in sim/main.c
  and touch_port_ft3168.c. Each platform logs "<NAME> tapped: card up/down"
  (the old per-helper counts in the device log are gone). The frogman and the
  sub have no `hit` hook, so a tap on them still goes through to the water.
- **`--golden-decor`** now also draws each helper's poses and the three cards
  (the snail on the glass and in either floor lane, the urchin chewing, the sub
  stopped with its periscope up, the school darting): 363 lines a board. The
  baseline was recorded from the commit before the refactor, with the widened
  oracle.
- **`--selftest-helpers`** (new, in `SELFTESTS`) loops over `HELPER[]` on each
  board:
  - bought once and not twice
  - inside the glass on arrival, every second through half an hour of the tank
    (film, tall grass, pellets, a shake, a startle) and after a night asleep
  - a tap on a helper with a card brings the card up, the card draws, and the
    next tap puts it away
  - the save and the load keep its record
  I checked that it fails when a row is wrong: the urchin forgetting its spot on
  a load, the shrimp's tap never bringing the card, the frogman ignoring the glass.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (17 selftests) | the oracle's 363 hashes and the save, identical to before | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | the same | yes (5.4.4, throwaway key) | no | no |
| 2.06 watch | passes | the same | yes (5.4.4, throwaway key) | no | no |

Firmware against the decor branch's builds: `.dram0.bss` +16 B and
`.flash.rodata` +~210 B on every board, `.flash.text` 3.8 to 5.4 KB smaller,
the signed app still 0x271000 (2% free), and the same three deprecation
warnings. The watch's static RAM rose by those 16 B.

**Open.**
- **Phase 2b, decided by the keeper:** SELL goes on the shop's item page, and
  every helper is sellable. That includes the snail: the shop's "permanent
  resident" goes, and five tests that assert a helper can't be sold flip. A
  sale refunds SD_SELL_PCT, 20% of the price, as decor does (this entry said 60% at first: my mistake, the keeper confirmed 20%); a sold school loses the shrimp it
  grew, and a rebuy starts at SHRIMP_START. The frogman and the sub get no
  cards for now ("they're not really helpers yet").
- **Found, not fixed (it is behavior, so 2b):** `tank_init` never resets the
  shrimp school (`t->school` keeps its count, cooldown and eaten tally), and
  the shop's buy only resets `food` and `cool`. After a reset and a rebuy,
  the school's card shows the old tank's pellets eaten until the next boot
  (a load of an unowned school clears it). 2b's `leave`
  hook and a shrimp `init` fix both. It changes the oracle's later themes,
  which reuse the same static tank.
- Not flashed. The first real flash is still the layout 3 save test (DEVICE.md).

## 2026-10-22: where the app slot's bytes go, and what frees them (measurement only)

No source change. Measured on the rect `refactor/decor` build (97cfb2a, ESP-IDF
5.4.4, throwaway key, not flashed): the signed app is 0x271000 of 0x280000.

**What fills it (flash-resident, from the map):** the sound bank 530 KB
(16 kHz s16, `EMBED_FILES`); icons 479 KB (174 icon_t: icons.c 139 KB, the
per-theme copies in theme_assets.c 294 KB, the four helper/jelly asset files
46 KB) plus fonts 43 KB; render.c code 225 KB; tank.c 56 KB; the merged
string pool ~142 KB (shows in the map under esp_app_desc.c and secure_boot.c);
Wi-Fi/lwIP/mbedTLS/wpa ~420 KB; newlib printf/scanf ~50 KB. The decor
refactor itself is about -1.5 KB net, not a lever.

**Measured with real builds (rect):**

| change | signed app | free in slot |
|---|---|---|
| today | 0x271000 | 61 KB (2%) |
| + sdkconfig trims (below) | 0x251000 | 188 KB (7%) |
| + tankcore at -Os | 0x231000 | 316 KB (12%) |

The trims: `COMPILER_OPTIMIZATION_SIZE` (IDF + main only; tankcore and llm set
their own -O2/-O3), `ASSERTIONS_SILENT`, `ESP_ERR_TO_NAME_LOOKUP=n`,
`MBEDTLS_TLS_CLIENT_ONLY`, curves other than P-256/P-384/X25519 off, softAP,
WPA-Enterprise, SAE-PK and OWE off, `LWIP_IPV6=n`. Built, not run: the OTA
download and Wi-Fi join need trying on a board. tankcore at -Os needs
`-Wno-error=format-truncation` (update.c:589) and has not been timed; the
render loop is the risk (keep hot spans at -O2 by attribute if it drops).

**Estimated, not built:** sound bank as IMA ADPCM -397 KB (25 dB SNR on the
bank) or mu-law -265 KB (38 dB, random access kept); icons deduplicated -46 KB
(generator only), as row spans of a > 0 pixels -249 KB lossless; 32x32 art
redrawn at 16x16 and shown at 2x -270 KB. Dropping the round and watch boards
frees ~0 on the rect: each board is its own image, the #ifdefs are compile-time.
Newlib nano printf is out: the firmware prints floats and 64-bit values.

**Board status:** nothing changed in the tree. rect built twice as above
(scratch, not flashed); round and watch not built for this.

## 2026-10-22: the decor refactor's firmware builds (ESP-IDF 5.4.4); QEMU limits

**Built.** `refactor/decor` (0365503) builds for all three boards with ESP-IDF
5.4.4, with no new warnings: the watch's `set_lock_icon` warning is on `main`
too, and director.c compiles clean. The builds were signed with a THROWAWAY
key in a scratch directory, not firmware/keys, so they were not flashed and
must not be.

Against `main` (5dcbee3), the same on every board:

| section | change |
|---|---|
| `.dram0.bss` | +304 B (`tank_t` +112, the decor tables' arrays) |
| `.dram0.data` | −64 B |
| static D/IRAM | +240 B net (watch: 161,319 → 161,559 of 341,760) |
| `.flash.text` | about −2.1 to −2.6 KB |
| `.flash.rodata` | about +1 KB |

The signed app is 0x271000 on both branches, 2% free in the 0x280000 slot (61 KB).

**QEMU (esp_develop 9.2.2) can't test the save on target, for three reasons:**
- A signed build aborts in IDF's startup check (`check_signature_on_update_check`):
  QEMU's flash can't read the signature block back. QEMU builds need signing
  off.
- The hardware config halts at the I2C probe (no panel). With
  `sdkconfig.qemu` the app boots through `progression_boot`, but the tank
  task stalls right after "tank task up" on `main` and on the branch alike:
  no 10-second status line, so no autosave. This predates the refactor.
- A save planted in NVS with `nvs_partition_gen.py` (v2 format) is not found by
  `persist_port_load`, even by `main`, so the PTK2 → fresh and layout-3 →
  restore boots stayed untested on target.

The save code is the same `common/progression.c` the sim's `--selftest-saves`
covers on all three boards, and its layout asserts hold on xtensa gcc.

**Board status (phase 1).**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes | yes | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | yes | yes (5.4.4, throwaway key) | no | no |
| 2.06 watch | passes | yes | yes (5.4.4, throwaway key) | no | no |

**Open.** The first real flash (`tools/flash.sh` with the real key) is the
save test: the board's tank starts fresh, then a reboot should restore the new
one. Check the watch's runtime internal heap at boot ("internal heap … free").

## 2026-10-21: save layout 3; the seagrass and the airstone get depths (refactor phase 1b)

**What changed.**
- **Save layout 3** ("PTK3", `common/progression.c`). This is a deliberate
  break, the keeper's call: a PTK2 save (everything before today) is not
  loaded and the tank starts fresh. On a real tank that means the first boot
  after updating loses its fish, sand dollars and decor. **The release that
  ships this must say so in its changelog**, and installer/README.md now does.
  The new layout:
  - one 100 B record per creature (25), with no 6 + 19 core/tail split
  - every frond of all four beds
  - the film
  - a record per decor piece keyed by its kind (`decor_save_t`, 12 slots, 9 used)
  - a record per helper keyed by `SVH_*` (`helper_save_t`, 8 slots, 3 used)
  - the keeper's settings and money in a header section

  3848 B, 152 B under the NVS budget. Within layout 3 the old rules hold: a
  field only appends (SAVE LAYOUT LOCK, asserted on both compilers), and a
  newer build's longer save loads its head. A new decor kind or helper is a
  record, not a field. The loader no longer reads `veg_growth`, the
  pre-bubble migration or the core/ext split.
- **`--selftest-saves`** was rewritten. A tank with every field set comes back
  field for field and re-saves the same bytes. A short save, a foreign magic
  and a roster of 0, 1 or 26 each start fresh, and a longer save loads its
  head. All 8 PTK2 fixtures start fresh at every length they ever had, and
  the new `ptk3-2026-10-21-rich.sav` loads on all three boards. The
  testdata README explains how to add one per release.
- **Depths for the seagrass beds and the airstone** (decor rows: all three).
  The beds are drawn by the grass's own pass at any depth. The airstone at
  BACK draws exactly as before: its stones after the back fronds, the column
  after the food. FRONT puts the stones and the whole column over the
  creatures. MIDDLE is the split: the stones stay behind, and every other
  bubble of the column comes in front.
- **The setup bubbles page has the DEPTH bar.** The placement page's bar is
  now shared helpers keyed by decor kind (`depth_bar`, `depth_bar_hit`), and
  the bubbles page has a tile of its own: bubbles behind, through or over the
  first fish. The drag zone starts under the bar. The placement page's stripe
  reads each piece's height from the table (`decor_top`) instead of its own
  copy. The seagrass depth is data only for now: the director and tests can
  set it, and a hold on the grass stays the trust gesture.
- `--selftest-decor` checks every woven piece (5 of them now) by comparing
  MIDDLE against BACK and FRONT around a column of creatures. Each depth of
  every piece survives the save. `--selftest-sleep`'s old-length checks became
  a plain reload, and `--selftest-themes` pokes the theme at its new offset 56.

All tank frames are identical to phase 1a (the oracle's 291 hashes per
board). The pictures that changed are the bubbles page and the save's hash.
The version-stamped pages differ only by the build id.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (16 selftests) | the bubbles page; oracle vs 1a | no (ESP-IDF v6.1 here, v5.4 pinned); common/ cross-compiles clean, layout asserts hold on xtensa gcc | no | no |
| 1.75C round | passes | yes | no (cross-compiled) | no | no |
| 2.06 watch | passes | yes | no (cross-compiled) | no | no |

- **`--snapshot` no longer saves over the sim's real tank.** Its staged
  tank saved on the way (a purchase in the shop shots) to
  `~/.cache/aqua-pets/tank.sav`. It now uses `/tmp/aqua-pets-snapshot.sav`,
  as the selftests do. This session's oracle runs had already overwritten the
  keeper's sim save that way. A sim that was running at the time writes its
  tank back when it quits with Q or Esc, or at its next autosave.

**Open.**
- Flash only knowing the tank on it will start fresh (DEVICE.md's in-flight
  table: anything mid-measurement on the board is lost with the save).
- Phase 2: helpers, their save records already in place (`SVH_*`).

## 2026-10-21: the decor is one table (refactor phase 1a)

**What changed.** The first step of a three-family refactor (decor, helpers,
creatures; then devices). The plan and the family rules: decor never moves
(it may grow and sway in place), can be placed, and has a depth; helpers (the
frogman, sub, shrimp, urchin, snail) move by their own rules and come in
phase 2; creatures are what the model decides for (`fish[]`) and come in phase 3.

- **`common/decor.h` / `decor.c`**: `DECOR[DK_COUNT]` has one row per static
  piece: the three seagrass beds, the sword plant, castle, coral, reef cluster,
  wreck and airstone. Each row holds its shop bit, where it may stand, the
  depths it offers (`z_mask`), its footprint and tap box, and its part in the
  creatures' world (den rank and offset, camouflage, the ground a walker
  climbs, the reef spot). Its hooks are bought, sold and grow.
  `tank_t.decor[DK_COUNT]` replaces `plant_x … wreck_z`. The bubble column's x
  is the airstone's (`tank_bubble_x`).
- The `tank_decor_*(item)` API is unchanged (thin wrappers by shop index), so
  setup.c, the touch port, the director and the sim's routing are untouched.
  progression.c's buy and sell go through the table. The save is byte-identical:
  the old fields are mapped in and out.
- **render.c**: `DECOR_DRAW[]` gives each piece's passes: baked into the scene,
  drawn without a cache, dragged, the back pass, the front pass. The `PAINT_*`
  orders are the painter's. One mask table (`FMASK`) replaces the per-piece
  `g_front_*` / `g_bake_*` globals, so the grass, the castle's front row and
  `front_restore` read every masked piece the same way. The castle, coral,
  cluster and wreck drawers moved, unchanged, into `common/decor_<name>.inc`.
- **MIDDLE is a split** (the keeper's rule): half of a piece is drawn behind
  the creatures and half in front, and each piece says which half is which.
  Seagrass and the plant split by alternate stalks.
- **`--selftest-decor`** (new, in `SELFTESTS`) loops over `DECOR[]` on each
  board. It checks the depths and the placement bar's segments, the default
  spot and every clamp inside the glass, the hold on a piece, the sale back to
  factory state, the save and load at every depth, and that a woven piece has
  pixels both over and under a creature in front of it. A new row is tested
  with no new code. I checked that it fails when a row is wrong.
- **`--golden-decor [prefix]`** (sim only, not a selftest) is the refactor's
  oracle. It renders every piece and helper in every depth mix, in two
  layouts, in four themes, with the scene cache on and off. It then runs a
  minute of the tank and saves, and prints a hash per frame. Two builds that
  print the same lines draw and save the same tank.

**Two pixel changes, both bug fixes, both found by the oracle:**
- Without the scene cache, a wreck set BEHIND was never drawn, and a FRONT
  wreck painted its holes' dark over the fish. A FRONT wreck drawn live (no
  cache, dragged, or with a placement page up) is now split like the baked
  one: its inside behind the creatures, its hull over them.
- The castle's front-row repaint (Original theme, cached) skipped a FRONT
  cluster's or coral's pixels but not a FRONT wreck's. Where the two overlapped
  and a fish passed, castle stone showed over the wreck's rail. It now leaves
  every masked FRONT piece alone.

Everything else is identical to `5dcbee3` on all three boards: 291 oracle
frames plus the save, the 431 pictures from `--snapshot` / `--selftest-themes` /
`--clip` / `--hero`, and the encoder's 150 lines.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (16 selftests) | oracle and snapshot hashes vs `main` | **no**: the machine has ESP-IDF v6.1, the project pins v5.4. common/ cross-compiles clean with xtensa-esp32s3-elf-gcc | no | no |
| 1.75C round | passes | yes, as above | no (cross-compiled) | no | no |
| 2.06 watch | passes | yes, as above | no (cross-compiled) | no | no |

`tank_t` grew by 112 bytes on every board (10952 to 11064 on the rect). Check
the watch's `.bss` at the next firmware build. `firmware/main/director.c`
changed field names only and was not compiled.

**Open.**
- Phase 1b: a new save layout (a deliberate break, approved: old saves start
  fresh), seagrass and airstone at BACK / MIDDLE / FRONT, and a depth bar on the
  setup bubbles page.
- Phase 2: helpers. Phase 3: creatures. Phase 4: devices.

## 2026-10-21: the BOOT key opens a main menu; the fish card only shows the card

**What changed.** The way into the pages used to be: tap a fish, tap MORE on
its card (the whole card was the hit box), land on the milestones page, then
use its SETTINGS / UPGRADES buttons. The sponge and scissors sat in a toolbox
under the card. Now:

- **BOOT's short press is the home key** (`touch_port_home`, called from
  `sleep_button_poll` in firmware/main/main.c at the release of a non-chord
  press). From the tank it opens the **main menu**, from the menu it goes back
  to the tank, and from any of its pages (milestones, shop, settings, updates)
  it returns to the menu. It is ignored while the setup flow or the reset prompt
  has the glass. Hold BOOT + tap is still the reset chord.
- **The main menu** (`render_menu` / `render_menu_tap` / `render_menu_tile_rect`
  in common/render.c, layout `MENU_*` in render.h) is a 3×2 grid inside PAGE
  x 56..392, y 60..300: TANK (milestones), UPGRADES (shop), SETTINGS / SPONGE,
  SCISSORS, and an empty cell. Picking a tool closes the menu and the DONE chip
  shows. Picking the tool already in hand puts it back. A tap off the grid
  closes the menu.
- **The card** lost its MORE button (`RENDER_CARD_H` 258 → 228, so the card
  cache is 7.4 KB smaller) and the toolbox (`render_tools_hit` and
  `RENDER_TOOLS_*` are gone). A tap on the card now puts it away.
- **The milestones page** lost SETTINGS and UPGRADES. CLOSE is alone at the
  foot, centred on the bowl, and returns to the menu, as do the shop's and
  settings' CLOSE. The sand dollar on the TANK row still opens the shop.
- Sim: **SPACE** is BOOT (`sim_home`). Director: `menu [off]`.
- The top-row icons are **placeholders** (the bubbles badge). The keeper is
  drawing the real ones. To add them: put `assets/icons/menu_{tank,upgrades,settings}.png`
  (32×32, shown at 2x) in place, run `tools/gen_icons.py`, and point
  `MENU_ICON[]` in render.c at them.

**Why.** The keeper found the tap-a-fish → MORE route to settings hidden and
roundabout, and BOOT went unused. The watch's second key is BOOT on GPIO0 too
(Waveshare's ESP32-S3-Touch-AMOLED-2.06 page), so one code path serves all
three boards. A board with **no PMIC** (QEMU, bring-up) used to sleep on a
BOOT short press. That is gone; the director's `deepsleep` / `poweroff` still
do it there. Every real board has the AXP2101's PWR key for sleep.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (`--selftest-menu` new) | yes: snapshot sheets of menu / card / milestones | **no**: no ESP-IDF on this machine | no | no |
| 1.75C round | passes | yes | **no** | no | no |
| 2.06 watch | passes | yes | **no** | no | no |

`tools/board_sheet.py` wasn't run (no Python 3 here). The pictures came from
`--snapshot` on each board's sim, put side by side.

**Open.**
- The firmware has not been compiled. It needs `idf.py build` plus the round
  and watch `--build-only` builds, and a look at the watch `.bss` (the card
  cache shrank, nothing grew in internal RAM).
- On the glass, check that BOOT's release is felt as one press. Debounce is
  50 ms, as before. The deep-sleep wake press is waited out by the sleep code
  before the tank task runs, so it should not also open the menu. Untested.
- The real icons for TANK / UPGRADES / SETTINGS.
- `docs/media/sim-stats-card.png` still shows the old MORE button and needs a
  fresh capture. `sim-tools.png` was removed and `sim-menu.png` added.
