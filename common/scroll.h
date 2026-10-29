/* scroll.h - one list's scroll: the drag, the fling, the ends (2026-10-28).
 *
 * A page with more than fits (the shop first; the milestones rows, the
 * settings and a long card to follow) keeps a scroll_t per list and hands
 * it the finger every frame: the coordinate along the list (y for a column,
 * x for a row), down or not, and whether the press is on the list. Past
 * SCROLL_SLOP the press is a drag and the list follows the finger; at the
 * lift it carries on at the finger's speed, slowing, and past either end it
 * gives a little and springs back. A press on a list still moving only
 * stops it. Either way scroll_took says the stroke was the list's, so its
 * release is no tap - the platforms keep classifying taps as before and the
 * page's tap call asks.
 *
 * Timed by the clock the caller passes, never by calls: the device polls the
 * touch three times a frame, the sim once, and a fling must go as far on
 * both. Pure motion - the page draws at -scroll_px and clips to its view. */
#ifndef AQUA_PETS_SCROLL_H
#define AQUA_PETS_SCROLL_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float    pos, vel, max;        /* px scrolled (0..max; past an end while it gives), px/s, the most it scrolls (0 = it all fits) */
    float    grab, grab_pos;       /* the drag's anchor: the finger, and pos then */
    float    ref_pos;              /* the speed's sample: pos at ref_ms */
    uint32_t ref_ms, last_ms, moved_ms;
    bool     down, in, dragging, took;
} scroll_t;

void  scroll_reset(scroll_t *s);                           /* back to the start, still (the page left) */
void  scroll_extent(scroll_t *s, int content, int view);   /* the list's length and its view's, px */
void  scroll_touch(scroll_t *s, float along, bool down, bool on_list, uint32_t now_ms);   /* every frame the page is up */
void  scroll_to(scroll_t *s, float pos);                   /* this far along, clamped, still */
int   scroll_px(const scroll_t *s);                        /* how far to draw the list up (or left) */
bool  scroll_took(const scroll_t *s);                      /* the last stroke was the list's: its release is no tap */
bool  scroll_moving(const scroll_t *s);                    /* dragged, flung or springing back */

#endif
