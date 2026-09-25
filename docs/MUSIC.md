# MP3 and the music player

The README listed it as a known gap: *"MP3 — the player handles 16-bit PCM WAV
only"*. The Music app listed `.mp3` files and then failed to play them. This is
what closing that gap took, what it costs, and what it does to the apps around
it. Every figure was measured on charlie.local (v2 board) on 2026-09-24,
playing the card's `music/sudbeat3/`: 31 tracks at 320 kbps CBR, 44.1 kHz
stereo, 6 to 12 minutes, 15 to 28 MB each.

## What was wrong besides MP3

Five more, found by reading the old player before touching it:

| | Before | After |
| --- | --- | --- |
| Formats | 16-bit WAV | 16-bit WAV and MP3 (MPEG-1/2 layer III, CBR or VBR, 8–48 kHz) |
| Subfolders | not listed: `sudbeat3/` did not appear | browsed, folders first |
| Long names | cut at 72 bytes, and the cut name was the path it tried to open | 255, what FAT allows |
| Next track | the app's timer, only while the player view was on screen: with the app closed the music stopped after one track | the HAL's queue: the folder plays through and round again, app open or not |
| Volume slider | applied at the next track (the codec's volume was set once, at open) | within 20 ms |
| Stereo | fed as is to a mono DAC (see below) | mixed to mono |

## The decoder

**minimp3** (lieff, CC0), vendored in `components/aos_hal/minimp3/` with one
local change: its 16 KB scratch came off the stack, and now comes from the
caller's heap (`MINIMP3_SCRATCH`), so the decoding task keeps a small
internal-RAM stack. Layers I and II are compiled out.

Chosen over Espressif's `esp_audio_codec` (a closed binary) and Helix (a
licence that does not sit well in an MIT repo), and because it builds for the
simulator too: `aos_audio.c`, the file-format half of the player, is plain C
and was tested on the Mac against ffmpeg before it ever ran on the board.

| Check, on the 31 real files | Result |
| --- | --- |
| Samples vs ffmpeg's decode | **83.2 dB SNR, 2 LSB worst difference** (after aligning the 2257 samples of LAME delay + Info frame that ffmpeg trims) |
| Duration | exact to the millisecond on all 31 (Xing/Info frame count, or bytes over bitrate) |
| Seek to 200 s | lands **5.5 ms** from `ffmpeg -ss 200`. Using the Info header's seek table on a CBR file landed half a second off (its 1/256 steps are 78 KB, two seconds at 320 kbps): the table is for VBR only |
| Tags | ID3v2.2/2.3/2.4 title, artist, album in all four encodings; the embedded picture's offset (read, not drawn yet). On synthetic files: UTF-16 "Canción ñandú", a JPEG cover found at its exact byte |

These files carry no artist tag and no cover: their 40–70 KB ID3 blocks are
Traktor's private data. So when the tags say nothing, "Artist - Title" is
split out of the file name, which is how most collections are named.

## Where it runs

```
player_task (decoder)      file -> aos_audio (WAV, MP3) -> (L+R)/2 -> ring
player_out_task (writer)   ring -> ES8311, 20 ms per write
```

The ring is **two seconds of mono PCM in PSRAM**. It is what lets the music
give way without being heard to: while the ring is over half full the decoder
sits at **priority 2**, under LVGL (4) and the app workers (5); under half,
the writer raises it to 5.

**Mono** because the board has one speaker behind one DAC. Fed a stereo
stream, the ES8311 plays a single slot, the one REG09's `SDP_IN_SEL` picks,
and esp_codec_dev leaves it at 0: left. So a stereo WAV lost anything mixed
only to the right. That is from the datasheet and the driver, not from an ear
test; `music/prueba-canales/` on the card has a tone on the right only and one
on the left only, and with this firmware both should sound.

## What it costs

| | At rest | Playing |
| --- | --- | --- |
| Internal RAM | **0** | **11.3 KB** (idle 146,503 free → 135,195): the two task stacks and nothing else |
| PSRAM | 0 | 243 KB: the ring (188 KB, sized for 48 kHz), the decoder (45 KB: state, scratch, 16 KB of file) |
| Static RAM (.bss/.data, internal) | +32 bytes | |
| Flash | +30 KB (minimp3 and its tables) | |
| CPU, decoding 320 kbps | | **12–14.5 % of one core** at 240 MHz |
| Reading the card | | another ~10 % of wall time, waiting on DMA, not CPU |

Stacks, from the high-water marks: the decoder uses 3.3 KB of its 6
(opening a file on FAT is the deep part); the writer 2.4 KB of its 4 — with 3
KB it had 636 bytes to spare, because opening the codec goes deep.

## What it does to the app in front

### The card

