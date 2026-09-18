/*
 * BLACKJACK - the bench, without the board and without LVGL
 *
 *   cc -O2 -I apps/blackjack/main apps/blackjack/tools/bj_harness.c \
 *      apps/blackjack/main/bj_art.c apps/blackjack/main/bj_court.c \
 *      apps/blackjack/main/bj_game.c -lm -o /tmp/bjh
 *
 *   /tmp/bjh sheet out.ppm      the 52 cards, the back, the chips and some
 *                               piles on the felt, to look at
 *   /tmp/bjh felt out.ppm       the table alone, as the app shows it
 *   /tmp/bjh play [N] [seed]    N rounds by a basic-strategy bot, checking
 *                               the rules and the money after every step
 *   /tmp/bjh time               what each drawing costs here (multiply by
 *                               twenty or thirty for the board)
 *
 * ppm -> png with tools/ppm2png.py.
 */
#include "bj_art.h"
#include "bj_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- images -------------------------------------------------------------- */

static void blit(bj_img_t *dst, const bj_img_t *src, int x0, int y0)
{
    for (int y = 0; y < src->h; y++) {
        for (int x = 0; x < src->w; x++) {
            int dx = x0 + x, dy = y0 + y;
            if (dx < 0 || dy < 0 || dx >= dst->w || dy >= dst->h) {
                continue;
            }
            uint32_t s = src->px[y * src->w + x];
            int      a = (int)(s >> 24);
            if (!a) {
                continue;
            }
            uint32_t d = dst->px[dy * dst->w + dx];
            int r = (int)(((s >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * (255 - a)) / 255;
            int g = (int)(((s >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * (255 - a)) / 255;
            int b = (int)((s & 0xFF) * a + (d & 0xFF) * (255 - a)) / 255;
            dst->px[dy * dst->w + dx] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

static int write_ppm(const char *path, const bj_img_t *m, int zoom)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", m->w * zoom, m->h * zoom);
    for (int y = 0; y < m->h * zoom; y++) {
        for (int x = 0; x < m->w * zoom; x++) {
            uint32_t c = m->px[(y / zoom) * m->w + x / zoom];
            unsigned char p[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
            fwrite(p, 1, 3, f);
        }
    }
    fclose(f);
    return 0;
}

static bj_img_t img_new(int w, int h)
{
    bj_img_t m = { (uint32_t *)calloc((size_t)w * h, 4), w, h };
    return m;
}

static void fill(bj_img_t *m, uint32_t c)
{
    for (int i = 0; i < m->w * m->h; i++) {
        m->px[i] = 0xFF000000u | c;
    }
}

/* --- sheet ---------------------------------------------------------------- */

static int cmd_sheet(const char *out, int zoom)
{
    const int cols = 13, gx = BJ_CV_W - 2, gy = BJ_CV_H + 2;
    bj_img_t  sheet = img_new(cols * gx + 8, 5 * gy + BJ_STACK_H + 16);
    fill(&sheet, 0x17693D);
    bj_img_t card = img_new(BJ_CV_W, BJ_CV_H);
    for (int c = 0; c < 52; c++) {
        bj_card_draw(&card, c);
        blit(&sheet, &card, 4 + (c % 13) * gx, 4 + (c / 13) * gy);
    }
    bj_card_draw(&card, -1);
    blit(&sheet, &card, 4, 4 + 4 * gy);

    bj_img_t chip = img_new(BJ_CHIP_CV, BJ_CHIP_CV);
    for (int i = 0; i < BJ_NCHIPS; i++) {
        bj_chip_draw(&chip, bj_chip_value[i]);
        blit(&sheet, &chip, 4 + gx + 4 + i * (BJ_CHIP_CV + 4), 4 + 4 * gy + 20);
    }
    bj_img_t pile = img_new(BJ_STACK_W, BJ_STACK_H);
    static const int amounts[] = { 10, 60, 160, 750, 1000, 2000, 990 };
    for (int i = 0; i < (int)(sizeof amounts / sizeof amounts[0]); i++) {
        bj_stack_draw(&pile, amounts[i]);
        blit(&sheet, &pile, 4 + 5 * gx + i * (BJ_STACK_W + 2), 4 + 4 * gy + 10);
    }
    return write_ppm(out, &sheet, zoom);
}

static int cmd_felt(const char *out)
{
    bj_img_t felt = img_new(BJ_SCREEN_W, BJ_SCREEN_H);
    bj_felt_draw(&felt);
    return write_ppm(out, &felt, 1);
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static int cmd_time(void)
{
    bj_img_t card = img_new(BJ_CV_W, BJ_CV_H);
    bj_card_draw(&card, 0);                 /* masks made once, outside */
    double t0 = now_ms();
    for (int r = 0; r < 20; r++) {
        for (int c = -1; c < 52; c++) {
            bj_card_draw(&card, c);
        }
    }
    double t1 = now_ms();
    bj_img_t felt = img_new(BJ_SCREEN_W, BJ_SCREEN_H);
    bj_felt_draw(&felt);
    double t2 = now_ms();
    bj_img_t pile = img_new(BJ_STACK_W, BJ_STACK_H);
    for (int r = 0; r < 50; r++) {
        bj_stack_draw(&pile, 990);
    }
    double t3 = now_ms();
    printf("card  %.3f ms\nfelt  %.3f ms\npile  %.3f ms\n",
           (t1 - t0) / (20 * 53), t2 - t1, (t3 - t2) / 50);
    return 0;
}

/* --- play ----------------------------------------------------------------- */

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; if (fails < 20) { \
    printf("round %u: ", g.rounds); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* The payout, worked out again from the cards alone. */
static int32_t expected_paid(const bj_hand_t *h, const bj_hand_t *d)
{
    int  pt = bj_total(h, NULL), dt = bj_total(d, NULL);
    bool pb = h->n == 2 && !h->split && pt == 21;
    bool db = d->n == 2 && dt == 21;
    if (pt > 21) return 0;
    if (pb && db) return h->bet;
    if (pb) return h->bet * 5 / 2;
    if (db) return 0;
    if (dt > 21 || pt > dt) return h->bet * 2;
    if (pt == dt) return h->bet;
    return 0;
}

int bj_harness_play(int rounds, uint32_t seed)
{
    static bj_game_t g;
    bj_game_init(&g, seed, BJ_BANK0);
    uint32_t r = seed * 2654435761u + 7;
    double   wagered = 0, returned = 0;
    long     splits = 0, doubles = 0, insured = 0, shuffles = 0, bjs = 0, dbjs = 0, steps = 0;
    long     played = 0;
    int      pure = getenv("BJH_PURE") != NULL;   /* no random plays, no insurance */

    for (int n = 0; n < rounds; n++) {
        if (bj_broke(&g)) {
            bj_refill(&g);
        }
        r = r * 1103515245u + 12345u;
        int bets[] = { 10, 20, 50, 100, 150, 500, 1000 };
        g.bet = 0;
        bj_bet_add(&g, bets[(r >> 16) % 7]);
        if (!bj_can_deal(&g)) {
            bj_bet_clear(&g);
            bj_bet_add(&g, BJ_BET_MIN);
        }
        int32_t before = g.bank;
        CHECK(bj_deal(&g), "could not deal, bank %d bet %d", g.bank, g.bet);
        int32_t ins_back = 0, ins_stake = 0, paid_ev = 0;
        int     guard = 0;

        for (;;) {
            if (++guard > 400) {
                CHECK(0, "stuck in state %d", g.state);
                break;
            }
            bj_ev_t e = bj_step(&g);
            steps++;
            if (e.type == BJ_EV_SHUFFLE) {
                int cnt[52] = { 0 };
                for (int i = 0; i < BJ_SHOE_N; i++) cnt[g.shoe[i]]++;
                for (int i = 0; i < 52; i++) CHECK(cnt[i] == BJ_DECKS, "card %d x%d", i, cnt[i]);
                shuffles++;
            } else if (e.type == BJ_EV_CARD && e.hand >= 0) {
                const bj_hand_t *h = &g.hand[e.hand];
                /* no card after 21 or a bust, except the dealing itself */
                if (h->n > 2) {
                    bj_hand_t prev = *h;
                    prev.n--;
                    CHECK(bj_total(&prev, NULL) < 21, "card on %d", bj_total(&prev, NULL));
                }
                CHECK(!(h->aces && h->n > 2), "third card on split aces");
            } else if (e.type == BJ_EV_INSURANCE) {
                ins_back += e.amount;
            } else if (e.type == BJ_EV_RESULT) {
                paid_ev += e.amount;
            } else if (e.type == BJ_EV_OVER) {
                break;
            } else if (e.type == BJ_EV_NONE) {
                if (g.state == BJ_ST_INSURANCE) {
                    r = r * 1103515245u + 12345u;
                    bool yes = !pure && ((r >> 16) % 5) == 0;         /* sometimes */
                    if (yes) { insured++; ins_stake = g.hand[0].bet / 2; }
                    bj_insure(&g, yes);
                } else if (g.state == BJ_ST_PLAYER) {
                    char a = bj_advice(&g);
                    r = r * 1103515245u + 12345u;
                    int wild = !pure && ((r >> 16) % 50) == 0;      /* a bad player, 2 % */
                    if (wild) a = "hsdp"[(r >> 8) % 4];
                    if (a == 'p' && bj_split(&g)) splits++;
                    else if (a == 'd' && bj_double(&g)) doubles++;
                    else if (a == 's' || a == 'd' || a == 'p') bj_stand(&g);
                    else bj_hit(&g);
                } else {
                    CHECK(0, "waiting in state %d", g.state);
                    break;
                }
            }
        }
        played++;

        /* the money, the other way */
        int32_t stakes = 0, exp = 0;
        for (int i = 0; i < g.nhands; i++) {
            stakes += g.hand[i].bet;
            exp += expected_paid(&g.hand[i], &g.dealer);
            CHECK(g.hand[i].paid == expected_paid(&g.hand[i], &g.dealer),
                  "hand %d paid %d, expected %d", i, g.hand[i].paid, expected_paid(&g.hand[i], &g.dealer));
        }
        bool dbj = g.dealer.n == 2 && bj_total(&g.dealer, NULL) == 21;
        int32_t ins_exp = (ins_stake && dbj) ? ins_stake * 3 : 0;
        CHECK(ins_back == ins_exp, "insurance back %d, expected %d", ins_back, ins_exp);
        CHECK(paid_ev == exp, "events paid %d, expected %d", paid_ev, exp);
        CHECK(g.bank == before - stakes - ins_stake + exp + ins_exp,
              "bank %d, expected %d", g.bank, before - stakes - ins_stake + exp + ins_exp);

        /* the dealer: stopped at 17 or more, and not later than that */
        bool all_bust = true, naturals = bj_is_blackjack(&g.hand[0]) && g.nhands == 1;
        for (int i = 0; i < g.nhands; i++) all_bust &= bj_total(&g.hand[i], NULL) > 21;
        if (!dbj && !all_bust && !naturals) {
            CHECK(bj_total(&g.dealer, NULL) >= 17, "dealer stopped at %d", bj_total(&g.dealer, NULL));
        }
        if (g.dealer.n > 2) {
            bj_hand_t prev = g.dealer;
            prev.n--;
            CHECK(bj_total(&prev, NULL) < 17, "dealer drew on %d", bj_total(&prev, NULL));
        }
        CHECK(g.hole_up, "hole card never shown");
        bjs  += bj_is_blackjack(&g.hand[0]) && g.nhands == 1;
        dbjs += dbj;
        wagered  += stakes;
        returned += exp;
        bj_next_round(&g);
        CHECK(g.state == BJ_ST_BET, "not back to betting");
    }
    printf("%ld rounds, %ld steps, %ld shuffles | splits %ld, doubles %ld, insured %ld\n",
           played, steps, shuffles, splits, doubles, insured);
    printf("player blackjacks %.2f %%, dealer %.2f %% | refills %u\n",
           100.0 * bjs / played, 100.0 * dbjs / played, g.refills);
    printf("returned %.4f of what was wagered: house edge %.2f %% (2 %% of plays are random)\n",
           returned / wagered, 100.0 * (1.0 - returned / wagered));
    printf("%s: %d failures\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "sheet")) {
        return cmd_sheet(argv[2], argc > 3 ? atoi(argv[3]) : 1);
    }
    if (argc >= 3 && !strcmp(argv[1], "felt")) {
        return cmd_felt(argv[2]);
    }
    if (argc >= 2 && !strcmp(argv[1], "time")) {
        return cmd_time();
    }
    if (argc >= 2 && !strcmp(argv[1], "play")) {
        return bj_harness_play(argc > 2 ? atoi(argv[2]) : 10000,
                               argc > 3 ? (uint32_t)atoi(argv[3]) : 1u);
    }
    fprintf(stderr, "usage: %s sheet out.ppm [zoom] | felt out.ppm | time | play [N] [seed]\n", argv[0]);
    return 2;
}
