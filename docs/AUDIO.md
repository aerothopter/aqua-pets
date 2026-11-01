# Audio

Keeper feedback (food dropped, a frond cut) and a few creature moments (a pellet eaten, a birth). **Sounds are feedback, never
behavior:** the advisor owns every decision and no sound changes what a creature does.

## How it fits together

- **The events.** `tank.c` emits a `TEV_*` (`common/tank_events.h`) the moment something audible happens and carries on. Whoever
  registered a listener turns it into a cue; no listener means nothing happens, so tests and QEMU need not care.
- **The mixer** (`common/audio.c`) is pure C, shared by the device and the sim, so pitch and cooldowns behave the same on both. The
  tank side only writes a voice slot, the output side mixes, and the platform serializes the two.
- **The bank** (`assets/sounds/sounds.bin`) is every clip's 16 kHz mono s16 samples back to back, with `common/sounds.h` / `sounds.c` as
  the generated index. The firmware embeds it (`EMBED_FILES`, in flash-mapped `.rodata`, ~530 KB); the sim reads it from disk.
- **The ports:** `firmware/main/audio_port_es8311.c` (I2S → ES8311 → NS4150B → the 12 mm speaker), `audio_port_stub.c` (QEMU and
  bring-up), and SDL in `sim/main.c`.

## Cues

Tiers set the night rule (below).

