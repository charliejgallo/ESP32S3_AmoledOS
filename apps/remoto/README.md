# Remoto — a programmable Home Assistant remote

A dynamic AmoledOS app. Pages of buttons, accelerometer gestures and a dial you
work by turning your wrist. What each thing does **is not in the code**: it comes
from a profile edited at `http://<board-ip>/remoto` and stored on the microSD.

```
  apps/remoto/
    main/
      rc_model.[ch]   the profile: reading and understanding it   (no LVGL, no HAL)
      rc_tilt.[ch]    the accelerometer: from three numbers to a gesture  (idem)
      rc_ha.[ch]      the network: calling services and fetching states
      remoto.c        screen, touches, and who hands what to whom
    tools/
      rc_harness.c    test bench, compiles with plain 'cc'
      fake_ha.py      a make-believe Home Assistant, for testing without the house
```

The first two units depend on nothing, and that is why they can really be
tested: `rc_harness` checks the angles against the four postures **measured on
the board**, verifies that walking around for a minute with the remote in your
hand fires no gesture, and reads a whole profile without anything having to be
flashed.

---

## What is needed on the Home Assistant side

A **long-lived access token**: user profile, right at the bottom, *Long-lived
access tokens*. Nothing else. There is no need to create one automation per
button or to configure webhooks: the remote calls services directly.

It is entered at `http://<ip>/remoto`, along with the address. The **Test**
button asks Home Assistant from the board and answers whether the token works;
if it does, the page fetches the entity list and from then on the fields
autocomplete by themselves.

> **The token travels in the clear.** The firmware does not do TLS on purpose:
> no certificates and no 40 KB of mbedtls per connection. That is acceptable
> against your own Home Assistant on your own LAN; it is not against an
> installation exposed to the internet. If yours is, use the internal address.

## How state shows up on the buttons

A button can show an entity's state: on/off (the button is painted filled or
tinted), the state as text, or an attribute as text.

All of that is fetched in **a single request**, with a Jinja template:

```
{{states('light.salon')}}|{{states('sensor.living')}}|{{state_attr('light.salon','brightness')}}
```

and Home Assistant answers `on|21.4|128` in plain text. One request over wifi
takes close to a second on this board: asking `/api/states/<entity>` one by one
would be fifteen seconds to refresh a screen, and there are not that many HTTP
slots in the HAL. Repeated entities share a slot: six buttons on the same light
are a single query.

## The gestures

| Gesture | What it is |
| --- | --- |
| Shake it | total accelerometer variation above a threshold |
| Tilt left / right | wrist roll relative to **how you were holding it** |
| Tilt forward / back | pitch, idem |
| Turn it over | face down, held |

Three things worth knowing:

- **Rest is a moving target.** The gestures are relative to the posture you hold
  the remote in, not to the vertical: holding it tilted fires nothing, and rest
  moves along by itself if you change posture.
- **The shake wins.** It is evaluated first and, on being recognised, the whole
  posture is discarded. A real shake has so much lateral component that halfway
  through it the board "is tilted"; the test bench used to recognise `TILT_R` on
  the first jerk before that rule existed.
- **Tilting forward and turning it over are the same movement** at different
  depths. If you configure both, the pitch wins and the face-down never gets to
  fire. The recogniser only looks for the gestures that are in the profile, so if
  you configure only one it works fine.

With any gesture configured, the app **keeps the screen on** while it is open.
That is not an oversight: the runtime closes the app when the screen dims, and a
closed app listens to nothing. It costs battery, so it can be turned off
(*Active: no* in the portal) and then the screen behaves as in any other app.

### Choosing the thresholds

**Hold the title down** and the diagnostics screen appears: the three raw
accelerometer readings, the modulus, the two angles, the current rest, the live
shake value and the last gesture recognised.

You shake the remote, look at the peak and set the threshold a little below it.
You tilt until the gesture's name appears. It is the only honest way to pick
these numbers: in the simulator they come from a formula, on the board they come
from a sensor and from the hand holding it.

The defaults (tilt 40 degrees, shake 2000, cooldown 1500 ms) are deliberately
conservative: a remote that turns lights on by itself is worse than one that
takes a bit of effort to fire.

## The dial

You press the circle in the middle and turn your wrist; on release it stays put.
With *Send every N ms* the light follows the turn live.

- Zero is **where you were when you pressed**, not the vertical. It works the
  same sitting at the table as sprawled on the sofa, and there is nothing to
  calibrate.
- The turn is **accumulated sample by sample**. `atan2` wraps at +-180, so
  subtracting against the latch angle makes a 200-degree turn read as -160 and
  the dial jumps to the other end exactly when you turn the most.
