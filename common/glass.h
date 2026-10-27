/* glass.h - the glass, whatever its shape (refactor phase 4b, 2026-10-23).
 *
 * The board's header says the shape (device.h DEVICE_GLASS): the
 * rectangle's glass is its frame (the one shape for now, 2026-10-26; the
 * bowl's and the watch's are at git tag multi-board-multi-theme).
 * glass.c holds every shape's code, picked at compile time; the rest of the
 * tank asks these calls (and, inside the reflex layer, the ones in
 * tank_internal.h) instead of a board's name. */
#ifndef AQUA_PETS_GLASS_H
#define AQUA_PETS_GLASS_H

float tank_glass_x0(float y);                          /* the side glass at height y (0 / TANK_W in the rectangle; a corner's arc on the watch) */
float tank_glass_x1(float y);
float tank_glass_top(float x);                         /* the glass above x (0 in the rectangle) */
void  tank_glass_clamp(float *x, float *y, float m);   /* (x,y) brought to at least m px inside the glass and above the bottom */

#endif