| tier | cue | when |
|---|---|---|
| 1 keeper | `feed` | the FEED tool's tap: once per drop, not per pellet |
| 1 | `light_on`, `light_off` | the light flips (with AUTO: a pick-up or touch after the idle time, and going idle) |
| 1 | `snip` | a frond cut, rate-limited 150 ms |
| 1 | `card_open` | a card comes up |
| 1 | `wheel_tick`, `confirm` | the letter wheel's detent; BEGIN, DONE, a purchase, a sale |
| 1 | `bubbles_loop` | the setup's bubble-placement page is up: the one looping voice, fading over 50 ms when the page is left |
| 2 creature | `eat` | a creature takes a pellet: pitch by stage, at most one per 250 ms |
| 2 | `investigate` | a long hold draws a creature over, once per hold |
| 3 system | `welcome` | the first-run setup's welcome page, a fresh install or a reset |
| 3 | `arrival` | a fry's birth flow |
| 3 | `milestone`, `stage_up` | with the announcement ([notices](#notices)) |
| 3 | `low_battery` | the gauge falls to 10%, once per discharge |
| 3 | `error` | a save failed, the reset prompt |

**Ids with no cue** keep their enum slot and play nothing: `tap`, `wipe`, `card_close`, `sleep`, `wake` have no clip, and `beg` and `bubbles`
have files that aren't shipped (`DEFERRED` in `tools/make_sounds.py`). `spook` is in the bank, but nothing plays it. Sand dollars are silent.

**Not in scope:** music, ambient loops, a per-decision sound (constant noise), any sound in the dark tank's sleep.

## The mixer's rules

Enforced in `common/audio.c`, so no caller has to (`audio.h` has the contract):

- **Rates:** per-cue cooldowns, and a global cap of 6 starts a second. Excess is dropped, never queued late.
- **Night:** tier 2 is muted, tier 1 plays at -12 dB, tier 3 plays as it is.
- **Volume:** 0 off, 1 quiet (-12 dB), 2 normal, over `AUDIO_MASTER_DB`. Set on the settings page, by the director (`snd off|quiet|normal`) or the
  sim's V key; saved in NVS (`tank/snd`).
- **Voices:** 4, and every voice fades in and out over 5 ms. A looping voice wraps at a crossfaded seam.

## Notices

`common/notice.c` queues the announcement modals, in the milestones page's style: a milestone the moment it is earned, a stage reached, low
battery, the first boot of a new release. One is up at a time for 6 s or until a tap, and each carries its cue. Notices wait for the setup and
birth flows and the reset prompt, and they do not mark a milestone seen: the ring on the page stays until the keeper visits.

**Low battery** fires at 10% and not charging, once per discharge, and the pill stays on screen until the gauge reads above 10% for 30 s or the
cable goes in. The AXP2101's %-scale is not linear, so 10% is the gauge's word for it.

## Hardware

| Part | Detail |
|---|---|
| Codec | ES8311, I2C 0x18, mono DAC + ADC |
| Amp | NS4150B class-D, enable = **GPIO46** (`PA_CTRL`, 10k pulldown: off unless driven high). A strapping pin, sampled only at reset |
| Speaker | 12 x 10 mm, 8 ohm, 1 W, soldered to the board: nothing below ~600 Hz |
| Mic | analog, into the ES8311 ADC (unused) |
| I2S | MCLK **GPIO16**, BCLK **GPIO9**, LRCK **GPIO45**, DAC data **GPIO8**, ADC data **GPIO10** |
| Rails | codec digital + amp on VCC3V3 (always on); codec AVDD + mic on A3V3 = **ALDO1** |

## Power

The amp and the codec are the one way this feature could wreck the battery work (a stuck amp costs 3 mA).

- **The amp is on only while a sound plays,** plus a short idle window.
- **The codec is fully down and ALDO1 off before any sleep** (`audio_port_sleep`).
- **No frame cost:** the tank task only pushes a cue; mixing and DMA run on core 1, at priority 6.
- **The sequence.** First cue after silence: ALDO1 on → ES8311 up → I2S with zeros ~20 ms → GPIO46 high → mix, under 60 ms from event to sound. 2 s
  after the last voice: GPIO46 low → I2S stop → ES8311 down → ALDO1 off. The amp comes on only after the DAC is streaming zeros and goes off before the
  clocks stop, so there are no pops. The codec stays up through a setup or birth flow, because `wheel_tick` is the cue that would show latency.

`director codec` shows the registers. A check after any change here: every cue plays with no click at each volume, the codec is down and GPIO46 low within
3 s of the last cue, fps is unchanged with cues firing, and a night on battery is within noise of the last.

## The about page's jingle

The one long sound, not in the bank: a 30 s clip would be 960 KB of flash. `audio_jingle(on)` runs a small synthesiser inside the mixer while the ABOUT page
is up (`SET_TAP_ABOUT`). Twelve bars of 4/4 at 96 BPM (a 16th is exactly 2500 samples, so the loop is 480,000 samples and wraps on a sample): a kalimba-like
arpeggio, a hummed melody, a soft pad and bubble chirps under a slow wobble and a low-pass, all above the speaker's floor. Each theme has a tune, a row of `J_SONGS`
on the same four voices.

## Making sounds

`tools/make_sounds.py` is the pipeline. `import <dir>` turns exports into the canonical `assets/sounds/<cue>[-<n>].wav`, and `build` makes the bank and `common/sounds.*`.

- **Format:** any WAV, any rate and depth; prefer mono, since wide stereo loses level when folded. Files are named `sound-<cue>.wav`. A trailing `_v<N>` is a revision
  (the highest wins), and a trailing `-<N>` is a variant (the player picks one at random, which keeps frequent sounds from annoying).
- **Short:** most cues 60 to 400 ms, the longest under 2 s, `wheel_tick` under 40 ms. A real fade to zero at the tail: a hard cut on a 12 mm speaker clicks.
- **Design for 800 Hz to 6 kHz.** The tool high-passes at 600 Hz and low-passes at 7 kHz, and a cue with its energy below that comes out faint. It also trims silence
  below -60 dBFS, adds 5 ms fades and peak-normalizes.
- **Loudness** lives in `assets/sounds/gains.csv`, one line per cue, so it is tuned without a re-export.

A new cue is an entry in `CUES` in `tools/make_sounds.py` (at the end: the order is the enum) and a call where the moment happens, through `tank_emit`.
