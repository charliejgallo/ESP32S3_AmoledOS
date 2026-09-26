# Cameras

A live view of the house's IP cameras on the watch: RTSP straight from the
camera, H.264 decoded on the board, and MJPEG from a transcoder (go2rtc) for
the streams the board cannot decode. Branch `rtsp`. This file is the working
paper: what the board can do, measured, before the viewer is written.

## Step 1: the decoder, on the bench

The S3 has no video block, so H.264 means Espressif's software decoder:
`espressif/esp_h264` 1.4.1, which is tinyh264 underneath. It only speaks
**constrained baseline**: CAVLC, no B-frames, no CABAC.

`GET /api/h264bench` ([`aos_h264bench.c`](../components/aos_web/aos_h264bench.c))
decodes an Annex-B `.h264` from the card's `videos` folder in a task pinned to a
core at priority 5 (a viewer's worker). It times every NAL, sums the calls that
built each picture, and reports I and P frames separately. `dual=1` turns on
tinyh264's second task on the other core. `conv=1` also times YUV 4:2:0 →
RGB565 BE scaled to 368 px wide, which the viewer will do to every frame.
`rate=N` feeds the clip at N fps like a live camera and reports the lag.

```bash
curl -X POST "http://<board>/api/upload?dir=videos&name=cam.h264" --data-binary @cam.h264
curl "http://<board>/api/h264bench?file=cam.h264&n=200&dual=0&rate=12"
```

How the clips were made:

- **The doorbell's clip is the camera's own bitstream.** It was recorded with
  `ffmpeg -rtsp_transport tcp -i rtsp://… -t 10 -an -c copy -bsf:v
  h264_mp4toannexb -f h264`.
- **The outdoor camera's clip is its own bitstream too**, but it is Main
  profile, which is the point of that row.
- **The other resolutions are x264 re-encodes of the outdoor clip in
  constrained baseline**, at the bitrate the camera would use. `tim576x`, the
  x264 re-encode of the doorbell, decodes within 5 % of the doorbell's own
  stream, so a re-encode is a fair stand-in for a camera set to that mode.

### The cameras

| Camera | Stream 102 as it comes | What it can be set to |
|---|---|---|
| Doorbell, Hikvision DS-KD8003-IME1 | H.264 Baseline, 704×576, 25 fps, GOP 50, ~440 kbps VBR, G.711 µ-law 8 kHz | Only H.264 at 704×576. 12 or 25 fps. GOP 1-400. No MJPEG and no snapshot (`/picture` is 400). |
| Outdoor, Hikvision DS-2CD1043G2-LIU | H.264 **Main (CABAC)**, 1280×720, 20 fps, GOP 50, 1024 kbps | H.264 / H.265 / **MJPEG**. 640×360, 640×480 or 1280×720. Baseline / Main / High. 1-25 fps. |

Neither camera marks any P frame as disposable: every P slice has
`nal_ref_idc=3`, and there is no SVC. **The only place a decoder that falls
behind can catch up is the next keyframe.**

### Decoder speed

Every clip was run for 200 frames at 240 MHz, with WiFi up and the watch idle, and the decoder in IRAM (the default). The fps column is the
decoder alone, 1000 / average ms.

| Stream | P avg (ms) | I avg (ms), 1 task → 2 | Decoder fps | Internal RAM peak | PSRAM |
|---|---|---|---|---|---|
| 352×288 (re-encode) | 14 | 65 → 58 | **66** | 33 KB | 0.5 MB |
| 640×360 (re-encode) | 31 | 183 → 172 | **30** | 55 KB | 0.9 MB |
| 640×480 (re-encode) | 38 | 231 → 212 | **24** | 55 KB | 1.1 MB |
| **704×576 doorbell, camera's own** | 54 | 218 → 157 | **17.4** | 60 KB | 1.4 MB |
| 1280×720 Baseline (re-encode) | 107 | 574 → 484 | **8.6** | **105 KB** | 2.9 MB |
| 1280×720 **Main/CABAC**, camera's own | — | — | **0**: 211 errors, not one picture | — | — |

What each number means:

- **The second decoder task only helps keyframes** (−10 to −30 %); P frames
  do not move. It is not worth a core that the viewer wants for the
  conversion and the blit.
- **Core 1 is ~7 % faster than core 0** (P 50 ms vs 54 ms on the doorbell),
  because WiFi and the portal live on core 0.
- **The decoder in IRAM costs 20 KB of internal RAM and is worth 18-22 %.**
  Without it (`CONFIG_ESP_H264_DECODER_IRAM=n`), the doorbell drops to 14.2 fps
  and 640×480 to 19.6 fps, and internal RAM goes back from 160 K to 181 K free.
  It stays in IRAM.
- **Internal RAM scales with the width, not with the area.** Most of it goes
  back when the decoder closes: no leak was measured in any run. **At 720p the
  peak is 105 KB** of the ~160 KB the watch has free, so 720p is out even
  where its frame rate would do.
- **Heights that are not a multiple of 16 come out padded.** 640×360 decodes
  as 640×368: tinyh264 ignores the SPS crop, so the viewer crops.
- **The conversion to RGB565 costs 20-28 ms a frame.** It depends on the output
  size, not the input, and this first, plain C version still has room to
  shrink. In the viewer it goes on the other core, so it overlaps the next
  decode instead of adding to it.

### Live: does it keep up?

Same bench with `rate=N`: frames are fed at the camera's pace. Here the
conversion ran in the same task, which is the worst case, and the lag is how
far behind the feed was when the clip ended.

| Stream @ fps | Lag at the end of 200 frames | Verdict |
|---|---|---|
| Doorbell 704×576 @ 25 (as it comes) | 3.7 s decoder alone, 9.0 s with conversion | **Does not keep up** |
| Doorbell 704×576 @ **12** | 26 ms (196 ms peak, on a keyframe) | **Keeps up** |
| 640×480 @ 20 | 3.5 s | Does not keep up |
| 640×480 @ **12** | 6 ms (255 ms peak) | **Keeps up** |
| 640×360 @ 20 | 0.8 s with conversion in line; the decoder alone does 30 fps | Keeps up once the conversion moves to the other core |

### What this means for the viewer and the cameras

1. **The doorbell works as it is, at 12 fps.** Setting its stream 102 to
   `maxFrameRate=1200` is the only change the viewer needs. Nobody else uses
   that stream.
2. **GOP = fps (one keyframe a second)** is advisable for any stream the watch
   opens. The first picture can only come from a keyframe, so GOP 50 at
   12 fps means up to 4 s of black on open. A one-second GOP is also how fast
   the viewer recovers after it drops frames.
3. **The outdoor cameras need their stream 102 changed** to be decodable at
   all, because Main/CABAC does not decode. Two ways:
   - **H.264 Baseline, 640×480 (or 640×360), 12-15 fps, GOP = fps.**
   - **MJPEG, 640×480.** The camera offers it, and the board already decodes
     JPEG with `esp_new_jpeg` (SIMD) for the Video app. That is a camera-side
     transcoder for free, with no go2rtc in between.
4. **Anything bigger than ~640×480 goes through go2rtc as MJPEG** at the
   watch's size: the 1080p main streams, 720p, and H.265. The watch then only
   decodes a JPEG of ~368 px.

The viewer will therefore take two kinds of URL in the same list: `rtsp://` for
H.264 Baseline decoded on the board, and `http://` for MJPEG (go2rtc, or the
camera's own MJPEG stream).

### Reproducing

The clips are not in the repo: they are pictures of a house. To make one:

```bash
ffmpeg -rtsp_transport tcp -i "rtsp://USER:PASS@CAMERA/Streaming/Channels/102" \
  -t 10 -an -c copy -bsf:v h264_mp4toannexb -f h264 cam.h264
# a baseline stand-in for a resolution the camera is not set to:
ffmpeg -i cam.h264 -vf scale=640:480 -an -c:v libx264 -profile:v baseline -bf 0 -refs 1 \
  -x264-params keyint=50:min-keyint=50:scenecut=0 -b:v 512k -maxrate 512k -bufsize 1024k \
  -f h264 cam480b.h264
```

`dual=` is a runtime choice only because the bench calls `h264bsdAlloc()`
itself; `esp_h264`'s own wrapper gates it behind `CONFIG_ESP_H264_DUAL_TASK`.

### Trap found on the way

- **tinyh264 writes the width and height only when it activates a parameter
  set**, not on every picture. The first version of the bench read them per
  call, got 0 on the second picture, and divided by it (panic,
  `IntegerDivideByZero`). They have to live as long as the decoder does, as
  they do in `esp_h264`'s wrapper.
- **A paced run that falls behind never sleeps**, so the idle task starves and
  the task watchdog resets the board. The worker yields one tick per picture,
  always.
