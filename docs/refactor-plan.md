# The entity refactor: decor, helpers, creatures, devices

Started 2026-10-21. Each family of things in the tank gets one definition
table and one generic path, so a new item, species or board is a row plus its
art (a board: a device header and a layout header). All four phases are done
(2026-10-23); this file says what each became (the plan they started from
differed in places). The day-by-day narrative is in HANDOFF.md.

## The three families

| family | moves? | decided by | bought | sold | bred | members |
|---|---|---|---|---|---|---|
| **Decor** | never; it may grow and sway in place | nothing | yes, or free (setup places it) | the shop pieces | no | seagrass beds 0-2, sword plant, castle, coral, reef cluster, wreck, airstone |
| **Helpers** | by its own rules; never placed | its rules | yes | yes | no | snail, urchin, shrimp school, frogman, sub |
| **Creatures** | yes | **the model** (goal and urgency) | yes, or born | yes | yes | the classic fish and every `SP_*` species |

Creatures are exactly the model's population, `t->fish[]`, which is also the
encoder's `friend` set; that is why a helper never joins it. In code the
creatures keep the fish's names (`fish_t`, `t->fish[]`, `update_fish`); the
docs call the family creatures (the keeper, 2026-10-23).

**Depths.** BACK paints the whole piece behind the creatures, FRONT over
them. MIDDLE is never a third pass: half the piece is drawn behind and half in
front, each piece saying which half is which (the grass's alternate stalks,
the airstone's alternate bubbles).

## Phase 1: decor (done, 2026-10-21/22)

As built (HANDOFF 2026-10-21 and 2026-10-22):
- `common/decor.h` / `decor.c`: `DECOR[DK_COUNT]`, a row per piece, and
  `tank_t.decor[]`. Where the plan had DEN / CAMO / GROUND flags, a row has
  ranks (`den_rank`, `camo_rank`, `hit_rank`) and hooks (`on_buy`, `on_sell`,
  `grow`, `camo_rgb`, `ground`); the flags left are `DF_SELLABLE`,
  `DF_MOVABLE` and `DF_REEF`. The `tank_decor_*(item)` API stayed as wrappers.
- render.c: `DECOR_DRAW[]` (`key, bake, bake_front, plain, dragged, back,
  front`), one `FMASK[]` mask table, `g_scene_key[]`, and the `PAINT_*`
  orders. The plan's named layer slots (`L_SCENE_BACK` ... `L_GLASS`) were
  never introduced: the order is render_tank's passes. The drawers are in
  `decor_<name>.inc`.
- Two bug fixes the oracle found (the plan expected one): an uncached wreck
  BEHIND was never drawn, and the castle's front row repainted over a FRONT
  wreck.
- Save layout 3 ("PTK3"): PTK2 saves start fresh (the keeper's call); decor
  records keyed by kind + 1 (`decor_save_t`, 12 slots); a helper section
  reserved (`helper_save_t`, 8 slots). The seagrass and the airstone got
  depths; setup's bubbles page got the DEPTH bar.
- `--selftest-decor` and the oracle `--golden-decor`.

## Phase 2: helpers (done, 2026-10-22)

As built (HANDOFF 2026-10-22):
- `common/helper.h` / `helper.c`: `HELPER[HK_COUNT]` with hooks `init`
  (factory state: tank_init and a sale), `arrive`, `tick`, `sleep` (the whole
  span), `sleep_slice` (each night slice), `startle`, `shake`, `hit`, `tap`,
  `body`, `drift`, `save`, `load`. The table's order is the RNG's order.
- State: no `helper_t helper[]` and no union (it would have put the shrimp
  array in all five). Each kind has its own struct: `t->snail`, `t->urchin`,
  `t->school`, and `t->frogman` / `t->sub` sharing a `drift_t`.
- render.c: `HELPER_DRAW[]` paints each owned helper in four slots (under the
  creatures, over them, over the decor IN FRONT, on the glass), and draws its
  card. The plan's `layer` hook became the slot argument. The drawers are in
  `helper_<name>.inc`.
- Taps: `render_helper_tap` and `render_card_is_helper` (the plan's
  `tank_tap_helper`). The frogman and the sub have no card ("not really
  helpers yet", the keeper).
- Save: the layout-3 records, mapped by `SVH_OF[]` in progression.c.
- 2b, the keeper's decisions: SELL on the shop's item page (not the card),
  every helper sellable (the snail too), the decor's 20% refund, a rebuy
  starts fresh. `tank_init` now resets the shrimp school (a bug 2a found).
- `--selftest-helpers` (inside the glass, the card, the save, the sale);
  the sleep catch-up's numbers stay `--selftest-sleep`'s.

## Phase 3: creatures

The keeper's decisions (2026-10-23):
- **The classic fish is refactored too.** `SP_FISH` becomes a full row, with
  hooks like every species; `update_fish`'s split on `bool sp` (tank.c, about
  ten branch points over its 380 lines) goes. The target is still a pure
  refactor, which needs no retrain. If the fish's behavior has to change, a
  retrain is acceptable: then it is its own step, with
  `model/gen_traces.py` brought along and the encoder checks rerun.
- **No per-species union** of the live fields. They are shared across species
  (`puff`: the puffer's ball and the crab's claws; `air_s`: the eel and the
  turtle; `ink`: the octopus and the squid), and shared code counts `ink` and
  `spark` down for every creature. It would save ~750 B of RAM the watch
  doesn't need.
- **Names:** `fish_t` and `t->fish[]` stay; the docs say creatures.
- **One creatures file, not one per species.** `common/creature.h` /
  `creature.c` hold `SPECIES[]` (the classic fish's row included),
  `SP_MOTION[]`, `FIN_HZ`, and every creature's motion. `common/creatures.inc`
  holds every creature's art, the classic fish's and every species', in every
  theme. It replaces `living_lagoon.inc`, `theme_creatures.inc`,
  `blackwater.inc` (all three are creature art only), `jellyfish.inc`, and the
  drawers in render.c (`draw_fish_core` and the `sk_*` kit through
  `draw_creature`, about render.c:226-1370).

### Steps

**3.0 The creatures oracle, first.** `--golden-decor` stages classic fish
only, so it can't prove phase 3 changed nothing. A new `--golden-creatures`
is recorded on the commit before any creature code moves:
- every species with the classic fish, every variant and stage, in the poses
  `--species-sheet` stages (the ball, the jets, the camouflage, the lure, the
  claws, the flip, a turn) and asleep
- all four themes, the scene cache on and off, day and night
- a mixed tank of every species running for several minutes on
  `advisor_rules`: pellets, a startle, a shake, a poke, a courtship, a night
  asleep
- a save hash, and the `--selftest-encoder` lines (what the model hears must
  not move)

**3a. The table (pure; done 2026-10-23, in place in tank.c).** The rows
took over the species code where it was; the move to `creature.c` comes
after 3b (step 3b+), so the creature code moves once, finished, and the tank.c
internals it needs are exposed once. `SP_MOTION[]` and `FIN_HZ` became the
rows' `mo` and `fin_hz`. A row got the hooks the code already had seams for
(as built: `poke`, `state`, `startle`, `flee`, `court`, `target`, `walking`,
`move`, plus data and `SPF_*` flags for the one-off tests - the bob, the gait,
the den's side, the clamp, who backs up, jets, breathes, puffs, inks, sparks,
visits the surface, keeps a home, stays level). The plan was:
- `state`, `poke`, `startle_hit`, `startle_steer`, `court`, `target`,
  `flourish`, `walking`, `move`, `clamp_body`
- `head`, `backward`, `jetting`
- what render needs: a draw hook and its `sk_setup` numbers

Today's switches share cases between groups (the jetters: octopus and squid;
the air-breathers: eel and turtle; the cruisers: shark and swordfish; the
floor walkers), so rows point at shared functions rather than one function
per species. Every `switch (f->species)` goes. The oracle is identical.

**3b. The classic fish's row (pure; done 2026-10-23).** As built: the fish's
row brings `flee`, and two new hooks, `dance` (its `court_steer`) and `swim`
(its turns, yaw, tail beat and glass: `fish_swim`); a species' row leaves them
empty for the species' way. `SPF_ROSTER` and `SPF_ROAMS` mark what is the
roster's and that it holds no station; `mutate_p` is its fry's mutation. The
plan was: The fish's own steering, its turns, the
tail beat and the hold become `SP_FISH`'s hooks, and `update_fish` calls the
row's hooks for every creature, with no `sp` split. The RNG is drawn in the
same order, so the oracle is identical. This is the riskiest step and gets
its own commit.

**3b+. `creature.c` (pure move; done 2026-10-23).** As built: six ranges
of tank.c moved verbatim (1,623 lines); the species types went from tank.h to
`creature.h`, which tank.h includes where they stood; `tank_internal.h` holds
what the two share (the small maths, `tank_xr`, eleven constants, the grass
and courtship calls, `update_fish` and `sp_startle_hit`). The plan was: with
the fish a row, every creature's
code - the roster, `SPECIES[]`, making, breeding and removing creatures, the
species' and the fish's motion, `update_fish` - moves from tank.c to
`common/creature.h` / `creature.c`, the tank.c internals it needs exposed
through a small internal header.

**3c. `creatures.inc` (pure move; done 2026-10-23).** As built, five
commits: render.c's creature drawers cut into `creatures.inc` at their
place, then `living_lagoon.inc`, `theme_creatures.inc`, `blackwater.inc` and
`jellyfish.inc` each folded in where it was included (2,346 lines, no
includes left). The order inside is the old include order, so every step
expands to the same text. The plan was: The art moves into one file. The order
inside it: the shared kit, the classic fish, the species, then each theme's
looks (or species by species, whichever keeps the move a move). One commit
per source file; the oracle is identical after each.

**3c+. `CREATURE_DRAW[]` (done 2026-10-23, the keeper's ask).**
`draw_creature`'s switch became a row a species in creatures.inc: its drawer,
`sk_setup`'s span and lean, a shared lean level, and whether it eases into a
jet. Only the three themes' looks functions still switch on the species.

**3d. `--selftest-creatures` (done 2026-10-23).** As built, for every row on
every board: the row is whole; a pair arrives as its kind in a design it
has; it draws in every theme; it stays inside the glass through twenty
minutes (pellets, a startle, a shake) and a night; a triple tap beside it
changes it exactly when its row has a startle, and `tank_poke` exactly when
it has a poke; the save keeps every creature's kind and design; eight fry
of a pair are its kind (a classic fish's may be another, by `mutate_p`).
Checked to fail on a do-nothing startle, a do-nothing poke, a fish let past
the glass and fry of the wrong kind. The plan was: A loop over `SPECIES[]` on every board, like
`--selftest-decor` and `--selftest-helpers`:
- a pair arrives and stays inside the glass through a run and a night
- its startle and its poke do what its row says
- the save keeps its species and variant
- breeding stays within the species

A new species is then tested with no new code. `--selftest-species` stays
for the per-species details (dens, camouflage, ground).

### Constraints
- Species IDs, `SPECIES[].token` and `SP_COUNT` are saved and heard by the
  model: they never move. A new species goes at the end.
- `--selftest-encoder | encoder_agree.py` stays byte-identical.
- The RNG order must not change (the oracle sees it).
- Breeding stays within a species (`pick_parents`).
- The save layout is untouched: a creature saves its species and variant
  already.

## Phase 4: device architecture

On `refactor/devices`, from `refactor/creatures`. The keeper's decisions
(2026-10-23):
- **Every step is pure.** Nothing on any board changes: both oracles and the
  `--snapshot` pages stay identical on all three. A behavior change the work
  turns up (for example, a bed's ceiling measured where the bed isn't) is
  noted and left for its own step later.
- **No pages oracle.** The `--snapshot` frames (118 pages a board), hashed,
  stand in for one. Build every sim with `GIT_DIR` pointing nowhere and
  `persist_port_sim.o` rebuilt, so that every build prints the same build id
  ("sim") on the settings and update pages.
- **The square-panel override goes** (`-DTANK_W=480 -DTANK_H=480`, tank.h).
  A new panel is a new device header.

The plan was (its line numbers had drifted, so the places were looked up
again):
- Compile-time device headers: `common/device.h` includes one of
  `common/devices/{amoled18,round175c,watch206}.h`, each with the world
  geometry, the glass shape, the film grid, the bed table (ending the
  `veg_bed_base` / `tank_veg_cap` duplicate), capability flags
  (`DEVICE_WORN`, `DEVICE_PAGE_BOWL`, `DEVICE_CAN_TURN`) and `PT_BOARD`.
  `TANK_ROUND` / `TANK_WATCH` stay only as the selector.
- One glass module (`common/glass.c`) for every shape: the swimmers' clamp,
  `wall_avoidance`, the step's reflection and the helpers' habitat rules call
  it.
- Per-device layout sections: the `MSP_*/SHP_*/SET_*/UPD_*` blocks move out
  of render.h and update.h, and inline literals become named constants.
- Feature branches become capability checks (the settings SCREEN row, the
  firmware's orientation, sim/theme_test.c's circle).

### Steps

**4a. The device headers (pure; done 2026-10-23).** As built:
`common/device.h` picks `devices/amoled18.h`, `round175c.h` or `watch206.h`
from `TANK_ROUND` / `TANK_WATCH` (both at once is an `#error`). A header
holds:
- `PT_BOARD` and `PT_BOARD_NAME` (taken from version.h, which includes
  device.h)
- the frame, `TANK_BOT` / `TANK_FX0` / `TANK_FX1`, and `DEVICE_GLASS`
  (`GLASS_RECT`, `GLASS_BOWL` with `TANK_RAD`, `GLASS_CORNERS` with
  `TANK_CORNER_R`)
- the film grid, `VEG_SEG_PX`, `FEED_ZONE_Y`, `DECOR_MARGIN`,
  `PLANT_X_DEFAULT`
- the flags `DEVICE_WORN` (was `TANK_WORN`) and `DEVICE_PAGE_BOWL` (was
  render.h's `PAGE_BOWL`; gone in 4d, when every number it chose moved into
  the layout headers)

The firmware's orientation (main.c) asks `DEVICE_WORN`, and the settings
SCREEN row already did, so the plan's "feature branches" step (4e) folded in
here. `DEVICE_CAN_TURN` was not added: on every board it would be
`!DEVICE_WORN`, and the one place that needed it (the live flip) is really
the worn rule. sim/theme_test.c draws no circle; it only reads the page flag.
The bed table moves in 4c, with the code that reads it.

**4b. One glass module (pure; done 2026-10-23).** As built: `common/glass.h`
/ `glass.c` hold every piece of code whose body depends on the glass's shape,
moved verbatim, one body a shape by `DEVICE_GLASS`:
- the edges and the clamp (`tank_glass_*`, from tank.c; glass.h is their
  public header, which tank.h includes where they were declared)
- `rand_water` and the film's cells (`glass_holds_cell`, was tank.c's
  `algae_ok`)
- the swimmers' glass, from creature.c: `glass_bounce` (the step's hard
  bounds and reflection; the turn's settle is an argument, as it is the swim's
  constant), `glass_clamp_target` (a target's room) and `glass_avoidance` (was
  `wall_avoidance`)

`corner_in` / `corner_clamp` became glass.c's own. The callers that clamp
only where the glass curves (an explore point, the shrimp, the snail's
stand, the sim's window mask) ask `GLASS_CURVED` (device.h). Everything is
compile-time, as before: no shape is dispatched at run time (the app slot is
98% full, and the frame budget is tight). The internal calls are declared in
tank_internal.h.

**4c. The bed table (pure; done 2026-10-23).** As built: a device header's
`DEVICE_BEDS` is beds 1 and 2's first frond and base frond count, and
`DEVICE_REEF_BED_LESS` the fronds its reef bed gives up. tank.c's `VEG_BED[]`
holds the table, and `veg_bed_base` and `tank_veg_cap` both read it (the
ceiling's beds by the glass were a second copy of the numbers). The reef bed
follows `reef_x` and the sword plant its own place, as before; the ceiling
still measures the reef bed at its widest from the reef's fixed x, which is
where `tank_init` puts it.

**4d. The layout headers (pure; done 2026-10-23).** The keeper's call: a
layout header per board, holding every page number that differs between
boards. As built: `common/devices/{amoled18,round175c,watch206}_layout.h`,
named by the device header's `DEVICE_LAYOUT` and included by render.h and
update.h (update.h through device.h). Each one has:
- the card, the tool chip, the battery pill and the "+n" toast
- the milestones page's rows, badges, portrait, page arrow and CLOSE (with
  its tap slop)
- the shop's header, foot and page arrows' inset
- the settings page's rows, segments, foot, last row's band, version line
  and `SET_BOUNDS_*` (was `render_settings_bounds`' literals)
- the update pages' CLOSE, button pair, password inset and calibration
  inset (was update.c's three `CAL_X` / `CAL_Y` arrays)

Today's `#ifdef` blocks and the flag ternaries (`PAGE_NARROW ||
DEVICE_PAGE_BOWL ? 172 : 176`) both became plain numbers per board, so a
board's pages are one file. render.h's dead first `SET_CLOSE_X` (and its
three `#undef`s) went. `DEVICE_PAGE_BOWL` and `PAGE_NARROW` had no users
left and went; theme_test.c's bowl check asks `DEVICE_GLASS` and `TANK_RAD`.
The bowl's deep sand (render.c `floor_rgb`) is keyed on what causes it:
`TANK_BOT < TANK_H`, a floor above the frame's bottom. `DEVICE_WORN` stays,
for the features that are the watch's own (the SCREEN row, the firmware's
orientation).

After 4d, `TANK_ROUND` / `TANK_WATCH` appear only in device.h's selector and
the ports.

The ports (`firmware/main/*_port_*`) keep their board `#ifdef`s: the panels
differ, and BOARDS.md allows it.

## Verification, every step

1. The oracle first, recorded on the commit before the change. Pure steps
   must match it line for line on all three boards; a behavior step explains
   every line that moves.
2. `make check-all` (MSYS2 UCRT64).
3. The pictures: `tools/board_sheet.py --selftest-card` needs Python 3, which
   the keeper's Windows machine lacks. Until then, `--snapshot` sheets of the
   pages a step touches, side by side.
4. Firmware builds for each board (ESP-IDF 5.4.4), with `.bss` against the
   step before: internal RAM has ~23 KB headroom.
5. A HANDOFF.md entry for each step, with the per-board table.
