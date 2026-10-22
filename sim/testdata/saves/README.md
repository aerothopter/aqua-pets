# Golden saves

Tank saves, frozen. `./fishsim --selftest-saves` (run from `sim/`) reads every
`*.sav` here by its magic:

- **Layout 3** (`ptk3-*.sav`, "PTK3", since 2026-10-21): it must load, and a
  tank re-saved from it must come back the same. This is the update promise
  from layout 3 on.
- **PTK2** (every other file, the layout from 2026-08-30 until 2026-10-21):
  whole and cut to each length it ever had, it must start a fresh tank,
  cleanly. Layout 3 replaced it (the decor refactor), and the keeper chose a
  clean layout over carrying the old one, so a tank saved before it starts
  over.

**Before every release, run the tank a bit and add its save here as
`ptk3-<date>-<size>.sav`.** Never edit or delete a file here: a layout 3 save
that stops loading is a bug in the code, not in the fixture.
`AQUA_PETS_SAVE_FIXTURE=<path> ./fishsim --selftest-saves` writes the test's
own tank (every field set) as a new fixture.

- From the sim (the same bytes the board writes): play a while, quit, then
  `cp ~/.cache/aqua-pets/tank.sav sim/testdata/saves/$(date +%F)-$(wc -c < ~/.cache/aqua-pets/tank.sav | tr -d ' ').sav`
- From the board: run `tools/preflight.py` first (reading the flash resets the
  chip), then `python -m esptool --chip esp32s3 read_flash 0x9000 0x6000 nvs.bin`
  and pull out NVS `tank`/`save` with ESP-IDF's parser:

      import sys; sys.path.insert(0, "<esp-idf>/components/nvs_flash/nvs_partition_tool")
      from nvs_parser import NVS_Partition
      p = NVS_Partition("nvs", bytearray(open("nvs.bin", "rb").read()))
      chunks = {e.metadata["chunk_index"]: b"".join(c.raw for c in e.children)[:e.data["size"]]
                for pg in p.pages for e in pg.entries
                if e.state == "Written" and e.key == "save" and e.metadata["type"] == "blob_data"}
      open("save.sav", "wb").write(b"".join(chunks[k] for k in sorted(chunks)))

What is here:

| file | from |
| --- | --- |
| `ptk3-2026-10-21-rich.sav` | layout 3's first: `--selftest-saves`' own tank, on the rectangle. 25 creatures (the classic fish and a pair of each species but the turtle) with names, ages and families; every decor piece at a depth of its own, including the seagrass and the airstone; the snail, urchin, shrimp, frogman and sub; the settings and 321 sand dollars |
| `2026-09-04-1304-device.sav` | the board's NVS dump of 2026-09-04 (4 fish, per-frond heights, before names) |
| `2026-09-13-1432-prebubble.sav` | the first public installer's layout: seen masks at 1404, no bubble_x (built from the 09-29 save) |
| `2026-09-24-1656-sim.sav` | Strato's sim tank as played on 2026-09-24 |
| `2026-09-29-1656.sav` | a synthetic tank with every tail set: 5 fish, names, a custom color, parents and a welcome still owed, every shop item placed, a colored coral, a cluster look, 271 sand dollars |
| `2026-09-29-1664-shrimp.sav` | the 1656 tank above, loaded and saved by the shrimp build: plus a school of 6 shrimp, 7 pellets toward the next, 321 s of cooldown |
| `2026-09-29-1672-v0.2.0.sav` | the release save for **v0.2.0**: the shrimp tank above loaded and saved by the 0.2.0 build (release stamp 0x000200 at 1664) |
| `2026-10-02-1688-urchin.sav` | the v0.2.0 save above grown to 1688 (built by hand from it: the watch's SCREEN byte 0, then the urchin tail): the urchin bought (unlock bit 6), at x 212, 4321.5 px of grass eaten |
| `2026-10-05-2096-species.sav` | the species' ten (built by today's progression.c from the urchin save: one of its five fish sold, a pair of crabs and of lobsters bought, two lobster fry born, a crab renamed and re-designed): 4 fish, 2 crabs, 4 lobsters - the core says 6 (what an older build loads, as six classic fish), fish 7..10 in the tail from 1692; the tenth still owed its welcome |
