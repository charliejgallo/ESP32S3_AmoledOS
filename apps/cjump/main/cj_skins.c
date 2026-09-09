/*
 * CLAUDE JUMP - the critter and its costumes
 *
 * The critter is drawn in code and not as an ASCII sprite (cj_blit) for a
 * concrete reason: cj_pal's palette is fixed, and here the SAME drawing has to
 * come out in sixteen different body colours. With a sprite there would have
 * to be sixteen copies of the same figure or a tinting mechanism would have to
 * be invented; drawing it with primitives, the colour is an argument.
 *
 * The costume is a few more rectangles on top, and everything it adds has to
 * fit within HERO_BOX_W x HERO_BOX_H: that is the rectangle that gets dirtied,
 * and whatever runs outside it leaves a trail stuck on the screen. The box is
 * centred horizontally on the body and sticks out 14 px above, which is the
 * height of the tallest costume (the wizard's hat).
 */
#include "cjump.h"

#include "aos_i18n.h"

/* --------------------------------------------------------------------------
 * The catalogue
 *
 * The names are marked with N_() and translated in the shop with _(). They go
 * into LVGL labels, not onto the canvas, so they may carry accents: that is
 * the difference from 2043 or arkanos, which draw their text with a bitmap
 * font of their own and have their catalogues transliterated.
 *
 * The first three cost 0 and are the ones the game comes with (CJ_SKINS_FREE).
 * -------------------------------------------------------------------------- */
const cj_skin_t cj_skins[CJ_SKINS] = {
    /*  name                   price   body      shadow   */
    { N_("Clásico"),                0, 0xD97757, 0x8E4630 },
    { N_("Nerd"),                   0, 0xD97757, 0x8E4630 },
    { N_("Café"),                   0, 0xC98A5E, 0x7E4E2C },
    { N_("Idea"),                  30, 0xE8B04B, 0x9A6A18 },
    { N_("Auriculares"),           50, 0xD97757, 0x8E4630 },
    { N_("Obrero"),                70, 0xCF7A46, 0x86451F },
    { N_("Dormilón"),              90, 0x9B7BC8, 0x5B4382 },
    { N_("Graduado"),             120, 0xD97757, 0x8E4630 },
    { N_("Hipnótico"),            150, 0xB86FD6, 0x6E3A88 },
    { N_("Navegador"),            190, 0x4FB6A8, 0x246B62 },
    { N_("Forzudo"),              240, 0xE0603C, 0x93301B },
    { N_("Mago"),                 300, 0x8E6BE0, 0x4C3499 },
    { N_("Ninja"),                380, 0x596273, 0x2C323E },
    { N_("Rey"),                  460, 0xE3B23C, 0x8F6A10 },
    { N_("Astronauta"),           560, 0xC8CEDC, 0x7C8394 },
    { N_("Fiesta"),               700, 0xF06BA8, 0x953961 },
};

/* --------------------------------------------------------------------------
 * Dirty box
 * -------------------------------------------------------------------------- */

void cj_hero_box(int x, int y, cj_rect_t *out)
{
    int cx = x + HERO_W / 2;
    out->x0 = (int16_t)(cx - HERO_BOX_W / 2);
    out->y0 = (int16_t)(y + HERO_H - HERO_BOX_H);   /* it sticks out upwards */
    out->x1 = (int16_t)(out->x0 + HERO_BOX_W);
    out->y1 = (int16_t)(out->y0 + HERO_BOX_H);
}

/* --------------------------------------------------------------------------
 * The body
 * -------------------------------------------------------------------------- */

static void legs(cj_buf_t *b, int x, int y, uint16_t c, int pose)
{
    /* Six little legs. Climbing they stretch and falling they tuck up: it is
     * the critter's only animation and it is enough for the state to read. */
    static const int8_t off[6] = { 1, 4, 7, 10, 13, 16 };
    int base = y + HERO_H - 4;

    for (int i = 0; i < 6; i++) {
        int len = 3;
        if (pose == 1) {
            len = (i & 1) ? 4 : 2;      /* climbing: tucked up and stretched  */
        } else if (pose == 2) {
            len = 4;                    /* falling: dangling                  */
        }
        cj_vline(b, x + off[i], base, len, c);
    }
}

/* A dark body swallows the eyes, which are black. It happened to the Ninja: in
 * the test bench's grid it was a grey rectangle with a headband and no face.
 * With a dark body the eyes get white behind them; with a light one they go
 * bare black, which is how the real critter has them. */
static bool dark_body(int skin)
{
    uint32_t c = cj_skins[skin].body;
    uint32_t lum = (((c >> 16) & 0xFF) * 30 + ((c >> 8) & 0xFF) * 59 +
                    (c & 0xFF) * 11) / 100;
    return lum < 120;
}

