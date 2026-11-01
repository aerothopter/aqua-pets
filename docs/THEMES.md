# Display themes

**There is one theme for now, the Original.** Themes are presentation only: the model, the behaviour and
the save don't depend on them, and there is no picker in the settings page.

## What a theme is

- **An ID** in `theme_id_t` (`common/theme.h`), saved as `save_t.theme` (byte 56 of save layout 3). IDs are
  saved, so they are never reordered or reused. IDs 1-3 are reserved: a save holding one, or an unknown ID,
  loads as the Original with the tank kept (`theme_valid`, `--selftest-themes`).
- **A palette row** in `common/theme.c`.
- **The hooks** it changes things through: `theme_ui_color`, `theme_creature_color`, `theme_icon`; the art asks
  `theme_active()`, and `render_use_theme` sets it for a frame.
- **A tune:** a row of `J_SONGS` (`common/audio.c`), the about page's jingle.

The selftests loop over `THEME_COUNT`, so a theme brought back is tested again with no new code.

## Rules for a theme

- **The keeper's colours are inputs.** A creature's saved body, fin and accent stay recognisable; a theme changes the
  material (flat, translucent, shaded), never the design the keeper bought.
- **Every theme behaves the same.** Motion, flourishes and goals are never themed.
- **Keep it dark.** An AMOLED spends power on every lit pixel, and a bright water field shown all day risks burn-in.
- **Mind the flash.** A full icon set and font atlas cost ~160 KB. Everything is flash-resident RGB565 + alpha, with no
  runtime SVG or font engine.

## Bringing themes back

Quiet Lagoon, Tidepool Club and Blackwater are at git tag `multi-board-multi-theme`, with a description of each:
`git show multi-board-multi-theme:docs/THEMES.md`. Restore from the tag:

- the themed branches of the art (`creatures.inc`, `render.c`, the decor and helper `.inc` files) and
  `blackwater_castle.inc` / `blackwater_wreck.inc`
- the picker (`SET_TAP` value 10 is kept for it) and the tunes
- `common/theme_assets.c`, the generated icons and font atlases (`tools/gen_theme_assets.py`; 325 KB of flash)

Their sources are still in the tree: `assets/themes/`, `docs/design/` (the review galleries and the jellyfish study) and
`tools/export_living_lagoon_art.cjs`. The four thumbnail files `common/{jellyfish,wreck,frogman,sub}_assets.c` still carry
their themed variants; trimming them means running their generators, which need Pillow.
