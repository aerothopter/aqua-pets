/* scroll.c - one list's scroll (scroll.h, 2026-10-28) */
#include "scroll.h"
#include <math.h>
#include <string.h>

#define SCROLL_SLOP     10.0f    /* px before a press is a drag (the platforms' tap allows 24, so a short drag is a scroll and no tap) */
#define SCROLL_RUBBER   0.35f    /* past an end the list follows a third of the finger */
#define SCROLL_FRICTION 3.5f     /* 1/s: a fling keeps e^-3.5 of its speed a second; ~v/3.5 px all told */
#define SCROLL_SPRING   14.0f    /* 1/s: back from past an end */
#define SCROLL_VMAX     3000.0f  /* px/s: the fastest fling */
#define SCROLL_VMIN     15.0f    /* px/s: slower than this it stops */
#define SCROLL_CATCH    120.0f   /* px/s: a press on a list moving faster only stops it */
#define SCROLL_SAMPLE_MS 40      /* the finger's speed over at least this long (the device's polls come ~13 ms apart) */
#define SCROLL_STILL_MS 100      /* the finger rested this long before the lift: no fling */

void scroll_reset(scroll_t *s) { float max = s->max; memset(s, 0, sizeof *s); s->max = max; }

void scroll_extent(scroll_t *s, int content, int view) {
    s->max = content > view ? (float)(content - view) : 0;   /* (a list that shrank springs back to its new end) */
}

void scroll_to(scroll_t *s, float pos) {
    s->pos = pos < 0 ? 0 : pos > s->max ? s->max : pos;
    s->vel = 0;
}

void scroll_touch(scroll_t *s, float along, bool down, bool on_list, uint32_t now_ms) {
    float dt = s->last_ms ? (uint32_t)(now_ms - s->last_ms) / 1000.0f : 0;
    if (dt > 0.1f) dt = 0.1f;                                 /* a stall is not a long fling */
    s->last_ms = now_ms;
    if (down && !s->down) {                                   /* the press */
        s->in = on_list && s->max > 0;
        s->took = s->in && fabsf(s->vel) > SCROLL_CATCH;      /* it caught the list moving: that is all it does */
        s->dragging = false;
        if (s->in) { s->vel = 0; s->grab = along; s->grab_pos = s->pos; s->ref_pos = s->pos; s->ref_ms = s->moved_ms = now_ms; }
    }
    bool lift = !down && s->down;
    s->down = down;
    if (down && s->in) {                                      /* the finger on the list */
        float d = along - s->grab;
        if (!s->dragging && fabsf(d) > SCROLL_SLOP) {         /* a drag now: from here, so the list does not jump the slop */
            s->dragging = s->took = true;
            s->grab += d > 0 ? SCROLL_SLOP : -SCROLL_SLOP; d = along - s->grab;
        }
        if (!s->dragging) return;
        float raw = s->grab_pos - d, was = s->pos;
        s->pos = raw < 0 ? raw * SCROLL_RUBBER : raw > s->max ? s->max + (raw - s->max) * SCROLL_RUBBER : raw;
        if (s->pos != was) s->moved_ms = now_ms;
        uint32_t span = now_ms - s->ref_ms;
        if (span >= SCROLL_SAMPLE_MS) {                       /* the finger's speed, eased over the last samples */
            float v = (s->pos - s->ref_pos) * 1000.0f / (float)span;
            s->vel = s->vel * 0.3f + v * 0.7f;
            s->ref_pos = s->pos; s->ref_ms = now_ms;
        }
        return;
    }
    if (lift && s->dragging) {                                /* the lift: a fling at the finger's speed, unless it had stopped */
        s->dragging = false;
        if (now_ms - s->moved_ms > SCROLL_STILL_MS) s->vel = 0;
        s->vel = s->vel > SCROLL_VMAX ? SCROLL_VMAX : s->vel < -SCROLL_VMAX ? -SCROLL_VMAX : s->vel;
    }
    if (dt <= 0) return;
    if (s->pos < 0 || s->pos > s->max) {                      /* past an end: the fling dies fast and the spring brings it back */
        float end = s->pos < 0 ? 0 : s->max;
        s->vel *= expf(-dt * 25);
        s->pos += s->vel * dt;
        s->pos = end + (s->pos - end) * expf(-dt * SCROLL_SPRING);
        if (fabsf(s->pos - end) < 0.5f && fabsf(s->vel) < SCROLL_VMIN) { s->pos = end; s->vel = 0; }
        return;
    }
    if (s->vel == 0) return;
    s->pos += s->vel * dt;
    s->vel *= expf(-dt * SCROLL_FRICTION);
    if (fabsf(s->vel) < SCROLL_VMIN) s->vel = 0;
}

int  scroll_px(const scroll_t *s) { return (int)lroundf(s->pos); }
bool scroll_took(const scroll_t *s) { return s->took; }
bool scroll_moving(const scroll_t *s) { return s->dragging || s->vel != 0 || s->pos < 0 || s->pos > s->max; }
