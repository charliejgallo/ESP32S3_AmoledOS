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
| Flash | +30 KB for minimp3 and its tables; 41.6 KB in all with the player, the cover and the app (3,812,224 → 3,853,808 bytes) | |
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

Unless mixing is on (below, "The second round").

## Using it

- **Music**: folders first, then tracks, sorted as a person expects (case
  ignored, "2" before "10"). A track shows its title over its artist. The
  pink row on top is what is playing (or, with nothing playing, the last
  track, to resume). Back goes up a folder. Opening the app
  while something plays goes straight to the player, in that folder.
- **Player**: the cover, position of the folder ("4 / 31"), shuffle, title in up to two
  lines, artist, format ("MP3 320 kbps 44 kHz"), a progress bar you can drag
  to seek, previous (the start first if more than 3 s in), play/pause, next,
  volume.
- **Control centre**: its player row drives the watch's own music when
  something plays, the phone's otherwise.
- **`/api/player`** drives it from a computer and shows the pipeline's
  numbers: `?do=play&dir=music/sudbeat3&i=4`, `pause`, `resume`, `next`,
  `prev`, `seek&ms=`, `shuffle&on=`, `volume&v=`, `stop`, `resume_last`,
  `mix&on=`. The figures above
  came from there.

For apps: `aos_hal_player_play()` plays one file and stops (the Video app's
sound); `aos_hal_player_play_folder()` plays a file and then its folder.
`aos_hal_player_info()` and `aos_hal_player_stats()` are the long form of
`aos_hal_player_status()`, whose struct stays the size it was so already
compiled apps keep working.

## The second round

After the first version played, the list of what it lacked, done in order.

### Gapless, and the encoder's silence

Two things made a gap between tracks. The player let a track play out before
it opened the next one (~¼ s), and the MP3's own padding went out as sound:
an encoder delays the audio (LAME: 576 samples, plus the 529 every layer III
decoder adds), pads the last frame, and writes both in the LAME tag of the
Info frame, which itself decodes to one frame of silence.

- `aos_audio.c` reads the LAME tag (and ffmpeg's `Lavc` one, same layout) and
  drops the Info frame, the delay and the padding. Against ffmpeg on
  `Contact`: **the same 16,405,200 samples, at lag 0**, 83.2 dB as before;
  before this it gave 2,736 samples more and started 2,257 late.
- A continuous tone cut at 4.321 s into two MP3s, decoded apart and joined:
  **exactly the original length**; the only error at the joint (139 on a
  2,896 amplitude) is what encoding two files separately does to the edge.
- The player opens the next file when the last ends and keeps filling the
  ring behind it; the writer switches title, position and folder index on
  the exact sample where the new track starts. On the board the ring stays at
  ~2.1 s through the change (it used to empty). Different sample rates still
  play out first: the codec has to reopen.
- The awkward window is the last two seconds of a track, when the decoder is
  already into the next one. Next, previous and seek act on what is **heard**:
  the decoder goes back to it first. Measured all three there.

### Remembering where it was

The track (when a new one starts) and the position (every minute, on pause,
on stop) go to NVS; NVS skips a write whose value did not change, so a paused
track costs nothing. After a restart nothing plays by itself: the Music app
opens in that folder with a **Resume** row on top ("Resume 2:06 · Eran
Aviner & BP"). Measured across a real restart: it went on at 2:06. The first
version saved "0:00" before the resume's seek landed, overwriting the good
position; it waits for the seek now.

### The cover

The picture inside the MP3 (ID3 APIC), or a `cover.jpg` / `folder.jpg` /
`front.jpg` beside the track. An embedded cover that cannot be read
(progressive JPEG) falls back to the folder's; with neither, the note stays.
Decoded off the LVGL task by a one-shot task, with esp_new_jpeg scaling as it
decodes; the result is kept, so the next track of the same folder, or opening
the app again, costs nothing.

| Cover | Read | Decode |
| --- | --- | --- |
| 600 px, 4:2:0, 20 KB | 61 ms | 43 ms |
| 600 px, 4:4:4, 35 KB | 145 ms | 58 ms |
| 96 px, 4:4:4 | 14 ms | 7 ms |
| 1200 px, 598 KB | 1,417 ms | 427 ms |
| progressive | — | refused → the folder's cover |

- The card reads ~470 KB/s (as VIDEO.md measured), so a big cover is mostly
  reading, and it happens in the background.
- **esp_new_jpeg will not scale below 1/8** of the original: a 1200 px cover
  gives 150 px at the least ("scaled width should be greater than the minimum
  1/8 scale width"). It decodes to that and nearest-neighbour does the rest.
- It read 4:4:4 here, which VIDEO.md says it refuses on full-size video
  frames. The difference was not looked into; a TJPGD fallback was written
  for it and removed, because nothing ever reached it.

### Mixing an app's sound over the music

**Settings → Sound → Mix music and apps**, off by default (off: the app
pauses the music, as above). On, an app that opens the streaming speaker
while music plays does not pause it: its ring is read by the player's writer
instead of a task of its own, brought from 16 kHz to the music's rate by
linear interpolation, and added over the music at half volume.

- With Turbo: the music goes on, 0 underruns, and Turbo's ring holds steady
  at 1,600 samples (the 100 ms it keeps queued): it is drained exactly as fast
  as the game fills it.
- Music paused with the game open: the game's sound goes on, over silence.
- Music stopped with the game open: the game's sound moves to a task of its
  own (`aos_spk` appears) and goes on from where it was in its ring.
- Never for the walkie-talkie (the UI tells the HAL which app is in front,
  `aos_hal_audio_foreground()`), nor while the microphone is open.

## Not done

- **The battery cost.** While the player runs the CPU stays at 240 MHz and
  the watch does not light-sleep (as it already did for WAV). Not measured on
  battery yet: `tools/battery_night.py` is the recorder, an hour with music
  and an hour without is the protocol.
- **One OTA of this branch did not take**: the image was rolled back by the
  bootloader, with no core dump saved. The same image, installed again, booted
  and confirmed, and every OTA of the branch since has taken. Noted in case
  it comes back.
