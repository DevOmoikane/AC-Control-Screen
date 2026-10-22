# Midea ESP-NOW AC Controller (ESP32-C3 + ESP32-2432S028)

**Status:** functional prototype source with host-side protocol tests. **Not yet compiled, flashed, or tested on the two actual ESP32 boards.** Hardware and AC-model verification are required.

This is a **single PlatformIO project with two build environments**:

- **`ir_node`** — ESP32-C3 Super Mini, 38 kHz infrared transmission using hardware RMT; GPIO1 and (optionally) GPIO2 IR emitters.
- **`cyd_remote`** — classic ESP32-2432S028 (CYD), 240×320 ILI9341 display in 320×240 landscape and XPT2046 resistive touch. ESP-NOW transmitter and optional Wi-Fi/MQTT Home Assistant bridge. The touch UI is **LVGL 9.4**, laid out in [EEZ Studio](https://eez.studio/) (`ac-control-cyberpunk/`).

The intended AC remote is **Midea RG57A7/BGEF**, matching an earlier project using 48-bit frames beginning `4D B2`. The dedicated codec does *not* assume that the generic `IRMideaAC` representation is interchangeable with the captures.

## Features

- Automatically discovers the peer via ESP-NOW (no MAC editing needed).
- No router, cloud, server, or Internet required by default.
- Change power, 17–30 °C, Auto/Cool/Dry/Fan/Heat, Auto/Low/Medium/High fan; LED and Turbo toggle.
- Touch UI built with **EEZ Studio + LVGL 9.4**: three screens (climate arc, extras, network), regeneratable from the `.eez-project` without hand-written drawing code.
- Splash screen with the project logo, shown at startup and after a period of inactivity; a tap dismisses it.
- Infrared transmitter uses the ESP32-C3's two RMT TX channels to drive two different output GPIOs with matching carrier timing.
- User input queues commands; adjacent state changes coalesce; application-level ACK, timeout and retry with duplicate sequence suppression.
- One-time three-point resistive-touch calibration stored in NVS.
- Remembers the last successfully transmitted logical state (not the measured physical AC state).
- Optional Home Assistant MQTT climate discovery and MQTT commands on the CYD, with no Wi-Fi credentials required in default ESP-NOW-only mode.

## Requirements

- PlatformIO CLI or PlatformIO IDE extension for VS Code.
- Python 3 for the host tools (`tools/`). `pyserial` is needed to record the original remote; the PlatformIO python already has it.
- ESP32-C3 Super Mini connected by USB for flashing.
- Standard ESP32-2432S028 / ESP32-2432S028R *resistive touch* board (ILI9341 + XPT2046) connected by USB for flashing.
- 1 or 2 IR LEDs **with transistor drivers and current-limiting resistors**, and a 3.3 V-compatible supply; see [wiring](docs/WIRING.md).
- `platformio.ini` pins target the common ILI9341/XPT2046 version. Other CYD revisions or a **literal 240×230 panel** are not covered without adjustment.

## Folder layout

```text
espnow-midea-ac/
├── platformio.ini             # Two build environments
├── include/
│   ├── AcConfig.h             # Channel, optional Wi-Fi/MQTT, IR GPIOs
│   ├── AcProtocol.h           # Shared wire protocol, states, ACKs
│   ├── MideaRG57Codec.h       # RG57 frame construction/checksum
│   ├── IrReceiver.h           # IR capture interface
│   ├── IrDecode.h             # Pulse burst to RG57 frame (host testable)
│   ├── IrAudit.h              # Capture analysis against the codec
│   └── lv_conf.h              # LVGL 9.4 configuration
├── src/
│   ├── ir_node.cpp            # ESP32-C3 / RMT / ESPNOW receiver
│   ├── cyd_remote.cpp         # LVGL UI + touch + ESPNOW + MQTT bridge
│   ├── ui/                    # EEZ Studio generated UI (do not edit by hand)
│   └── splash/                # Splash bitmap, generated from splash.png
├── tools/
│   ├── acports.py             # Find a board by its USB VID:PID
│   ├── pio.sh                 # Flash and monitor without hardcoded ports
│   ├── ir_record.py           # Record the original remote, semi automated
│   ├── ir_record.sh           # Same, finds a python with pyserial
│   ├── ir_audit.cpp           # Compare captures with the codec, write report
│   ├── png_to_lvgl.py         # Turn a PNG into an LVGL RGB565 bitmap
│   ├── render_splash.c        # Host renderer for the splash screen
│   └── render-splash.sh       # Same, writes a PNG
├── captures/                  # Recordings of the original remote
├── ac-control-cyberpunk/      # EEZ Studio project (source of truth for the UI)
│   ├── AC_Control_Cyberpunk.eez-project
│   ├── splash.png             # Splash artwork
│   ├── build-eez.sh           # Headless regeneration of src/ui
│   └── INTEGRATION.md
├── tests/
│   ├── test_codec.cpp         # Host-side codec tests
│   ├── test_ir_decode.cpp     # Host-side IR decoder tests
│   ├── test_ir_ring.cpp       # Host-side pulse buffer tests
│   ├── test_ir_audit.cpp      # Host-side capture analysis tests
│   ├── test_ir_record.py      # Host-side recorder log parsing tests
│   ├── test_ir_record_cancel.py # Host-side recorder cancel key tests
│   └── run.sh
└── docs/
    ├── WIRING.md
    └── HOME_ASSISTANT.md
```

## Quick start

1. **Wire the IR LEDs safely:** [docs/WIRING.md](docs/WIRING.md). GPIO1 is the primary IR output. GPIO2 is optional and needs boot-strapping care.
2. Open this folder as a PlatformIO project (the folder with `platformio.ini`).
3. In `include/AcConfig.h` leave `ENABLE_MQTT = false` and `ESPNOW_CHANNEL = 1` on the first test. Both devices need to use the **same 2.4 GHz channel**.
4. Connect **ESP32-C3** to USB. Build and upload its firmware:

   ```sh
   tools/pio.sh upload ir_node
   tools/pio.sh monitor ir_node
   ```

5. Connect **ESP32-2432S028** instead. Build and upload:

   ```sh
   tools/pio.sh upload cyd_remote
   tools/pio.sh monitor cyd_remote
   ```

   The wrapper finds the board by its USB VID:PID, so the same commands work
   on Linux, macOS and Windows. `pio run -e ir_node -t upload` also works on
   its own when only one board is connected.

6. On the CYD's first boot, touch the three displayed crosshairs **in order**. Calibration is saved. To repeat later, open the CYD serial monitor and send the character `c`.
7. Power both boards. The link indicator in the top-right of the header should change from `OFFLINE` to `LINKED` after the ESP-NOW Hello frames arrive. The IR node logs its MAC and each actual 6-byte IR packet.
8. Point the emitters at the AC. Switch the AC on and vary the temperature. Verify operation on your particular AC before relying on the optional LED/Turbo tiles.

### Finding the serial port

`platformio.ini` deliberately has no `upload_port` or `monitor_port`. The
device names differ per operating system (`/dev/ttyACM0`, `/dev/cu.usbmodem101`,
`COM3`), so writing one down only works on one machine. The USB vendor and
product ids do not change, so they are what the wrapper looks for.

```sh
tools/pio.sh ports          # every connected device with its VID:PID
```

| Board | USB VID:PID | Linux | macOS |
| --- | --- | --- | --- |
| ESP32-C3 (IR node) | `303A:1001` | `/dev/ttyACM0` | `/dev/cu.usbmodemXXXX` |
| ESP32-2432S028 (CYD) | `1A86:7523` (CH340), `10C4:EA60` (CP210x), `0403:6001` (FT232) | `/dev/ttyUSB0` | `/dev/cu.usbserial-XXXX` or `/dev/cu.SLAB_USBtoUART` |

| Board | Port | Carries |
| --- | --- | --- |
| ESP32-C3 (IR node) | native USB | the IR node's log, `Serial` **is** the USB CDC on this board |
| ESP32-2432S028 (CYD) | USB-UART bridge | the CYD's log, `Serial` is UART0 |

The IR node used to log to `Serial1`, which is UART1 on GPIO9/GPIO10. Those
pins are not wired to the USB connector, so nothing was ever visible. It now
logs to `Serial`, which is the USB CDC because `ARDUINO_USB_CDC_ON_BOOT=1` and
`ARDUINO_USB_MODE=1` are set for that environment.

If `/dev/ttyACM0` vanishes right after flashing, that is usually just the CDC
re-enumerating: the device gets a new node, so re-run `pio device list` and
reopen the monitor. If it does not come back, the node is not running — reset
it and watch the log.

Build firmware without uploads:

```sh
pio run -e ir_node
pio run -e cyd_remote
```

Run the **host-side** (no MCU required) codec tests on Linux/macOS with `g++`:

```sh
./tests/run.sh
```

## Cloning the original remote (IR receiver)

The IR node can listen to the original RG57 remote through a 38 kHz receiver
module (TSOP38238 / VS1838B style) on **GPIO4**, so the protocol can be
verified against the real handset before the encoder is changed.

```c
// include/AcConfig.h
constexpr bool IR_RX_ENABLED = true;
constexpr int IR_RX_PIN = 4;
constexpr int IR_RX_PULL = 0;  // 0 = none, 1 = pull-up, 2 = pull-down
```

### Wiring

| Receiver | ESP32-C3 |
| --- | --- |
| VCC | **3.3 V** — a module powered at 5 V drives its output to 5 V, which the C3's GPIO cannot tolerate |
| GND | GND, common with the C3 |
| OUT | GPIO4 |

Most 38 kHz modules (TSOP38238, VS1838B) have a **push-pull** output and need
no pull at all, which is why `IR_RX_PULL` defaults to 0. A module with an
**open-collector** output needs a pull-**up** (`IR_RX_PULL = 1`); with a pull-down
the line would sit low forever and no edge would ever arrive.

### Diagnostics

The IR node's status line reports the input activity every five seconds:

```text
radio channel 5 (want 5), remote paired, last frame from remote 1ms ago, ir interrupts 0, ir pulses 0, ir dropped 0, ir level LOW
```

- `ir interrupts 0` — the pin never changes: the module is not driving it.
  Check VCC, GND, and that OUT really reaches GPIO4. To separate wiring from
  firmware, disconnect the module and briefly bridge GPIO4 to GND with a
  jumper: the counter must jump.
- `ir interrupts` growing but no burst printed — the capture works but the
  burst analysis is not seeing it.
- `ir pulses` growing — pulses are arriving and are long enough to be real;
  the raw trace will follow.
- `ir dropped` — pulses that were lost because the reader did not keep up.
  **This must stay at 0.** A frame is about a hundred pulses and the buffer
  holds 512, so a value that is not zero means frames are being thrown away.
- `ir level` — the level the pin is at right now.

A pin with nothing connected to it floats and picks up noise, which shows up
as a rising `ir interrupts` with a high `ir glitches` count and no captures.
That is what a missing receiver module looks like.

The module is demodulated by the receiver, so the firmware only has to measure
pulse widths. Every edge is captured in a GPIO interrupt, so the timings do not
depend on polling. **No protocol is assumed** — the node reports the raw trace
plus a histogram of the pulse widths, which is what identifies an unknown
encoding:

```text
[ir rx] capture 1: 98 edges
[ir rx] decode: header 4350/4400 us, 48 bits, bytes 4D B2 FC 03 06 F9
[ir rx] raw (level:width us):
[ir rx]  H:4350  L:4400  H:560  L:560  H:560  L:1690 ...
[ir rx] pulse width histogram (level:width us x count):
[ir rx]   H:4350   x 1
[ir rx]   H:560    x 48
[ir rx]   L:560    x 18
[ir rx]   L:1690   x 30
[ir rx] long pulses (>= 1500 us): H:4350 L:4400
[ir rx] 98 edges, 4 distinct widths, 2 long pulses
```

The histogram is the useful part for an unfamiliar remote: a mark/space scheme
shows up as a small set of distinct widths, and the long pulses delimit the
frame.

The `decode` line is the same decoder that `include/IrDecode.h` holds, the one
`tests/test_ir_decode.cpp` exercises. It reports the frame as bytes when the
burst looks like a Midea one, and says so when it does not.

## Recording the original remote

Two tools do the work, both on the computer, not on the board.

### 1. Record the frames

`tools/ir_record.py` watches the IR node's log and asks which state the frame
is supposed to carry. Name the state, press the button on the remote that
produces it, and the frame is written down. Then it asks for the next one.

```sh
tools/ir_record.sh
```

```text
IR node on /dev/cu.usbmodem11201
captures go to captures/remote-20261021-122428.txt
session log goes to captures/remote-20261021-122428-session.log
State for the next capture. Only type what changes, enter keeps everything.
  mode cool  fan auto  temp 24   power on
capture> 22
  state set to cool_auto_22 (cool, fan auto, 22 C)
  waiting... press the button on the remote now
  recorded cool_auto_22: 4D B2 FC 03 06 F9  (48 bits, header 4350/4400 us)  [2 repeat(s)]
State for the next capture. Only type what changes, enter keeps everything.
  mode cool  fan auto  temp 22   power on
capture> fan high
  state set to cool_high_22 (cool, fan high, 22 C)
  waiting... press the button on the remote now
  recorded cool_high_22: 4D B2 FC 03 06 F9  (48 bits, header 4350/4400 us)  [2 repeat(s)]
```

Two files are written: the captures, and the **whole session log**, which
holds every line the IR node sent, with the time it arrived:

```text
13:45:34 radio channel 5 (want 5), remote none, ir interrupts 5786, ir pulses 512, ir glitches 5274, ir level HIGH
13:45:39 radio channel 5 (want 5), remote none, ir interrupts 5786, ir pulses 512, ir glitches 5274, ir level HIGH
```

That is the place to look when a capture does not come: the status line says
how much the receiver saw (`ir interrupts`, `ir pulses`, `ir glitches`), which
tells a broken receiver from a broken script. The port is read by a thread
of its own, so the log is complete even while you are typing.

The remote sends complete packets, so the state is what names a capture. The
MODE button produces three different frames depending on where the remote is,
so a button name would be ambiguous while a state never is.

The state is kept between captures, so only what changes has to be typed:

| Input | Meaning |
| --- | --- |
| `22` | the temperature only, `21.5` for a half degree |
| `fan med` | the fan only, `fan 1` to `fan 7` for the numbered points, `fan max` for the top |
| `mode auto` | the mode only |
| `cool high 25` | all three at once |
| `off`, `on` | the power |
| `led`, `turbo` | the two toggle frames, which are not states |
| `none` | a frame that carries no state |
| `swing`, `timer`, `vane`, `vaneangle`, `soft`, `silent`, `selfclean`, `m` | the buttons of the remote that send a frame of their own |
| `enter` | keep everything as it is |

`auto` on its own means the mode, since the mode is what a button press
cycles most of the time. Write `fan auto` for the fan. A fan point of `max`
is the same as point 7.

Whatever is typed is stored in the canonical form `cool_high_22`, which is
what the audit compares against. `q` quits, `l` lists what has been recorded,
`u` removes the last capture.

### While it waits

- **Cancel a capture:** press `c` and Enter, or Ctrl+C. The capture is given
  up and the script goes back to the prompt. Ctrl+C cancels the capture, not
  the session.
- **The repeats of one press are all collected.** After the first frame the
  script waits until the remote goes quiet for a moment, so every repeat of
  that press is counted, not only the first ones.
- **What is left over from the previous press is dropped.** A repeat that
  arrives late, after the script already moved on, is not taken as the next
  capture. The script says how many frames it dropped, so nothing happens
  without you seeing it. Frames are never dropped because of what they
  contain, so pressing the same button twice still gives two captures.
- **A capture that does not finish is kept.** If the frame was cut short,
  the pulses that did arrive are written down and marked as incomplete, so
  the packet can be looked at.
- **A lost connection is reported.** If the board is reset or the cable
  moves, the script says so and stops, instead of waiting for ever.

The script needs `pyserial`. `tools/ir_record.sh` finds an interpreter that
has it (the system python, or the one PlatformIO installed), so there is
usually nothing to install.

### 2. Compare the frames with the codec

```sh
g++ -std=c++17 -Iinclude tools/ir_audit.cpp -o /tmp/ir_audit
/tmp/ir_audit captures/remote-20261021-122428.txt
```

The report says, for every capture, whether the frame the original remote
sends is the frame this project would send, and what to change in
`include/MideaRG57Codec.h` when it is not:

```text
## Changes to make in include/MideaRG57Codec.h

- TEMP_NIBBLE[5] (22 C) is 0x0E in the codec, the remote sends 0x04 -> change that entry in include/MideaRG57Codec.h
```

The report also compares the **pulse widths** of the remote with the ones
the transmitter sends, because a frame with the right bytes and the wrong
timings is not understood by the AC:

```text
## Pulse timings

| what | the remote | the transmitter |
| --- | --- | --- |
| header mark | 4342 us | 4345 us |
| mark | 440 us | 450 us |
| space of a zero | 649 us | 630 us |
| space of a one | 1728 us | 1725 us |
```

The report is written next to the capture file, with the suffix
`-report.md`.

The codec cannot express everything the remote sends yet. Half degrees and
the seven fan points are reported as what they are, with the bytes of the
frame, so the encoding can be worked out from the captures:

```text
### cool_auto_21.5

- remote bytes: `4D B2 FD 02 06 F9`
- decoded: state, fan byte 0xFD (Auto), inverse 0x02, mode 0x00 (Cool), temperature nibble 0x06 (21 C), checksum ok
- verdict: the codec cannot encode this state yet
- note: the codec has no half degrees
```

Recording a 21.0 °C frame and a 21.5 °C frame and comparing the two shows
which bit carries the half.

A capture that was cut short is reported as such, with the pulses that did
arrive, so a broken frame can be told apart from a frame the remote never
sent:

```text
### cool_auto_22

- remote bytes: `4D B2 FC 03 06 F9`
- verdict: incomplete capture, 6 pulse(s) arrived
```

The analysis lives in `include/IrAudit.h`, which uses the project's own
`IrDecode.h` and `MideaRG57Codec.h`, so nothing about the protocol is
duplicated. `tests/test_ir_audit.cpp` covers it, and `tests/test_ir_record.py`
covers the log parsing.

```sh
./tests/run.sh
```

## Display controls

The CYD runs an LVGL 9.4 UI with three screens, laid out in EEZ Studio:

- **Climate** — a 17–30 °C temperature arc with −/+ buttons, a power button, mode (Auto/Cool/Dry/Fan/Heat), fan (Auto/Low/Med/High) and swing cards, plus a link indicator in the header.
- **Extras** — Eco, Turbo, Display LED and Sleep tiles.
- **Network** — ESP-NOW link state, MQTT/Home Assistant state, and Pair / Retry buttons.

The three screens share a header and a bottom navigation bar. All of them are
real LVGL widgets, so they scroll, animate and react to touch natively.

Modes cycle Auto → Cool → Dry → Fan → Heat. Fan cycles Auto → Low → Medium →
High, except Auto and Dry force automatic fan. You can change preset controls
while off; pressing POWER ON applies the selected setpoints. The LED and Turbo
actions are **toggles**, not known on/off values, so the tile shows the last
locally requested state and may drift from the AC.

**Protocol limits.** The ESP-NOW wire format carries power, mode (Cool / Auto /
Heat / Dry / Fan), fan and temperature. The swing, eco and sleep tiles are
therefore **rejected by the firmware** with a note on the Network screen; the
tile still flips locally. The LED and Turbo tiles map onto the two dedicated
toggle actions. Extending the modes means changing `include/AcProtocol.h` and
the IR node codec in tandem.

The status `IR command sent` means the IR node's hardware transmission
completed, **not** that the AC confirmed reception.

## Splash screen

The CYD shows a splash screen with the `arkusnexus` logo:

- once the touch calibration is done, when the app starts;
- again whenever the screen has been left alone for 30 seconds;
- a **tap anywhere dismisses it** and brings the normal screens back.

The splash is an overlay on LVGL's top layer rather than a screen of its own,
so it covers whichever of the three screens is active and a tap lands on the
splash instead of on a button underneath it.

The artwork lives in `ac-control-cyberpunk/splash.png` and is turned into an
RGB565 bitmap by `tools/png_to_lvgl.py`, which scales it to the 320 × 240
panel and crops it to the logo — the rest of the panel is the black
background of the overlay. The bitmap ends up in flash, so the splash costs
no RAM at runtime:

```sh
python3 tools/png_to_lvgl.py -i ac-control-cyberpunk/splash.png \
    -o src/splash/splash_img.c --header src/splash/splash_img.h \
    --symbol splash_img
```

Preview it **without hardware**, on top of the generated UI:

```sh
./tools/render-splash.sh /tmp/splash.png
```

The idle timeout is `SPLASH_IDLE_MS` in `src/cyd_remote.cpp`.

## Working on the UI (EEZ Studio)

`ac-control-cyberpunk/AC_Control_Cyberpunk.eez-project` is the source of truth
for the screens. Open it in EEZ Studio, edit, and build — the generated C goes
straight to `src/ui/`, which PlatformIO compiles as part of `cyd_remote`.

EEZ Studio needs **LVGL 9.4.0** and a 320 × 240 landscape display to match this
firmware.

Two rules when editing the project:

- **Widget identifiers must be unique across the whole project**, not just
  within a page. EEZ indexes widgets into one global table, so a repeated
  identifier silently points generated code at the wrong widget. The shared
  header and navigation bar are therefore named per page
  (`brand_name_main`, `brand_name_extras`, `brand_name_network`).
- Do not rename the `actions.c` / `ac_ui_bridge.h` build files: they carry the
  bridge between the UI and the ESP-NOW firmware.

Regenerate without opening the GUI:

```sh
./ac-control-cyberpunk/build-eez.sh /path/to/eez-studio
```

Preview the result **without hardware** — it compiles LVGL and the generated
`src/ui` for the host, renders them and writes a PNG of what the panel would
show:

```sh
./ac-control-cyberpunk/render-preview.sh ac-control-cyberpunk/preview.png
```

See [ac-control-cyberpunk/INTEGRATION.md](ac-control-cyberpunk/INTEGRATION.md)
for the bridge API and the mode/fan translation.

## Supported Midea protocol and limitations

The custom codec currently implements:

- ON state packet: `4D B2 <fan> <~fan> <mode|temperature> <checksum>`, LSB first.
- Captured **OFF** packet, regardless of displayed setpoint: `4D B2 DE 21 07 F8`.
- Known-good captured **ON Cool/High/21 °C** example: `4D B2 FC 03 06 F9`.
- Captured **ON Dry/Auto/17 °C** example: `4D B2 F8 07 20 DF`.
- Captured **ON Fan/Auto/17 °C** example: `4D B2 FD 02 27 D8`.
- Mode bits of byte 4: Cool `0x00`, Auto `0x10`, Dry `0x20`, Heat `0x30`.
  Fan-only mode shares the `0x20` bits and carries the "do not care" nibble
  `0x07` instead of a setpoint, which is why fan-only mode has no temperature.
- Nonlinear temperature LUT for 17–30 °C, special Auto fan value `F8`, checksum `(0xFD - sum(first 5 bytes)) & 0xFF`.
- LED Toggle `AD 52 AF 50 A5 <checksum>` and Turbo Toggle `AD 52 AF 50 45 <checksum>`.
- 38 kHz carrier; 4345/4490 µs header; 450 µs marks; 630/1725 µs zero/one spaces; frame repeated twice.

The pulse widths above are **measured from the original remote**, not assumed.
They live in `include/MideaRG57Codec.h` as `HEADER_MARK_US`, `HEADER_SPACE_US`,
`MARK_US`, `SPACE_ZERO_US`, `SPACE_ONE_US` and `FRAME_GAP_US`, so the
transmitter and the audit tool use the same numbers. The remote's receiver
module idles high, so the marks are the **low** levels.

What the recordings of the original remote confirmed:

- The frame format is right: 48 bits, LSB first, `4D B2 ...`, the same
  checksum, and the frame is sent twice.
- The temperature table is right for 17, 21 and 22 °C, and the OFF packet is
  byte for byte the one the codec sends.
- The half degrees are **not** in the frame: the frames for 21 °C and 21.5 °C
  are byte for byte the same.
- The fan levels of the remote are **not** known yet. The recordings show
  only the three values the codec knows (`F9`, `FA`, `FC`), because the
  labels ran ahead of the remote. The seven points of the remote need a
  recording where the fan button is pressed once per capture, waiting for
  the display to settle.
- Dry and fan-only mode are encoded from the recording of
  `captures/remote-20261021-145952.txt`. Dry carries the setpoint and uses
  the auto override fan byte `F8`; fan-only mode carries no setpoint and
  keeps the fan byte of the chosen speed. Only 17 °C was recorded for both,
  so a capture of each at another temperature would confirm the setpoint
  encoding of dry and the fixed nibble of fan-only mode.

**Swing, Follow Me, timers, and other undocumented functions are intentionally
not claimed as working**. The fixed OFF packet is based on a prior captured
frame, not an inferred formula.

## Link details

- **A disconnected Wi-Fi station keeps scanning other channels.** An ESP-NOW
  frame is over in ~200 µs, so it only gets through when the receiver's radio
  happens to be on the right channel. `WiFi.channel()` reports the *configured*
  channel, not where the radio actually is.
- Two things keep the radio where it needs to be: each side **re-asserts the
  channel every 200 ms**, and a Hello is **answered straight from the receive
  callback**, so the reply goes out within microseconds of the request, while
  the radio is provably on the right channel.
- The CYD advertises a Hello every second, both broadcast and — once the IR
  node is known — unicast to it. The IR node answers every Hello.
- The link is considered up while a frame from the IR node arrived within the
  last 20 s.
- **Both sides must be on the same channel.** Without MQTT both use
  `ESPNOW_CHANNEL`. With MQTT the CYD follows the router's channel, so
  `ESPNOW_CHANNEL` must be set to the router's channel for the IR node, and
  the CYD must not re-assert the channel itself.
- The IR node prints its **actual** radio channel every 5 s, next to the
  wanted one — if the two differ, the radio is still scanning.
- Each side locks onto the first peer it hears and ignores others until it
  restarts, so only one controller / IR-node pair is supported.
- Commands carry sequence numbers. Receiver remembers the latest completed sequence and repeats its ACK without repeating the IR signal if the same command is retried.
- Remote waits for node-side ACK; it retries up to 4 attempts with 550 ms between attempts. Dropped commands are reported on the display.
- The link is **not encrypted or authenticated** in this prototype. Auto-pairing is convenient but does not provide protection from nearby attackers or other custom ESP-NOW senders. Do not treat MAC addresses as security credentials. Add explicit PSK/LMK-based encryption and a provisioning flow before deploying in an untrusted radio environment.
- This is **one-way IR**: acknowledgement is from the IR microcontroller, not the air conditioner. If someone uses the manufacturer's remote, the controller's displayed state may drift from the AC's actual settings.

## Optional Home Assistant integration

See [docs/HOME_ASSISTANT.md](docs/HOME_ASSISTANT.md) for enabling the CYD Wi-Fi/MQTT bridge and configuring **matching ESP-NOW Wi-Fi channels**. In MQTT mode the CYD connects to your access point while continuing to send ESP-NOW to the C3; the C3 does **not** need an IP address or access-point connection.

## Troubleshooting

- **Wrong board is flashed, or the port is not found:** Run `tools/pio.sh ports`. The board is picked by its USB VID:PID, so the same command works on every machine. If a board shows up without an id (`????:????`), it is not a USB serial device and cannot be selected this way; pass `--port` instead.
- **CYD black screen:** Verify this really is an ILI9341 240×320 CYD. On typical ESP32-2432S028, backlight is GPIO21. Different CYD revisions have different pins/drivers.
- **Screen works, touch doesn't:** Confirm XPT2046 model. TFT and touch use different SPI buses. To redo calibration, send `c` over serial on the CYD.
- **Both running but always OFFLINE:** Check `ESPNOW_CHANNEL`, both station MAC prints, power and distance. For MQTT mode, the C3 must use the **router's 2.4 GHz channel**; it won't auto-follow channel changes.
- **LINKED but AC does nothing:** Check polarity/wiring/transistor/base resistors, IR LED direction and distance. Look at IR node serial hex logs. Test captured known-good Cool/21/High after enabling power on. ESP-NOW success alone does not prove an IR signal arrived.
- **IR node fails to boot:** GPIO2 is a strapping pin. Disconnect the secondary LED/transistor network or set `USE_SECOND_IR_LED = false` (and use only GPIO1) until the boot circuit is fixed.
- **Flashing ESP32-C3 USB problems:** Hold BOOT, briefly tap RESET, then release BOOT and retry the correct serial port.
- **Compilation error about RMT driver:** Use the pinned `platform = espressif32@6.12.0` and its Arduino/IDF version; the current project intentionally targets the IDF 4.x legacy RMT API, *not* IDF 5's new `rmt_tx` API.
- **`lv_conf.h` seems ignored:** PlatformIO's own `-Iinclude` only reaches the project sources, not the LVGL library sources, and LVGL's `__has_include` lookup then silently skips the include. `platformio.ini` therefore passes `-I$PROJECT_DIR/include` explicitly. If you add another environment that uses LVGL, do the same.
- **Duplicate identifier error in EEZ Studio:** Widget identifiers must be unique across the entire project, not only within a page.
- **CYD shows a white background with black text:** the ILI9341 on some CYD
  revisions has inverted colour polarity. `platformio.ini` passes
  `-DTFT_INVERSION_ON` for that reason; remove it if your panel shows the
  colours correctly the other way round.
- **Buttons need two taps, or the previous button stays lit:** the touch driver
  must report a release. The XPT2046 library reports pressure, and its
  interrupt pin is not pulled up on every CYD revision, so the firmware never
  derives the pressed state from that pin — only from the pressure reading.
- **CYD shows a light theme:** the EEZ project's *Project settings → General → Dark theme* checkbox controls it; the generated `lv_theme_default_init()` takes a `dark` argument. The firmware also forces the theme dark itself.
- **ESP-NOW never links:** both nodes print their MAC and channel at boot. If the channels differ, set the same `ESPNOW_CHANNEL` in `include/AcConfig.h` on both. A disconnected Wi-Fi station keeps scanning and drifts off the data channel, so both nodes re-assert the channel before every advertisement.

## References

- [ESP32-CYD pinout / dual SPI](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/)
- [RG57A7/BGEF protocol notes (community reverse engineering)](https://github.com/smruchira/Midea-AC-IR-Protocol-Decode)
- [Espressif ESP32-C3 strapping pins](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/schematic-checklist.html)
- [TFT_eSPI PlatformIO configuration](https://github.com/Bodmer/TFT_eSPI/wiki/Installing-on-PlatformIO)
