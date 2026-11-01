# Handoff log

Newest first. What was done, where each board stands, what's open, and why.

## 2026-10-31: the line-by-line comment pass, firmware too; this log stays under 1000 lines

**What changed.** No behaviour. Every line of `firmware/`, `sim/`, `tools/` and `common/` (generated files aside) was checked
against the code beside it. `tank.h`, `tank.c`, `creature.c`, `progression.h`, `progression.c` and `render.h` were read line by
line in the documentation pass below; this round searched them again rather than reading them a third time, and the search
found three more stale comments there. The rest were read in full this round.

- **`firmware/`.** Dates, names and history are gone. Wrong comments fixed: 60 fps (the ceiling is 25), three brightness levels
  (ten), the audio port's 30 ms and 2 s (~250 ms and 5 s), the codec "never used", ALDO1 "unused", the prewarm note, the
  section numbers into AUDIO.md, and notes about drag-feeding, the lights-out notice and drowsing.
- **`common/`.** References to `tank.c` for code that lives in `creature.c` or `progression.c`, the floor's four tones, 14.2M,
  the wreck's cabin, and comments moved to the functions they describe.
- **`sim/`.** The mouse gestures still listed feeding by tap and drag; `--snapshot`'s pages; a wrong password keeps the store;
  several selftest comments sat above the wrong function.
- **`tools/`.** The installer's dialog edits ("three more", there are six); Blackwater's wreck thumbnail is a galleon;
  `preflight.py --quiet` is not what `flash.sh` runs; `make_sounds.py`'s AUDIO.md reference.
- **CLAUDE.md.** Trim this log after adding to it. "Strato" now appears only in trimmed entries and as a pretend Wi-Fi
  network in the sim's tests. This trim dropped the oldest entries; git keeps them, and no doc or comment cites one.
- **`model/schema.md`.** Only the v5 line now, as one spec with each field's meaning: the v2, v3 and v4 sections and their history are
  gone (git keeps them). Those encoders stay in `gen_traces.py` and `advisor_core.c`, so the comments in `model/` that sent readers to
  schema.md for them were corrected. The rest of `model/`'s comments were not part of this pass and still carry dates.

**Proof.** The scripted check compared 101 changed files in `common/`, `sim/`, `tools/` and `firmware/` with HEAD, comments
stripped: 99 identical. The other two are expected: `sim/testdata/saves/README.md` (prose) and `tools/gen_icons.py` (the template
for `icons.h`'s comments). `.sh`, `CMakeLists.txt`, `partitions.csv`, `Makefile` and `.cjs` get a weaker line-based comparison.
`python -m compileall` is clean over `tools` and `model`. No generated file changed.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes | not retaken: no pixel can have changed | not rebuilt: `firmware/` and `common/` compare identical without comments | not flashed; what is on the tank is still 2026-10-28 20:40 | - |

**Open.** Left alone because they are code or generated, not comments:
- **Stale text inside strings.** `director.c`'s help (`level 100|60|30`, `kbd ... 09-13`); `update_mode.c`'s "(the measurement
  docs/OTA.md waits for)"; the "shafts" labels in the firmware's and the sim's bench output. `sim/main.c`'s key messages ("on page 2",
  "swipe sideways through a canopy", "tap near them to scatter", lights-out "MANUAL (double-tap the glass...)") and its selftest
  messages ("wheel-era", "drowse slices", "(2026-09-23: it did, every time)", "a hold on its foot", "the shop's hold", "pre-shop",
  "pages in the mmap'd weights"). The installer's Wi-Fi form text says "on the pendant".
- **Dated generated headers.** `common/icons.h`, `common/{frogman,sub,wreck}_assets.c`, and the strings in
  `tools/gen_{frogman,sub,wreck}_assets.py` that write them. These need a regeneration, which needs Pillow (and librsvg for the
  assets), not on this machine. `gen_icons.py` is already fixed; the other three scripts change with their regeneration, since
  `--check` compares bytes.
- **Leftover code.** `setup.c`'s `(void)coral_hint`; `audio_port_es8311.c`'s unused `IDLE_US`; `net_err_name` has no entry for the
  board error; the `ESP_LOGI("card tap...")` in `touch_port_ft3168.c` sits inside a comment and never compiles (its text was
  removed from that comment, so the tokens are unchanged).
- `installer/index.html` was left as it is, on the keeper's call.

## 2026-10-31: the documentation pass (the keeper's "Docs" TODO)

**What changed.** No behaviour. Comments and docs now say what is, with no dates, no "used to", and no names of who
asked. CLAUDE.md has that as a rule, with this log as the one exception.

- **Comments in `common/`, `sim/` and `tools/`.** Beyond removing history, these were wrong and are fixed: the founding pair
  is two fry (the comments said adults); the coral and the cluster offer BEHIND / IN FRONT only (they said AMONG too);
  `slash_armed` means the scissors' stroke; trait drift's rate (a whole 0..1 trait per `DRIFT_HOURS`, not 0.11); the card has
  no stage pips; brightness is ten steps. sim/main.c's header lists every flag and key.
- **`docs/`.**
  - Merged: `progression-next.md` into `progression.md`; `retrain-v3/v4/v5/next.md` into `retrain.md`.
  - Removed: `refactor-plan.md`; `board-amoled-1.75c.md` and `board-amoled-2.06-watch.md` (both are at the tag, and
    BOARDS.md says how to read them).
  - Rewritten as they stand today: `stats`, `pipeline`, `species` (now helpers too), `THEMES` (the full write-up is at the
    tag), `BOARDS`, `DEVICE`, `memory_budget`, `bringup`, `AUDIO`, `OTA`. Also README and the installer README.
- **Pictures.** 32 `docs/media/sim-*.png` regenerated from `--snapshot` / `--selftest-card` (`sim-stats-card` and
  `sim-tank-tally` showed removed buttons). Removed: `sim-three-boards.png`, the round and watch `.webp`. Kept: the tank scenes with
  no UI (`sim-tank`, `sim-shrimp`, `sim-species*`, `sim-shrimp-card`), which are still accurate.

