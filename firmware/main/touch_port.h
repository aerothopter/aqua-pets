#ifndef TOUCH_PORT_H
#define TOUCH_PORT_H
#include <stdbool.h>
#include <stdint.h>
#include "tank.h"
bool touch_port_init(void);
bool touch_port_deep_sleep(void);   /* the night's deep sleep: the chip's own sleep, its reset left high (true = it took it); the wake is a reboot */
void touch_port_poll(tank_t *t);
int  touch_port_selected(void);   /* tapped fish for the stats card, RENDER_CARD_SNAIL for the snail's, -1 = none */
/* milestones page: the main menu's TANK opens it; its CLOSE goes back to the
 * menu. touch_port_show_milestones is the director's cue. */
bool touch_port_milestones(void);
void touch_port_show_milestones(bool on);
/* the MAIN MENU (2026-10-21, render_menu): the BOOT key's page. touch_port_home
 * is BOOT's short press (main.c): from the tank the menu, from the menu the
 * tank, from any of its pages (milestones, shop, settings, updates) back to the
 * menu; nothing while the setup or the reset prompt has the glass. The menu's
 * tiles open those pages or put a tool in hand; a tap off the grid closes it.
 * touch_port_show_menu is the director's cue. */
bool touch_port_menu(void);
void touch_port_show_menu(bool on);
void touch_port_home(tank_t *t);
void touch_port_dismiss(void);    /* drop the card and the page: a flow (the birth flow) took the glass */
void touch_port_set_inverted(bool inverted);   /* mirror coords when the screen is flipped */
/* reset confirm prompt (render_confirm_reset): opened by main.c's chord -
 * BOOT held, then a finger lands on the glass - or the director's `reset`.
 * While it is up every other gesture is swallowed; a press AND release on
 * the same button answers it, and it answers NO by itself after 20 s (or
 * when the tank goes to sleep). The answer is one-shot: main.c takes it
 * once per frame and a YES wipes the tank. */
void touch_port_confirm_open(void);
bool touch_port_confirm_answer(int ans);       /* +1 yes / -1 no; false = no prompt up */
bool touch_port_confirm_up(void);
bool touch_port_down(void);                    /* a finger is on the glass right now (the night's frame rate, main.c) */
float touch_port_confirm_frac(void);           /* time left before it gives up, 1 -> 0 */
int  touch_port_confirm_take(void);            /* +1 / -1 once, then 0 */
bool touch_port_pressed_since(int64_t us);     /* a finger is down and landed after `us` */
/* the settings page (2026-09-15): opened from the main menu's SETTINGS (the
 * milestones page's SETTINGS button until 2026-10-21); a tap on a segment is handed to main as SET_TAP_BRIGHT / _VOLUME
 * with its value (one-shot), CLOSE ends the page */
bool touch_port_settings(void);
void touch_port_show_settings(bool on);
int  touch_port_take_setting(int *value);       /* SET_TAP_* or 0 */
/* the shop page (2026-09-15): up / show it; an UNLOCK or MOVE tapped hands
 * the raw tap code (SHOP_TAP_BUY / SHOP_TAP_MOVE + item; 0 = none) to main
 * once, which buys (progression_buy) or opens the placement page */
bool touch_port_shop(void);
void touch_port_show_shop(bool on);
int  touch_port_take_shop(void);
/* the battery pill and its page (2026-09-24): main.c says each frame whether
 * it drew the pill (a tap in RENDER_BAT_HIT counts only then); the page is up
 * until any release, or 30 s; touch_port_show_battery is the director's cue */
void touch_port_set_pill(bool up);
bool touch_port_battery(void);
void touch_port_show_battery(bool on);
/* the UPDATES page (2026-09-30, docs/OTA.md): from the settings page's
 * UPDATES button; its CHECK is handed to main once (UPD_TAP_CHECK), which
 * saves and restarts into update mode; CLOSE returns to the settings page */
bool touch_port_updates(void);
void touch_port_show_updates(bool on);
int  touch_port_take_update(void);              /* UPD_TAP_CHECK once, else 0 */
/* update mode's raw read (no tank, no pages): the finger in tank space, true while down */
bool touch_port_read_raw(float *x, float *y);
void touch_port_set_bias(int px);              /* finger-landing correction: reported y moves up by px */
int  touch_port_bias(void);
void touch_port_set_log(bool on);              /* one log line per press, at its release (director `touch log on|off`) */
/* the round board's lift rule (2026-10-03): silence of ms inside a press is a
 * lift (0 = keep), and said 1/0 = a "no finger" report lifts at once or not
 * (-1 = keep); director `touch lift <ms>` / `touch said on|off`, not saved */
void touch_port_set_lift(int ms, int said);
int  touch_port_lift_ms(void);
bool touch_port_lift_said(void);
void touch_port_raw_seen(int *x0, int *x1, int *y0, int *y1);   /* the raw report's extremes since boot, in tank space (x1 < x0: no touch yet) */
#endif
