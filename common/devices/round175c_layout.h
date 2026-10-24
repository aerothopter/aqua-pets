/* devices/round175c_layout.h - the bowl's pages: every page number that
 * differs between boards (device.h's DEVICE_LAYOUT; amoled18_layout.h says
 * what each line is). PAGE coordinates unless a line says the frame's. The
 * circle cuts the page's corners: a few buttons are drawn in from the glass.
 * Strato on the glass, 2026-10-01: the top fish's portrait and its last badge
 * were cut by the circle - "nudge this down", SETTINGS "to the top, dead
 * center as it would fit in that opening", UPGRADES and CLOSE "closer
 * together centered": the milestones rows start where the circle is wide
 * enough for them and stand 34 px apart. (SETTINGS and UPGRADES left the page
 * for the main menu, 2026-10-21; CLOSE stays put.) */
#ifndef AQUA_PETS_LAYOUT_H
#define AQUA_PETS_LAYOUT_H

/* the fish card: where the circle is tall enough for the card and the toolbox under it (the frame's) */
#define RENDER_CARD_X 78
#define RENDER_CARD_Y 62
#define TOOL_CHIP_X   84                /* up in the left shoulder, inside the glass */
#define TOOL_CHIP_Y   72
/* the bowl has no top right corner: the pill top centre, the bolt left of it (the frame's) */
#define RENDER_BAT_X  ((TANK_W - RENDER_BAT_W) / 2 + 10)
#define RENDER_BAT_Y  16
#define RENDER_TOAST_Y 44               /* the bowl's top is a narrow cap: lower */

/* the milestones page */
#define MSP_ROW_Y0    56
#define MSP_ROW_H     34
#define MSP_TANK_Y    264
#define MSP_ROW_MID   17
#define MSP_ROW_STRIP 27
#define MSP_ROW_BADGE 1
#define MSP_ROW_BAR   33
#define MSP_BADGE_X0  172               /* a 38 px pitch: the tank row's arrow inside the glass */
#define MSP_BADGE_DX  38
#define MSP_FISH_X    52
#define MSP_TPG_X     396               /* its "new" ring and the page pips inside the circle */
#define MSP_CLOSE_X   ((PAGE_W - MSP_CLOSE_W) / 2)   /* alone at the foot, centred (UPGRADES stood left of it until 2026-10-21) */
#define MSP_CLOSE_Y   318
#define MSP_CLOSE_SLOP 6

/* the shop: the header and the foot drawn in from the glass */
#define SHP_COIN_X    62
#define SHP_HEAD_X    140
#define SHP_EARN_X    97
#define SHP_CLOSE_X   259               /* HOW TO EARN 97..247 beside CLOSE */
#define SHP_ARROW_IN  66                /* further in */

/* the settings page */
#define SET_TITLE_Y   14
#define SET_ROW1_Y    58             /* BRIGHTNESS */
#define SET_ROW2_Y    108            /* VOLUME */
#define SET_NOTE_Y    146            /* "FISH ARE QUIET AT NIGHT" */
#define SET_ROW3_Y    176            /* LIGHTS OUT */
#define SET_ROW4_Y    222            /* AUTO FEED */
#define SET_ROW5_Y    268            /* ROTATION */
#define SET_FOOT_Y    MSP_CLOSE_Y
#define SET_SEG_X     172            /* the third segment inside the glass - the circle is narrowest at the BRIGHTNESS row */
#define SET_SEG_DX    80
/* the foot: in from the glass, four across 60..388 */
#define SET_UPD_X     60
#define SET_UPD_W     90
#define SET_ABT_X     156
#define SET_ABT_W     70
#define SET_RST_X     232
#define SET_RST_W     70
#define SET_CLOSE_X   308
#define SET_CLOSE_W   80
#define SET_ABT_Y     SET_FOOT_Y
#define SET_RST_Y     SET_FOOT_Y
#define SET_ROW5_END  (SET_FOOT_Y - 4)
#define SET_VER_X(w)  (SET_LABEL_X + 96)
#define SET_VER_Y     (PAGE_H - 8 - 6)
#define SET_BOUNDS_X  73
#define SET_BOUNDS_Y  81
#define SET_BOUNDS_W  320
#define SET_BOUNDS_H  304

/* the update pages (update.h) */
#define UPD_CLOSE_X   296            /* in from the glass */
#define UPD_CLOSE_Y   318
#define UPD_BTN_L_X   62             /* the pair drawn in from the glass */
#define UPD_BTN_R_X   230
#define UPD_BTN_W     156
#define UPD_PASS_DX   30
#define UPD_CAL_IN_X  110            /* a 3 x 3 the circle holds (the frame's px) */
#define UPD_CAL_IN_Y  110

#endif