- *Wrist degrees* is how far you have to turn to cover the whole range, minimum
  to maximum. 140 is comfortable; less makes it twitchy.
- *Full scale* is what the attribute is worth at maximum, so the real value can
  be shown when the page opens: a light's `brightness` goes from 0 to **255**
  even though you send it `brightness_pct` from 0 to 100.

There are also two `-` / `+` buttons that move by a twentieth of the range, for
adjusting without lifting the remote.

## Navigation

| | |
| --- | --- |
| Swipe left | next page |
| Swipe right | previous page; on the first one, exit |
| Physical button | exit (the app does not keep it) |
| Hold the title | accelerometer diagnostics screen |

The app asks for `AOS_APP_FLAG_NO_SWIPE` because the horizontal swipe is how you
change page. In exchange there are **two exits** and both get used without
thinking.

## The profile

A JSON at `<data>/remoto.json` on the microSD. The firmware **does not
understand it**: it receives it, writes it and hands it back as it is; the one
that interprets it is the `.so`. The only thing they share is the NVS key
`rc_gen`, which goes up on every save and which the app re-reads now and then to
find out there is something new. With the app open, saving from the browser
reloads it by itself.

Only `rc_url` and `rc_token` stay in NVS: they are short, they are secret, and
they have no business floating around in a file readable from any card reader.

Ceilings: 8 pages, 9 buttons per page, 40 entities with state.

`example-profile.json` in this folder is a working one to start from: three
pages, a dial, the four gestures, and the entities `fake_ha.py` serves.

To look at a profile without the board:

```bash
cc -O1 -Iapps/remoto/main -Icomponents/aos_ui/include \
   -Icomponents/aos_hal/include -o /tmp/rc_harness \
   apps/remoto/tools/rc_harness.c \
   apps/remoto/main/rc_model.c apps/remoto/main/rc_tilt.c -lm
/tmp/rc_harness apps/remoto/example-profile.json
```

## Trying it without the board

```bash
# 1. a make-believe Home Assistant answering the two endpoints the remote uses
python3 apps/remoto/tools/fake_ha.py &

# 2. the portal, against the same folder the simulator uses
python3 tools/portal_dev_server.py &        # http://localhost:8088/remoto

# 3. the simulator
cd sim && cmake --build build -j8
AOS_SIM_VIEW=aos.remoto ./build/amoledos_sim
```

The three share `sim/sim_fs`, so you can keep the page open beside the
simulator: you save and the app reloads itself, just as on the board.
`fake_ha.py` prints every call it receives, which is what it is for.

`--sin-token` and `--lento N` force the two cases that are awkward to reproduce
against the real Home Assistant: the rejected token and the slow answer.

### The accelerometer in the simulator

Posture **3, "in the hand"** (key `I` until you get there) maps the mouse to roll
and pitch with the axes measured on the board. The other three predate that
measurement and were left alone, because the spirit level and the games are
calibrated against them.

Hands-free, from a script:

```bash
AOS_SIM_VIEW=aos.remoto AOS_SIM_KEYS="pose:3,tilt:0x0,ms:500,swipe:left,\
ms:900,hold:184x178:6000,tilt:5x0,tilt:10x0,tilt:15x0,tilt:20x0,tilt:25x0" \
  ./build/amoledos_sim
```

`tilt:` works **while a finger is down as well**, which is the only way to test
the dial: you latch by pressing and then keep turning.

### Hunting memory corruption

The simulator builds with AddressSanitizer without touching anything:

```bash
cd sim
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan -j8
AOS_SIM_VIEW=aos.remoto AOS_SIM_KEYS="..." ./build-asan/amoledos_sim
```

A long script that walks through everything and **ends by leaving the app** is
worth writing: that is when the context is destroyed, and that is where this
app's only use-after-free showed up. The folder is 125 MB, delete it afterwards.

## Development switches

| | |
| --- | --- |
| `REMOTO_PERFIL=<path>` | read the profile from somewhere else (or from nowhere, to see the help screen). `apps/remoto/example-profile.json` is the one the screenshots use |

## Building and installing

```bash
./tools/build_apps.sh remoto
cp apps/remoto/build/remoto.so /Volumes/<sd>/apps/
```

**This app needs new firmware.** What was added:

- `aos_hal_http_request()` — the HAL only knew how to do a GET with no headers,
  and Home Assistant's REST API wants a POST with `Authorization: Bearer` and a
  body.
- `aos_ui_take_gesture()` — so that an app with horizontal pages can see the
  swipes the touch chip detects.
- `AOS_ICON_REMOTE` and its drawing in `aos_icon.c`.
- The portal's `/remoto` page and its six endpoints.
- `__extendsfdf2` / `__truncdfsf2` in the symbol table: any `snprintf("%f", x)`
  with a float drags them in.
