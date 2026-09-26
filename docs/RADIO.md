# Internet radio

A radio on the wrist: nine preset keys, a lit dial with the station, the song
and its cover, and a needle over a numbered scale. The keys are chosen in the
portal's `/radio` page from a list of stations kept on the card, searched in
radio-browser.info's open directory. Every figure here was measured on
charlie.local (v2 board) on 2026-09-26, against real stations.

<img src="img/app-radio.png" width="220"> <img src="img/app-radio-info.png" width="220"> <img src="img/app-radio-idle.png" width="220">

## How it is split

The sound is the firmware's; the app is the front panel.

```
aos_radio.c     reader task        station -> redirects, playlists, ICY -> byte ring (192 KB, PSRAM)
aos_audio.c     player_task        byte ring -> minimp3 -> (L+R)/2 -> PCM ring (2 s, PSRAM)
aos_hal_esp32.c player_out_task    PCM ring -> ES8311
apps/radio      the panel          aos_hal_radio_play(keys, 9, i), then reads the state back
aos_web         /radio, /api/radio the keys (NVS), the list and the logos (card)
```

A station is **one more source for the player** that plays the card's MP3s
(MUSIC.md): the same decoder task, the same PCM ring and writer, the same
yielding to an app that opens the speaker. So what the Music player got for
free, the radio gets too: it goes on with the app closed, the control centre
drives it (its title row shows the song, or the station between songs), and
`/api/player` measures it. `aos_audio_open_src()` is the only new entry into
the decoder: MP3 from a function instead of a file, with no tags, no length
and no seeking.

The app hands the whole list of keys to `aos_hal_radio_play()`, so next and
previous walk the keys from anywhere: the app, the control centre, the portal.

## What a station URL turns out to be

In the order the reader meets them, all seen on stations from radio-browser:

| What | Seen on | What the reader does |
| --- | --- | --- |
| A 302 to a numbered edge server | StreamTheWorld (Metro, Aspen, La 100, Rock & Pop...) | follows it. The address carries a token that expires: after a drop it starts again from the station's own URL |
| A 302 with a **relative** `Location` | Mediainbox (La Popu) | resolved against the URL that sent it |
| A `.pls` or `.m3u` | older stations | the first http(s) line is the stream |
| HLS (`.m3u8` with `#EXT-X-`) | BBC and others | refused: "HLS: not supported" |
| `HTTP/1.0 200`, `HTTP/1.1 200` or `ICY 200 OK` | Icecast, CDNs, Shoutcast 1 | all three taken |
| `Content-Type: audio/aac` | Radio Paradise Mellow | refused with the type ("audio/aac: only MP3 streams play") |
| chunked transfer | none so far (HTTP/1.0 is asked for) | taken apart anyway |

With `Icy-MetaData: 1` the server interleaves, every `icy-metaint` bytes of
audio, a length byte and `StreamTitle='Artist - Title';`. The reader takes it
out before the decoder sees anything and **remembers the audio byte where it
arrived**. The decoder reports when it has read past that byte, and the
player publishes the title when that sample reaches the speaker: with ~10 s
of buffer, a title shown when it was downloaded would change ten seconds
before the song does.

Two repairs on the way:

- **Twice-encoded UTF-8.** METRO 95.1 sends "Así" as `C3 83 C2 AD`: UTF-8 read
  as Latin-1 and encoded again. Valid UTF-8 whose characters all fit in a
  byte, and whose bytes are UTF-8 again, is undone once. Latin-1 titles
  (invalid UTF-8) are converted; real UTF-8 is left alone.
- A title of only punctuation ("-", La Nación +Música between songs) is
  nothing, and the dial shows the station's own name.

## What it costs

| | At rest | Playing |
| --- | --- | --- |
| Internal RAM | 0 | **17.9 KB**: the reader's stack (7 KB) and the player's two (10 KB); back to the byte after stopping |
| PSRAM | 0 | 257 KB: the byte ring (192 KB), the reader's buffers, the decoder's; plus the player's PCM ring, allocated once |
| CPU (decoder, 96-192 kbps) | | **11-12 % of one core** (`load_permille` 116-124) |
| Reader's stack | | 4.7 KB used of 6 on a redirect with two TLS handshakes; 7 KB given |
| Firmware | | +51 KB against v0.7.0 (3,960,048 -> 4,012,032 B), 33 KB of it the `/radio` page |

The app itself is 24 KB of `.so` and 51 KB of PSRAM for its two covers.

## Time to sound

From the tap to the first sample, polled through `/api/radio`:

| Station | Path | WiFi in modem sleep | Power save off while connecting |
| --- | --- | --- | --- |
| Radio Paradise | http | 3.3 s | **1.3 s** |
| Nacional Rock | http | 1.2 s | 1.2 s |
| Radio Swiss Jazz | http, 302 to a node | 8.2 s | **2.6 s** |
| SomaFM | https | 6.2 s | **3.0 s** |
| Metro 95.1 | https, 302, https | 10.3 s | **4.1 s** |
| La 100, Rock & Pop | https, 302, https | - | 3.7, 4.2 s |

