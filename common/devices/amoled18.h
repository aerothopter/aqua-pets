/* devices/amoled18.h - the Waveshare ESP32-S3-Touch-AMOLED-1.8: the tank,
 * the reference board (device.h names what each line means). A 448 x 368
 * landscape glass; its walls are x = 0 and TANK_W, its bottom TANK_H. */
#define PT_BOARD         "amoled18"
#define PT_BOARD_NAME    "ESP32-S3-Touch-AMOLED-1.8"

#define TANK_W 448
#define TANK_H 368
#define DEVICE_GLASS GLASS_RECT
#define TANK_BOT  TANK_H
#define TANK_FX0  0
#define TANK_FX1  TANK_W

#define ALGAE_CELL 16                          /* px per glass-film grid cell */
#define ALGAE_COLS (TANK_W / ALGAE_CELL)       /* 28 */
#define ALGAE_ROWS (TANK_H / ALGAE_CELL)       /* 23 */
#define VEG_SEG_PX 3.2f                        /* a full frond's tip just under the surface */

#define FEED_ZONE_Y 26.0f
#define DECOR_MARGIN    30                 /* the snail's margin: inside the panel's rounded bezel */
#define PLANT_X_DEFAULT (208.0f + PLANT_HALF_W)   /* the open floor between the reef bed and bed 2 */

#define DEVICE_WORN      0
#define DEVICE_PAGE_BOWL 0
