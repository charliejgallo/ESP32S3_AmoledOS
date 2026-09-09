# Recorder

A voice recorder for AmoledOS: it records from the microphone to **WAV on the
microSD**, shows the elapsed time and a live waveform, and lets you play back
and delete what you recorded.

```bash
# in the simulator, without the board
cd sim && cmake --build build -j8
AOS_SIM_VIEW=app.recorder ./build/amoledos_sim

# the .so for the microSD
source ~/esp/esp-idf/export.sh
cd apps/recorder
idf.py -G 'Unix Makefiles' set-target esp32s3     # first time only
idf.py so                                          # build/recorder.so
cp build/recorder.so /Volumes/<sd>/apps/
```

It needs a firmware with the `aos_hal_rec_*` API (see below).

## The three views

| View | What is there |
| --- | --- |
| **main** | REC pill, elapsed time, live waveform, buttons and the last three recordings |
| **list** | every recording with date, size and duration |
| **detail** | the whole file's waveform, play and delete |

You record with the red button or with the board's **side button**, which is
what you want a physical button on a recorder for: starting and stopping
without looking at the screen. Pause stops the writing but leaves the file
open, so a conversation with interruptions ends up as a single WAV.

Going back from the detail view returns to wherever you came from (the list or
the main view); only from the main view does back leave the app.

## What gets stored

`/sdcard/recordings/grabacion_0007.wav`, 16-bit mono PCM at 16 kHz: 32 KB per
second, about 2 MB per minute. Deliberately uncompressed — the encoder that
would save those megabytes costs more CPU and more code than the space is worth
on a 32 GB card, and a WAV opens in anything.

The number comes from looking at the ones already there, so deleting the last
one does not make the next overwrite it.

## The waveform

The bars are not computed here. The HAL stores **the peak of each 50 ms block**
in a ring, and the app drains it with `aos_hal_rec_peaks()`. That is the
opposite of the obvious approach (polling `aos_hal_mic_level()` every frame),
and it is deliberate: an app that checks the level now and then misses the peaks
between checks, and a waveform with holes in it does not draw what happened, it
draws when you looked.

In the detail view the envelope comes from the file: it jumps to the start of
each of the 40 stretches and looks at a 256-sample window. Reading the whole WAV
to draw 40 little bars would leave the screen frozen for a long second.

## What it needs from the firmware

This app uses API that was added alongside it:

- `aos_hal_rec_start/pause/resume/stop/status/peaks()` and
  `aos_hal_path_recordings()` in `aos_hal.h`
- the libc file functions (`fopen`, `opendir`, `stat`, `remove`...) in
  `EXTRA_SYMBOLS` in `tools/gen_symbols.py`
- the `AOS_ICON_MIC` vector icon in `aos_ui/aos_icon.c`

There is no microphone in the simulator: the HAL synthesises fake speech
(low-pitched syllables with noise, separated by silences) and **writes the WAV
all the same**. The files exist, are listed, are deleted and open in any player,
so the whole interface is genuinely tested without the board.
