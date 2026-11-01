# Progression

The layer that makes a tank grow over weeks: population, growth, trust, chores,
milestones, sand dollars and the shop. The code is `common/progression.c` (the
rules and the save) over `common/tank.c` and `common/creature.c` (what the
creatures do). Creatures are `fish_t` in code.

## The core principle

**The tank is shaped by attention, never ruined by absence.** Nothing dies. Left
alone, progression plateaus and nothing decays into guilt. It is a desk
companion, not a demanding pet.

**No ambient UI.** Stats are shown on demand: a tap on a creature brings up its
card. Milestones live on their own page, never over the tank.

**The model owns the decisions.** Everything here either changes the numbers the
model reads or presents what it chose. Nothing overrides a goal it picked.

## Population

A new tank is **two fry of contrasting personality** (`tank_new_population`:
bold at least 0.45 apart, sociable at least 0.3). The other 23 places
(`N_FISH_MAX` 25, `POP_CAP`) fill two ways: **arrivals**, which care earns, and
the **shop**, which sells one juvenile of a species at a time.

**A tank keeps two** (`FISH_KEEP_MIN`): breeding takes a pair. The state line's
`friend` field is the only thing that encodes population, and a lone creature hears
`friend none`. The training data has carried a `lonely` case since schema 3 (a starving
fish with no friend in view chose `seek_food` at 0.98 when last probed, on the v4
model), so one creature is not out of distribution, but the shipped model has not been
probed for it. Run `model/probe_dist.py`'s `friend none` check before building on a tank
of one ([retrain.md](retrain.md)).

### Arrivals

An arrival takes every **care gate** for the tank's current size, plus a clean
glass, a nursery bed and a pair to court. Gates count care, never time alone
(`care_gates`).

| creatures now | grow (the youngest) | meals | also |
|---|---|---|---|
| 2 | - | 12 | every creature's trust at 6; one calm hold-approach |
| 3 | to juvenile | 40 | CHANGE: the youngest's drift pressure reaches 0.11 |
| 4 | to adult | 80 | every creature's trust at 7 |
| 5 | to adult | 140 | every creature's trust at 8 |
| 6 | to adult | 200 | every creature's trust at 8 |
| 7 and up | to adult | 270, then ~+70 a place (1490 at 24) | every creature's trust at 9 |

Every arrival also needs:

- **GLASS:** film on no more than `ALGAE_DIRTY` (15%) of the glass. A fry already
  staged is not held back by it, or a night's film would keep it from ever landing
  on a morning.
- **GRASS:** a bed at `VEG_NURSERY` or taller to be born in.
- **A pair:** two of one species (`pick_parents`, the species' best pair, grown
  first and then by trust). A tank with no two of a kind has no fry to court or list.

The milestones page's NEW FRY row lists the gates (`progression_next_fry`), and its
HOW? button says how to move each one (`progression_fry_tip`). Both read the same
table as the rule, so they cannot disagree with it.

**The tell and the spawning.** One condition shy of an arrival, with a nursery, the
parents-to-be start courting: a few seconds of circling down in the fronds every
minute or so. Once every gate is met the arrival is staged. After
`SPAWN_WAIT_MIN_S`..`MAX_S` awake seconds the pair swims into the nursery and circles
there, and after `SPAWN_DANCE_S` together the fry is born between them, lit or dark.
A page over the tank holds it (`ui_cover`). A tank put to sleep first has its fry at
the wake.

**The birth flow** (`setup.c`) opens over the live tank: A NEW FRY!, then its name
on the letter wheel, then the family page. The debt rides in the save until the
keeper finishes it.

**Inheritance.** Bold and sociable are the parents' average with a nudge; the body
colour comes from one parent and the markings from the other. A species keeps a
parent's design 80% of the time. A classic fish's fry hatches as a new species 4% of
the time (`SP_MUTATE_P`).