static void eyes(cj_buf_t *b, int x, int y, int skin, int pose)
{
    const uint16_t ink   = cj_rgb(0x1A1010);
    const uint16_t white = cj_rgb(0xFFFFFF);

    int ey = y + 5;

    if (skin == 6) {                    /* Sleepyhead: eyes closed            */
        cj_hline(b, x + 4, ey + 1, 3, ink);
        cj_hline(b, x + 11, ey + 1, 3, ink);
        return;
    }
    if (skin == 8) {                    /* Hypnotic: spiral eyes              */
        cj_ring(b, x + 5, ey + 1, 2, ink);
        cj_ring(b, x + 12, ey + 1, 2, ink);
        cj_px(b, x + 5, ey + 1, ink);
        cj_px(b, x + 12, ey + 1, ink);
        return;
    }

    if (pose == 2) {                    /* falling: the eyes open             */
        cj_rect(b, x + 4, ey - 1, 3, 4, white);
        cj_rect(b, x + 11, ey - 1, 3, 4, white);
        cj_rect(b, x + 5, ey, 2, 2, ink);
        cj_rect(b, x + 12, ey, 2, 2, ink);
        return;
    }
    if (dark_body(skin)) {
        cj_rect(b, x + 4, ey, 4, 3, white);
        cj_rect(b, x + 10, ey, 4, 3, white);
        cj_rect(b, x + 5, ey, 2, 2, ink);
        cj_rect(b, x + 11, ey, 2, 2, ink);
        return;
    }
    cj_rect(b, x + 4, ey, 3, 3, ink);
    cj_rect(b, x + 11, ey, 3, 3, ink);
}

static void mouth(cj_buf_t *b, int x, int y, int pose)
{
    const uint16_t white = cj_rgb(0xFFFFFF);
    if (pose == 2) {
        /* mouth open in fright */
        cj_round(b, x + 7, y + 9, 5, 4, 1, white);
        return;
    }
    cj_round(b, x + 6, y + 9, 7, 3, 1, white);
}

/* --------------------------------------------------------------------------
 * The costumes
 *
 * Each one draws on top of the body. Nothing may run outside the box: 5 px on
 * each side of the body and 14 px above.
 * -------------------------------------------------------------------------- */

