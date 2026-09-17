# Steps

How the watch counts steps, what the board can and cannot do for it, and
the numbers it was tuned against. The code is `aos_step_detect.c` (the
detector, pure C), `aos_steps.c` (the day, the week and the goal, in both
HALs), the Activity app, and `tools/steps/` (the recorded walks and the
bench that replays them).

## What the board has

The IMU is a **QMI8658C**, not a QMI8658A. Its datasheet (QST, rev 0.6)
lists Wake on Motion and nothing else in the way of motion engines: no
pedometer, no tap, no any-motion or significant-motion detection. The step
count is software, and it is going to stay software on this board.

Its INT1 goes to the TCA9554 expander (EXIO6, per the schematic), whose own
interrupt line is only pulled up and never reaches the ESP32 — the same
dead end the PMU's interrupt met (HARDWARE.md). So Wake on Motion could not
wake the chip from light sleep either, and it needs accel-only mode, which
breaks the accelerometer reading on this part (HARDWARE.md again).

What does work: the accelerometer is polled by the housekeeping task every
40 ms with the screen on and, as the recordings show, every 100 ms in light
sleep, so steps are counted with the screen off. The 58 % light sleep of
POWER.md was measured with that poll running.

## The detector

Orientation-free, because the watch is worn on the wrist or carried in a
pocket, and it must not be told which: it works on the magnitude of the
acceleration. Time-based, because the poll runs at 25 Hz or 10 Hz.

1. **Gravity** is a slow low-pass of the magnitude (2 s).
2. **The dynamic part**, magnitude minus gravity, is smoothed (0.16 s) so a
   stride is one hump. In a pocket the impact and the toe-off are two peaks
   a hundred milliseconds apart; the old fixed threshold counted both.
3. **A step** is a hump higher than `max(0.04 g, 0.35 × the recent hump
   height)`, at least 330 ms after the previous. The adaptive part is what
   serves both places: a pocket signal is 0.25 g of swing and a wrist one
   0.13 g, and a threshold that fits one misses or doubles the other.
4. **A run**: steps at most 1.5 s apart. A run counts once it has four,
   all at once; a single jolt, a grab of the watch, or putting it down
   never reaches four at a walking rhythm.

The old detector was a fixed 1.18 g / 1.02 g hysteresis on the magnitude
with a 250 ms dead time: fine on the wrist, 24 % high in a pocket.

## Measured

Recorded with `/api/imu`, which serves the last three minutes of the
accelerometer at the poll's rate as CSV (`t_ms,ax,ay,az,steps`), and
replayed with `tools/steps/bench.c` (the board's detector, compiled on the
Mac) and `tools/steps/steps_bench.py` (the same arithmetic with a parameter
grid). The two agree to the step.

| recording | steps | old detector | new detector |
| --- | --- | --- | --- |
| `walk_pocket_100.csv`, watch in a trouser pocket | 100, counted | 124 | 98 |
| `walk_wrist_approx100.csv`, on the wrist, brisk | about 100 | 108 | 102 |
| `quiet_pocket_desk.csv`, a minute at the desk, then picked up and pocketed | 0 | 0 | 0 |

Outside the walks both recordings are flat: nothing is counted at the desk
or while the watch is being handled. The grid over `tau_lp`, `thr_min`,
`thr_frac`, `min_ms` and `need` picked the set in `aos_step_detect.h`; the
Python bench's defaults are the same values and must stay so.

To tune again: walk a counted number of steps, fetch `/api/imu` within
three minutes, drop the file in `tools/steps/`, add it to the grid with its
count, and copy the winner into the header.

## The day, the week, the goal

`aos_steps.c` folds the raw since-boot counter into today's count, kept in
NVS (written every five minutes when it changed, and at every day change,
so a restart costs at most five minutes of walking), cut at midnight by the
clock, with the last seven days behind it. Before the clock has ever been
set the day is unknown: the steps count anyway and the first valid time
adopts the day. The goal (default 8000) is changed by tapping the ring in
Activity and stored. `/api/status` publishes `steps` and `steps_goal`.

The distance in Activity is an estimate at 0.72 m a stride.