**Selling a creature** (its card's SELL, two taps): 5 / 15 / 30 / 60 sand dollars by
stage, a species' creature at most its shop price. The tank always keeps two, a fry
still owed its welcome holds every sale, and the slot is earned again: the next fry
waits for `SELL_FRY_MEALS` (12) more meals.

## Growth

Stages run fry → juvenile → adult → elder at 30 min, 3 h and 24 h of grown life
(`STAGE_*_AGE`). Every awake second counts, lit or dark; a slept span counts at
`SLEEP_GROWTH_FRAC` (0.25). Size is the stage's scale (0.55 / 0.78 / 1.04 / 1.23)
times up to 1.18 for meals eaten. Reaching a stage shows as a brief notice.

## Trait drift

Bold and sociable creep with experience, while the light is on (`progression_tick`):

| pressure | moves | rate (a whole 0..1 trait per `DRIFT_HOURS`, 2 h) |
|---|---|---|
| stress over 7 | bold down | full |
| hunger under 4 and stress under 2 | bold up | half |
| following a friend | sociable up | half |
| exploring | sociable down | a fifth |

Both enter the model's state line, so the model expresses the drift as behaviour,
and the model's response has cliffs a keeper can see: a very social fish starts
following friends, a very bold one starts darting. Traits clamp to 0.05..0.95, but
`drift_acc` counts all the pressure, clamp or not, so a fry born on a clamp can still
earn the CHANGE gate. "The tank changed someone" is a bold or sociable 0.11 from
where it started.

## Trust and touch

Trust is 0..10: 5 for a new creature, 4 for an arrival or a bought juvenile. Two
things move it:

- **A resting finger** raises every creature's trust by 0.02 a second, wherever the
  finger is (`HOLD_RADIUS` covers the glass).
- **Going hungry with AUTO FEED off:** a creature at hunger 8.5 or more in a lit tank
  loses a point per 20 minutes after a 5 minute grace, down to 2.

**Bare hands only reach the creatures:**

| gesture | what it does |
|---|---|
| tap on a creature or helper | its card |
| two quick taps on a creature | its trick, if it has one (8 of the 13 kinds: [species.md](species.md)) |
| hold (~3 s of contact) | creatures with trust 4 or more come over, unless hunger is 7.5 or more |

A creature that was out in the tank when the draw began and comes all the way in
earns its first hold-approach. The reactions are reflex-layer, not model decisions,
and the model never sees a finger.

**The tools** come from the main menu, and a tool in hand owns the glass until its
chip's DONE (or 2 minutes untouched):

| tool | what it does |
|---|---|
| FEED | a tap drops a portion of three pellets at the surface over it; five portions fit |
| SPONGE | a stroke wipes the film along its path |
| SCISSORS | a mostly sideways stroke cuts every frond it crosses, at the height it crosses it |

## Hunger

A meal lasts about 5-6 awake minutes. Hunger scales how hard a fish goes for food,
whichever brain chose `seek_food`: a peckish one saunters, a starving one charges.

The tank's own **trickle** is hunger-gated and per-second: while someone is at 7.5 or
more and fewer than two pellets are live, it drops one about every 7 s. An untended
tank cycles between fed and peckish and never reaches ravenous while awake
(`--selftest-hunger`). The keeper's pellets are what make a creature full.

**Ravenous** is the after-sleep event. When everyone is at 8.5 or more with empty
water, they beg at the surface over the keeper's usual feeding spot and the trickle
holds off, so the keeper's pellets are the event. Live pellets bring a dash. If
nobody comes within 150 s they give up and the tank feeds itself.

**AUTO FEED off** (settings) stops the trickle. Nobody dies of it: they beg once,
give up, and go hungry quietly until the keeper feeds, losing trust as above.

## Upkeep

Chores are feedback, not punishment, and they earn sand dollars.

**The grass.** Three beds, plus the shop's sword plant, grow upward: from nubs to
their ceilings in ~45 h awake or ~20 h asleep. Every frond has its own height and its
own ceiling (a hashed 0.72..0.95 of the way to the surface), so a grown bed is a ragged
skyline. The scissors never cut below nubs. The grass is a comfort system; stress is
already in the schema, so the model reacts with no schema change:

- **Any canopy calms,** more for a creature tucked inside one.
- **No cover** (every bed under `VEG_BARE`) is a mild unease, gone once one tuft regrows.
- **Smothered** (the second-tallest bed past 85% of its ceiling) is real stress. One
  bed at its ceiling is just a good place to hide.

**The film.** Algae claims the glass in steps, one per 4 min awake and one per 3 min
asleep, from the edges in, up to 30% of the cells; existing cells only thicken past
that. A night lands near the 15% gate. The sponge wipes it.

The **snail** and the **urchin** ([species.md](species.md)) lighten both chores, awake
and through the night, and never count as the keeper's.

## The light and time away

**The light** is the settings page's LIGHTS OUT: NEVER by default (it stays on), or
AUTO after 5 s, 15 s, 30 s, 1, 3, 5, 10 or 30 min still. With AUTO it is on while the
device is handled (moved, or touched) and goes off after the idle time. A setup page
or a prompt holds it on. In the dark the creatures rest by the reef and sleep; trait
drift pauses but growth does not. The director and the sim can override it, never
saved.

**Time away.** A boot or a wake lives through the stretch since the save, capped at a
week (`progression_slept`, `tank_tick_sleep`): hunger rises 0.8 an hour, energy
returns, stress goes, the grass and the film grow and the helpers graze, one
`SLEEP_SLICE_S` at a time. So a long absence ends in ravenous begging by itself, and a
tank whose cell died overnight still gets its night. One stretch of 6 h earns "first
full night's sleep". Without a clock the tank simply resumes.

## Milestones

Firsts are **detected from what the creatures actually did**, so when one lands is
earned and differs per tank. A badge earned since the keeper last closed the page wears
a ring.

| | badges |
|---|---|
| a creature's six | first meal from you, first hold-approach, first rest in the seagrass, first bubbles, first follow, first dart |
| its growth strip | reached juvenile, adult, elder |
| the tank's | first feeding, first trimming, first glass cleaning, first full night's sleep, first play session (two darting at once), the tank changed someone |
| the tank's, once the thing is bought | a school of ten shrimp, first visit to the reef |
| the population strip | a pair, a trio, a quartet, a quintet, a sextet |

Personality sliders on the card are revealed by the same bits: you learn a creature by
watching it.

## Sand dollars and the shop

Care earns sand dollars and the shop spends them. Nothing is ever needed: a tank with
no dollars is a complete tank. A "+N" toast shows what lands during play. The ledger is
saved, so nothing pays twice.

| earned by | dollars |
|---|---|
| a meal (a creature ate from a keeper's feeding) | 2 |
| reaching juvenile / adult / elder | 5 / 10 / 25 |
| a fry born | 20 |
| full trust (10.0), once a creature | 15 |
| every 100 algae colonies wiped (a colony is a connected patch whose last cell went under the keeper's sponge) | 25 |
| every 250 cm of grass cut (24 px to the tank's inch) | 25 |

| the shop | price | |
|---|---|---|
| sword plant | 40 | a fourth bed of broad leaves, trimmed and counted as cover like the grass |
| snail | 80 | helper: grazes the glass |
| castle | 150 | a swim-through arch |
| coral | 100 | grows for a month, in the keeper's colour |
| reef cluster | 240 | arrives mature, then blooms; three looks; becomes the fish's `reef` |
| shrimp | 180 | helper: a school that multiplies on pellets |
| sea urchin | 120 | helper: nibbles the tall grass |
| wreck | 200 | a sunken boat with holes to swim past |
| frogman | 10 | helper: a drifting diver |
| submarine | 60 | helper: cruises and raises a periscope |
| a species' juvenile | 10 | one a purchase, as often as there is room; two of a kind breed |

The model sees none of the decor or helpers directly. They reach the creatures through
cover and the film, and the reef cluster through the `reef` sighting.

**Placing a piece.** After a purchase (or the shop's MOVE) the placement page opens over
the live tank: a finger on the water drags the piece, a DEPTH bar picks BEHIND / IN FRONT
(and AMONG for the plant) and the tank redraws live. MIDDLE is a split: half the piece
behind the creatures, half in front.

**Selling a piece or a helper** takes two taps on SELL: 20% of the price back to the
balance (not earnings), the thing gone, and a rebuy starts fresh.

## Why the model and not rules

Progression pushes the numbers the model reads, lets the keeper see it read them, and
uses its distribution for variety. `model/probe_dist.py` reproduces the measurements;
[stats.md](stats.md) has the shipped model's.

- **Identity is continuous conditioning.** Bold, sociable and stage shape a joint
  function over every situation, from prose in the teacher's prompt, with no code.
- **Sampling.** The goal is sampled from the model's own softmax at T = 1. About 1
  decision in 5 differs from greedy, almost always to the teacher's own second choice,
  and survival decisions are essentially untouched.
- **Hesitation.** A low-confidence switch shows as a pause and a glance at the
  runner-up's target. A rule engine has no runner-up to glance at.
- **Fry are erratic** because the model is less sure about them. Elders settle.

The model's goal is never altered; only its certainty is shown.

## Saves

One blob in NVS (`tank`/`save`), layout 3. Saves go out on events, coalesced to at
least 30 s apart, plus a 10-minute heartbeat. The layout is locked: a new field only
goes at the end of `save_t` (`SAVE LAYOUT LOCK` in `common/progression.c`), a new decor
kind or helper is a record, and `--selftest-saves` holds every layout 3 save to the
promise. Earlier layouts start a fresh tank.
