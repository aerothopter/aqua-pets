/* device.h - the board this build is for.
 *
 * A board is a compile-time world (docs/BOARDS.md): its glass, its floor,
 * its film grid, its numbers, its id. Each board's are in one header under
 * devices/, and the selector below picks it. There is one board for now, the
 * 1.8 rectangle (docs/BOARDS.md says how another comes back). A board's build
 * flag is only the selector - the code asks the header's names:
 *
 *   PT_BOARD, PT_BOARD_NAME     the image's id and the maker's name (version.h)
 *   TANK_W, TANK_H              the frame
 *   DEVICE_GLASS                the glass's shape: GLASS_RECT (glass.c has a
 *                               body per shape it knows)
 *   TANK_BOT, TANK_FX0, TANK_FX1  the floor (tank.h)
 *   ALGAE_CELL, _COLS, _ROWS    the film grid
 *   VEG_SEG_PX                  a frond segment's height
 *   DECOR_MARGIN, PLANT_X_DEFAULT  the decor's room on the floor
 *   DEVICE_WORN                 worn on a wrist: the picture's way up is the
 *                               keeper's setting (settings SCREEN), never the
 *                               live IMU's
 *   DEVICE_BEDS, DEVICE_REEF_BED_LESS  the grass beds (tank.c)
 *   DEVICE_LAYOUT               the board's page layout header
 *                               (devices/<board>_layout.h): every page number
 *                               that differs between boards (render.h and
 *                               update.h include it)
 *
 * No other includes: version.h reads it without tank.h. */
#ifndef AQUA_PETS_DEVICE_H
#define AQUA_PETS_DEVICE_H

#define GLASS_RECT    0
#define GLASS_BOWL    1     /* (the round bowl and the watch's rounded corners: kept as ids for a board brought back) */
#define GLASS_CORNERS 2

#if defined(TANK_ROUND) || defined(TANK_WATCH)
#error "the round and watch boards are out for now (git tag multi-board-multi-theme)"
#else
#include "devices/amoled18.h"
#endif

/* a glass with curves: the frame alone does not keep a thing inside it
 * (glass.h) */
#define GLASS_CURVED (DEVICE_GLASS != GLASS_RECT)

#endif
