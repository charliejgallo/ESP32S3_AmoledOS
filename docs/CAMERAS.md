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
  x264 re-encode of the doorbell, decodes ~10 % faster than the doorbell's own
  stream (51.6 vs 57.5 ms a frame), so a re-encode is a slightly optimistic
  stand-in for a camera set to that mode.

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

## Step 2: the app

`apps/camaras/` (`aos.camaras`, "Cámaras"). The portal's `/camaras` page
keeps up to eight cameras in NVS: a name, a URL (`rtsp://` or `http://`), a
user and a password. The password never goes back to the browser, and a URL
pasted with credentials in it is taken apart on save. The watch lists them;
tapping one opens it. Tapping the picture switches between **fit** (the
whole picture, name and numbers in the bands) and **fill** (the whole
screen, cropped at the sides). Back closes the connection.

What each piece does:

| File | Job |
|---|---|
| `cam_rtsp.c` | RTSP over one TCP connection: DESCRIBE with Digest (or Basic), SDP, SETUP with `interleaved=0-1`, PLAY, `GET_PARAMETER` keep-alive, TEARDOWN |
| `cam_depay.c` | RTP back into H.264 NAL units (single, STAP-A, FU-A) and JPEG files (RFC 2435, headers rebuilt from the RFC's tables) |
| `cam_http.c` | MJPEG over HTTP (go2rtc), frames found by their SOI/EOI markers |
| `cam_view.c` | decode, lag policy, the hand-over between cores, the strip renderer |
| `cam_conv.c` | I420 and RGB565 to the panel, nearest neighbour, table-driven |

The firmware lends what an app could not have: `aos_hal_tcp_*` and
`aos_hal_md5_hex()` (`aos_tcp.c`, the same file on board and simulator) and
`aos_hal_h264_*` (`aos_h264.c`; in the simulator `sim/sim_codec.c` does it
with libavcodec, and fakes `esp_new_jpeg` too). The simulator therefore
plays the real cameras, and the RTSP client and both depacketisers worked
there against the doorbell and the outdoor camera before a byte went to the
watch.

### How the work is split

On the board neither core alone keeps up, so:

- **Core 0, the worker (priority 3):** socket, RTP, decoding.
- **Core 1, LVGL's timer:** converting the newest picture into two 10-row
  strips of internal RAM and pushing each to the panel while the next one
  is converted.

For H.264 the worker hands over the decoder's own picture, not a copy. That
is safe because picture N is the reference of N+1, so decoding N+1 does not
touch it. A small lock-free handshake (`cam_view.h`) holds the decoder back
before N+2 or an IDR while the UI still converts N. For JPEG the rule is
"newest wins": the session hands over every frame, and only the last one
complete when the socket has been drained gets decoded.

### Measured on the watch (2026-09-26)

| | Doorbell, H.264 704x576 @ 12 | Outdoor, MJPEG 640x480 @ 12 (~45 KB/frame) |
|---|---|---|
| Decode | P 70-86 ms, I 220-230 ms | 55-60 ms |
| Convert + push (fit / fill) | 46 / 65 ms | 45 ms |
| On the panel | **~10 fps**, and every 7-10 s a skip to the next keyframe (up to 1 s frozen) | **9-11 fps** |
| Lag | 0.2-0.7 s | 0.3-1.5 s (the camera sends more than the watch drinks) |
| Free internal RAM while streaming | ~63 K (160 K at rest) | ~115 K |

The doorbell needs ~1.1 s of decoding per second at 12 fps. That is 10 %
more than the watch has, so the lag grows through every GOP and the view
skips to a keyframe when it passes 700 ms. Fit mode costs the decoder less
than fill, because the conversion on the other core competes with it for
cache and PSRAM.

### What was tried, and why it is not there

| Tried | Result |
|---|---|
| Decode and convert in the worker | 107 ms a picture, 9 fps and constant skips. Hence the split. |
| Worker on core 1 (the default) | 140 ms a picture: LVGL and the conversion share the core. |
| Worker on core 0 at priority 5 | Freezes of 1-3 s: it starved `app_main`, which holds the LVGL lock (APP-GUIDE section 14). Priority 3 fixed it. |
| tinyh264's second task (prio 5, 3, before and after that fix) | Keyframes 225 -> 165 ms, but P frames 65-106 ms and the conversion slower: always worse on the panel. Single task stays. |
| Letting `esp_new_jpeg` scale to the panel's size | 150 ms a frame instead of 57 at the picture's own size, and a bug: a scaled handle reports the *scaled* size, which flipped the decoder between scaled and not every other frame and painted stripes ("noise between frames"). Now the size comes from the JPEG's SOF and the UI scales while copying. |
| Lag threshold 350 ms instead of 700 | A keyframe alone reaches ~300 ms, so it skipped every GOP: 2 fps. |
| TCP window 5.7 KB (lwIP default) | The outdoor camera arrived at 2.2 Mbps of its 4.2: at 16 KB, 3.2-3.8 Mbps and 5.9 -> 7.7 fps before the JPEG fix, 10 after. No internal RAM cost at rest (the segments live in PSRAM). In `sdkconfig.defaults`. |
| Bigger caches (the S3's are 16 KB instruction, 32 KB data, shared by both cores) | Not tried: it would take 48 KB of the internal RAM the watch does not have. This is why P frames cost 54 ms on the bench and 70-86 in the app. |

### What to set a camera to

- **H.264: Baseline, at most 704x576, 12 fps, GOP = fps.** 640x480 is
  lighter still (24 fps of decoder on the bench) and should hold 12 without
  skips.
- **MJPEG, 640x480** decodes in ~57 ms and needs no keyframes. It is the
  easiest stream for the watch, at 3-4 Mbps of WiFi. A lower quality would
  cut both the decode time and the lag.
- Anything bigger, Main/High profile or H.265: through go2rtc as MJPEG
  (`http://<host>:1984/api/stream.mjpeg?src=<name>`).

The two cameras used here were reconfigured for these tests (stream 102 of
the doorbell to 12 fps with GOP 12; the outdoor camera's stream 102 to MJPEG
640x480 @ 12). Their original settings were kept to be restored.

## Step 3: a big camera through a transcoder

The case the watch cannot decode: `rtsp://<go2rtc>:8554/<camera>`
from go2rtc, **H.265 Main, 2560x1440, 20 fps**. Transcoded to MJPEG 640x360
at 12 fps, served over HTTP, the watch shows it at **12 fps, 45 ms a frame to
decode, 0 errors**.

The test ran with the Mac as the transcoder, since go2rtc's HTTP API (port
1984) was not reachable from the LAN. The watch only sees the HTTP stream, so
the stand-in behaves like go2rtc:

```bash
ffmpeg -rtsp_transport tcp -i rtsp://<ha>:8554/<stream> -an \
  -vf "fps=12,scale=640:360" -c:v mjpeg -q:v 7 -pix_fmt yuvj420p \
  -f mpjpeg -listen 1 http://0.0.0.0:8090/cam.mjpeg
```

With go2rtc itself, a stream sized for the watch keeps the transcoding on the
server and the JPEGs small (`go2rtc.yaml`):

```yaml
streams:
  <camera>_watch: ffmpeg:<camera>#video=mjpeg#width=640#height=360#raw=-r 12 -q:v 7
```

and the watch opens `http://<go2rtc>:1984/api/stream.mjpeg?src=<camera>_watch`.
go2rtc's API is unauthenticated unless `api: username/password` is set; the
app sends HTTP Basic when the camera has a user.

Two things found on the way, both fixed:

| Found | Fix |
|---|---|
| ffmpeg's HTTP server answers `Transfer-Encoding: chunked` even to an HTTP/1.0 request. Its chunk lines (`\r\n629\r\n`) landed inside JPEGs, which failed to decode near the bottom (`rc -5`). | `cam_http.c` undoes chunked transfer as the bytes arrive. |
| With the screen on, the HAL keeps WiFi in `WIFI_PS_MIN_MODEM`: 180-315 ms of ping to the watch. Through a 16 KB TCP window that capped the stream at 5-9 fps, with the decoder idle half the time. | `aos_hal_net_low_latency(true)` turns power save off while a camera is open (ping 5-40 ms, 12 fps). The app releases it on close, and the HAL releases it when an app's worker stops. It is refused while Bluetooth is up. |

With power save off, the outdoor camera's MJPEG (45 KB frames) went from
9-11 fps to a steady 10-11, with the decoder (57 ms) as the limit. The
doorbell did not change: it is limited by the CPU.

