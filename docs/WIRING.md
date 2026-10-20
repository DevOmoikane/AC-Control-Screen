# Wiring and hardware

## IR transmitter (ESP32-C3 Super Mini)

**Do not drive high-current IR LEDs directly from ESP32 pins.** Use a transistor driver for **each** IR LED. An easy low-side driver for each emitter (for example using 2N2222 or equivalent) is:

```text
                  ESP32-C3 3.3 V
                        |
                 100 ohm resistor    (approx. 19 mA LED pulse;
                        |              adjust to LED/transistor specs)
                    IR LED anode (+)
                    IR LED cathode (-)
                        |
                 NPN collector
GPIO1 --- 1kΩ --- NPN base
                 NPN emitter
                        |
                     GND ------------- ESP32-C3 GND
```

For a second emitter, replicate the entire circuit but connect its 1 kΩ base resistor to **GPIO2**. Both LED paths can share GND and an adequately rated 3.3 V supply; use **separate current-limiting resistors**. Position both LEDs to illuminate the AC's IR sensor. The RMT channels output 38 kHz modulated marks with active-high transistor inputs.

### Important GPIO2 boot caveat

On ESP32-C3, **GPIO2 is a boot strapping pin**. If the secondary transistor circuit forces GPIO2 low during reset, flashing or normal startup can fail. Test with one emitter on GPIO1 first by setting `USE_SECOND_IR_LED = false` in `include/AcConfig.h`. If adding GPIO2, avoid a strong pull-down at boot; test reset/boot repeatedly. You could also redesign the second output on a different safe pin, updating `IR_LED_PIN_2`.

A 100 Ω resistor is only a conservative example for a roughly 1.2 V infrared LED and a saturated NPN at a 3.3 V rail. Select resistors based on your LED current rating, duty cycle and transistor characteristics. Do not connect 5 V to an ESP32 GPIO. With an externally powered LED path, connect its supply GND and ESP32 GND together.

Your previous ESPHome design also used GPIO3 as the **IR receiver** input. This prototype deliberately leaves it free; a receiver can later be added to synchronize with physical remotes or verify remote captures.

## Touch controller board (ESP32-2432S028 / CYD)

The common ESP32-2432S028(R) already connects these on its PCB; **no external LCD wiring** is required.

| Part | Signal | ESP32 GPIO |
|---|---|---:|
| ILI9341 TFT | MOSI | 13 |
| ILI9341 TFT | MISO | 12 |
| ILI9341 TFT | Clock | 14 |
| ILI9341 TFT | CS | 15 |
| ILI9341 TFT | DC | 2 |
| ILI9341 TFT | RESET | board reset / -1 |
| TFT backlight | enable | 21 (HIGH) |
| XPT2046 touch | MOSI | 32 |
| XPT2046 touch | MISO | 39 |
| XPT2046 touch | Clock | 25 |
| XPT2046 touch | CS | 33 |
| XPT2046 touch | IRQ | 36 |

TFT_eSPI uses its **HSPI** port while XPT2046_Touchscreen gets a separate **VSPI** object. Do **not** set `TOUCH_CS` as a TFT_eSPI build flag in this project. Typical CYD size is **240×320 pixels** (320×240 landscape), not 240×230.

## Radio

Both chips use their internal 2.4 GHz Wi-Fi radios for ESP-NOW. No wiring is required between devices. No Ethernet, separate ESP-NOW module, home Wi-Fi router, or MCU-to-MCU signal wires are required for the baseline.
