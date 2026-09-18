# Blackjack

Blackjack against the house on a green table. Tap chips onto the betting
circle, deal, and play the hand: hit, stand, double or split. The dealer's
hole card turns over when you are done, the dealer draws to 17, and the hand
is paid. The bank is kept between sessions.

---

## The table's rules

They are the ones printed on the felt, and the usual ones of a six-deck
shoe game:

| | |
| --- | --- |
| Shoe | six decks, reshuffled when the cut card (about 75 %) comes out |
| Dealer | stands on every 17, soft ones included |
| Blackjack | pays 3 to 2 |
| Insurance | offered when the dealer shows an ace; pays 2 to 1 |
| Peek | with an ace or a ten up the dealer checks for blackjack, so a dealer blackjack only takes the first bet |
| Double | on any first two cards, also after a split |
| Split | once per round, any two cards of the same value; split aces get one card each, and 21 there is not a blackjack |
| Bets | 10 to 1000, in chips of 10, 50, 100 and 500 |

With these rules basic strategy loses about half a percent, and that is what
the bench measures (below): **0.46 %** over three million hands. The menu has
a **Hint** switch that rings in gold the button basic strategy would press.

If the bank cannot cover the smallest bet, the round-over row offers a new
bank of 1000. Leaving in the middle of a round gives the stakes back: the
round never finished.

## How it is drawn

The table is one opaque RGB565 canvas drawn once when the app opens -
cloth, rail, gold lines and the curved words, which are written letter by
letter along the arc with the system's fonts (`lv_draw_letter` with a
rotation), so they are translated like everything else.

Every card is an ARGB8888 canvas of its own (86 x 116 with the shadow, 40 KB
in PSRAM), drawn once when it is dealt; from then on it is an object that
moves and LVGL repaints only the area it crosses (APP-GUIDE 6.4). The motion
is the app's: its timer moves each card along a tween and turns the hole
card by squeezing its horizontal scale to an edge, changing the face and
opening it again. No `lv_anim`: nothing is left to call back into the app
after it closes, and the one callback that would have been needed,
`lv_anim_set_ready_cb`, is not in the firmware's symbol table.

The art is plain C with no LVGL (`bj_art.c`), so the bench can lay out the
whole deck on a sheet:

- **The suits are implicit curves**, sampled 4 x 4 per pixel once per size
  into an alpha mask and tinted. The heart is the classic sextic
  `(x² + y² - 1)³ - x²y³ ≤ 0`; the spade is that heart upside down on a
  flared stem; the club three circles; the diamond a superellipse with
  slightly hollow sides.
- **The indices are a stroke font**: segments and arcs with round caps,
  antialiased by distance, so the same drawing serves the corner of a card
  and the value on a chip.
- **The court cards are pixel art** (`bj_court.c`): half a figure, 23 x 20,
  drawn at x2 and turned 180 degrees for the other half, as on a real deck.
  The king holds a sword, the queen a rose, the jack a halberd; the clothes
  change colour with the suit.
- Whatever a real card prints twice - the index, the lower pips, the figure -
  is drawn once through a transform that turns it, so the halves cannot
  disagree by a pixel.

The icon travels inside the `.so` (`aos_icon_set_ops()`, firmware v0.3.8 or
later): a red card's back and an ace of spades, fanned on the cloth.

## The rules engine

`bj_game.c` knows nothing of LVGL or the HAL. An action only queues work;
`bj_step()` does one thing - a card, the reveal, a result - and says what it
did, so the app can animate each card before asking for the next. When
nothing is queued it returns `BJ_EV_NONE` and the game is waiting for the
player.

## The bench

```bash
cc -O2 -I apps/blackjack/main apps/blackjack/tools/bj_harness.c \
   apps/blackjack/main/bj_art.c apps/blackjack/main/bj_court.c \
   apps/blackjack/main/bj_game.c -lm -o /tmp/bjh

/tmp/bjh sheet /tmp/sheet.ppm 2    # the deck, the back, the chips, some piles
/tmp/bjh play 1000000              # a bot plays; checks after every hand
BJH_PURE=1 /tmp/bjh play 3000000   # basic strategy only: the house edge
/tmp/bjh time                      # what each drawing costs on the Mac
```

`play` recomputes every payout from the cards alone and compares it with
what the engine paid and with the bank; checks that the dealer stopped at
17 and not later, that no card came after 21 or on split aces, and that each
fresh shoe has six of every card. A bot plays basic strategy with 2 % of
random moves and insures one time in five, to walk the paths a good player
never takes.

## Development switches

In the simulator (on the board `getenv()` always returns NULL):

```bash
BJ_AUTO=1                   # basic strategy plays by itself, round after round
BJ_SEED=42                  # the same shoe every time
BJ_BANK=5000                # start with that bank
BJ_CARDS=0,12,9,22 BJ_DEAL=1   # the top of the shoe, in dealing order:
                            # player, dealer up, player, hole, then hits
                            # (card = suit*13 + rank; rank 0 is the ace)
BJ_ACTS=psh                 # play these as the game waits (h s d p; y/n insure)
BJ_SCREEN=menu              # open the menu, for the layout audit
BJ_HINT=1                   # the strategy hint on
```

`AOS_SIM_SHOT_MS` counts from the start of the process and the simulator
takes about four seconds to boot, so a shot of a dealt hand wants 8000 or
more.

## Files

| File | What |
| --- | --- |
| `main/blackjack.c` | the app: table, cards as objects, controls, menu, preferences |
| `main/bj_game.c` | the rules, one step at a time, and basic strategy |
| `main/bj_art.c` | cards, chips, piles and the table, into ARGB8888 buffers |
| `main/bj_court.c` | the jack, queen and king |
| `tools/bj_harness.c` | the bench |

Preferences: `bj_bank`, `bj_bet`, `bj_best`, `bj_rounds`, `bj_wins`,
`bj_push`, `bj_bjs`, `bj_sfx`, `bj_hint`.
