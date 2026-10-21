# Midea ESP-NOW AC Controller (ESP32-C3 + ESP32-2432S028)

**Status:** functional prototype source with host-side protocol tests. **Not yet compiled, flashed, or tested on the two actual ESP32 boards.** Hardware and AC-model verification are required.

This is a **single PlatformIO project with two build environments**:

- **`ir_node`** — ESP32-C3 Super Mini, 38 kHz infrared transmission using hardware RMT; GPIO1 and (optionally) GPIO2 IR emitters.
- **`cyd_remote`** — classic ESP32-2432S028 (CYD), 240×320 ILI9341 display in 320×240 landscape and XPT2046 resistive touch. ESP-NOW transmitter and optional Wi-Fi/MQTT Home Assistant bridge. The touch UI is **LVGL 9.4**, laid out in [EEZ Studio](https://eez.studio/) (`ac-control-cyberpunk/`).

The intended AC remote is **Midea RG57A7/BGEF**, matching an earlier project using 48-bit frames beginning `4D B2`. The dedicated codec does *not* assume that the generic `IRMideaAC` representation is interchangeable with the captures.

## Features

- Automatically discovers the peer via ESP-NOW (no MAC editing needed).
- No router, cloud, server, or Internet required by default.
- Change power, 17–30 °C, Cool/Auto/Heat, Auto/Low/Medium/High fan; LED and Turbo toggle.
- Touch UI built with **EEZ Studio + LVGL 9.4**: three screens (climate arc, extras, network), regeneratable from the `.eez-project` without hand-written drawing code.
- Infrared transmitter uses the ESP32-C3's two RMT TX channels to drive two different output GPIOs with matching carrier timing.
- User input queues commands; adjacent state changes coalesce; application-level ACK, timeout and retry with duplicate sequence suppression.
- One-time three-point resistive-touch calibration stored in NVS.
- Remembers the last successfully transmitted logical state (not the measured physical AC state).
- Optional Home Assistant MQTT climate discovery and MQTT commands on the CYD, with no Wi-Fi credentials required in default ESP-NOW-only mode.

## Requirements

- PlatformIO CLI or PlatformIO IDE extension for VS Code.
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
│   └── lv_conf.h              # LVGL 9.4 configuration
├── src/
│   ├── ir_node.cpp            # ESP32-C3 / RMT / ESPNOW receiver
│   ├── cyd_remote.cpp         # LVGL UI + touch + ESPNOW + MQTT bridge
│   └── ui/                    # EEZ Studio generated UI (do not edit by hand)
├── ac-control-cyberpunk/      # EEZ Studio project (source of truth for the UI)
│   ├── AC_Control_Cyberpunk.eez-project
│   ├── build-eez.sh           # Headless regeneration of src/ui
│   └── INTEGRATION.md
├── tests/
│   ├── test_codec.cpp         # Host-side codec tests
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
   pio run -e ir_node
   pio run -e ir_node -t upload
   pio device monitor -b 115200
   ```

5. Connect **ESP32-2432S028** instead. Build and upload:

   ```sh
   pio run -e cyd_remote
   pio run -e cyd_remote -t upload
   pio device monitor -b 115200
   ```

   If PlatformIO cannot guess which USB serial device to use, supply `--upload-port /dev/ttyACM0` or the correct local serial device to the upload command (e.g., on Linux `pio run -e ir_node -t upload --upload-port /dev/ttyACM0`).

6. On the CYD's first boot, touch the three displayed crosshairs **in order**. Calibration is saved. To repeat later, open the CYD serial monitor and send the character `c`.
7. Power both boards. The link indicator in the top-right of the header should change from `OFFLINE` to `LINKED` after the ESP-NOW Hello frames arrive. The IR node logs its MAC and each actual 6-byte IR packet.
8. Point the emitters at the AC. Switch the AC on and vary the temperature. Verify operation on your particular AC before relying on the optional LED/Turbo tiles.

### Which serial port carries what

| Board | Port | Carries |
| --- | --- | --- |
| ESP32-C3 (IR node) | `/dev/ttyACM0` (native USB) | the IR node's log — `Serial` **is** the USB CDC on this board |
| ESP32-2432S028 (CYD) | `/dev/ttyUSB0` (USB-UART bridge) | the CYD's log — `Serial` is UART0 |

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
radio channel 5 (want 5), remote paired, last frame from remote 1ms ago, ir interrupts 0, ir pulses 0, ir level LOW
```

- `ir interrupts 0` — the pin never changes: the module is not driving it.
  Check VCC, GND, and that OUT really reaches GPIO4. To separate wiring from
  firmware, disconnect the module and briefly bridge GPIO4 to GND with a
  jumper: the counter must jump.
- `ir interrupts` growing but no burst printed — the capture works but the
  burst analysis is not seeing it.
- `ir pulses` growing — pulses are arriving and are long enough to be real;
  the raw trace will follow.
- `ir level` — the level the pin is at right now.

The module is demodulated by the receiver, so the firmware only has to measure
pulse widths. Every edge is captured in a GPIO interrupt, so the timings do not
depend on polling. **No protocol is assumed** — the node reports the raw trace
plus a histogram of the pulse widths, which is what identifies an unknown
encoding:

```text
[ir rx] burst of 98 edges
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

The Midea-specific decode lives in `include/IrDecode.h` and is exercised by
`tests/test_ir_decode.cpp`, but the firmware does not use it — the receiver
currently sees a different remote.

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
High, except Auto mode forces automatic fan. You can change preset controls
while off; pressing POWER ON applies the selected setpoints. The LED and Turbo
actions are **toggles**, not known on/off values, so the tile shows the last
locally requested state and may drift from the AC.

**Protocol limits.** The ESP-NOW wire format only carries power, mode (Cool /
Auto / Heat), fan and temperature. Dry and Fan-only modes, and the swing, eco
and sleep tiles, are therefore **rejected by the firmware** with a note on the
Network screen; the tile still flips locally. The LED and Turbo tiles map onto
the two dedicated toggle actions. Extending this means changing
`include/AcProtocol.h` and the IR node codec in tandem.

The status `IR command sent` means the IR node's hardware transmission
completed, **not** that the AC confirmed reception.

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
- Nonlinear temperature LUT for 17–30 °C, special Auto fan value `F8`, checksum `(0xFD - sum(first 5 bytes)) & 0xFF`.
- LED Toggle `AD 52 AF 50 A5 <checksum>` and Turbo Toggle `AD 52 AF 50 45 <checksum>`.
- 38 kHz carrier; 4350/4400 µs header; 560 µs marks; 560/1690 µs zero/one spaces; frame repeated twice.

This protocol was reverse-engineered for the RG57A7/BGEF family. Your exact unit may have different behavior, particularly Heat/Auto/LED/Turbo. **Dry, fan-only, swing, Follow Me, timers, and other undocumented functions are intentionally not claimed as working**. The fixed OFF packet is based on a prior captured frame, not an inferred formula. The IR node includes no IR receiver in this version; your previously used RX GPIO3 can be reintroduced later.

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
