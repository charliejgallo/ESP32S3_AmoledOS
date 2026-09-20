# Link: two watches talking (ESP-NOW)

The plan for v0.4: a radio link between two watches with no router in
between, and the apps that make it worth having — a game across two screens,
a card table, a walkie-talkie. Written before the first line of code, on
2026-09-19, when the second board arrived; it will be kept as the record of
what was planned against what was measured, phase by phase, the way
[VIDEO.md](VIDEO.md) and [STEPS.md](STEPS.md) were.

Branch `espnow`, merged as **v0.4.0** on 2026-09-19: the link, its HAL, the
simulator's version of it, and the first two-player app. The rest of the
apps come as v0.4.x on top.

<p align="center">
  <img src="img/photo-link-paired.jpg" width="640" alt="Two watches paired by a bump">
</p>

## What ESP-NOW is, and what it costs here

ESP-NOW is WiFi action frames without an association: no access point, no IP,
no handshake. A peer is registered by its MAC and a frame is sent to it; the
controller reports in a callback whether the frame was acknowledged at the
MAC level (unicast only; a broadcast always "succeeds"). Latency is a few
milliseconds, which is what makes a real-time game possible.

The numbers, checked against the ESP-IDF 5.5.5 headers this firmware builds
with (`esp_now.h`, `esp_wifi.h`):

| | |
| --- | --- |
| payload per frame, ESP-NOW v1 | 250 bytes |
| payload per frame, ESP-NOW v2 (`ESP_NOW_MAX_DATA_LEN_V2`) | 1470 bytes |
| peers, total / encrypted | 20 / 6 |
| the sender's RSSI | in every receive (`esp_now_recv_info_t.rx_ctrl`) |
| power saving without a connection | `esp_now_set_wake_window()` + `esp_wifi_connectionless_module_set_wake_interval()` |
| distance by time of flight (FTM) | supported by the S3 (`SOC_WIFI_FTM_SUPPORT`), off in this build (`ESP_WIFI_FTM_ENABLE` not set) |

Three things about this firmware that the link has to live with:

1. **One 2.4 GHz radio**, shared with WiFi and with Bluetooth; software
   coexistence is on (`ESP_COEX_SW_COEXIST_ENABLE`). Bluetooth holds one
   connection to the phone (`BT_NIMBLE_MAX_CONNECTIONS=1`) and the link must
   not need a second: ESP-NOW rides on WiFi, which is the point.
2. **The channel is the access point's.** ESP-NOW frames go out on the
   channel the station is on. Two watches on two different networks are on
   two channels and never hear each other. The link therefore has a policy
   (below), and it is the first thing to measure.
3. **The radio sleeps.** With the screen off the HAL puts WiFi in
   `WIFI_PS_MAX_MODEM` and the chip light-sleeps between wake-ups (58 % of
   the time, POWER.md). A frame that arrives while the radio is off is
   lost. So the link listens only while a link app is open, and it uses the
   connectionless wake window for the time in between if a "reachable while
   asleep" mode is ever wanted.

## The design

### Two services in the HAL, no protocol in the apps

`aos_hal_link_*`, in `aos_hal.h`, with the same discipline as the worker
and the blit: a small surface, measured, and the apps build on flags and
callbacks. The simulator gets the same API over UDP broadcast on the Mac
(two instances, two ports), so a two-player app is designed on the laptop
like everything else.

**Discovery and pairing.** While a link app is open the watch sends a
beacon every second: a magic, the protocol version, the watch's name
(v0.3.19), and what it is offering (an app id). Neighbours appear in a
list with their RSSI. Pairing is the gesture that shows what the device is:
**bump the two watches**. Both feel a spike on the accelerometer, each
broadcasts "bump at t with nonce N", and if the other's arrives within
300 ms with a strong RSSI they are paired and exchange the key for the
encrypted peer. Nobody across the room can bump from a distance.

**Two channels on top of one link.** ESP-NOW does not retry beyond the MAC
acknowledgement and does not reorder. For a card game that is not enough;
for a paddle it is too much:

- *reliable*: sequence number, acknowledgement, resend on timeout, in order.
  Turns, moves, a file.
- *unreliable*: send and forget, the last one wins. Positions at 30 Hz.

Both are in the HAL so the apps do not each write a worse one.

### The channel policy

