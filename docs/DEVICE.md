# The device: what is in flight, and the rules for touching it

Read this before ANY action that resets the tank (flash, esptool, power-off). Battery measurements run alongside feature
work, and the tank's own battery log is the instrument.

## Rules

1. **Flash only through `tools/flash.sh`** (`--model` for the model partition too). It runs `tools/preflight.py` first, which
   archives `batlog` + `state` to `docs/batlog/<date_time>.txt`, and refuses to flash without the archive. Commit the archive
   with the flash.
2. **Any morning after a night on battery: `tools/preflight.py` first**, before the director, before a flash, before anything.
   Then update the table below.
3. **The log survives resets** (`batlog.c`: RTC_NOINIT + magic + crc), but a power-on or a PMIC power-off clears it, so the
   archive is the record between sessions.
4. **Every session that starts a measurement writes it in the table below, with what would spoil it.** Every session that ends
   one closes its row and moves the numbers to [stats.md](stats.md). The table is the handoff between parallel workstreams;
   HANDOFF.md carries the narrative, this file carries the state.

## In flight on the device

| started | what | spoiled by | status |
|---|---|---|---|
| | *nothing is in flight* | | |

## Measuring the battery

Everything needed is committed.

1. **A night.** The PWR key is the sleep and wake key: a short press saves, turns the panel off and starts a 20 minute grace,
   then the PMIC powers off. Unplug the tank, press PWR and leave it overnight. In the morning a HELD PWR press boots it (a USB
   plug-in powers it on too), then `tools/preflight.py` FIRST. The `bed` row from NVS and the boot row give the night's draw.
   The gauge's %-scale runs ~0.6x, so compare nights and don't trust the absolute.
2. **Awake draw.** Unplug at a known charge, leave the tank awake and untouched for an hour at 60% brightness, then preflight.
   The lever is the LLM's duty: the boot log's `t=...s N fish goals: ... | asks N decisions M` line. The knobs are
   `ADVISOR_MIN_INTERVAL` / `ADVISOR_IDLE_CEILING` (`common/tank.h`) and the `state_signature` bands (`common/tank.c`).
   `pmic` on the director dumps the rails.
3. Record the results in [stats.md](stats.md) and close the row.

## Firmware on the tank right now

**Flashed 2026-10-28 20:40** on the keeper's machine: `main` at a93c056, app only (archive `docs/batlog/2026-10-28_2040.txt`).

- **App:** save layout 3, the 1.8 alone and the Original theme alone.
- **Signed with the keeper's own key** (`tools/ota_key.sh`, `firmware/keys/ota_signing_key.pem`), not the one the public releases
  are signed with. This tank refuses any over-the-air image signed with another key until it is flashed by cable. Back the key up
  off this disk.
- **Model partition:** v5j with its trailer (written 2026-10-23 by `tools/flash.sh --model`).
- **The tank from before layout 3:** NVS, otadata and both app slots are backed up raw in
  `~/.cache/aqua-pets/backup-2026-10-23_2056/` on the keeper's machine (not in git). Written back together, they are that old tank.