static void accessory(cj_buf_t *b, int x, int y, int skin, int pose)
{
    const uint16_t ink = cj_rgb(0x1A1010);
    int cx  = x + HERO_W / 2;
    int top = y;                        /* top edge of the head               */

    switch (skin) {
    case 1: {                           /* Nerd: the critter's glasses        */
        uint16_t frame = cj_rgb(0x6A2FB5);
        uint16_t lens  = cj_rgb(0x7BE9FF);
        cj_rect(b, x + 2, y + 3, 6, 6, lens);
        cj_rect(b, x + 10, y + 3, 6, 6, lens);
        cj_frame(b, x + 2, y + 3, 6, 6, frame);
        cj_frame(b, x + 10, y + 3, 6, 6, frame);
        cj_hline(b, x + 8, y + 5, 2, frame);
        cj_rect(b, x + 4, y + 5, 2, 2, ink);
        cj_rect(b, x + 12, y + 5, 2, 2, ink);
        break;
    }
    case 2: {                           /* Coffee: the cup in its left hand   */
        uint16_t cup = cj_rgb(0xF2F2F5);
        uint16_t lid = cj_rgb(0x2A6B4F);
        uint16_t hot = cj_rgb(0xD8DCE6);
        cj_rect(b, x - 5, y + 4, 5, 7, cup);
        cj_rect(b, x - 5, y + 3, 5, 2, lid);
        cj_hline(b, x - 5, y + 8, 5, cj_rgb(0x2E6B4F));
        cj_px(b, x - 4, y, hot);
        cj_px(b, x - 3, y - 2, hot);
        cj_px(b, x - 4, y - 4, hot);
        break;
    }
    case 3: {                           /* Idea: the lit bulb                 */
        uint16_t glass = cj_rgb(0xFFE45E);
        uint16_t glow  = cj_rgb(0xFFB800);
        cj_glow(b, cx, top - 8, 6, glow, 6);
        cj_disc(b, cx, top - 8, 4, glass);
        cj_rect(b, cx - 2, top - 4, 4, 2, cj_rgb(0x8E6A2A));
        cj_hline(b, cx - 2, top - 2, 4, cj_rgb(0x5B4318));
        cj_px(b, cx - 7, top - 12, glass);
        cj_px(b, cx + 6, top - 12, glass);
        cj_px(b, cx, top - 14, glass);
        break;
    }
    case 4: {                           /* Headphones                         */
        uint16_t band = cj_rgb(0x4A9DF5);
        uint16_t cups = cj_rgb(0x1F4FBF);
        cj_hline(b, x + 2, top - 3, 14, band);
        cj_px(b, x + 1, top - 2, band);
        cj_px(b, x + 16, top - 2, band);
        cj_rect(b, x - 2, top - 1, 4, 7, cups);
        cj_rect(b, x + 16, top - 1, 4, 7, cups);
        break;
    }
    case 5: {                           /* Builder: the hard hat              */
        uint16_t hat = cj_rgb(0xFFD60A);
        uint16_t sh  = cj_rgb(0xC08A00);
        cj_round(b, x + 2, top - 5, 14, 6, 2, hat);
        cj_rect(b, x - 1, top - 1, 20, 2, hat);
        cj_hline(b, x - 1, top + 1, 20, sh);
        cj_vline(b, cx, top - 5, 5, sh);
        break;
    }
    case 6: {                           /* Sleepyhead: nightcap and snores    */
        uint16_t cap = cj_rgb(0x4A9DF5);
        cj_round(b, x + 2, top - 5, 14, 6, 2, cap);
        cj_rect(b, x + 12, top - 9, 4, 4, cap);
        cj_text(b, cx + 2, top - 13, "Z", cj_rgb(0xFFFFFF));
        cj_text(b, cx + 8, top - 8, "Z", cj_rgb(0xD5DCEB));
        break;
    }
    case 7: {                           /* Graduate: the mortarboard         */
        uint16_t cap = cj_rgb(0x232B41);
        uint16_t tas = cj_rgb(0xFFD60A);
        cj_rect(b, x + 4, top - 4, 10, 3, cap);
        cj_rect(b, x - 1, top - 6, 20, 2, cap);
        cj_vline(b, x + 18, top - 5, 5, tas);
        cj_px(b, x + 18, top, tas);
        break;
    }
    case 8: {                           /* Hypnotic: the spiral              */
        uint16_t a = cj_rgb(0x6A2FB5);
        uint16_t w = cj_rgb(0xE9DEFF);
        cj_disc(b, cx, top - 7, 6, a);
        cj_ring(b, cx, top - 7, 4, w);
        cj_ring(b, cx, top - 7, 2, w);
        cj_px(b, cx, top - 7, w);
        break;
    }
    case 9: {                           /* Browser: the little window        */
        uint16_t win = cj_rgb(0xF2F2F5);
        uint16_t bar = cj_rgb(0x2AAE9B);
        cj_rect(b, cx - 8, top - 11, 16, 10, win);
        cj_rect(b, cx - 8, top - 11, 16, 3, bar);
        cj_px(b, cx - 6, top - 10, cj_rgb(0xFFFFFF));
        cj_px(b, cx - 4, top - 10, cj_rgb(0xFFFFFF));
        cj_px(b, cx - 2, top - 10, cj_rgb(0xFFFFFF));
        cj_hline(b, cx - 6, top - 6, 10, cj_rgb(0x9AA3B8));
        cj_hline(b, cx - 6, top - 4, 7, cj_rgb(0x9AA3B8));
        break;
    }
    case 10: {                          /* Strongman: arms and a lightning bolt */
        uint16_t arm = cj_rgb(0xE0603C);
        uint16_t sh  = cj_rgb(0x93301B);
        cj_round(b, x - 5, y + 2, 5, 7, 1, arm);
        cj_round(b, x + 18, y + 2, 5, 7, 1, arm);
        cj_hline(b, x - 5, y + 8, 5, sh);
        cj_hline(b, x + 18, y + 8, 5, sh);
        uint16_t bolt = cj_rgb(0x4ADE80);
        cj_vline(b, cx + 1, top - 8, 4, bolt);
        cj_vline(b, cx - 1, top - 4, 4, bolt);
        cj_px(b, cx, top - 5, bolt);
        break;
    }
    case 11: {                          /* Wizard: the pointed hat           */
        uint16_t hat = cj_rgb(0x6A2FB5);
        uint16_t rim = cj_rgb(0x4C3499);
        cj_rect(b, x - 1, top - 3, 20, 2, rim);
        cj_rect(b, x + 3, top - 6, 12, 3, hat);
        cj_rect(b, x + 5, top - 9, 8, 3, hat);
        cj_rect(b, x + 7, top - 12, 4, 3, hat);
        cj_rect(b, x + 8, top - 14, 2, 2, hat);
        cj_px(b, x + 4, top - 5, cj_rgb(0xFFE45E));
        cj_px(b, x + 12, top - 8, cj_rgb(0xFFE45E));
        break;
    }
    case 12: {                          /* Ninja: the headband               */
        uint16_t band = cj_rgb(0xC0246A);
        cj_rect(b, x, y + 2, 18, 3, band);
        cj_rect(b, x - 5, y + 3, 5, 2, band);
        cj_px(b, x - 5, y + 6, band);
        cj_px(b, x - 4, y + 6, band);
        break;
    }
    case 13: {                          /* King: the crown                   */
        uint16_t g = cj_rgb(0xFFD60A);
        uint16_t d = cj_rgb(0xC08A00);
        cj_rect(b, x + 2, top - 4, 14, 3, g);
        cj_vline(b, x + 3, top - 7, 3, g);
        cj_vline(b, cx, top - 8, 4, g);
        cj_vline(b, x + 14, top - 7, 3, g);
        cj_px(b, x + 3, top - 8, cj_rgb(0xFF6FAE));
        cj_px(b, x + 14, top - 8, cj_rgb(0xFF6FAE));
        cj_hline(b, x + 2, top - 1, 14, d);
        break;
    }
    case 14: {                          /* Astronaut: the helmet             */
        uint16_t vis = cj_rgb(0x7BE9FF);
        cj_ring(b, cx, y + 5, 10, cj_rgb(0xFFFFFF));
        cj_ring(b, cx, y + 5, 9, cj_rgb(0xD5DCEB));
        cj_px(b, cx - 6, y + 1, vis);
        cj_px(b, cx - 5, y, vis);
        cj_vline(b, cx + 9, y - 5, 5, cj_rgb(0xD5DCEB));
        cj_disc(b, cx + 9, y - 6, 1, cj_rgb(0xFF4A3D));
        break;
    }
    case 15: {                          /* Party: party hat and confetti     */
        uint16_t a = cj_rgb(0x2AF0C8);
        uint16_t c = cj_rgb(0xFFE45E);
        cj_rect(b, x + 5, top - 3, 8, 2, a);
        cj_rect(b, x + 6, top - 6, 6, 3, cj_rgb(0xFF6FAE));
        cj_rect(b, x + 7, top - 8, 4, 2, a);
        cj_disc(b, x + 9, top - 9, 1, c);
        cj_px(b, x - 3, top - 6, cj_rgb(0x4ADE80));
        cj_px(b, x + 20, top - 4, cj_rgb(0x4A9DF5));
        cj_px(b, x - 2, top + 2, c);
        cj_px(b, x + 21, top + 4, cj_rgb(0xFF6FAE));
        break;
    }
    default:
        break;
    }
    (void)pose;
}

