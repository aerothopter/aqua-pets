# Creatures and helpers

**Creatures** are what the model decides for: `t->fish[]`, one row each in `SPECIES[]`
(`common/creature.c`), the classic fish included. There are thirteen kinds. Each breeds, comes in
four designs, rolls its own personality inside its species' range, and moves the way the real
animal does. The model chooses every creature's goal; the species decides how that goal is
carried out. A new species is a row and its hooks.

**Helpers** move by their own rules and are bought, never bred ([below](#helpers)). Decor stays put
([progression.md](progression.md#sand-dollars-and-the-shop)).

## Where creatures come from

- **The founding pair** is two classic fish.
- **The shop** sells **one juvenile** of a species for 10 sand dollars (`progression_buy`), as often
  as there is a free place. Two of a kind are what breed, so a keeper buys a second. A newcomer's
  boldness contrasts with the last of its kind already here, the way the founding pair's does.
  Juveniles start at `STAGE_JUV_AGE` and are not paid the juvenile stage's dollars.
- **A birth** ([progression.md](progression.md#arrivals)) is the species of its parents, and breeding is
  within a species: `pick_parents` picks the best pair of one kind. With a small chance (`SP_MUTATE_P`,
  4%) a classic fish's fry hatches as a random new species, and the birth flow reads "A SURPRISE!".

A species' shop bit means "some of them are in the tank" (`progression_species_sync`), never "bought":
it does not lock the row. A creature sells back from its card for its shop price at most.

The cap is `N_FISH_MAX`, 25. Each creature is a turn in the advisor's queue: at ~3.7 s a decision on the
device, a full tank gets a fresh decision every ~90 s, and the reflex layer keeps everyone moving in
between.

## The table

`SPECIES[]`. Sizes are tank-scaled: 1.0 is the classic fish's ~42 px. A newborn's bold and social are the
parents' mean ± 0.15, clamped to the range widened by 0.1; a mutant's are rolled in the range.

| species | token | size | bold | social | curiosity | lazy | turn | moves as |
|---|---|---|---|---|---|---|---|---|
| fish | `fish` | roster | 0.10-0.90 | 0.10-0.90 | roster | roster | roster | `LOCO_FIN` |
| seahorse | `seahorse` | 0.80-1.00 | 0.08-0.35 | 0.50-0.85 | 4.0 | 0.70 | 1.6 | `LOCO_UPRIGHT` |
| octopus | `octopus` | 1.00-1.25 | 0.40-0.80 | 0.05-0.25 | 8.5 | 0.40 | 3.0 | `LOCO_JET`, crawls the floor |
| pufferfish | `puffer` | 0.85-1.05 | 0.25-0.60 | 0.20-0.50 | 7.0 | 0.50 | 4.6 | `LOCO_HOVER` |
| anglerfish | `angler` | 0.95-1.20 | 0.50-0.80 | 0.05-0.20 | 2.5 | 0.90 | 1.4 | `LOCO_AMBUSH` |
| electric eel | `eel` | 0.98-1.23 | 0.60-0.90 | 0.10-0.30 | 4.5 | 0.60 | 1.8 | `LOCO_UNDULATE` |
| hammerhead | `shark` | 1.12-1.37 | 0.80-0.95 | 0.50-0.80 | 5.0 | 0.10 | 1.5 | `LOCO_CRUISE` |
| squid | `squid` | 0.90-1.10 | 0.30-0.60 | 0.70-0.95 | 6.0 | 0.30 | 3.5 | `LOCO_JET`, hovers in open water |
| crab | `crab` | 0.80-1.00 | 0.40-0.80 | 0.20-0.50 | 6.5 | 0.50 | 3.5 | `LOCO_SIDEWALK` |
| lobster | `lobster` | 1.10-1.40 | 0.50-0.85 | 0.05-0.25 | 5.5 | 0.60 | 2.0 | `LOCO_WALK` |
| jellyfish | `jellyfish` | 0.95-1.15 | 0.10-0.40 | 0.40-0.75 | 5.0 | 0.70 | 1.6 | `LOCO_HOVER`, pulses |
| swordfish | `swordfish` | 1.10-1.35 | 0.70-0.95 | 0.30-0.60 | 5.5 | 0.10 | 1.3 | `LOCO_CRUISE`, faster |
| turtle | `turtle` | 1.05-1.30 | 0.45-0.80 | 0.20-0.50 | 6.0 | 0.60 | 1.6 | `LOCO_HOVER`, slow strokes and glides |

## Designs

Four a species (`SPECIES[].var`, `fish_t.variant` 0..3), each a body / fin / accent and a pattern the renderer
draws. An arrival takes one parent's design (80%) or a random one, and the family's colours as the classic
fish does: the body from one parent, the markings from the other.

| species | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| seahorse | golden (bands) | crimson (speckles) | black (white spots) | lavender (spines) |
| octopus | common (mottled) | blue-ringed (rings) | mimic (stripes) | violet (spots) |
| pufferfish | spotted | dogface (dark mask) | saddled (black saddles) | golden (plain) |
| anglerfish | abyss (cyan lure) | frogfish (warts) | mottled (blotches) | pink warty (yellow lure) |
| electric eel | olive (orange belly) | charcoal (yellow belly) | bronze | spotted green |
| hammerhead | grey | bronze | slate blue | pale scalloped |
| squid | pink (chromatophores) | firefly blue (glowing dots) | bigfin white | reef amber |
| crab | red rock | blue (orange claws) | Sally Lightfoot (blue flecks) | green shore |
| lobster | common (orange antennae) | rare blue | spiny (gold spots) | calico (cream patches) |
| jellyfish | peach | pearl | rose | blue |
| swordfish | steel | cobalt (bars) | sunset (bars) | ghost |
| turtle | green (scutes) | hawksbill (streaks) | leatherback (ridges) | seafoam (spots) |

## How they move

The model picks one of the 8 goals; `target_for_goal` and the locomotion step in `common/creature.c` turn it into
the animal's own motion. `fish_t` in `common/tank.h` holds the heading and facing convention ("THE CONVENTION").

- **Classic fish (`LOCO_FIN`):** committed U-turns and a tail beat; it glances back before it commits to a turn.
- **Seahorse (`LOCO_UPRIGHT`):** swims upright and slowly (speed ×0.35), climbing and sinking more than it travels.
  REST: wraps its tail round the nearest grass frond and sways with it. Feeding: drifts close, then a quick snick.
- **Octopus (`LOCO_JET`, crawler):** EXPLORE, REST and INSPECT on the floor and the reef, crawling on its arms.
  DART_PLAY: jets mantle first, arms trailing, in pulses. REST: a den at the reef cluster, castle or grass foot, and it
  **takes on the colour of what it sits on** (`fish_t.camo`).
- **Pufferfish (`LOCO_HOVER`):** slow and boxy, fins sculling. It can stop, turn on the spot and back up.
- **Anglerfish (`LOCO_AMBUSH`):** an ambush hunter. It barely moves, stays low, waits with its lure bobbing and lunges
  short and fast at food that comes near. The lure **glows**, brightest at night. It "walks" the floor on its fins when
  it explores.
- **Electric eel (`LOCO_UNDULATE`):** a long body rippling from head to tail. It swims backward as easily as forward and
  lies along the floor to rest. It **breathes air**: every 1-2 minutes it rises to the surface for a gulp.
- **Hammerhead (`LOCO_CRUISE`):** never stops (it breathes by swimming): a floor of speed, wide smooth turns, the head
  sweeping. REST is a slow patrol lap.
- **Squid (`LOCO_JET`, hoverer):** hovers in open water with its fins rippling, moves forward or backward, and jets in
  pulses. Squid hold station near each other.
- **Crab (`LOCO_SIDEWALK`):** walks the floor **sideways**, legs stepping in a ripple, in stop-and-go. It climbs the reef
  cluster's rock and never swims up: a goal in open water becomes the nearest ground below it. REST: tucked in a den.
- **Lobster (`LOCO_WALK`):** walks the floor head first, antennae sweeping. REST: in a den. It stays on the floor and the rock.
- **Jellyfish (`LOCO_HOVER`, pulsing):** an upright bell that never turns on its side. One phase, `fish_t.jet`, drives
  both the thrust and the bell the renderer draws, so motion and picture agree (~0.6 Hz exploring). It drifts in the open
  water and keeps out of the floor band and the surface. REST: it hangs still in mid-water at ~0.2 Hz.
- **Swordfish (`LOCO_CRUISE`):** a long slim cruiser like the hammerhead, faster and never still, the bill out front.
- **Turtle (`LOCO_HOVER`):** slow strokes and long glides; it never backs. REST: lying on the sand like the eel. It shares
  the eel's trip for air, with bubbles off the beak at the surface.

### Double-tap tricks

Two quick taps on a creature (`tank_poke`, the row's `poke` hook; `flee` for a getaway from where the finger landed), with
no stress and no lost trust. Eight of the thirteen kinds have one; for the rest a double tap is just the card up and down.

| creature | trick |
|---|---|
| pufferfish | inflates into a spiny ball for a few seconds, then lets it out slowly |
| octopus, squid | squirts a black ink cloud and jets off |
| seahorse | grips the nearest stem and holds on for ~6 s |
| crab | claws up, then a fast sideways scuttle away from the finger |
| lobster | faces the finger and flips its tail, a few strokes that shoot it backward |
| jellyfish | a hurried pulse away (~1.3 Hz) for ~4 s |
| eel | sparks |

### Idle flourishes

Small displays of their own, independent of the advisor and in every theme (`sp_flourish`):

- **A trip to the surface:** the octopus, pufferfish (it inflates for the trip), anglerfish, crab and lobster.
  Up to 40 s, then an 18 s return.
- **A squirt of ink:** the octopus and the squid, every 50-120 idle seconds by day. Every third idle turn of the octopus
  is a surface trip instead.
- **A spark:** the eel.

The first event is staggered by creature (45-124 s), then 100-210 idle seconds between events. Rest, night, urgent hunger,
flight, naming, touch and an active spawning all take priority. `--selftest-themes` checks them.

## The gentle tank

No creature harms another. The small ones keep their distance from the big ones (the hammerhead, the eel, the swordfish): a
separation push inside `SP_AVOID_R`, weakened on a final approach to food so a hungry one can still reach its pellet. Every
creature eats pellets.

## The keeper's side

- **A tap** reaches a creature by `tank_fish_hit_r`: its species' `hit_r` at its size, never under `TANK_HIT_MIN_R` (32 px, a
  fingertip). The nearest by its own reach wins.
- **The card** names a creature under its name in the 8 px font: "SEAHORSE - GOLDEN", or the design first, or the token ("EEL -
  SPOTTED") when the card is too narrow.
- **The milestones page** shows six rows whole; with more (up to 25 creatures and the NEW FRY row) they scroll
  (`common/scroll.c`). A card opened or stepped to scrolls its row into view. A big creature's portrait is held to `MSP_ROW_SP_MAX`.
- **The birth flow** names what it is: "A NEW SEAHORSE!", "NAME THE NEW SEAHORSE".
- **The director:** `buy <species>` (token, name or the shop's plural), `spawn <species>` (a staged pair, free), and `state` prints
  each creature's species and design.

## What is saved

Each creature's 100 B record (`fish_save_t`, save layout 3) holds its **species** and **variant** with the rest of it. The live state
(puff, ink, camo, the eel's air, the seahorse's frond) is not saved.

## The model

Schema 5 sends `species <token>` ([model/schema.md](../model/schema.md)). The shipped model, **v5j**, knows the classic fish and the ten
species words up to `jellyfish`. **The swordfish and the turtle have no word yet:** the advisor sends a word only when the loaded
vocabulary has it (`advisor_core_species_word`) and otherwise `species fish`, so their traits and the reflex layer make them what they
are. [retrain.md](retrain.md#one-species-on-a-shipped-model) has the recipe for adding them; [stats.md](stats.md) the numbers.

## Helpers

One row each in `HELPER[]` (`common/helper.c`), bought from the shop and sold back at 20%. They are never placed or bred and never join
`t->fish[]`, so the model never sees them: they reach the creatures through the film and the grass. Their work never counts as the keeper's
chores, and they lighten the chores rather than remove them. The table's order is the RNG's order, so a new helper goes at the end.

| helper | rules | a tap |
|---|---|---|
| **snail** | crawls the glass to the nearest film and grazes it, cell by cell, awake and asleep; with nothing to graze it walks the floor, passing in front of a piece placed IN FRONT | its card: cells grazed |
| **urchin** | gets hungry slowly, crawls to the tallest grass frond and chews it down a bite; through a night it takes a share of every frond's height; never below `URCHIN_KEEP` (0.40), never the sword plant | its card: grass eaten, what it is up to |
| **shrimp** | a school of 4 at the grass's foot that swarms pellets below the grass's top; every 10 pellets bring one more, up to 10, at most one per 15 min; with over half the glass fouled they refuse food and stop growing | its card: how many, pellets eaten, progress to the next |
| **frogman** | a diver who floats sideways glass to glass, easing to a new lane now and then, waving at times | none: a tap goes through |
| **submarine** | cruises the same way, propeller turning; every 22-40 s it stops and a periscope rises, scans and sinks | none: a tap goes through |

The frogman and the sub pass each other: within `PASS_DX` and nearly level, each eases off to a lane clear of the other.

Rendering is through `HELPER_DRAW[]` in four slots: under the creatures, over them, over the decor IN FRONT, and on the glass. Each one's art
is in `common/helper_<name>.inc`.