Opening an app while music plays reads the app's pak from the same card. One
read+decode waited **912 ms** while Turbo loaded its 4 MB pak, **683 ms** for
Visor 3D, **374 ms** during a 2.2 MB upload through the portal. The ring
covered all three: **0 underruns**, lowest fill 1.65 s.

### The CPU: two lessons

The test was Visor 3D spinning Mila (15,690 triangles), the heaviest renderer
there is: its worker runs flat out on core 0, LVGL and the blit on core 1.

**First: a starved task cannot raise its own priority.** The first version had
the decoder raise itself when the ring ran low. With both cores busy above
priority 2 it never ran to do it, and the ring emptied: **3 underruns in 12
seconds**. The writer, which always runs (priority 6, every 20 ms), now does
the raising.

**Second: an FPU task is pinned wherever it first used the FPU.** The decoder
was created unpinned. But on the ESP32-S3, the first floating-point
instruction pins a task to the core it ran on (`portasm.S`: "CP operations are
incompatible with unpinned tasks"), and minimp3 is all floats. The task list
showed `aos_player` on core 0 — the core Visor's worker spins on — while core
1 had time to spare. Now the decoder **follows the app**: when a worker starts
on one core it moves to the other (a new task pinned there picks up the state,
which lives in statics for that reason), and with no worker it sits on core 0,
away from LVGL.

| Visor 3D, ms per frame (half-size render) | ms | fps |
| --- | --- | --- |
| music paused | 71 | 14.1 |
| music playing, decoder stuck on the worker's core | 90 | 11.1 |
| **music playing, decoder on the other core** | **79** | **12.7** |

The ring stays full through it (lowest 1.66 s) and the decoder never needs
priority 5. The 8 ms that remain are not CPU: the triangle pass alone goes
from 56 to 62 ms, the two cores sharing the cache and the PSRAM bus. That is
the worst case; an app that leaves a core idle does not notice the music.

### The speaker and the microphone

There is one codec and no mixer. The app in front wins:

- An app that opens the **streaming speaker** (`aos_hal_spk_open`: Turbo,
  Golf, Doom, Mila, Monster Hop, Chatarra, the walkie-talkie) **pauses the
  music**, and it comes back by itself when the app closes it. Measured with Turbo.
- Same with the **microphone** (recorder, tuner, walkie). Measured with the
  tuner.
- Counted, and with 0.8 s of grace after the last one lets go: the
  walkie-talkie releases the microphone and opens the speaker a moment later,
  and the music would blip in between.
- `aos_hal_beep()` stays silent while music plays, as before.

Mixing an app's sound over the music would need resampling (the apps stream at
16 kHz, music at 44.1) and a mixer in the writer. Possible; not done.

## Using it

- **Music**: folders first, then tracks, sorted as a person expects (case
  ignored, "2" before "10"). A track shows its title over its artist. The
  pink row on top is what is playing. Back goes up a folder. Opening the app
  while something plays goes straight to the player, in that folder.
- **Player**: position of the folder ("4 / 31"), shuffle, title in up to two
  lines, artist, format ("MP3 320 kbps 44 kHz"), a progress bar you can drag
  to seek, previous (the start first if more than 3 s in), play/pause, next,
  volume.
- **Control centre**: its player row drives the watch's own music when
  something plays, the phone's otherwise.
- **`/api/player`** drives it from a computer and shows the pipeline's
  numbers: `?do=play&dir=music/sudbeat3&i=4`, `pause`, `resume`, `next`,
  `prev`, `seek&ms=`, `shuffle&on=`, `volume&v=`, `stop`. The figures above
  came from there.

For apps: `aos_hal_player_play()` plays one file and stops (the Video app's
sound); `aos_hal_player_play_folder()` plays a file and then its folder.
`aos_hal_player_info()` and `aos_hal_player_stats()` are the long form of
`aos_hal_player_status()`, whose struct stays the size it was so already
compiled apps keep working.

## Not done

- **The cover.** Its offset is found; drawing it means decoding a JPEG of
  500–1000 px down to the art square (esp_new_jpeg is in the firmware and
  scales by 1/2, 1/4, 1/8).
- **Gapless.** A track plays out before the next opens: about a quarter of a
  second between them. The LAME delay/padding in the Info frame is not
  trimmed either (51 ms of silence at the start, which ffmpeg removes).
- **Mixing** app sound over music (above).
- **Remembering** the track and position across a restart.
- **The battery cost.** While the player runs the CPU stays at 240 MHz and
  the watch does not light-sleep (as it already did for WAV). Not measured on
  battery.
- **One OTA of this branch did not take**: the image was rolled back by the
  bootloader, with no core dump saved. The same image, installed again, booted
  and confirmed, and has run since. Noted in case it comes back.
