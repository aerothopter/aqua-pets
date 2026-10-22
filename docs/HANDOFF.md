# Handoff log

Newest first. What was done, where each board stands, what's open, and why.

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