The lesson: with the screen on the watch's WiFi sits in modem sleep, and a
round trip takes 200-300 ms instead of 10. A TLS handshake is a dozen of them.
So the radio turns power save **off while a station connects** and back to
modem sleep once it plays (`s_radio_ps` in `pm_policy_apply()`). With
Bluetooth up, coexistence needs modem sleep and it is left alone.

The other half: with the screen off the watch normally drops to the
**deepest** modem sleep, whose long listen interval would starve a stream.
While a station plays it never goes deeper than the light one.

## Holding up

Four minutes of Metro 95.1 with the screen off: **0 underruns, 0
reconnects**, the buffer between 9.1 and 10.0 s the whole time, the PCM ring
never under 1.6 s, the decoder at priority 2 throughout (it only rises to 5
when the ring falls under half). Two title changes, both on the song.

A dropped connection is retried by the reader on its own, 1, 2, 4, 8 and then
every 15 s, from the station's own URL; eight failures in a row with no audio
between them and it gives up with the reason. The decoder only sees a pause.
A 404, a 403, an AAC stream or an HLS playlist is given up at once: waiting
will not change them.

**Pausing** keeps the connection while the ring holds (about 16 s at 96 kbps,
10 at 160): the watch goes on reading behind the pause. Past that the
connection is let go (a server drops a listener that does not read anyway),
and a pause longer than 20 s resumes **live**, reconnecting: the listener
expects the radio, not a recording of what it said a minute ago.

## The covers

In the dial, in order: the song's cover, the key's logo, a drawn speaker
grille.

- **The song's**, from the iTunes Search API
  (`itunes.apple.com/search?entity=song&limit=1&term=artist+title`, over
  https through the HAL's client). iTunes serves its covers at any size by
  URL, so the watch asks for exactly 160 px. A search for a jingle finds
  somebody else's record, so the hit must share its artist's first real word
  with the title; titles without " - ", and those whose "artist" is the
  station itself ("METRO - Dance"), are not searched. It can be turned off
  in `/radio`: what leaves the watch is "artist title".
- **The logo**, made **in the browser** when a station goes on a key: the
  station's favicon drawn on a black 160 px square and saved as JPEG to
  `radio/logoN.jpg`. The firmware never decodes a PNG, an ICO or an SVG.
  When the station's site does not allow the image to be read (no CORS),
  the page asks `wsrv.nl`, a public image proxy, for it.

## The portal

`/radio` has what plays now with its controls, the nine keys (drag one onto
another to swap them, with their logos), **My list** (kept on the card as
`radio/library.json`; the first time, 25 stations checked against this code),
a search in radio-browser.info (country, genre, and "only MP3", on by
default; an AAC station is marked in red), a form for an address by hand,
and the covers switch. Each station can be heard in the browser, or tried on
the watch without putting it on a key.

```
GET  /api/radio                          keys, switches, what plays, the stream's figures
POST /api/radio  do=set&k=N&name=&url=   put a station on key N (0..8)
                 do=clear&k=N | do=swap&a=&b=
                 do=play&k=N | do=test&name=&url=
                 do=pause|resume|stop|next|prev | do=vol&v= | do=art&on=
```

The keys are NVS strings `rad0`..`rad8` = `name \x1F url` and `rad_gen` goes
up on every save: the arrangement of the cameras, and the app rebuilds its
keys when it moves.

## Testing without the board

- **The simulator plays the radio for real**: the same `aos_radio.c` and
  `aos_audio.c`, and SDL for the sound (`AOS_SIM_RADIO_MUTE=1` decodes in
  silence). The keys come from `sim/sim_fs/prefs.txt`, the same file
  `tools/portal_dev_server.py` writes from the `/radio` page.
- **`tools/radio_bench/radio_bench.c`** runs the stream code alone against a
  URL and says what it found: redirects, headers, codec, titles, and the
  decoded audio as a WAV if asked. The first list was checked with it: 26 of
  28 candidates played; the other two were AAC and a host that no longer
  exists.

## Not done

- **AAC.** Many stations stream only AAC or HE-AAC. minimp3 does not decode
  it, and the open AAC decoders have licences that do not sit well in an MIT
  repository (FDK, faad2) or are closed binaries (Espressif's
  `esp_audio_codec`). The search marks those stations, and a station that
  answers `audio/aac` is refused with that reason.
- **The battery cost.** The CPU stays at 240 MHz and the WiFi out of deep
  modem sleep while a station plays. Not measured yet
  (`tools/battery_night.py`, an hour with the radio and one without).
- **An observation, not a finding:** on the day of these measurements four of
  seven firmware uploads (OTA) were cut halfway by the WiFi (`bcn_timeout`,
  the access point's beacons missed), three of them before this firmware was
  on the watch. Uploads with the screen awake and no station playing went
  through.