1. If both watches are on the same access point, nothing to do: same channel.
2. Otherwise a link app **leaves the access point** for as long as it is
   open and parks on a fixed channel (the setup access point's, since that
   code exists), then reconnects on exit. The watch already knows how to live
   without WiFi. The beacon carries the channel it was sent on, and a watch
   that hears nothing scans the channels for a beacon before parking.

Which of the two the first version does is decided in phase 1, by measuring
how long leaving and rejoining the network takes on this firmware.

### The apps, from the least demanding to the most

| app | channel | what it proves |
| --- | --- | --- |
| **Pong** across two screens | unreliable, 20-byte states at 30 Hz | latency, coexistence with the phone, the bump |
| **Truco** for two | reliable, by turns | the reliable channel, and it is the app for the video |
| **Send a Pixel Art drawing** | reliable, a few KB | the link moves data, not only buttons |
| **Walkie-talkie** | unreliable, push to talk | audio: IMA ADPCM at 16 kHz is 8 KB/s; 20 ms frames of 160 bytes in v1, or 100 ms per frame in v2; jitter buffer of three frames |
| **Radar**: where is the other watch | RSSI, then FTM | distance in metres by time of flight, not by RSSI |

Pong first, on purpose: it is the smallest app that exercises the whole
system in a weekend.

### The simulator

`hal_sim.c` implements the same `aos_hal_link_*` over UDP broadcast on
127.0.0.1 with one port per instance (`AOS_SIM_LINK_PORT`), the "MAC" being
the port, RSSI a constant, and the bump a key. Two simulators on one Mac
play Pong against each other; the layout audit runs on both screens.

## Phases

Each ends in something that can be checked, and the numbers go into this
page as they land.

### Phase 0 — the ground truth

