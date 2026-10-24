/* devices/watch206.h - the Waveshare ESP32-S3-Touch-AMOLED-2.06: the watch
 * (2026-10-02; device.h names what each line means). Its 410 x 502 glass as
 * it is worn - a PORTRAIT tank (Strato, with the first landscape picture on
 * his wrist: "I have to turn my head"). */
#define PT_BOARD         "watch206"
#define PT_BOARD_NAME    "ESP32-S3-Touch-AMOLED-2.06"

#define TANK_W 410
#define TANK_H 502
/* the glass is the rectangle with ROUND CORNERS: ~8 mm of radius on the
 * maker's drawing, 100 px. The bottom stays flat (TANK_BOT is TANK_H); the two
 * lower corners take the ends of the floor - the sand line (y 486) meets the
 * glass 46 px in - so the floor things keep to TANK_FX0..TANK_FX1, and the
 * swimmers, the film and the snail meet the arcs (tank.c, tank_glass_*) */
#define DEVICE_GLASS GLASS_CORNERS
#define TANK_CORNER_R 100.0f
#define TANK_BOT  TANK_H
#define TANK_FX0  44
#define TANK_FX1  366

#define ALGAE_CELL 18                          /* 23 x 28 cells of 18 px (414 x 504: the last column and row hang
                                                * 4 / 2 px off the glass) - 644 cells, what the save keeps */
#define ALGAE_COLS 23
#define ALGAE_ROWS 28
#define VEG_SEG_PX 4.3f                        /* the tank is 118 px taller than the 1.8's, so its segments are
                                                * too (107 x 4.3 from y 486: the tip at 26) */
/* the grass beds (tank.c veg_bed_base). The portrait floor is 322 px: bed 1
 * against the right glass, bed 2 five fronds beside it, the reef bed four
 * fronds fewer - open sand between */
#define DEVICE_BEDS          { { TANK_FX1 - 72, 4 }, { TANK_FX1 - 146, -1 } }
#define DEVICE_REEF_BED_LESS 4

#define FEED_ZONE_Y 26.0f
#define DECOR_MARGIN    8                  /* the floor ends where its corners begin */
#define PLANT_X_DEFAULT 196.0f             /* the gap between the reef bed and bed 2 on the watch's floor */

#define DEVICE_WORN      1                 /* (2026-10-02) the arm swings through every angle (tank_screen_*) */
#define DEVICE_LAYOUT    "devices/watch206_layout.h"   /* its pages' numbers (render.h, update.h) */
