# ISRA / AC controller — EEZ Studio + LVGL 9.4

This folder contains a **modified version of the uploaded `AC_Control.eez-project`**, preserving its LVGL 9.4.0 target and temperature-arc concept. The result is a 320 × 240 landscape interface for the common ESP32-2432S028/CYD touch display.

**The UI is generated from this project and is already integrated into the
CYD firmware** — see [INTEGRATION.md](INTEGRATION.md).

## Files

- `AC_Control_Cyberpunk.eez-project` — **open this file with EEZ Studio** and edit/build the GUI.
- `build-eez.sh` — regenerates `src/ui/` headlessly, without opening the GUI.
- `all-screens-preview.png` — concept preview of the three screens (drawn separately as an SVG mock-up; **not** a screenshot of EEZ Studio or LVGL running on hardware).
- `preview-*.png`, `preview-*.svg` — individual visual references.
- `actions.c`, `ac_ui_bridge.h` — the native C implementation of the UI state, embedded as project build file templates. EEZ Studio exports them into `src/ui` alongside the generated screens; edit them here, not in `src/ui`.
- `INTEGRATION.md` — how the UI is linked to the ESP-NOW/IR firmware.

## Screens

**Main / Climate:** 17–30 °C interactive temperature arc (original temperature variable retained), 1-degree plus/minus buttons, power toggle, mode (Auto/Cool/Dry/Fan/Heat), fan (Auto/Low/Med/High), swing, live link text, bottom navigation.

**Extras:** Eco, Turbo, Display LED, Sleep toggle tiles. Swing, Eco and Sleep need **additional IR protocol support** and are rejected by the firmware; Turbo and the display LED use the existing toggle actions.

**Network:** live link and MQTT status, Pair and Retry hooks, optional Home Assistant connectivity indication.

## EEZ Studio instructions

1. Open `AC_Control_Cyberpunk.eez-project` in a recent EEZ Studio version supporting LVGL **9.4.0**.
2. Project settings → **Display** must be **320 × 240** (landscape) to match the firmware and `include/lv_conf.h`.
3. Select **Main**, **Extras**, or **Network** to edit each page. Every element is a regular editable LVGL Arc, Label, Button, or Panel (no flattened screenshot in the project).
4. Build/generate C files. The destination is `../src/ui`, which PlatformIO compiles into the `cyd_remote` environment.
5. `pio run -e cyd_remote` and flash.

### Widget identifiers must be unique project-wide

EEZ Studio indexes widgets into a single global table, so an identifier that
appears on two pages is an error (*"Duplicate identifier"*) and would silently
point the generated code at the wrong widget. The shared header and bottom
navigation are therefore named per page:

```text
brand_name_main      brand_name_extras      brand_name_network
power_button_main    power_button_extras    power_button_network
tab_climate_main     tab_climate_extras     tab_climate_network
```

If you duplicate a widget, rename the copy.

### Theme: dark

The project has **Project settings → General → Dark theme** enabled. EEZ turns
that into `lv_theme_default_init(..., dark = true, ...)` in the generated
`src/ui/screens.c`, so the whole UI uses LVGL's dark palette. The per-widget
colours in this project are cyberpunk and are applied on top of it.

If you ever need the light theme, clear that checkbox and rebuild — but the
hand-tuned background colours in this project assume a dark palette.

### Button press feedback

The widgets in this project only style `LV_STATE_DEFAULT`. A local `DEFAULT`
style outranks the theme's `PRESSED` style, so the buttons would show no
press feedback at all. The firmware therefore walks the object tree after
`ui_init()` and gives every `lv_button` an explicit pressed colour
(`src/cyd_remote.cpp`, `styleButtons()`). If you restyle the buttons in EEZ
Studio, update that colour to match.

### LVGL configuration

`include/lv_conf.h` enables the Montserrat **12, 14, 16 and 36** fonts the
project references, and disables the widgets the UI does not use. If you add
a widget that needs another font, enable it there too.

**Architectural choice:** This edit sets `flowSupport: false` and uses native C actions/variables to reduce the need for EEZ Flow firmware runtime. The source GUI remains editable in EEZ Studio. EEZ Flow is not required for the GUI.

## What's complete / what remains

- **Complete:** fully laid-out editable screens; buttons and arc have EEZ action handlers; native C state/getters/setters and firmware hook API included; the generated UI is compiled and linked into the CYD firmware.
- **Not done:** flash-test validation on the physical boards, and IR protocol support for swing, eco and sleep (and for dry/fan-only modes).
- **Important:** the `ac_ui_send_state`, `ac_ui_start_pairing` and `ac_ui_request_status` hooks have **weak no-op defaults** in `actions.c`; the strong implementations live in `src/cyd_remote.cpp`.