/* --------------------------------------------------------------------------
 * The whole critter
 * -------------------------------------------------------------------------- */

void cj_hero_draw(cj_buf_t *b, int x, int y, int skin, int pose, int sq,
                  int facing, bool rocket)
{
    if (skin < 0 || skin >= CJ_SKINS) {
        skin = 0;
    }
    const cj_skin_t *s = &cj_skins[skin];
    uint16_t body  = cj_rgb(s->body);
    uint16_t shade = cj_rgb(s->shade);

    /* The bounce's squash: the body flattens and drops, so the contact with
     * the platform is visible. It is four frames. */
    int h = HERO_H - 4;
    int w = HERO_W;
    if (sq > 0) {
        h -= sq;
        w += sq;
        x -= sq / 2;
        y += sq;
    }

    if (rocket) {
        /* The flame goes UNDER the body and inside the box: HERO_BOX_H sticks
         * out upwards, so there is little room here. Two rows. */
        uint16_t f1 = cj_rgb(0xFFD60A);
        uint16_t f2 = cj_rgb(0xFF6A00);
        cj_rect(b, x + 5, y + HERO_H - 2, 8, 2, f1);
        cj_rect(b, x + 7, y + HERO_H - 1, 4, 1, f2);
    }

    legs(b, x, y, shade, pose);

    cj_round(b, x, y, w, h, 2, body);
    cj_hline(b, x + 1, y + h - 1, w - 2, shade);
    cj_hline(b, x + 2, y, w - 4, cj_tone(body, 2));

    eyes(b, x, y, skin, pose);
    mouth(b, x, y, pose);

    /* Looking the way it is going: shifting the eyes is enough, and it is one
     * pixel of work against having one sprite per direction. */
    if (facing == 1) {
        cj_vline(b, x + 1, y + 5, 3, shade);
    } else if (facing == 2) {
        cj_vline(b, x + w - 2, y + 5, 3, shade);
    }

    accessory(b, x, y, skin, pose);
}
