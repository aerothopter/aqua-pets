# Golden saves

Tank saves, frozen. `./fishsim --selftest-saves` (run from `sim/`) reads every
`*.sav` here by its magic:

- **Layout 3** (`ptk3-*.sav`, magic "PTK3"): it must load, and a tank re-saved
  from it must come back the same. This is the update promise.
- **PTK2** (every other file): whole and cut to each length it ever had, it must
  start a fresh tank, cleanly.

**Before every release, run the tank a bit and add its save here as
`ptk3-<date>-<size>.sav`.** Never edit or delete a file here: a layout 3 save
that stops loading is a bug in the code, not in the fixture.
`AQUA_PETS_SAVE_FIXTURE=<path> ./fishsim --selftest-saves` writes the test's
own tank (every field set) as a new fixture.

- From the sim (the same bytes the board writes): play a while, quit, then
  `cp ~/.cache/aqua-pets/tank.sav sim/testdata/saves/ptk3-$(date +%F)-$(wc -c < ~/.cache/aqua-pets/tank.sav | tr -d ' ').sav`
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

| file | what it holds |
| --- | --- |
| `ptk3-2026-10-21-rich.sav` | `--selftest-saves`' own tank: 25 creatures (the classic fish and a pair of each species but the turtle) with names, ages and families; every decor piece at a depth of its own, including the seagrass and the airstone; the snail, urchin, shrimp, frogman and sub; the settings and 321 sand dollars |
| `2026-*.sav` (the rest) | PTK2 saves of every length and feature that layout had: a board's NVS dump, a sim tank, a synthetic tank with every tail set, the shrimp, the urchin, the species |