- Both boards on v0.3.19, named (`charlie.local` and the friend's).
- Measured before anything: `/api/mem` (`heap`, `exec`) on both, with
  Bluetooth on and off, as the baseline the link is measured against.
- **Done when** the two baselines are in this page.

### Phase 1 — a frame from one watch to the other

- `aos_hal_link_start/stop`, `_send_raw`, one receive callback, ESP-NOW v2
  if it is negotiated (`esp_now_get_version`), v1 otherwise.
- A `/api/link` endpoint on each watch: peers seen, frames sent and
  received, the last RSSI. The measurement is done over HTTP from the Mac,
  not by reading the screen.
- The channel policy tried both ways: same access point, then one watch
  parked on a fixed channel. Time to leave and rejoin the network, measured.
- Coexistence: the same test with the iPhone connected over BLE and a
  notification arriving.
- **Done when** 1000 frames go one way with the loss rate, the round trip
  and the RSSI in this page, on the same channel and on the parked one, with
  Bluetooth connected and with it off. *Done: see Measured, phase 1.*

### Phase 2 — discovery and the bump

- The beacon, the neighbour list with RSSI, the bump pairing, the encrypted
  peer. The Link screen on the watch: neighbours, "bump to pair", paired
  with whom.
- **Done when** two watches pair by bumping, do not pair from across the
  room, and a third watch (the simulator standing in) is refused. *Done for
  the bump; the far-away and third-watch cases are still to be tried.*

### Phase 3 — the reliable channel and the simulator

- Sequence, acknowledgement, resend, ordering, on top of phase 1; the UDP
  version in the simulator with the same API.
- **Done when** 100 KB go from one watch to the other over the reliable
  channel with a forced 10 % loss (dropped on purpose on the receiving
  side) and arrive whole, and the same test passes between two simulators.
  *Done: see Measured, phase 3.*

### Phase 4 — Pong, and v0.4.0

- Pong across two screens: host-authoritative (one simulates, the other
  sends its paddle and draws), 30 Hz states, the ball crossing from one
  screen to the other. The first app on the link, a `.so` like Burbujas.
- CHANGELOG, README (a "two watches" section), this page with the numbers,
  release with both watches on it.
- **Done when** two people play a game on two watches, and the fps and the
  state rate are on this page. *Done: see Measured, phase 4.*

### Phase 5 — Truco for two, and v0.4.1

The existing Truco against the machine gets a second seat over the reliable
channel. Nothing new in the HAL: it is the first app written entirely on
top of the link as released in v0.4.0, and the proof that the reliable
channel is enough for a game by turns.

**Host-ordered lockstep.** The engine (`apps/truco/main/tr_game.c`) is
deterministic given its seed: the deal comes from the state's own random
generator and nothing else. So both watches run the same engine from the
same seed and only the *moves* travel. The watch with the lower MAC is the
host; it picks the seed, takes every move (its own taps and the guest's
proposals), applies it and echoes it. The guest applies nothing on its own,
not even its own taps: it sends them and waits for the echo. Moves that
arrive wait in an inbox until the table is quiet (nothing moving, no event
pending), exactly where the machine used to think its move, so the
animations keep their pace on both screens even when one is behind, and a
hand that ends on one watch while the other is still gathering its cards
does not desynchronise anything: the next hand is dealt by the same
generator on both sides.

Three messages, all on the reliable channel: a hello (MAC, seed, a nonce
for this run of the app) twice a second until the other answers, a move
(seat, call, argument) from guest to host, and the same move echoed from
host to guest. A hello with a new nonce mid-game means the partner
re-entered Truco: both start over. The link is lost when the reliable
channel gives up, when the partner's beacons stop, or when its beacon
starts offering another app. The seat is a variable of the UI (`ME`/`THEM`,
never `TR_YO`/`TR_EL`), which is what puts the guest at the bottom of its
own table.

### Phase 6 — a drawing, and where the other watch is (v0.4.2)

Two small apps on the link as released, no new protocol in the HAL for the
first and a thin one for the second.

**Pixel Art sends a drawing.** With a partner paired, the editor's menu gets
"Enviar a <name>". The `.pix` file goes as it is on the card (a 16x16
document with four frames is 1132 bytes), in 236-byte chunks over the
reliable channel; the other watch, also in Pixel Art, writes it into its
first empty slot, validates the header, refreshes its gallery and answers
with the slot number (or that it has no room). Both sides keep the link up
only while a partner exists: the app costs nothing on a watch that is alone.

**Radar.** Two answers to "where is the other watch". The cheap one:
both watches ping each other ten times a second over the fast channel and
every ping carries the RSSI the radio saw it at; a smoothed value through a
path-loss model (`d = 10^((P0 - rssi) / 10n)`, n = 2.2, P0 calibrated by
holding the watches one metre apart and tapping the button) puts the
partner as a dot on three rings. RSSI is a poor ruler -a hand over the
antenna is worth metres- but it says "closer" and "further" well. The
exact one: FTM, fine timing measurement (IEEE 802.11mc). The watch with
the lower MAC brings its softAP up as FTM responder, on the station's
channel so the portal stays up, and tells the other its BSSID and channel
in the pings; the other runs a 16-frame session every 1.5 s and the driver
gives a distance in centimetres, which goes back in the pings so both
screens show it. `CONFIG_ESP_WIFI_FTM_ENABLE=y` and four HAL calls
(`aos_hal_ftm_responder/responder_info/measure/result`); the simulator
says "sin soporte".

### Phase 7 — the walkie-talkie (v0.4.3)

Push to talk, half duplex, and the half is the hardware's: the speaker and
the microphone are the same ES8311 and the HAL already treats them as
exclusive (the tuner cannot play its reference tone and listen at once).
Hold the button and the microphone is open: every 464 samples at 16 kHz
(29 ms) go out as one frame on the fast channel, IMA ADPCM at 4 bits a
sample, 232 bytes of audio plus the coder's state (predictor and step
index) so a lost frame costs its 29 ms and nothing after it; 34 frames
and 8.3 KB a second, a seventh of what the channel carries. Let go and the
speaker takes the codec back.

What the HAL lacked was a speaker for audio that is not a file. The
streaming speaker (`aos_hal_spk_open/write/queued/close`) is a ring of one
second in PSRAM and a task that feeds the codec 20 ms at a time and plays
silence when the ring is empty, so the amplifier never has to wake up
mid-word; write() never blocks. It takes the codec like the recorder does
and waits, bounded, for the microphone to let go of it: that wait plus the
codec's open is the release-to-listen cost, about 200 ms, and the app says
"un momento..." for it. The tone task stays off the codec while the
streaming speaker holds it.

### v0.4.x — the rest

- Radar: the walk with a tape measure (the calibration point is measured,
  the slope is not).
- The watch as an ESP-NOW remote for the other ESP32 projects on the bench.

## Measured

Filled in as each phase lands.

### Phase 0 (2026-09-19)

