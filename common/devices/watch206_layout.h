/* devices/watch206_layout.h - the watch's pages: every page number that
 * differs between boards (device.h's DEVICE_LAYOUT; amoled18_layout.h says
 * what each line is). PAGE coordinates unless a line says the frame's. The
 * glass (410 x 502, portrait) is 38 px NARROWER than the page: the page sits
 * centred - PAGE_X is -19, so the glass shows page x 19..428 - in the middle
 * of the glass's height (PAGE_Y 67, where the round corners no longer reach
 * it). The layouts keep their margins of 24..32 px, so most of every page is
 * inside that window as it stands; the few columns here had to come in. */
#ifndef AQUA_PETS_LAYOUT_H
#define AQUA_PETS_LAYOUT_H

/* the fish card: in from its round corner (the frame's) */
#define RENDER_CARD_X 36
#define RENDER_CARD_Y 36
#define TOOL_CHIP_X   RENDER_CARD_X
#define TOOL_CHIP_Y   RENDER_CARD_Y
/* the battery pill: the top right, in from its round corner (the frame's) */
#define RENDER_BAT_X  (TANK_W - RENDER_BAT_W - 56)
#define RENDER_BAT_Y  22
#define RENDER_TOAST_Y 8

/* the milestones page */
#define MSP_ROW_Y0    4
#define MSP_ROW_H     40
#define MSP_TANK_Y    254
#define MSP_ROW_MID   20
#define MSP_ROW_STRIP 30
#define MSP_ROW_BADGE 4
#define MSP_ROW_BAR   38
#define MSP_BADGE_X0  172               /* a 38 px pitch: the tank row's arrow inside the glass */
#define MSP_BADGE_DX  38
#define MSP_FISH_X    56                /* an elder's tail inside the glass */
#define MSP_TPG_X     398
#define MSP_CLOSE_X   324
#define MSP_CLOSE_Y   312
#define MSP_CLOSE_SLOP 8

/* the shop */
#define SHP_COIN_X    32
#define SHP_HEAD_X    112
#define SHP_EARN_X    32
#define SHP_CLOSE_X   MSP_CLOSE_X
#define SHP_ARROW_IN  28

/* the settings page (2026-10-02): it uses the glass above and below the PAGE
 * box (page y -67..435 is on the glass; the round corners take the ends of
 * the first and last 100 px): the title above the box, the rows up by 44,
 * the foot's pair centred under them */
#define SET_TITLE_Y   (-40)
#define SET_ROW1_Y    14
#define SET_ROW2_Y    64
#define SET_NOTE_Y    102
#define SET_ROW3_Y    132            /* LIGHTS OUT */
#define SET_ROW4_Y    188            /* AUTO FEED */
#define SET_ROW5_Y    244            /* SCREEN: NORMAL / TURNED */
#define SET_NOTE5_Y   286            /* under it: who it is for */
#define SET_FOOT_Y    392            /* UPDATES and CLOSE */
#define SET_SEG_X     178            /* the third segment inside the glass */
#define SET_SEG_DX    80
/* the foot: no room for four, so ABOUT and RESET stand above the foot's pair -
 * UPDATES 114..226 and CLOSE 242..334, clear of the lower corners */
#define SET_UPD_X     114
#define SET_UPD_W     112
#define SET_CLOSE_X   242
#define SET_CLOSE_W   92
#define SET_ABT_X     114
#define SET_ABT_W     112
#define SET_ABT_Y     338
#define SET_RST_X     242
#define SET_RST_W     92
#define SET_RST_Y     338
#define SET_ROW5_END  (SET_ABT_Y - 6)               /* the last row's tap band stops at ABOUT and RESET */
#define SET_VER_X(w)  ((PAGE_W - (w)) / 2)          /* the version line: centred under the foot, between the lower corners */
#define SET_VER_Y     (SET_FOOT_Y + MSP_CLOSE_H + 2)
#define SET_BOUNDS_X  28
#define SET_BOUNDS_Y  33
#define SET_BOUNDS_W  354
#define SET_BOUNDS_H  436

/* the update pages (update.h) */
#define UPD_CLOSE_X   324
#define UPD_CLOSE_Y   312
#define UPD_BTN_L_X   32
#define UPD_BTN_R_X   240
#define UPD_BTN_W     176
#define UPD_PASS_DX   0
#define UPD_CAL_IN_X  70             /* in from its round corners (the frame's px) */
#define UPD_CAL_IN_Y  70

#endif