**Proof.** A scripted check compared every changed C / header / `.inc` file with HEAD after stripping comments (gcc
`-fpreprocessed`, whitespace dropped), and every Python file by AST without docstrings: identical, except `tools/gen_icons.py`'s
template text for icons.h's comments. `make check-all` passes.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | every doc picture regenerated and the changed ones looked at | not rebuilt: `common/` is token-identical, and `firmware/` changed in one comment | not flashed; what is on the tank is still 2026-10-28 20:40 | - |

**Open.**
- The keeper's TODOs still open: **Solo mode**, **Trust**, **Play**. For Solo mode: the v2 model broke on `friend none`, the
  training data has carried a `lonely` case since schema 3, and v5j has not been probed for it (`model/probe_dist.py`).
- Generated files keep their dated comments until they are regenerated (needs Pillow): `common/icons.h` (its generator is already
  fixed) and `common/*_assets.c`.
- `firmware/` and `model/schema.md`: the entry above.
- `installer/index.html` still shows Quiet Lagoon and Tidepool Club previews ("Pick your atmosphere").
- `tools/director.py` still finds its port by the Mac's `/dev/cu.usbmodem*`; on Windows pass `-p COMn` or `TANK_PORT`.
- A few selftest failure messages quote old dates (`sim/main.c`, code, not comments).
- `sim-species.png` shows ten of the thirteen kinds: a new one would need a staged scene.

## 2026-10-28: no double-tap light, no startle, no hold-to-move (the TODO's other half)

**What changed (the keeper's asks and calls).** Bare hands now only reach
the creatures: a tap on one (its card, two quick taps its trick), a tap on
a helper (its card), and a held finger (the trusting come over).
- **The light: AUTO or NEVER.**
  - MANUAL and its double-tap are gone. The settings row is AUTO after
    5 SEC .. 30 MIN, then NEVER, the default, which keeps the light on.
  - The keeper's call: a NEVER choice rather than a default idle time. A new
    tank stays lit as before.
  - `tank_light_choice` runs 0..LIGHT_IDLE_N-1 (the times) and LIGHT_IDLE_N
    (NEVER). `light_auto` false = NEVER, so a MANUAL save loads as NEVER.
  - `light_manual_off` and `light_tip_seen` left `tank_t`. The save writes
    them as 0 (the layout keeps the bytes).
  - NOTICE_LIGHTS_OUT went: it taught the double-tap.
- **The startle: deleted** (the keeper's call, as with the shake).
  - Gone from tank.c/tank_internal.h: `tank_startle`, the tap burst
    (`tap_count`, `tap_burst_t`, `tap_x/y`, `TAP_WINDOW`), `startled`,
    `startle_x/y`, `startle_cooldown` and `STARTLE_*`. `tank_touch_tap` with
    bare hands is a knock's cue and nothing else.
  - Gone from creature.c: `sp_startle_hit`, every species' `startle` and
    `flee` hook and `sp_startle_steer`.
  - The species' startle-only reactions went with their state and art:
    - the seahorse's `SPM_CLING`
    - the crab's `SPM_CLAWS`/`SCUTTLE` (its claws-up art; `crab_state`)
    - the lobster's `SPM_FLIP` (`sp_flip`, `lobster_move`, the tail curl,
      `lean_flip`)
    - the jellyfish's hurried pulse (`jelly_state`)
  - Kept: the poke's tricks, the puff and the ink, which the double tap on a
    creature still does. The eel's spark stays too: it is also an idle
    flourish.
  - The helpers' `startle` hook and `helper_startle_all` went. So did the
    shrimp's scatter and dart (`shrimp_t.dart`, the dart art) and their tap
    counting (`school.taps/tap_t`, `tank_shrimp_tap`; the shrimp have no
    tap hook now, a tap is their card).
  - `TEV_SPOOK` went (its sound stays in the generated bank).
- **Hold-to-move: gone** on both platforms (`held_page` / `s_held_page`). A
  piece's placement page opens from the shop's MOVE. `tank_decor_hit` stays
  (the director and the tests use it).
- **The words:**
  - The care page lost "TWO TAPS FLIP THE LIGHT".
  - The TRUST tip lost "3 QUICK TAPS SCARE THEM".
  - The puffer's shop words: "A YOUNG ONE. TAP IT TWICE / AND IT PUFFS UP".
  - README (trust, the tools, the shrimp, selling back, the light, the
    sim's mouse, the status list), CLAUDE.md's helper hooks, docs/AUDIO.md,
    docs/species.md and docs/progression.md.

**Tests.**
- `--selftest-sleep`'s light block: NEVER at the row's end, the steps back
  through the times, both ends holding, a wheel-era 20 s, and two and three
  taps on the glass never touching the light.
- The spawning leg's dark is the director's override (`light_override`).
- The startle checks went from `--selftest-species`, `-creatures`, `-shop`
  (the shrimp: a tap is the card, no tap hook), `-tend` and the helpers'
  run. The puffer's puff test pokes it: the poke sets the same 5 s timer.
- The species sheet lost the crab's claws-up and the lobster's flip poses.
  The golden decor run's shrimp pose pecks instead of darting
  (`shrimp_peck`).

**Proof.**
- The baseline is HEAD (15832b6) with only the golden runs' startle taps
  and the startle-only poses (the claws-up crab, the flipping lobster, the
  darting shrimp) taken out.
- `--golden-decor` hashes the same as it.
- `--golden-creatures` differs only in four "what the model hears" lines.
  That hash takes each creature's raw `sp_mode` byte, and the enum was
  renumbered (the four values removed). With a temporary map back to the old
  numbers, every hash matched.
- So every frame, every state line and the save are untouched.

**Then the reactions came back as double-tap tricks (the keeper's ask, the
same day).** The seahorse grips a stem, the crab raises its claws and
scuttles, the lobster flips its tail, the jellyfish pulses away, the eel
sparks. With the puffer's puff and the inkers' ink, 8 of the 13 kinds now
have a trick.
- **The poke knows the finger.** `tank_poke(t, idx, x, y)` and the row's
  `poke(t, idx, f, x, y)`; the platforms pass the press point (one word
  each).
- **A getaway is the creature's own.** `fish_t.flee_x/flee_y/flee_t` holds
  where the finger landed and the seconds left; the tank-wide startle
  cooldown is gone. While `flee_t` runs, `sp_flee_steer` and the row's
  `flee` hook steer it: the seahorse to its stem, the crab sideways (still
  in the display, fast in the scuttle), the jellyfish away at a hurried
  drift. `flee_t` per poke: the seahorse's grip 6 s, the crab's 2 s, the
  jellyfish's 4 s.
- **The lobster's flip** moves by itself (`lobster_move`/`sp_flip`, from
  HEAD). It faces the finger and shoots backward from it.
- **The eel's spark** runs 1.3 s, the idle flourish's length; the startle's
  was 0.6.
- **Restored from HEAD:** `crab_state`, `jelly_state`, the seahorse's
  `SPM_CLING` hold, the art (the crab's raised claws, the lobster's tail
  curl and `lean_flip`) and the sheet's two poses. `SPM_*` is back in its
  old order, so no number changed.
- **Tests:** `--selftest-species` pokes each one. It checks:
  - the seahorse holding at 4 s and let go by 8 s
  - the crab's claws at 1.00 and 71 px away
  - the lobster 92 px backward, tail curled, back on the floor
  - the jellyfish's 5 pulses in 4 s, 59 px away, no ink
  - the eel's spark

  `--selftest-creatures` counts 8 kinds with a poke (each changes its
  creature).
- **Golden:**
  - against the startle-free baseline above, `--golden-decor` is the same.
  - `--golden-creatures` differs only in the sheet's `states_2` frames (the
    two poses back) and from `run06` on (90..105 s: the run pokes every
    creature at 95 s, and five more now do something), the night and the
    save. Every line before the pokes, the state hashes too, is the same.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | the settings page (NEVER) and the care page looked at | yes (5.4.4, throwaway key, no warnings): app 0x201000, `.dram0.bss` 58,928 (58,632 without the startle; +296 for the tricks: `fish_t`'s three getaway floats) | **yes, 2026-10-28 20:40** (`tools/flash.sh`, app only; archive docs/batlog/2026-10-28_2040.txt; with the FEED tool, the entry below) | not yet by the keeper; the serial log after boot: 3 creatures, 13.2 tok/s, heap 40 KB, no errors |

**Open.** The keeper's TODO "no startle by tap or shake" is done (with the
shake, earlier today).

