/* devices/round175c.h - the Waveshare ESP32-S3-Touch-AMOLED-1.75C: the bowl
 * (2026-10-01; device.h names what each line means). The round 466 px glass:
 * the frame is the square around it. Water fills the circle, and sand lies
 * flat across its bottom - the floor is the chord at TANK_BOT - 16 (the sand
 * line) from TANK_FX0 to TANK_FX1, with sand under it down to the glass. */
#define PT_BOARD         "round175c"
#define PT_BOARD_NAME    "ESP32-S3-Touch-AMOLED-1.75C"

#define TANK_W 466
#define TANK_H 466
#define DEVICE_GLASS GLASS_BOWL
#define TANK_RAD  233.0f        /* the circle: centre (TANK_RAD, TANK_RAD) */
#define TANK_BOT  396           /* sand line y 380: the glass is 180.8 px either side of the centre there */
#define TANK_FX0  54
#define TANK_FX1  412

#define ALGAE_CELL 18                          /* 26 x 22 cells of 18 px over the circle down to TANK_BOT -
                                                * 572, inside the 644 the save keeps (progression.c) */
#define ALGAE_COLS 26
#define ALGAE_ROWS 22
#define VEG_SEG_PX 3.2f
/* the grass beds (tank.c veg_bed_base). The floor is 358 px, not 448: the
 * two right-hand beds keep their place against the right glass, the reef bed
 * gives up two fronds */
#define DEVICE_BEDS          { { TANK_FX1 - 72, 4 }, { TANK_FX1 - 170, 2 } }
#define DEVICE_REEF_BED_LESS 2

#define FEED_ZONE_Y 45.0f   /* the bowl's surface is its rim: 26 px there is 2.5 mm of glass and a fingertip scatters 9 px (2026-10-03) -
                               a feed tap that fell short was a tank tap, and two of them the light's double tap */
#define DECOR_MARGIN    8                  /* the bowl's floor is short and its glass hides nothing */
#define PLANT_X_DEFAULT 218.0f             /* the gap between the reef bed and bed 2 on the bowl's floor */

#define DEVICE_WORN      0
#define DEVICE_LAYOUT    "devices/round175c_layout.h"   /* its pages' numbers (render.h, update.h) */