| watch | firmware | heap | exec | Bluetooth | wifi |
| --- | --- | --- | --- | --- | --- |
| `charlie.local` (192.168.1.107) | v0.3.19 | 155235 | 107236 | advertising | on |
| `amoledos.local` (192.168.1.100, the friend's) | v0.3.19 | 190583 | 142584 | off | on |

Read from `/api/status` with both idle at the watchface. The 35 KB between
them is the NimBLE stack, which the second watch does not have running: the
link's cost is measured against each watch's own row.

### Phase 1 (2026-09-19)

`aos_link.c`: ESP-NOW on the station interface, a broadcast peer, a task
between the driver's callback and a ring for the app, counters, and the
test protocol `/api/link` drives (`do=start|stop|reset`, `do=test&n=&gap=&
to=&echo=&len=`, `do=park&ch=&secs=...`). Both watches negotiated
**ESP-NOW v2** and sat on **channel 6**, their access point's, a hand's
width apart (RSSI -11 to -14 dBm). The link costs **4.5 KB of internal
RAM** while up (the queue and the task).

| test | frames | on the air | lost | round trip avg / min / max | rate |
| --- | --- | --- | --- | --- | --- |
| broadcast, 32 B, 10 ms apart | 1000 | 1000 | 0 | — | — |
| unicast echo, 32 B, 20 ms apart | 200 | 200 | 0 | 4.4 / 3.0 / 13.0 ms | — |
| unicast, 200 B, 2 ms apart | 1000 | 605 | 0 | — | the sender refused 395 (`esp_now_send` NO_MEM) |
| unicast echo, 200 B, 5 ms apart | 300 | 300 | 0 | 10.6 / 4.2 / 35.2 ms | — |
| unicast, 250 B, wait for the send callback | 1000 | 1000 | 0 | — | 238 frames/s, **60 KB/s** |
| unicast, 32 B, wait for the send callback | 1000 | 1000 | 0 | — | 485 frames/s |
| broadcast, 250 B, wait for the send callback | 1000 | 1000 | 0 | — | 268 frames/s, 67 KB/s |
| **parked on channel 1**, unicast echo, 250 B, 5 ms apart | 500 | 500 | 0 | 12.9 / 4.6 / 49.3 ms | — |

- **Nothing was lost on the air**, in eight tests and 6000 frames. The one
  "loss" was the sender's: at 2 ms apart `esp_now_send()` refuses with
  NO_MEM once its queue is full. A sender that waits for the send callback
  before the next frame never hits it, and that is what the reliable and the
  unreliable channels will do.
- **The round trip is 3-5 ms** for a small frame, 10-13 ms average for a
  full one under load. A game at 30 Hz has 33 ms per state: room to spare.
- **60 KB/s** unicast with the acknowledgement wait: a Pixel Art drawing
  (a few KB) is instant, a walkie-talkie (8 KB/s) is an eighth of the air.
- **Parking works and costs 1.1 s to come back**: both watches left the
  access point, sat on channel 1, ran the echo test with zero loss, and were
  back on the network with an address **1090-1093 ms** after unparking.
  While parked the portal is off the air, so a parked episode schedules its
  own return (the `park` job does).

**The channel policy, decided:** on the same access point the link uses
its channel and nothing else happens; parking is the fallback for two
watches on different networks, and it is measured, not just planned.

**With the iPhone connected over BLE** to the sending watch (ANCS, AMS and
the rest up, the phone idle in a pocket):

| test | frames | on the air | lost | round trip avg / min / max | rate |
| --- | --- | --- | --- | --- | --- |
| unicast echo, 32 B, 20 ms apart | 200 | 200 | 0 | 5.1 / 3.1 / 22.1 ms | — |
| unicast echo, 250 B, 5 ms apart | 500 | 449 | 0 | **146.7** / 9.0 / 194.5 ms | the sender refused 51 (NO_MEM) |
| unicast, 250 B, wait for the send callback | 1000 | 1000 | 0 | — | 202 frames/s, **50 KB/s** |

So Bluetooth takes about a sixth of the air (60 → 50 KB/s) and adds half a
millisecond to a small frame, and nothing is lost on the air either way.
What it does punish is a sender that does not wait: full frames pushed
every 5 ms queue up behind the phone's connection events, the round trip
balloons to 150 ms and the driver starts refusing. Same conclusion as
before, only louder: **the sender waits for the send callback**, and a game
at 30 Hz with 20-byte states does not notice the phone at all. Not measured:
a notification arriving mid-test (it needs the phone to be sent one).

### Phase 2 (2026-09-19)

Beacons once a second with the watch's name, neighbours of the last five
seconds with their RSSI, the bump, the encrypted partner in NVS, the Link
app. Measured:

- Two simulated bumps 100 ms apart (`/api/link?do=bump` on each) paired the
  watches in both directions; 200 echo frames over the encrypted peer came
  back with a 4.6 ms round trip; the partner survived a link restart.
- **The real bump paired at the first knock** with the IMU threshold of
  0.7 g on one sample at 25 Hz, RSSI -1 to -3 dBm case against case. But
  **one knock was seventeen pairings**: the case keeps ringing above the
  threshold, every ring re-paired with a fresh key, and the two sides' last
  keys did not match. Three seconds of deafness after a pairing fixed it;
  the mismatched keys it left behind showed up in phase 3.
- The fonts have no block glyphs: the signal meter is ASCII.

### Phase 3 (2026-09-19)

The link split into a common layer (`aos_link.c`, the same file on the
board and in the simulator) over a raw layer per platform (`aos_link_esp32.c`
with ESP-NOW and the encrypted peer; UDP on 127.0.0.1 in `hal_sim.c`, one
port per simulator). The reliable channel: go-back-N, window 4, 80 ms
retransmit, cumulative acks, in order, and a sync frame so a sender whose
numbering restarted is understood by the receiver. `/api/link?do=bulk`
sends N bytes over it with a pattern the receiver checks; `do=bulkrx&drop=`
makes the receiver lose that percent on purpose.

| run | frames | retransmitted | dropped on purpose | received | time | rate |
| --- | --- | --- | --- | --- | --- | --- |
| 100 KB, 10 % loss | 558 | 36 | 50 | 100000 B, 0 bad | 6.2 s | 16 KB/s |
| 100 KB, no loss | 414 | 0 | 0 | 100000 B, 0 bad | 3.1 s | 32 KB/s |
| 100 KB, 30 % loss | 1209 | 199 | 364 | 100000 B, 0 bad | 20.4 s | 5 KB/s |
| 50 KB the other way, 10 % loss | 267 | 15 | 20 | 50000 B, 0 bad | 2.9 s | 17 KB/s |
| two simulators, 100 KB, 10 % loss | 598 | 46 | 67 | 100000 B, 0 bad | 6.6 s | — |

- **Every byte arrived, in order, under every loss rate**, on the boards and
  between two simulators on one Mac.
- Without losses the reliable channel does 32 KB/s against the raw 60: the
  window of four and one frame per 10 ms poll are the knobs, untouched until
  an app needs more.
- **The first run failed and taught the sync frame.** The reliable channel
  on the boards restarted its numbering at 1 for the second transfer while
  the receiver still expected 415; the acks "everything before 415 is done"
  made the sender declare 100 KB delivered that never arrived. Now a sender
  starts on a number chosen at random and tells the receiver with 'S'
  before the first data frame.
- **And the run before it failed on the keys**: after the seventeen-pairing
  knock, the two watches held different keys and every encrypted frame was
  dropped silently by the driver, with the MAC-level acknowledgement saying
  all was well. The confirm exchange over the encrypted peer is the only
  proof the keys match; the Link app shows it as "canal cifrado listo", and
  that is the line to look at before playing.

### Phase 4 (2026-09-19)

Pong (`apps/pong/`), a `.so` like the other games now that the link is in
the symbol table. The field is two screens glued top to top; the watch
with the lower MAC is the host, simulates at 30 Hz from both paddles and
sends the state over the fast channel; the guest sends its paddle and draws
the last state it got. Roles from a hello exchange, the paddle from the
finger, `NO_SWIPE` and `LONG_DRAG` so a drag is a drag.

| | states in / out per second |
| --- | --- |
| two simulators on one Mac | 31 / 31 |
| two watches, iPhone connected to the host | 30 / 30, "29 or 31 now and then, never lower" (the player) |

Played for a while on the two boards: the ball crosses without a stutter,
the paddle follows the finger, the score and the end of the game show on
both. v0.4.0.

### Phase 5 (2026-09-19)

Truco for two (`apps/truco/`, the same `.so` as the game against the
machine; with a partner paired in Enlace the table asks "contra quién?"
first). Tested first with two simulators on one Mac, then on the two
boards, driven from the portal (`/api/accion que=abrir`, `/api/mem?tap=`,
`tools/captura.py`) so both screens could be captured at every step.

| | |
| --- | --- |
| hello to roles decided, on the boards | 0.7 s (host), 0.5 s (guest) |
| a hand's first four moves: play, envido, quiero, and the answer | host 6 frames out / 3 in, guest 3 / 6; 0 retransmits, 0 lost |
| what travels per move | 5 bytes of payload in a 13-byte frame |
| RAM the mode adds to the app | 16 moves of inbox, 80 bytes |

Both tables agree at every capture: the same cards on the table, the trick
won by the same side, the envido counted the same (24 against 1, two points
to the host) and the scoreboard's matchsticks on the right side of each
watch. The banners name the other watch ("AMOLEDOS: ENVIDO", "ESPERANDO A
CHARLIE") from the partner's device name.

Played a whole game on the two boards afterwards, facing each other
across a table (the photos are in the README): "todo genial".

Two simulators: `TRUCO_LINK=1` skips the question, and
`AOS_SIM_LINK_PORT=47000 AOS_SIM_LINK_PARTNER=47001` on one with the ports
swapped on the other. `TRUCO_SHOWALL=1` shows both hands face up, which is
how the captures were compared. Trap: with two simulators on one Mac the
script's clock (`AOS_SIM_KEYS`) runs at about half speed while the
screenshot's (`AOS_SIM_SHOT_MS`) keeps wall time, so a script that reads
as 12 s needs a capture at 45 s.

### Phase 6 (2026-09-19)

Pixel Art, on the two boards driven from the portal: the kitten (1132
bytes) from charlie's slot 1 to amoledos's slot 5.

| | |
| --- | --- |
| frames on the reliable channel | 6 out (start + 5 chunks), 1 back with the slot |
| retransmits / lost | 0 / 0 |
| from the tap to the toast on the other watch | under a second |

Radar, on the two boards side by side on the desk (about 10 cm apart),
amoledos the initiator and charlie the responder:

| | |
| --- | --- |
| RSSI seen at 10 cm | -8 to -10 dBm, both ways |
| pings | 10/s each way; the initiator's arrive at 60-70 % while its FTM sessions run, the responder's at 100 % |
| FTM session | 16 frames asked, 12-14 valid readings out of 12-14 received, two bursts of 8, 200 ms apart; a session ends in about 300 ms |
| FTM raw distance, 20 sessions at 10 cm | 105 to 240 cm, mean about 170 cm |
| FTM resolution | the driver reports whole nanoseconds of RTT: one step is 15 cm |

So the driver's number carries an offset of about a metre and a half on
these boards and jumps by three or four steps from one session to the
next; the app averages the last five sessions and the calibration at one
metre takes the offset out (`radar_ftm0` in the preferences, alongside
`radar_p0` for the RSSI). What is not measured yet, because it needs
someone walking with a watch and a tape measure: the slope. FTM should be
right to about a metre from two to twenty metres indoors; RSSI is expected
to be useless past a few metres. Both boards on a desk say nothing about
that, and the table above is the calibration point, not the verdict.

Trap found on the way: an app without `KEEP_AWAKE` is closed when the
screen times out (30 s), which makes a test driven from the portal with a
pause in the middle look like a crash. Pixel Art is such an app; the
tests had to be scripted without gaps.

### Phase 7 (2026-09-19)

The walkie on the two boards, driven from the portal: the talk button held
for three seconds on one watch with the other listening, both ways.

| | |
| --- | --- |
| frames while the button is held | 100 in 3 s, 34/s, as designed |
| frames received on the other watch | 100 of 100, 0 lost |
| on the air | 8.3 KB/s, 240-byte frames on the fast channel |
| IMA ADPCM round trip (tools, on the Mac) | 30 dB SNR on a two-tone test signal |
| release to listening | the microphone's task lets go of the codec a little after close(); the speaker waits for it (bounded at 800 ms) and opens |

Trap found on the way: the first version opened the speaker right after
closing the microphone and lost every time, because the capture task
releases the codec when it ends, not when close() returns. The HAL's
open() now waits for it; the app retries from its tick as well.

## The sixth app: a place, not a mode (Chatarra's phone booth)

The five apps above are link apps: you open them to talk to the other watch.
Chatarra is an RPG played alone for hours that grew **a phone booth in each of
its eight towns**. The radio goes up when you walk into one and comes down when
you walk out, so being reachable is somewhere you go rather than a mode you
switch on — and the other 51 rooms of the game are not on the air at all.

Two things it contributed back to this document:

- **Bring the radio up BEFORE asking whether anybody is paired.** On the board
  the partner is in NVS and either order works; in the simulator the partner is
  only put there by the link's own tick, which is why Truco and Pixel Art each
  need a development flag to skip the question. Start, then poll for a partner
  for a second or two, and no flag is needed — and it is the honest order, since
  "is anybody on the air?" cannot be asked with the radio off.
- **Anything a shared engine decides at random has to be phrased so both sides
  mean the same thing by it.** Chatarra's combat breaks a tie on speed by asking
  "does the rival go first?", which is the *opposite* question on the two
  watches: the same coin would have both of them answering yes. It draws "does
  the HOST go first?" instead and each side turns that into its own answer.

<p align="center">
  <img src="img/photo-chatarra-booth.jpg" width="300" alt="The phone booth on two watches, connected to each other">
  <img src="img/photo-chatarra-link-battle.jpg" width="300" alt="The same battle seen from both watches">
</p>
<p align="center"><em>The booth, and the same battle from both sides: one watch
is CRATE LV6 and sees the rival at LV5, the other is CAJA N5 and sees the rival
at N6. Only two bytes a turn travel; both watches compute the damage.</em></p>

## Traps expected, to be confirmed or struck out

- A watch that leaves its access point loses the portal and the phone's
  time; the Link screen has to say so, and reconnect on exit even if the app
  crashed (the HAL's stop path, not the app's).
- Encrypted peers are six at most; more than six watches in a room is a
  party, not a link.
- The MAC-level acknowledgement of a unicast says the frame was received by
  the radio, not by the app; the reliable channel's acknowledgement is its
  own.
- The bump needs both accelerometers polled at 25 Hz: the screen is on
  during pairing, so it is; in light sleep the poll is 10 Hz (STEPS.md) and
  a bump could be missed, another reason the link only lives while its app
  is open.
- With the iPhone connected, ANCS traffic and ESP-NOW share the air;
  measured before it is designed around.
- **The radio up AND the panel pushed hard at the same time can reset the
  board.** Seen once, on the first Chatarra battle between the two watches: the
  crash is a race inside ESP-IDF's SPI bus lock, where the LVGL flush is inside
  `req_core()` and the SPI interrupt lands in the window before the device is
  published, so `bg_exit_core()` calls `resume_dev_in_isr(NULL)`. The radio
  alone survives (the Link app, 90 s; Chatarra idle in the booth, 3 minutes);
  it takes both. Not the protocol's, and not fixable from an app.
- **The radio up and the panel flushing hard can reboot the watch, and it
  is IDF's, not the link's.** Seen once in a Chatarra battle over the link
  (2026-09-20): a coredump in the panel's SPI interrupt, `LoadProhibited` at
  address 0 inside `resume_dev_in_isr()`, with the LVGL task in the middle
  of a flush on the other core. It is
  [espressif/esp-idf#18527](https://github.com/espressif/esp-idf/issues/18527):
  `spi_bus_lock.c` reads `acquiring_dev` twice in `bg_exit_core()` and
  another core can clear it in between. The condition is two cores on the
  same bus lock at once, which the firmware used to provide for free: the
  panel's SPI interrupt lived on core 0 (allocated by `app_main`) and the
  LVGL task had no affinity. Since v0.4.4 the display is brought up from a
  task pinned to core 1, so the interrupt lives there, the LVGL and
  housekeeping tasks are pinned there, and a brightness or sleep command
  from any other task (the portal, BLE) runs on that core through the IPC
  task; WiFi and its interrupts stay on core 0. `/api/mem?intr=1` shows the
  placement. The driver's own fix is in `tools/idf-patches/`, to apply to
  the local IDF until upstream ships it. Measured: 20 minutes of Pong
  (30 frames/s each way, a full-screen canvas at 30 fps) plus a loop of
  brightness writes from the portal task, on both boards, before and after,
  with no reboot either way, and the video player at the same frame rate:
  the race is rarer than that, and the change is argued from the
  interrupt placement, not from a reproduction. The write-up is
  `docs/internal/HANDOFF-SPI-WIFI-NUCLEOS.md`.
- Lockstep only holds while the engine is deterministic: a move decided by
  `tr_ai_decide()` on one side, or anything that reads the state's random
  generator outside the deal, would fork the two games silently. In link
  mode the machine never moves and the generator is only read by the deal.
  If a future rule needs randomness, the host must send the result, not the
  question.