## 2026-10-28: a FEED tool; bare hands only reach the creatures

**What changed (the keeper's asks).**
- **FEED is the main menu's sixth tile** (the empty cell before),
  `TOOL_FEED` and `MENU_TAP_FEED` (appended to the enum).
  - With FEED in hand a tap anywhere drops a portion of three at the surface
    over it (`tank_touch_tap`). It also restarts the tool's idle clock and
    counts toward no light or startle. Its strokes wipe and cut nothing.
  - Its chip and tile wear the first-feeding badge's can
    (`icon_ms_first_feeding`) until FEED has its own art; new art needs
    gen_icons.py, i.e. Pillow. `tool_icon()` picks a tool's art, and the
    chip's width follows the art (the can is 32 px, the tools' 24).
  - `render_menu_tool(tap)` maps a tile to its tool, so both platforms route
    all three tools with one call. With a tool in hand, a tap that isn't the
    chip's DONE now goes to `tank_touch_tap` (FEED feeds; the others still
    do nothing).
- **Bare hands no longer feed, wipe or cut.**
  - `tank_touch_drag` only wipes with the SPONGE and only cuts with the
    SCISSORS.
  - `tank_touch_tap` no longer feeds at the surface. `FEED_ZONE_Y` left the
    board header and device.h.
  - The platforms' drag down from the top no longer feeds (sim/main.c,
    touch_port_ft3168.c).
  - Bare hands' own slash rules are gone: arming on a frond, `SLASH_PX`,
    `SLASH_RATIO`. The wall-frond run-on stays for the scissors.
  - Still with bare hands: a tap on a creature or helper (its card), a hold
    (the trusting come over), the double tap (the light, in MANUAL), the
    triple tap (the startle, the TODO's other half), and the hold on a piece
    (its placement page).
- **The words that taught the old gestures:** the first run's CARING FOR
  THEM page (six lines, "THE MENU HOLDS YOUR TOOLS: FEED, SPONGE AND
  SCISSORS"), and the fry checklist's FEED and GLASS tips (they name the
  menu's FEED and SPONGE). Also README (the menu, Upkeep, "The tools", the
  sim's mouse), docs/AUDIO.md's feed cue, the sim's usage text and the
  touch port's header.

**Tests.**
- `--selftest-tend`:
  - The two "a scrub begun mid-glass never cuts" tests are now one bare-hands
    test: sweeps glass to glass along the floor and up a fouled glass wipe
    nothing and cut nothing, and a surface tap feeds nothing.
  - The cutting precision tests (the flick, the outer blade by the glass,
    the lone wall frond) run with the SCISSORS: the rules are the cut's.
  - The bed mow takes the scissors and the glass squeegee the sponge. That
    squeegee now fouls the glass first: the night's film used to be wiped by
    bare hands' trimming tests, which earned "first glass cleaning".
  - New FEED block: the sixth tile is `MENU_TAP_FEED` (and
    `render_menu_tool`); a tap mid-water drops three pellets at the surface
    over it and counts no tap; its floor sweep wipes and cuts nothing.
- `--selftest-shop`'s colony and mowing chores use the sponge and the
  scissors. `--selftest`'s surface-tap meal is the FEED tool's.
  `--selftest-menu` expects FEED. `--selftest-species`' startles tap the
  creature itself (they kept below the feed zone before).
- **Proof:** both golden oracles hash the same as HEAD (15832b6). They tap
  mid-water and call `tank_feed` directly, so the tank's frames, what the
  model hears and the save are untouched.
- The snapshot mode also pictures FEED's chip (`_tool_chip_feed`). README's
  sim-menu.png is the new menu.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | the menu, the care page, both tool chips looked at | yes (5.4.4, throwaway key, no warnings): app 0x201000 | yes, 2026-10-28 20:40 (with the entry above) | not yet by the keeper |

**Open.**
- FEED's own art (`assets/icons/tool_feed.png` and gen_icons.py).
- The triple tap's startle: the other half of the "no startle by tap or
  shake" TODO.

## 2026-10-28: the milestones page's lists scroll

**What changed.** The keeper tried the shop on the glass ("a bit stiff
but okay for now"). The milestones page is second on the scroll helper.
- **The creature rows** are one vertical list from the page's top to above
  the TANK row's rule (`MSP_LIST_Y0`/`Y1`). Six rows fit whole as before.
  More scroll, with the bar at `PAGE_BAR_X` (renamed from `SHP_BAR_X`: one
  bar column for every page's list).
  - The portraits draw on the frame, not through the page's clip, so after
    the rows the bands outside the view are painted with the page's ink
    again before the TANK row draws.
  - Gone: the chevron, its pips, `MSP_FPG_Y`, `fish_pages` and `g_ms_fpage`.
- **The TANK row's badges** scroll sideways in a clipped view from the first
  badge out to `MSP_TB_X1` (436, the board's; replaces `MSP_TPG_X`). Six fit
  and a seventh (the shrimp school's, the reef's) peeks in at the right
  edge. A bar runs under the row.
  - Gone: the arrow, its pips, `tank_pages`, `g_ms_tpage` and `MSP_PER_ROW`.
- **The pagers' "unseen" ring** became a teal mark on a bar's end, shown
  while a row or badge with a badge not seen yet lies wholly past that end.
  `scroll_bar` now draws down a column or across a row.
- **Modals:** a modal opened on a row or a badge, or stepped to one by its
  arrows, scrolls it into view (`ms_show_row` / `ms_show_tbadge`, the least
  scroll that shows it whole).
- **The platforms** call `render_milestones_touch(t, x, y, down, now_ms)`
  every frame the page is up. Its tap path checks `scroll_took` for both
  lists.
  - Gone: `render_milestones_swipe` and its release-time branches in
    sim/main.c and touch_port_ft3168.c.
- **For the tests:**
  - `render_milestones_row` returns whether the row is whole in view (it no
    longer assumes a page).
  - New: `render_milestones_show_row`, `render_milestones_tank_badge`,
    `render_milestones_show_tank_badge` and `render_milestones_scrolls`.
  - Gone: `render_milestones_pager` and `render_milestones_fish_page`.
  - The sim's `ms_stroke` / `ms_idle` / `ms_press_tap` drive the page's
    touch at the device's poll rate.
- **Tests:**
  - `--selftest-pop` (a full tank of 25): a drag's distance and that its
    release opens no card; flicks to the last row, whose card opens; a
    card's arrows bring row 0 back into view.
  - `--selftest-shop`'s shrimp block, without shrimp: the row doesn't
    scroll, and a drag or a tap past the sixth badge does nothing.
  - With shrimp: a tap on the seventh's peeking edge opens it and scrolls
    it in; a drag along the row brings it in without moving the creature
    rows; a sideways drag on a creature row moves nothing; the modal's
    arrow from the sixth to the seventh brings it in.
  - The reef block asks `render_milestones_scrolls` instead of tapping the
    arrow.
- **Pictures:**
  - README's sim-milestones.png (it was stale: the old SETTINGS and
    UPGRADES buttons), sim-shop.png, sim-shop2.png and sim-shop-species.png
    are the new snapshots.
  - README, docs/species.md and docs/progression.md describe the lists.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | the milestones snapshots looked at (a full tank at the top and scrolled; the shrimp row peeking and scrolled) | yes (5.4.4, throwaway key, no warnings): app 0x201000, `.dram0.bss` 58,728 (+72: the two lists' state) | **yes, 2026-10-28 19:28** (`tools/flash.sh`, app only; archive docs/batlog/2026-10-28_1928.txt; with the fades and the shop's foot) | not yet by the keeper; the serial log after boot: 3 creatures, 10.4 tok/s, heap 40 KB, no errors |

**Then the edges fade (the keeper's ask, the same day).**
- **`list_fade`** (render.c, beside `scroll_bar`): where a view cuts its
  list, with more of the list past that edge, the last 14 px
  (`LIST_FADE`) blend into the page's ink. The foot fades at the start,
  both edges along the way, and the head at the end. It runs down a column
  or across a row.
- **Where:** the shop's list, the milestones rows, and the TANK row's
  badges (across: the peeking seventh badge fades at the right).
- **A list that fits shows neither fade nor bar.** That was already so for
  the bar (`s->max <= 0`). A new check in `--selftest-shop`'s no-shrimp
  block reads the page's pixels on a small tank: the TANK row's bar band
  and both its view's edges, the rows' bar column and their view's foot are
  all the page's ink.
- README's four pictures were taken again with the fades.

**And the shop lost its header (the keeper's ask, the same day).** The 64 px
coin, "SAND DOLLARS" and the balance at 4x are gone. The list runs from the
page's top (`SHP_ROW_Y0` 98 -> 9, so `SHP_LIST_Y0` is 0: about five and a
half rows in view instead of four). The balance is at the foot: the
milestones page's 32 px coin (`icon_ms_sand_dollar`) with the number to its
right at 2x, the pair centred between HOW TO EARN and CLOSE (`SHP_COIN_Y`).
`SHP_COIN_X`/`SHP_HEAD_X` left the layout header. `icon_shop_sand_dollar_64`
is now drawn by nothing, but it stays in the generated icons.c until the
icons are regenerated (gen_icons.py needs Pillow, which this machine lacks).
The snapshot taps that named row 98 in pixels ask `SHOP_ROW_Y` now. README's
shop pictures were taken again.

**Open.**
- Next on the helper: the settings page once it outgrows the screen, a fish
  card that does, and the updates page's network list (it still pages with
  arrows, `UPD_ARROW_X`).

## 2026-10-28: a scroll helper; the shop's list scrolls

**What changed.** The keeper asked for swipes and scrolling in place of
fixed pages and page buttons. The plan is one helper for the shop, the
milestones page, the settings page and a fish card that grows too big. The
shop is first.
- **`common/scroll.c`/`scroll.h`:** one list's scroll, with no drawing. A
  page keeps a `scroll_t` and hands it the finger every frame: the
  coordinate along the list, down or not, whether the press is on the
  list, and a millisecond clock.
  - A press becomes a drag past 10 px. At the lift the list flings at the
    finger's speed and slows (friction e^-3.5 a second).
  - Past either end it follows a third of the finger and springs back.
  - A press on a moving list only stops it.
  - `scroll_took` says the stroke was the list's, so its release is no tap.
    The platforms still classify taps as before (under 350 ms, under
    24 px), and the page's tap call asks the helper.
  - **Why the clock:** the device polls touch three times a frame and the
    sim once, and a fling must go as far on both.
- **The shop:**
  - All 22 rows are one column, drawn at `-scroll_px` in a clipped context
    (`ctx_clip`) between the header's rule and the foot (`SHP_LIST_Y0`/`Y1`,
    `SHP_LIST_H` in render.h). The context already carried a clip box.
  - A 3 px bar at `SHP_BAR_X` (the board's layout header) shows the view's
    place, brighter while it moves.
  - Gone: the header's arrows, `SHP_PER_PAGE`, `SHP_PAGES`, `SHP_ARROW_*`
    and `SHP_ARROW_IN`.
  - New calls:
    - `render_shop_touch(x, y, down, now_ms)`: the platforms call it every
      frame while the shop is up. It is one line each in sim/main.c and
      touch_port_ft3168.c.
    - `render_shop_row(item, &x, &y)` and `render_shop_show_item(item)`:
      for the tests, so they ask where a row is instead of counting pages.
- **Tests:** a new block in `--selftest-shop` drives the gesture at the
  device's poll rate. It checks:
  - a drag's distance, and that its release opens nothing
  - a flick that carries on, and flicks to the end that settle with the last
    row tappable
  - the rubber band past the top and its spring back
  - a press that stops a fling (and is no tap)
  - a still tap that still opens a row

  The old arrow taps became `render_shop_show_item`, so the rows' offsets
  are unchanged.
- **Docs:** README's shop paragraph and docs/species.md describe the list.

**Cost.** The pages were already redrawn whole every frame over the tank,
so a scrolled list draws as much as a page did. The helper is about 80
lines and a few dozen bytes of RAM.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | the shop's snapshot pages looked at (top and scrolled) | yes (5.4.4, throwaway key, no warnings): app 0x201000 (unchanged), `.dram0.bss` 58,656 (−440 from the shake's removal: `fish_t` lost its toss) | **yes, 2026-10-28 19:00** (`tools/flash.sh`, app only; archive docs/batlog/2026-10-28_1900.txt; with the shake's removal) | not yet by the keeper; the serial log after boot: 2 fish (the save kept), resting at night, 13.1 tok/s, heap 40 KB, no errors |

**Open.**
- Next: the milestones page's creature rows (vertical; now a chevron and a
  sideways swipe) and the TANK row's badges (sideways; now an arrow).
  Then settings (it fits today, so it only needs the helper once it grows)
  and the fish card.
- On the glass: the feel. The slop (10 px), friction, spring and catch
  speed are `SCROLL_*` at the top of scroll.c.

## 2026-10-28: no shake (half of the "no startle by tap or shake" TODO)

**What changed.** The keeper's call was to delete all of it rather than just
unhook the trigger. It is in git history if Play ever wants a deliberate
shake. Nothing of it was in the save or the model's schema. Removed:
- **The firmware:** the IMU's shake detector (`imu_port_shaken`,
  `SHAKE_THRESH`/`COUNT`/`COOLDOWN_US`) and main.c's call. It was
  hand-rolled from raw QMI8658 samples: three polls in a row each with about
  1.8 g of change. The chip's own motion engine was never used. The IMU's
  flip and "handled" logic is untouched.
- **`common/`:** `tank_shake`, `TOSS_S`, the creatures' tumble
  (`fish_t.toss*`, `toss_tick`), the helpers' `shake` hook and
  `helper_shake_all`, the shrimp's scatter on a shake, and the frogman's
  and the sub's tumble (`drift_t.toss/vx/vy`). `frogman_init` is just
  `tank_frogman_place` now.
- **The sim:** the 1 key, the shop selftest's shake block, and the shakes in
  `--selftest-helpers`, `--selftest-creatures` and both golden oracles.
  `--golden-decor`'s `t0_cache_shaken` frame is now `t0_cache_later`.
- **The docs:** README's shake paragraph is now a short removal note, and
  CLAUDE.md's helper-hook list no longer has `shake`.
  docs/refactor-plan.md is history and still mentions it.

**Proof.** The baseline was HEAD (73b283e) with only the oracles'
`tank_shake` calls deleted. Both golden oracles from the new build hash
identically to it, so with no shake every frame, state line and save is
unchanged. All 118 snapshot pages are identical.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | snapshot pages identical; board_sheet.py not run | yes (5.4.4, throwaway key, no warnings): app 0x201000 | yes, 2026-10-28 19:00 (with the scrolling shop, the entry above) | not yet by the keeper |

**Open.** The tap half of the TODO: the triple tap that startles the
creatures.

## 2026-10-28: pushed; the keeper's TODOs so far

The keeper looked at the flashed 1.8 ("it all looks good"). `main`
(b424c4d) and the tag `multi-board-multi-theme` are pushed to
`origin` (aerothopter/aqua-pets). The local refactor branches are deleted;
`origin/refactor/helpers` is still on GitHub (delete it when the keeper says).

The 2026-10-23 TODOs, where they stand:
- [ ] **Docs:** comb through all the documentation and code comments for
  accuracy and style.
- [x] **One board for now** (2026-10-26, flashed 2026-10-28). The round and
  watch boards are at the tag.
- [x] **One theme for now** (2026-10-26, flashed 2026-10-28). The other
  three themes are at the tag.
- [x] **The menu over the tank** (2026-10-28, flashed). Also: no "MENU"
  title.
- [ ] **Solo mode:** a tank of one creature, with more interactive features.
- [ ] **No startle by tap or shake:** remove the triple tap and the shake.
- [ ] **Trust:** make an untrusting creature and a trusting one differ much
  more.
- [ ] **Play:** more games and interactions between the creatures and the
  keeper.
- [x] **Battery** shows with the menu, not the fish card (2026-10-28,
  flashed).

Done along the way (the keeper's asks, 2026-10-28): the settings page has
no title, UPDATES and RESET are a row under ROTATION, and the firmware
builds with no warnings.

**Board status.** 1.8 rect: as in the entry below (checked, snapshot
pages pictured, built, flashed 2026-10-28 14:17, looked at on the glass by
the keeper).

## 2026-10-28: the menu over the tank; the battery pill with the menu; settings rearranged

**What changed** (two of the keeper's TODOs, then the keeper's settings asks):
- **No titles.** The menu's "MENU" and the settings page's "SETTINGS" are
  gone (the keeper: the settings title didn't match the other panels).
- **Settings: UPDATES and RESET are a row under ROTATION**, two wide buttons
  from the labels' column to the controls' right edge, RESET with its red
  edge. The foot keeps ABOUT (bottom left) and CLOSE (bottom right). The
  rows moved up into the title's room with a tighter pitch (amoled18_layout.h:
  rows 26 / 72 / note 108 / 136 / 182 / 228, row 6 at 274, foot 312; 6 px
  between rows). `SET_TITLE_Y` and `SET_ROW5_END` are gone; `SET_ROW6_Y` and
  `SET_UPD_Y` are new. A tap in row 6's band is UPDATES left of the gap,
  RESET right of it; the foot's band is ABOUT or CLOSE.
- **The firmware builds with no warnings** (it had 3 deprecation warnings).
  main.c's async memcpy config sets `dma_burst_size = 64`, the same union
  field `psram_trans_align = 64` set (IDF 5.4.1 in CI and 5.4.4 here both
  have it); `sram_trans_align` was read by nothing (the alignment comes from
  the GDMA channel). touch_port_ft3168.c reads the 1.8's touch with
  `esp_lcd_touch_get_data` instead of `get_coordinates`. It returns ESP_OK
  with no finger too, so the point count (both drivers set it from the chip
  on every read) is what says touched.
- **The main menu stands over the live tank**, the way the fish card does.
  `render_menu` no longer fills the frame. Each tile is a pane in the card's
  look: the card's ink blended at 215 over the water, a teal edge, and the
  tool in hand lit as before. The firmware already drew the tank before the
  menu; the sim now does too. Announcements and the "+N" toast wait while
  the menu is up, as they wait for any page.
- **The battery pill shows with the menu, not with the fish card.** It still
  shows while the charge is low and for a few seconds after the cable goes
  in. A tap on it from the menu is `MENU_TAP_BATTERY`, so the platforms still
  only route: the menu closes and the battery page opens (any tap closes it,
  back to the tank). The pill's tap zone overlaps the SETTINGS tile's top,
  so in the menu a tile always wins and only an off-grid tap is the pill's.
- `--selftest-menu` checks both: the frame off the tiles is untouched, a
  tile's pane shows what is behind it, a tap on the pill is the battery's,
  and the SETTINGS corner under the pill's zone is still SETTINGS.
- The snapshot mode draws the menu over the tank, and the pill pictures with
  the menu. README's menu picture (docs/media/sim-menu.png) is the new
  snapshot.

**Proof.** Both golden oracles are unchanged. Of the 118 snapshot pages,
the menu, the six pill pictures and the two settings pages changed as
intended (looked at side by side); the updates pages changed only in their
build line. The settings selftests tap UPDATES, RESET, ABOUT and CLOSE where
the layout now puts them.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | snapshot pages as above; board_sheet.py not run | yes (5.4.4, no warnings): app 0x201000, `.dram0.bss` 59,096 (both unchanged) | **yes, 2026-10-28 14:17** (`tools/flash.sh`, app only; archive docs/batlog/2026-10-28_1417.txt) | not yet by the keeper; the serial log a minute after boot: 2 fish (the save kept), deciding at 11.2 tok/s, heap 40 KB, no errors |

This flash also put the one-board / one-theme strip (2026-10-26) on the
glass for the first time.

**Open.**
- On the glass, check that the pill is easy to hit beside the SETTINGS tile, and that the
  tiles read well over a busy tank, and that the settings rows (now 6 px
  apart) are still easy to hit; BRIGHTNESS's arrows sit 16 px from the top.

## 2026-10-26: one board and one theme for now (two of the keeper's TODOs)

**What changed.** The keeper's calls: delete (not just hide) the round and
watch boards and the three newer themes, tag first, keep the extensible
seams, and trim CI to the 1.8. On branch `strip/one-board-one-theme`, two
commits:
- The annotated tag **`multi-board-multi-theme`** (a48a503, local, not
  pushed) holds everything removed. To bring a board or a theme back,
  restore its files from the tag. docs/BOARDS.md and the top of
  docs/THEMES.md list what went and what stayed.
- **One board** (47a65dc). Gone: the round175c/watch206 device and layout
  headers, the bowl's and the watch's glass bodies, the ports'
  compile-time `TANK_ROUND`/`TANK_WATCH` branches, the Kconfig options,
  `sdkconfig.round`/`.watch`, `flash_round.sh`/`flash_watch.sh`, the
  Makefile's `ROUND=1`/`WATCH=1`, and their CI builds and installer
  entries. Kept: device.h's selector (an `#error` for the old flags names
  the tag), the layout header, `DEVICE_GLASS`/`GLASS_CURVED` (glass.c errors
  on a shape it has no body for), `DEVICE_WORN`, the `BOARDS` lists in
  pt_boards.py and board_sheet.py, and `make check-all` (= `check` for
  now). The 1.8 image's runtime handling of the 1.75C and 2.06 panels
  stays, because it is part of what the 1.8 image runs.
- **One theme** (69137be). Gone: every themed branch of the art, the
  themed castle's front mask, `theme_assets.c`, blackwater_*.inc, the
  picker (`SET_TAP_THEME`), the about page's per-theme creatures, the
  themed naming wheel and the three other tunes. Kept: `theme_id_t` with
  the Original alone (IDs 1-3 reserved), the palette table, the hooks
  (`theme_ui_color`, `theme_creature_color`, `theme_icon`,
  `render_use_theme`), `J_SONGS` and the tests' loops over `THEME_COUNT`.
  The settings title is the plain "SETTINGS" it was before the themes.

**How it was proven pure.** A baseline sim and firmware were built from the
tag in a scratch worktree.
- Boards: both golden oracles and all 118 snapshot pages identical. The
  firmware's sections are the same size, with the same bytes apart from
  `__TIME__` and eight `__LINE__`s (ESP_ERROR_CHECK in display_port_init:
  17 lines were removed above it).
- Themes: both oracles identical to the tag's Original (`t0_`) lines and
  save hashes. 116 of 118 snapshot pages are identical; the other two are
  the settings page (its title, looked at side by side). The full sim build
  has the tag's same 5 warnings.
- `--selftest-themes` now checks that a save holding IDs 1, 2, 3 or 255
  loads as the Original with the tank kept.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | oracles and snapshot pages as above; board_sheet.py not run | yes (5.4.4, throwaway key): app 0x201000, was 0x271000 (20% of the slot free, was 2%); `.dram0.bss` 59,096 (-8) | no | no |
| 1.75C round | removed (at the tag) | - | - | - | - |
| 2.06 watch | removed (at the tag) | - | - | - | - |

**Open.**
- Not merged, not pushed: `strip/one-board-one-theme`, and the tag.
- On the next flash, a tank whose save holds a theme comes up in the
  Original. The theme is presentation only, so nothing else changes.
- A 1.75C or 2.06 tank on its own image gets no newer release until that
  board is back. The release now publishes `latest-amoled18.json` alone.
- jellyfish/wreck/frogman/sub `_assets.c` still carry their themed icon
  variants (unused, still in flash; not measured). Their generators need Pillow, which isn't
  installed; trim them when it is.
- `assets/themes/`, `docs/design/` and `tools/export_living_lagoon_art.cjs`
  stay as the themes' sources.
- Python 3.13 is on this machine now (no Pillow), so
  `tools/board_sheet.py` (standard library only) can run. It hasn't yet.
- The keeper's icon edits (`icon_bold.png`, `icon_cautious.png`) are still
  unstaged.

## 2026-10-23: the keeper's TODOs (end of session)

Next, in no fixed order (the keeper, at sign-off):
- **Docs:** comb through all the documentation for accuracy and style, both
  the docs files and the comments in the code.
- **One board for now:** temporarily remove support for the round and watch
  boards while features are built, keeping the standard 1.8. (Since phase 4
  a board is `common/devices/<board>.h` and `<board>_layout.h` plus its
  ports, sdkconfig and flash script.)
- **One theme for now:** temporarily remove the other themes while features
  are built.
- **The menu over the tank:** draw the main menu over the tank, not over a
  black screen.
- **Solo mode:** experiment with a tank of one creature, with more
  interactive features.
- **No startle by tap or shake:** remove the triple tap and the shake that
  startle the creatures. No keeper would do either except by accident.
- **Trust:** fine-tune the trust stat, so that an untrusting creature and a
  trusting one differ much more.
- **Play:** build more games and interactions between the creatures and the
  keeper.
- **Battery:** show it in the menu view, not with the fish card.

## 2026-10-23: the entity refactor merges to main

The keeper looked through the flashed 1.8 tank ("all looked well") and
called the refactor ready. `refactor/devices` (which carries
`refactor/decor`, `refactor/helpers` and `refactor/creatures`) was
fast-forwarded into `main`: 30 commits with this entry, phases 1-4 and the Windows flash
tools. Not pushed. The four refactor branches still exist and can be deleted.

Still open:
- The round and watch boards: built, not flashed.
- Back up `firmware/keys/ota_signing_key.pem` off this disk.
- The device's floats after 4b (the 4d entry).

## 2026-10-23: the 1.8 flashed from Windows (the refactor's first time on the glass)

**The flash tools run on Windows now.** They were the original owner's Mac
scripts: `/dev/cu.usbmodem*` ports, `~/esp/esp-idf/export.sh`, a
`/Users/strato/...` python.
- `tools/esp_env.sh` (sourced by flash.sh, flash_round.sh, flash_watch.sh
  and ota_key.sh) is the ESP-IDF setup. On a Mac it is export.sh, as
  before. On Windows it uses the environment of the ESP-IDF 5.4.4 PowerShell
  that Git Bash was started from:
  `. C:\Espressif\tools\Microsoft.v5.4.4.PowerShell_profile.ps1`, then
  `& "C:\Program Files\Git\bin\bash.exe" tools/flash.sh`. `idf.py` and
  `espsecure.py` become bash functions there.
- One trap: `idf.py` does nothing at all while `MSYSTEM` is set. It warns
  "MSys/Mingw is no longer supported" and never calls `main()`, so the exit
  code is 0 and nothing is built. Git Bash puts `MSYSTEM` back into every
  Windows program it starts, so the function has python drop it and then run
  idf.py.
- `tools/tank_ports.py` finds boards by Espressif's USB vendor id (0x303A).
  flash.sh and preflight.py use it on both systems. The rules stay: more
  than one board means name it with `TANK_PORT` (COM4 here), and preflight
  refuses a port that doesn't answer as the tank's director.

**The flash.** The keeper's calls:
- the 1.8
- a NEW signing key (`tools/ota_key.sh`; in firmware/keys, not in git, to be
  backed up off the disk)
- the old tank may start fresh

Done in order:
- `tools/preflight.py` (archive 2026-10-23_2056). It showed the tank as it
  was: skippy, mira, ripple, coral and pancho, 369 sand dollars, every shop
  item.
- A raw backup of NVS, otadata, ota_0 and ota_1 with esptool `read_flash`
  (to `~/.cache/aqua-pets/backup-2026-10-23_2056/`, not in git). The old
  tank can be put back by writing all four back.
- `tools/flash.sh --model` (archive 2026-10-23_2059): the v5j model and its
  trailer (matches version.h), the app (0x271000) and otadata, every write
  verified.

On boot the layout-2 save started a fresh tank (kelp and pip, two fry). The
model decides at ~10 tok/s, ~4.4 s a decision. The internal heap is 40 KB
free, with 4.6 MB of PSRAM. No errors in the first 35 s of the log.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes | as at 4d | yes (the new key) | **yes, 2026-10-23 20:59** | yes: the keeper looked through the tank, "all looked well" |
| 1.75C round | passes | as at 4d | yes (throwaway key) | no | no |
| 2.06 watch | passes | as at 4d | yes (throwaway key) | no | no |

**Open.**
- The layout 3 save test PASSED: a held PWR (on USB it powers straight
  back on) brought kelp and pip back at 0.1 h, older than the boot.
- Back up `firmware/keys/ota_signing_key.pem` off this disk. The tank only
  takes OTA updates signed with it now.
- The round and watch tools have the same Windows setup but haven't run here
  (no board).

## 2026-10-23: the layout headers (refactor phase 4d; phase 4 done)

**What changed.** A pure refactor, and the keeper's call: a layout header per
board, holding every page number that differs between boards.
- `common/devices/{amoled18,round175c,watch206}_layout.h`: the device header
  names it `DEVICE_LAYOUT`, and render.h and update.h include it. It holds:
  - the card, the tool chip, the battery pill and the toast
  - the milestones page's rows, badges, arrow and CLOSE
  - the shop's header and foot
  - the settings page's rows, segments, foot, version line and bounds
  - the update pages' buttons, password inset and calibration inset
- Both kinds of per-board choice became plain numbers per board: the
  `#ifdef` blocks and the flag ternaries (`PAGE_NARROW || DEVICE_PAGE_BOWL ?
  172 : 176`).
- render.c's literals (`render_settings_bounds`, CLOSE's slop, the toast's
  height, the version line) and update.c's three `CAL_X` / `CAL_Y` arrays
  became named numbers.
- `DEVICE_PAGE_BOWL` and `PAGE_NARROW` had no users left and went (with
  render.h's dead first `SET_CLOSE_X` and its `#undef`s). theme_test.c's bowl
  check asks `TANK_RAD`.
- The bowl's deep sand is keyed on `TANK_BOT < TANK_H`.

`TANK_ROUND` / `TANK_WATCH` now appear only in device.h's selector and the
firmware ports. A new board is a device header and a layout header (and its
ports).

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | oracles and snapshot pages identical | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | identical | yes | no | no |
| 2.06 watch | passes | identical | yes | no | no |

Firmware against 4c: every section byte for byte the same on each board.

**Phase 4, overall** (4a-4d, on `refactor/devices`). Every step was pure:
- both oracles and all 118 snapshot pages identical to 04b15e5's on all
  three boards
- the same sim warnings
- firmware RAM unchanged from cd65114 on every board; `.flash.text` down
  0.1-0.5 KB; the app still 0x271000

**Open.**
- Nothing is merged or pushed. The branches stack: `refactor/decor` ->
  `refactor/helpers` -> `refactor/creatures` -> `refactor/devices`.
- Not flashed. The first real flash (`tools/flash.sh`) is still the layout 3
  save test (2026-10-22 entries).
- The device's floats after 4b: glass.c's functions left their callers'
  files, so the device's inlining and `-ffast-math` contraction may differ.
  The sim can't show it.
- The ports keep their board `#ifdef`s (display_port_sh8601.c,
  touch_port_ft3168.c): the panels really differ. A port table would be its
  own piece of work, testable only on the boards.
- `tools/board_sheet.py` still hasn't run (no Python 3 here).
- The keeper's icon edits (`assets/icons/icon_bold.png`,
  `icon_cautious.png`) are still in the tree, unstaged.

## 2026-10-23: the bed table (refactor phase 4c)

**What changed.** A pure refactor. A device header's `DEVICE_BEDS` (beds 1
and 2: first frond, base frond count) and `DEVICE_REEF_BED_LESS` (the fronds
the reef bed gives up) are now the board's grass layout. tank.c's `VEG_BED[]`
holds them, and both `veg_bed_base` and `tank_veg_cap` read it: the ceiling
by the glass had its own copy of the beds' x. tank.c names no board now. The
ceiling still measures the reef bed at its widest from the reef's x as
`tank_init` sets it (`reef_x` never moves), so the two still agree.

**Board status.**

| board | `make check-all` | pictured | firmware built | flashed | on the glass |
|---|---|---|---|---|---|
| 1.8 rect | passes (18 selftests) | oracles and snapshot pages identical | yes (5.4.4, throwaway key) | no | no |
| 1.75C round | passes | identical | yes | no | no |
| 2.06 watch | passes | identical | yes | no | no |

Firmware against 4b: RAM unchanged, `.flash.text` -32 / -36 / -36 B,
`.flash.rodata` +16 B (the table), the app still 0x271000.

**Next:** 4d, the layout sections.

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
