# Integration into the ESP-NOW Midea AC controller

**Status: implemented.** The CYD firmware (`src/cyd_remote.cpp`) drives the UI
generated from this project, and the three bridge hooks are wired to the
ESP-NOW/IR stack. This file documents how the two halves fit together.

## Where things live

| Piece | Location |
| --- | --- |
| EEZ project (source of truth) | `ac-control-cyberpunk/AC_Control_Cyberpunk.eez-project` |
| Generated C | `src/ui/` (EEZ build destination is `../src/ui`) |
| LVGL configuration | `include/lv_conf.h` |
| Firmware | `src/cyd_remote.cpp` |
| Bridge header | `ac-control-cyberpunk/ac_ui_bridge.h` → `src/ui/ac_ui_bridge.h` |

Regenerate the C from the project with EEZ Studio (Build), or headlessly:

```sh
./ac-control-cyberpunk/build-eez.sh /path/to/eez-studio
```

## The runtime shape

- `setupLvgl()` creates a 320 × 240 LVGL display on top of TFT_eSPI, registers
  the XPT2046 as a pointer input device, and calls the generated `ui_init()`.
- `loop()` calls `ui_tick()` (refreshes the widgets from the bridge variables)
  and then `lv_timer_handler()` (renders and polls the touch).
- The display flushes through `dispFlush()` in `cyd_remote.cpp`. LVGL stores
  RGB565 in host byte order, so the buffer is pushed with TFT_eSPI's byte swap
  enabled.
- The tick source is `millis()`, via `lv_tick_set_cb()`.

## The bridge API

`ac_ui_bridge.h` declares the UI-side API. `ac_ui_bridge.h` and `actions.c`
are EEZ **build files**, so they are copied into `src/ui/` on every build.
Edit them in EEZ Studio, not in `src/ui/`.

The firmware provides **strong** definitions for the three hooks (the
generated `actions.c` only has weak no-op defaults):

```cpp
extern "C" void ac_ui_send_state(const ac_ui_state_t *s);
extern "C" void ac_ui_start_pairing(void);
extern "C" void ac_ui_request_status(void);
```

| Hook | What the firmware does |
| --- | --- |
| `ac_ui_send_state` | Translates the UI state into `ac::State` and queues an ESP-NOW command. |
| `ac_ui_start_pairing` | Forgets the discovered IR node, clears the queue and re-broadcasts a Hello. |
| `ac_ui_request_status` | Re-broadcasts a Hello and refreshes the link indicator. |

The other functions in `ac_ui_bridge.h` are implemented in the generated
`actions.c` and are called *by* the firmware on the LVGL thread:

```cpp
ac_ui_set_link_status(1, "IR transmitter responding");
ac_ui_set_mqtt_status(1);
ac_ui_apply_remote_state(&remote);   // e.g. after an MQTT/HA change
```

`ac_ui_state_t` fields: `temperature_c` 17..30; `power` 0/1; `mode`
0=AUTO, 1=COOL, 2=DRY, 3=FAN, 4=HEAT; `fan` 0=AUTO, 1=LOW, 2=MED, 3=HIGH;
`swing`, `eco`, `turbo`, `display_led`, `sleep` toggles 0/1.

## Translation and the protocol limits

`include/AcProtocol.h` supports **Cool=0, Auto=1, Heat=2, Dry=3, Fan=4** and
the wire `ac::State` only includes **power, mode, fan, tempC**.

- **Mode** is translated explicitly; the EEZ numbering is never cast to
  `ac::Mode`. `DRY` and `FAN` are supported, so all five modes of the mode
  card reach the AC.
- **Fan** happens to line up (Auto/Low/Med/High = 0..3) and is copied
  directly. Auto and Dry modes force the automatic fan, because the AC picks
  the speed itself in both of them.
- **Swing, eco and sleep** are not in the wire format and are rejected.
- **Turbo and the display LED** map onto the two dedicated toggle actions
  (`ac::Action::TurboToggle`, `ac::Action::LedToggle`). They are *toggles*, so
  the tiles show the last locally requested state and can drift from the AC.

Adding a mode means changing `include/AcProtocol.h` **and** the IR node codec
in tandem, not just the UI. Swing/eco/sleep still need the wire format to
grow first.

## Threading

- ESP-NOW frames arrive in the Wi-Fi callback task and are pushed into a
  FreeRTOS queue. The queue is drained in `loop()`, so all LVGL calls happen
  on the LVGL thread.
- The link indicator is refreshed from `loop()`, never from the callback.
- The arc's `VALUE_CHANGED` event updates the local temperature preview; the
  `RELEASED` event transmits **one** command. The +/- buttons transmit
  immediately.
- `ac_ui_send_state` is called from the UI's own event handlers, so it must
  not block. It only queues a frame.
