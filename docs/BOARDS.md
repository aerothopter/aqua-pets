# Boards: how a change reaches them

The rule for changing anything boards share. Read it before a feature, the way DEVICE.md is read before a
flash.

**There is one board for now: the Waveshare ESP32-S3-Touch-AMOLED-1.8**, 448 x 368 with round corners, the
reference. `make` in `sim/` builds its simulator and `tools/flash.sh` flashes it. The seams for others stay, so
features are written against the device's names, never against the 1.8's pixels.

## What is shared, and where a board may differ

One source tree, one save layout, one model. A board is a compile-time world:

- **The world** (`common/devices/<board>.h`, picked by `common/device.h`): `TANK_W` / `TANK_H`, the glass's shape
  (`DEVICE_GLASS`), the floor `TANK_BOT`, the walls `TANK_FX0` / `TANK_FX1`, the film grid, the grass beds, the board's
  id and its flag `DEVICE_WORN`. The game is written against these names.
- **The page** (`common/render.h`): every page is laid out in a 448 x 368 PAGE space that sits centered on the glass at
  `PAGE_X` / `PAGE_Y`. The creatures live in TANK coordinates and the pages draw in PAGE coordinates, so anything that
  crosses over subtracts `PAGE_X` / `PAGE_Y` (a tap coming in, a ring round a fish going out). Every page number that
  differs between boards is in the board's layout header (`common/devices/<board>_layout.h`, named `DEVICE_LAYOUT`);
  `render.h` and `update.h` hold the shared ones.
- **The ports** (`firmware/main/*_port_*`, `board_pins.h`): the chips.

A board `#ifdef` belongs in those places only: `device.h`'s selector and a port. The glass's shapes are `common/glass.c`'s,
picked by `DEVICE_GLASS`, and code that only needs to know the glass curves asks `GLASS_CURVED`. If a feature's LOGIC needs a
board's name, it was written against a pixel instead of a name.

## The rules for a feature

1. **Write it once, in `common/`.** The platforms only route: the page returns what was tapped (`MS_TAP_*`, `SHOP_TAP_*`,
   ...), and the sim's loop and the firmware's touch port do the same few lines with it. The route is written twice
   (`sim/main.c`, and `touch_port_ft3168.c` + `main.c`), so keep it to a call or two and put the behavior behind it. Sounds
   go through `tank_emit`, not through the platform.
2. **Lay new page elements out in the box every glass shows whole:** PAGE x 56..392, y 60..300 (the modal's box). A page that
   must use the corners gets a named number in the board's layout header (the milestones page's `MSP_*`), not scattered
   conditions.
3. **Give the layout a voice.** A test, the director or a tool that needs to tap something asks the page where it is
   (`render_milestones_card`, `render_milestones_row`, `render_shop_row`) or uses the layout's named constants with `PAGE_X` /
   `PAGE_Y`. A literal pixel in a test is a test for one board.
4. **A save field only ever appends** (`SAVE LAYOUT LOCK` in `common/progression.c`, save layout 3), and the save means the
   same on every board. A decor kind or a helper is a record, not a field.
5. **Nothing is done until every board says so:**

   | step | command (from the project root) | what it proves |
   |---|---|---|
   | the checks | `make -C sim check-all` | every selftest on every board's world |
   | the pictures | `tools/board_sheet.py --selftest-card` (or `--snapshot 20`) | the page on each glass, cut to the glass |
   | the firmware | `tools/flash.sh` | it builds; compare `.bss` with the last flashed build (internal RAM has ~23 KB of headroom) |
   | the glass | flash it (it has the preflight: [DEVICE.md](DEVICE.md)) | the keeper's finger |

   The handoff entry for a feature says where each board stands: checked / pictured / built / flashed / tried on the glass.
   "Not flashed" is a fine state; "unknown" is not.

## Releases

One image per board, one release number. The machinery takes a board as a row in `tools/pt_boards.py` and a build in each
workflow:

| piece | how a board is kept to its own image |
|---|---|
| the board's id | `PT_BOARD`, in the board's header, read through `version.h` |
| the image | a board marker right after the app descriptor (`net_port_esp.c` `pt_board_marker`, at `PT_BOARD_MARKER_OFFSET` 0x120; kept by `-u pt_board_marker` in `firmware/main/CMakeLists.txt`). `tools/pt_boards.py` reads it back from a `.bin` |
| over the air | a tank fetches `latest-<PT_BOARD>.json`; `common/update.c` refuses a manifest for another board (WRONG BOARD, never offered); the download reads the marker back from the slot at its first 4 KB and aborts on another board's (`NET_ERR_BOARD`). Boards share the signing key, so the signature alone would let one install another's image |
| the release | `.github/workflows/release.yml` builds the image; `make_ota_manifest.py` names both files from the image's marker (`latest-<board>.json`, `aqua_pets-v<rel>-<board>.bin`) |
| the installer | `installer.yml` builds it; `make_installer.py --build-dir <1.8> [--board-build <other>]`: the 1.8 keeps `manifest.json` / `manifest-erase.json`, another `manifest-<board>[-erase].json`, and a board's file that differs from the 1.8's is `<name>-<board>.bin`. The page shows a "Pick your board" step when it carries more than one |

The workflows publish only from `aerothopter/aqua-pets`. A cable install of the
wrong board's image is not caught on the board (the page's picker is the guard); it boots blank and the right install puts it
back, the save untouched.

## Bringing another board back

The Waveshare 1.75C (a 466 px circle) and 2.06 watch (410 x 502 portrait, 100 px corners) are at git tag
`multi-board-multi-theme`. Their hardware notes are there too:

```bash
git show multi-board-multi-theme:docs/board-amoled-1.75c.md
git show multi-board-multi-theme:docs/board-amoled-2.06-watch.md
```

Restore from the tag: the device and layout headers, the shape's bodies in `glass.c`, the ports' `TANK_ROUND` / `TANK_WATCH`
branches, the Kconfig options, `sdkconfig.round` / `.watch`, `flash_round.sh` / `flash_watch.sh`. Then put back the board's row in
`tools/pt_boards.py` and `tools/board_sheet.py`, its build in `sim/Makefile`, and its builds in the workflows. The 1.8's image still
knows those panels at runtime (it fits or centres its frame on them).
