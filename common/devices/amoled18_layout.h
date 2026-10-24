/* devices/amoled18_layout.h - the 1.8's pages: every page number that
 * differs between boards (refactor phase 4d, 2026-10-23; device.h names this
 * file DEVICE_LAYOUT, render.h and update.h include it and hold the shared
 * numbers). PAGE coordinates unless a line says the frame's. The 1.8 is the
 * reference: the other boards' layouts are its pages drawn in from their
 * glass. */
#ifndef AQUA_PETS_LAYOUT_H
#define AQUA_PETS_LAYOUT_H

/* the fish card and the tool chip in its place (the frame's coordinates) */
#define RENDER_CARD_X 14
#define RENDER_CARD_Y 8
#define TOOL_CHIP_X   RENDER_CARD_X
#define TOOL_CHIP_Y   RENDER_CARD_Y
/* the battery pill and the "+n" toast (the frame's) */
#define RENDER_BAT_X  (TANK_W - RENDER_BAT_W - 28)     /* clear of the curved bezel; the bolt sits left of it */
#define RENDER_BAT_Y  8
#define RENDER_TOAST_Y 8

/* the milestones page */
#define MSP_ROW_Y0    4
#define MSP_ROW_H     40
#define MSP_TANK_Y    254
#define MSP_ROW_MID   20                /* in a row: the portrait's centre ... */
#define MSP_ROW_STRIP 30                /* ... the growth strip / the fry's ticks ... */
#define MSP_ROW_BADGE 4                 /* ... the badges' top ... */
#define MSP_ROW_BAR   38                /* ... the bar under a gate still owed */
#define MSP_BADGE_X0  176
#define MSP_BADGE_DX  40
#define MSP_FISH_X    52                /* a row's portrait */
#define MSP_TPG_X     412               /* the arrow's column: right of the sixth badge's slop, out to the glass */
#define MSP_CLOSE_X   324               /* the CLOSE button, bottom right, inside the bezel curve; clear of the brightness row's number */
#define MSP_CLOSE_Y   312
#define MSP_CLOSE_SLOP 8                /* CLOSE's tap reaches this far left of it */

/* the shop */
#define SHP_COIN_X    32
#define SHP_HEAD_X    112               /* "SAND DOLLARS" and the balance */
#define SHP_EARN_X    32
#define SHP_CLOSE_X   MSP_CLOSE_X
#define SHP_ARROW_IN  28                /* the page arrows, in from the right edge */

/* the settings page */
#define SET_TITLE_Y   14
#define SET_ROW1_Y    58             /* BRIGHTNESS */
#define SET_ROW2_Y    108            /* VOLUME */
#define SET_NOTE_Y    146            /* "FISH ARE QUIET AT NIGHT" */
#define SET_ROW3_Y    176            /* LIGHTS OUT */
#define SET_ROW4_Y    222            /* AUTO FEED */
#define SET_ROW5_Y    268            /* ROTATION */
#define SET_FOOT_Y    MSP_CLOSE_Y    /* UPDATES and CLOSE: the milestones page's foot */
#define SET_SEG_X     190            /* the first segment */
#define SET_SEG_DX    82
/* the foot: four across 32..418 */
#define SET_UPD_X     32
#define SET_UPD_W     104
#define SET_ABT_X     150
#define SET_ABT_W     80
#define SET_RST_X     244
#define SET_RST_W     80
#define SET_CLOSE_X   338
#define SET_CLOSE_W   80
#define SET_ABT_Y     SET_FOOT_Y
#define SET_RST_Y     SET_FOOT_Y
#define SET_ROW5_END  (SET_FOOT_Y - 4)              /* the last row's tap band stops at the foot's */
#define SET_VER_X(w)  SET_LABEL_X                   /* the version line (w px wide): bottom left, on the labels' x */
#define SET_VER_Y     (PAGE_H - 8 - 6)
/* the short pages' inscribed rectangle (the frame's: render_settings_bounds) */
#define SET_BOUNDS_X  22
#define SET_BOUNDS_Y  20
#define SET_BOUNDS_W  404
#define SET_BOUNDS_H  328

/* the update pages (update.h) */
#define UPD_CLOSE_X   324            /* the settings page's CLOSE, same place */
#define UPD_CLOSE_Y   312
#define UPD_BTN_L_X   32
#define UPD_BTN_R_X   240
#define UPD_BTN_W     176
#define UPD_PASS_DX   0              /* "PASSWORD FOR" in from the panel's edge */
#define UPD_CAL_IN_X  48             /* touch calibration: the outer crosshairs in from the frame's edges (the frame's px, as the panel reports) */
#define UPD_CAL_IN_Y  40

#endif
