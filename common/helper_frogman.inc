/* ---- the frogman (2026-10-10, item 8; he hung from the wreck's stern that morning) ----
 * A little diver floating SIDEWAYS: horizontal, face forward, air tank on his back (the
 * top), flippers kicking behind him, drifting across the water (helper.c: frogman.x, frogman.yaw -
 * the body thins through a turn, as a fish does), bobbing on the water's slow lift. His
 * arm reaches forward - and every 23 s, for 2.6 s, it goes up and the forearm waves from the
 * elbow. A mist of fine bubbles rises from his mask. The suit is brighter than the wreck's
 * old grey diver: a yellow-and-black banded wetsuit with red fins (each theme had its own:
 * git tag multi-board-multi-theme). Live every frame; the rect he and his mist cover comes back. */
static void draw_frogman(ctx_t *c, const tank_t *t, int *bx0, int *by0, int *bx1, int *by1) {
    float clock = t->clock;
    float fx = t->frogman.x, fy = t->frogman.y + sinf(clock * 1.4f) * 2.5f + sinf(clock * 0.6f + 1) * 1.5f;
    float d = t->frogman.yaw >= 0 ? 1 : -1, sx = fabsf(t->frogman.yaw) < 0.15f ? 0.15f : fabsf(t->frogman.yaw);
    uint32_t suit  = 0xf5c242, band = 0x1d2a33, fin = 0xd8402c, tankc = 0xb8c4cc;
    uint32_t glass = 0x9fd8e2, mask = 0x1d2a33, skin = 0xf1c69b, mist = 0x9fd8e2;
#define FX(lx) (fx + (lx) * d * sx)                                                /* local x along the body, + = forward */
    float kick = sinf(clock * 2.6f) * 2.5f;
    /* the mist: a dozen fine bubbles rising from the mask, each on its own slow loop; behind him */
    for (int i = 0; i < 12; i++) {
        float ph = fmodf(clock * (0.42f + 0.03f * (i % 4)) + i * 0.083f, 1.0f);
        float bx = FX(10) + sinf(clock * 2.5f + i * 1.7f) * (1.5f + ph * 4) + (i % 3 - 1) * 1.5f, by = fy - 7 - ph * 46;
        float r = 0.6f + ph * 1.3f; int a = (int)(150 * (1 - ph * 0.8f));
        fill_ellipse(c, bx, by, r, r, mist, a);
        if (r > 1.2f) px_blend(c, (int)(bx - 0.4f), (int)(by - 0.5f), 0xffffff, 110);
    }
    /* the legs and flippers, behind: from the hips, kicking against each other */
    thick_line(c, FX(-7), fy - 1.5f, FX(-16), fy - 1.5f + kick * 0.5f, 2.6f, suit);
    thick_line(c, FX(-7), fy + 1.5f, FX(-16), fy + 1.5f - kick * 0.5f, 2.6f, suit);
    fill_ellipse(c, FX(-20), fy - 1.5f + kick, 4.5f * sx + 0.6f, 1.8f, fin, 255);
    fill_ellipse(c, FX(-20), fy + 1.5f - kick, 4.5f * sx + 0.6f, 1.8f, fin, 255);
    /* the air tank along his back (the top), its valve toward the head */
    fill_ellipse(c, FX(-1), fy - 5.2f, 7 * sx + 0.4f, 2.4f, tankc, 255);
    fill_ellipse(c, FX(6.5f), fy - 5.2f, 1.2f, 1.2f, 0x565a5d, 255);
    px_blend(c, (int)(FX(-3) + 0.5f), (int)(fy - 6), 0xffffff, 120);
    /* the torso, with its bands */
    fill_ellipse(c, FX(0), fy, 9 * sx + 0.5f, 4.2f, suit, 255);
    fill_ellipse(c, FX(-4), fy, 1.3f * sx + 0.4f, 4.0f, band, 255);
    fill_ellipse(c, FX(3), fy, 1.3f * sx + 0.4f, 4.0f, band, 255);
    /* the arm: forward along the body, or up and waving (every 23 s, for 2.6 s) */
    float wph = fmodf(clock, 23.0f);
    if (wph < 2.6f) {
        float swing = sinf(clock * 9.0f) * 0.55f, lift = fminf(1, wph * 4) * fminf(1, (2.6f - wph) * 4);
        float ex = FX(4), ey = fy - 3 - 6 * lift, hx = ex + sinf(swing) * 7 * lift * d + (1 - lift) * 6 * d * sx, hy = ey - cosf(swing) * 7 * lift;
        thick_line(c, FX(2), fy - 1, ex, ey, 2.4f, suit);
        thick_line(c, ex, ey, hx, hy, 2.2f, suit);
        fill_ellipse(c, hx, hy, 1.7f, 1.7f, band, 255);
    } else {
        thick_line(c, FX(3), fy + 1, FX(12), fy + 2.5f, 2.4f, suit);
        fill_ellipse(c, FX(12.5f), fy + 2.5f, 1.6f, 1.6f, band, 255);
    }
    /* the head: a hood, the face, the mask and its glass, the regulator's hose back to the tank */
    thick_line(c, FX(9), fy - 1, FX(6), fy - 4.5f, 1.2f, 0x3a3d40);
    fill_ellipse(c, FX(11), fy - 1, 4.2f * sx + 0.8f, 4.2f, suit, 255);
    fill_ellipse(c, FX(12.5f), fy - 0.5f, 2.6f * sx + 0.4f, 2.6f, skin, 255);
    fill_ellipse(c, FX(12.5f), fy - 2, 2.9f * sx + 0.4f, 1.9f, mask, 255);
    fill_ellipse(c, FX(12.8f), fy - 2, 2.0f * sx + 0.2f, 1.2f, glass, 255);
    px_blend(c, (int)(FX(13.5f) + 0.5f), (int)(fy - 2.5f), 0xffffff, 170);
#undef FX
    *bx0 = (int)fx - 46; *bx1 = (int)fx + 46; *by0 = (int)fy - 58; *by1 = (int)fy + 12;   /* 46: room Blackwater's torch beam needed (43), kept */
}
