/* ---- the submarine (2026-10-10 night, item 9) ---- a little sub cruising the water: a hull with a
 * conning tower, dive planes, a rudder, portholes, a two-blade propeller spinning behind it while it
 * moves, a stream of bubbles from the stern; at a stop (helper.c sub.stop) the propeller idles and a
 * periscope rises from the tower, its head turning to look both ways before it sinks again. The
 * hull is the theme's: Original the yellow sub with black trim, Quiet Lagoon a moss-green navy boat
 * with brass, Tidepool Club a red-and-white toy with a cyan tower, Blackwater a grey steel boat with
 * a black waterline and rust. Live every frame; the rect it and its bubbles cover comes back. */
static void draw_submarine(ctx_t *c, const tank_t *t, int *bx0, int *by0, int *bx1, int *by1) {
    float clock = t->clock;
    float fx = t->sub.x, fy = t->sub.y + sinf(clock * 0.9f) * 1.5f;
    float d = t->sub.yaw >= 0 ? 1 : -1, sx = fabsf(t->sub.yaw) < 0.2f ? 0.2f : fabsf(t->sub.yaw);
    int th = theme_active();
    uint32_t hull  = th == THEME_QUIET_LAGOON ? 0x5f8f62 : th == THEME_TIDEPOOL_CLUB ? 0xe8524a : th == THEME_BLACKWATER ? 0x56606a : 0xf2c230;
    uint32_t belly = th == THEME_QUIET_LAGOON ? 0x3f6a48 : th == THEME_TIDEPOOL_CLUB ? 0xfff4e0 : th == THEME_BLACKWATER ? 0x23282d : 0xd9a520;
    uint32_t trim  = th == THEME_QUIET_LAGOON ? 0xc9a55a : th == THEME_TIDEPOOL_CLUB ? 0x2fb8c8 : th == THEME_BLACKWATER ? 0x8a3a2a : 0x3a3f44;
    uint32_t tower = th == THEME_QUIET_LAGOON ? 0x4e7a52 : th == THEME_TIDEPOOL_CLUB ? 0x2fb8c8 : th == THEME_BLACKWATER ? 0x3e464e : 0xf2c230;
    uint32_t glass = th == THEME_QUIET_LAGOON ? 0xc4e6d1 : th == THEME_TIDEPOOL_CLUB ? 0xfff6d9 : th == THEME_BLACKWATER ? 0xaee6f0 : 0x9fd8e2;
    uint32_t lit   = mix(hull, 0xffffff, 0.35f), metal = th == THEME_TIDEPOOL_CLUB ? 0xffc040 : 0xb8c0c6;
    uint32_t mist  = th == THEME_TIDEPOOL_CLUB ? 0x3d8e83 : th == THEME_BLACKWATER ? 0xc8e8ee : th == THEME_QUIET_LAGOON ? 0xc4e6d1 : 0x9fd8e2;
#define FX(lx) (fx + (lx) * d * sx)
    bool moving = t->sub.v > 1.5f;
    /* the bubbles: a stream from the stern while moving (fast, many), a lazy few from the tower at a stop */
    int nb = moving ? 14 : 5;
    for (int i = 0; i < nb; i++) {
        float ph = fmodf(clock * (moving ? 0.55f + 0.04f * (i % 4) : 0.3f + 0.03f * (i % 3)) + i * 0.071f, 1.0f);
        float ox = moving ? -28 - ph * 26 * sx : 2, oy = moving ? -2 - ph * 34 : -9 - ph * 40;
        float bx = FX(ox) + sinf(clock * 2.1f + i * 1.9f) * (1 + ph * 3), by = fy + oy;
        float r = 0.6f + ph * (moving ? 1.1f : 1.5f); int a = (int)(150 * (1 - ph * 0.8f));
        fill_ellipse(c, bx, by, r, r, mist, a);
        if (r > 1.2f) px_blend(c, (int)(bx - 0.4f), (int)(by - 0.5f), 0xffffff, 110);
    }
    if (th == THEME_BLACKWATER) {
        /* the abyss (2026-10-12): a HEADLIGHT from the bow, a cone of pale light widening over 40 px, three
           nested wedges of a few alpha each, undimmed - the sub lights its way at night */
        src_t beam = src_color(0xbfefff, 1);
        for (int i = 0; i < 3; i++) {
            float L = (42 - i * 9) * sx, hw = (11 - i * 2.5f) * fminf(1, L / 20);
            float px[4] = { FX(24), FX(24) + L * d, FX(24) + L * d, FX(24) }, py[4] = { fy - 1.2f, fy - hw, fy + hw, fy + 1.2f };
            fill_poly_s(c, px, py, 4, &beam, 16);
        }
    }
    /* the propeller behind the stern: two blades, spinning on the clock while moving, idling at a stop */
    { float spin = moving ? clock * 22 : clock * 1.5f, bl = fabsf(sinf(spin));
      thick_line(c, FX(-24), fy, FX(-29), fy, 1.6f, metal);                                           /* the shaft */
      fill_ellipse(c, FX(-29.5f), fy, 1.2f * sx + 0.3f, 5.5f * bl + 0.6f, metal, 230);
      fill_ellipse(c, FX(-29.5f), fy, 1.2f * sx + 0.3f, 5.5f * (1 - bl) + 0.6f, mix(metal, 0x303438, 0.4f), 200); }
    /* the rudder and the dive planes */
    { float rx[4] = { FX(-20), FX(-26), FX(-27), FX(-21) }, ry[4] = { fy - 5, fy - 10, fy - 10, fy - 2 }; fill_poly(c, rx, ry, 4, trim); }
    { float rx[4] = { FX(-20), FX(-26), FX(-27), FX(-21) }, ry[4] = { fy + 5, fy + 10, fy + 10, fy + 2 }; fill_poly(c, rx, ry, 4, trim); }
    fill_ellipse(c, FX(-19), fy + 1, 5 * sx + 0.4f, 1.4f, trim, 255);                                    /* the stern planes */
    /* the hull: a long ellipse, the belly dark, a highlight along the top */
    fill_ellipse(c, FX(0), fy, 25 * sx + 0.8f, 7.5f, hull, 255);
    fill_ellipse(c, FX(0), fy + 3.2f, 23 * sx + 0.6f, 3.6f, belly, 255);
    fill_ellipse(c, FX(2), fy - 4.3f, 15 * sx + 0.4f, 1.3f, lit, 200);
    if (th == THEME_BLACKWATER) { fill_ellipse(c, FX(-8), fy + 1, 3 * sx + 0.3f, 1.2f, 0x7a4a30, 150); fill_ellipse(c, FX(10), fy + 2, 2 * sx + 0.3f, 0.9f, 0x7a4a30, 140); }   /* rust */
    if (th == THEME_TIDEPOOL_CLUB) { fill_ellipse(c, FX(-10), fy, 2.2f * sx + 0.3f, 7, belly, 255); fill_ellipse(c, FX(12), fy, 2.2f * sx + 0.3f, 6.5f, belly, 255); }   /* the toy's white bands */
    /* the bow planes and three portholes */
    fill_ellipse(c, FX(14), fy + 1, 4 * sx + 0.3f, 1.2f, trim, 255);
    for (int i = 0; i < 3; i++) {
        float px = FX(-9 + i * 9);
        fill_ellipse(c, px, fy - 0.5f, 1.9f * sx + 0.4f, 1.9f, trim, 255);
        if (th == THEME_BLACKWATER) { src_t lamp = src_color(0xfff0c0, 1); fill_ellipse_s(c, px, fy - 0.5f, 1.2f * sx + 0.3f, 1.2f, &lamp, 255); }   /* lit from within, undimmed */
        else fill_ellipse(c, px, fy - 0.5f, 1.2f * sx + 0.3f, 1.2f, glass, 255);
        px_blend(c, (int)(px - 0.5f), (int)(fy - 1.3f), 0xffffff, 140);
    }
    /* the conning tower, its window, a rail */
    { float tx[4] = { FX(-5), FX(7), FX(6), FX(-3) }, ty[4] = { fy - 6, fy - 6, fy - 14, fy - 14 }; fill_poly(c, tx, ty, 4, tower); }
    if (th == THEME_BLACKWATER) { src_t lamp = src_color(0xfff0c0, 1); fill_ellipse_s(c, FX(4), fy - 10, 1.5f * sx + 0.3f, 1.5f, &lamp, 255); }
    else fill_ellipse(c, FX(4), fy - 10, 1.5f * sx + 0.3f, 1.5f, glass, 255);
    thick_line(c, FX(-3), fy - 14.5f, FX(6), fy - 14.5f, 1.0f, trim);
    /* the periscope: rises from the tower at a stop, the head turning to look each way */
    if (t->sub.peri > 0.02f) {
        float up = t->sub.peri * 13, px = FX(1), top = fy - 14 - up;
        float look = sinf(t->sub.look * 1.7f);                                                       /* -1 left .. 1 right */
        thick_line(c, px, fy - 14, px, top, 1.6f, metal);
        thick_line(c, px, top + 0.5f, px + look * 4.5f, top + 0.5f, 1.8f, metal);                     /* the head, turned */
        fill_ellipse(c, px + look * 4.8f, top + 0.5f, 1.3f, 1.3f, 0x20262a, 255);                     /* the lens */
        px_blend(c, (int)(px + look * 4.8f), (int)(top), 0xffffff, 200);                               /* its glint */
    }
#undef FX
    *bx0 = (int)fx - 70; *bx1 = (int)fx + 70; *by0 = (int)fy - 62; *by1 = (int)fy + 14;   /* 70: the Blackwater headlight reaches 66 */
}
