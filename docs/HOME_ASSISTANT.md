# Home Assistant + MQTT

In the default configuration, **neither board connects to your Wi-Fi access point**. They exchange commands directly via ESP-NOW on the configured 2.4 GHz channel. The CYD can optionally be upgraded to a Wi-Fi/MQTT gateway using code already included in `src/cyd_remote.cpp`.

## Enable the MQTT bridge

1. Install/configure a **working MQTT broker**, for example Mosquitto accessible by your Home Assistant system, and add Home Assistant's MQTT integration.
2. Open `include/AcConfig.h` and change `ENABLE_MQTT = true`.
3. Set `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_HOST`, `MQTT_PORT`, and if required `MQTT_USERNAME` and `MQTT_PASSWORD`. Use the broker's **IP or DNS name as accessible on the local network**. Keep this file private if it has credentials.
4. Identify your access point's **2.4 GHz Wi-Fi channel** (1–11 in many regions; some support 12–13). The CYD's ESP-NOW interface will be constrained to the **channel chosen by the Wi-Fi connection**. Set `ESPNOW_CHANNEL` in `AcConfig.h` to that channel **for the ESP32-C3** and reflash the IR node. Do **not** call `esp_wifi_set_channel` on the CYD while Wi-Fi is connected.
5. Flash CYD using `pio run -e cyd_remote -t upload`, and IR node using `pio run -e ir_node -t upload`. Check the CYD serial log for its actual connected channel and `MQTT connected and HA discovery published`.
6. A discovered entity named **Midea AC** should appear through Home Assistant's MQTT integration, assuming MQTT discovery is enabled.

**Important:** if the access point auto-selects a different Wi-Fi channel, ESP-NOW will disconnect until the C3's hardcoded channel is updated. A stable AP 2.4 GHz channel is recommended for this proof of concept. The C3 deliberately stays off the Wi-Fi network; only the CYD acts as MQTT gateway.

## Topics

Using the default `MQTT_TOPIC_BASE` (`home/ac/espnow`):

| Direction | Topic | Payloads |
|---|---|---|
| Home Assistant → CYD | `home/ac/espnow/set/mode` | `off`, `auto`, `cool`, `heat` |
| Home Assistant → CYD | `home/ac/espnow/set/temperature` | `17` to `30` |
| Home Assistant → CYD | `home/ac/espnow/set/fan` | `auto`, `low`, `medium`, `high` |
| Home Assistant → CYD | `home/ac/espnow/set/action` | `led`, `turbo` (optional command topic; *not* exposed by the climate entity) |
| CYD → Home Assistant | `home/ac/espnow/state/mode` | Current last-sent mode (or `off`) |
| CYD → Home Assistant | `home/ac/espnow/state/temperature` | Last-sent desired temperature |
| CYD → Home Assistant | `home/ac/espnow/state/fan` | Last-sent fan setting |
| CYD → Home Assistant | `home/ac/espnow/availability` | `online` or `offline` based on IR-node heartbeat |
| CYD → Home Assistant | `homeassistant/climate/midea_espnow/config` | Retained climate discovery JSON |

Climate states are reported after the **IR node acknowledges a transmission**, not after the air conditioner physically confirms it. IR control is open-loop; using the original remote can cause state drift. In the future, you could add the previous GPIO3 IR receiver, room-temperature sensor, and a gateway that tracks state more accurately.

## Future enhancements

The shared `include/AcProtocol.h` provides a versioned protocol and message types, making it easier to later add telemetry (room temperature/humidity/energy readings), time-sync, sensors, and new IR commands without replacing the touchscreen design.

For deployment beyond a trusted home environment, add real ESP-NOW encryption and provisioning (per-peer LMK keys) rather than relying on open discovery and MAC filtering. MQTT credentials here are ordinary plaintext configuration constants and MQTT packets are not protected by TLS.
