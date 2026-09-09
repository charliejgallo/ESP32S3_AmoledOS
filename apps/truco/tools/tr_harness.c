/*
 * TRUCO - test bench without a screen
 *
 * It compiles ONLY the rules (tr_game.c does not include LVGL) and plays
 * thousands of games of the machine against itself. It serves three purposes:
 *
 *  1. That no game hangs. The typical bug in a call state machine is leaving
 *     somebody waiting for a turn that never comes: the bench cuts off at 400
 *     actions per hand and shouts about it.
 *  2. That the score adds up. Every hand has to give points to somebody and
 *     nobody may go past 30.
 *  3. Seeing the distribution: how many hands go to the deck, how many are
 *     called, how much the envido pays. If the opponent hardly ever calls
 *     truco, it shows here.
 *
 *   cc -O2 -I ../main tr_harness.c ../main/tr_game.c -o /tmp/trh && /tmp/trh 2000
 */
#include "tr_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SUIT[4] = { "E", "B", "O", "C" };

static void card_str(tr_card_t c, char *out)
{
    sprintf(out, "%d%s", tr_rank(c), SUIT[tr_suit(c)]);
}

int main(int argc, char **argv)
{
    int games = argc > 1 ? atoi(argv[1]) : 500;
    /* "ia" as the second argument puts the machine against itself, which is
     * the only way to see whether the thresholds are balanced; anything else
     * prints the hand as it is played. */
    int ai_vs_ai = argc > 2 && strcmp(argv[2], "ia") == 0;
    int verbose  = argc > 2 && !ai_vs_ai;

    long hands = 0, mazo = 0, env_called = 0, truco_called = 0;
    long env_pts = 0, hand_pts = 0;
    int  wins[2] = { 0, 0 };
    long longest = 0;

    for (int g = 0; g < games; g++) {
        tr_state_t st;
        tr_new_game(&st, (uint32_t)(g * 2654435761u + 12345u));

        int guard_game = 0;
        while (st.phase != TR_P_GAME_END) {
            if (++guard_game > 400) {
                printf("GAME %d HUNG at %d-%d\n", g, st.score[0], st.score[1]);
                return 1;
            }

            int before0 = st.score[0], before1 = st.score[1];
            long acts = 0;
            hands++;

            while (st.phase == TR_P_TURN || st.phase == TR_P_ANSWER) {
                if (++acts > 400) {
                    printf("MANO COLGADA (partida %d): fase=%d turno=%d baza=%d\n",
                           g, st.phase, st.turn, st.trick);
                    return 1;
                }

                /* Both sides play with the same opponent: who is "them" takes
                 * turns. Copying the state with the players swapped would be
                 * tidier, but for verifying rules it is enough for the human
                 * to play just as simply. */
                int arg = 0;
                tr_call_t c;
                if (st.turn == TR_EL || ai_vs_ai) {
                    c = tr_ai_decide(&st, st.turn, &arg);
                } else {
                    /* The bench's "human": it says yes to everything cheap and
                     * plays the first card it has left. That way the
                     * quiero/no-quiero paths are exercised without copying the
                     * AI. */
                    if (st.phase == TR_P_ANSWER) {
                        c = (tr_envido_points(st.hand[TR_YO], TR_HAND_N) >= 24 ||
                             st.pending == TR_C_TRUCO) ? TR_C_QUIERO : TR_C_NOQUIERO;
                    } else {
                        c = TR_C_PLAY;
                        arg = -1;
                        for (int i = 0; i < TR_HAND_N; i++) {
                            if (!st.spent[TR_YO][i]) { arg = i; break; }
                        }
                        if (arg < 0) {
                            printf("NO CARDS but on turn (game %d)\n", g);
                            return 1;
                        }
                    }
                }

                if (c == TR_C_NONE) {
                    printf("THE RIVAL DECIDED NOTHING (game %d, phase %d)\n", g, st.phase);
                    return 1;
                }
                if (c == TR_C_ENVIDO || c == TR_C_REAL || c == TR_C_FALTA) env_called++;
                if (c == TR_C_TRUCO || c == TR_C_RETRUCO || c == TR_C_VALE4) truco_called++;
                if (c == TR_C_MAZO) mazo++;

                if (!tr_apply(&st, st.turn, c, arg)) {
                    printf("ILLEGAL ACTION %d (game %d, phase %d, turn %d)\n",
                           c, g, st.phase, st.turn);
                    return 1;
                }

                tr_ev_t ev;
                while (tr_pop_event(&st, &ev)) {
                    if (ev.kind == TR_EV_ENVIDO) env_pts += ev.n;
                    if (verbose && ev.kind == TR_EV_PLAY) {
                        char b[8];
                        card_str((tr_card_t)ev.b, b);
                        printf("  %s tira %s\n", ev.who ? "EL" : "YO", b);
                    }
                    if (verbose && ev.kind == TR_EV_SAY) {
                        printf("  %s: %s\n", ev.who ? "EL" : "YO",
                               tr_call_name((tr_call_t)ev.a));
                    }
                }
            }

            if (acts > longest) longest = acts;

            int gained = (st.score[0] - before0) + (st.score[1] - before1);
            if (gained <= 0) {
                printf("HAND WITH NO POINTS (game %d)\n", g);
                return 1;
            }
            hand_pts += gained;

            if (st.score[0] > TR_TARGET || st.score[1] > TR_TARGET) {
                printf("PUNTAJE PASADO (partida %d): %d-%d\n", g, st.score[0], st.score[1]);
                return 1;
            }

            if (st.phase == TR_P_HAND_END) tr_new_hand(&st);
        }

        wins[st.score[0] >= TR_TARGET ? 0 : 1]++;
    }

    printf("partidas      %d\n", games);
    printf("hands         %ld  (%.1f per game)\n", hands, (double)hands / games);
    printf("gano YO       %d  (%.1f %%)\n", wins[0], 100.0 * wins[0] / games);
    printf("HE won        %d  (%.1f %%)\n", wins[1], 100.0 * wins[1] / games);
    printf("envido calls  %ld  (%.2f per hand)\n", env_called, (double)env_called / hands);
    printf("truco calls   %ld  (%.2f per hand)\n", truco_called, (double)truco_called / hands);
    printf("folded        %ld  (%.1f %% of the hands)\n", mazo, 100.0 * mazo / hands);
    printf("points/hand   %.2f  (envido %.2f)\n",
           (double)hand_pts / hands, (double)env_pts / hands);
    printf("max actions   %ld in one hand\n", longest);
    return 0;
}
