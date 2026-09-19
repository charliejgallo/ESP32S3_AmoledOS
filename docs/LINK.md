# Link: two watches talking (ESP-NOW)

The plan for v0.4: a radio link between two watches with no router in
between, and the apps that make it worth having — a game across two screens,
a card table, a walkie-talkie. Written before the first line of code, on
2026-09-19, when the second board arrived; it will be kept as the record of
what was planned against what was measured, phase by phase, the way
[VIDEO.md](VIDEO.md) and [STEPS.md](STEPS.md) were.

Branch `espnow`. The merge is v0.4.0: the link, its HAL, the simulator's
version of it, and the first two-player app. The rest of the apps come as
v0.4.x on top.

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
  state rate are on this page.

### v0.4.x — the rest

- Truco for two (reliable channel; the existing Truco against the machine
  gets a second seat).
- Send a drawing from Pixel Art.
- The walkie-talkie: the first thing that needs a streaming speaker API in
  the HAL (`aos_hal_spk_open/write/close`), which the player of WAV files
  does not offer today. That API is its own small piece of work, and the
  video's clock problem (VIDEO.md) says it should carry a sample counter.
- Radar: RSSI first; FTM behind `ESP_WIFI_FTM_ENABLE`, measured against a
  tape measure.
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
