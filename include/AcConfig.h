#pragma once

// If MQTT is disabled, BOTH ESP32s use this fixed 2.4 GHz channel.
// If MQTT is enabled, the CYD instead uses the Wi-Fi router's channel, so
// this value has to be the router's channel for the two to see each other.
// (The CYD logged "Wi-Fi channel 5" with this router, hence 5.)
constexpr unsigned char ESPNOW_CHANNEL = 5;

// Opt-in MQTT + Wi-Fi on the CYD. Leave false for ESP-NOW-only operation.
constexpr bool ENABLE_MQTT = true;
constexpr char WIFI_SSID[] = "omoikane";
constexpr char WIFI_PASSWORD[] = "D3v0m01k4n3";
constexpr char MQTT_HOST[] = "10.73.19.192";
constexpr unsigned short MQTT_PORT = 1883;
constexpr char MQTT_USERNAME[] = "mqttclient";
constexpr char MQTT_PASSWORD[] = "k4m1z4m4";
constexpr char MQTT_TOPIC_BASE[] = "home/ac/espnow";

// IR node uses two independent ESP32-C3 RMT TX channels.
// GPIO2 is a BOOT STRAPPING pin; transistor input must not force it low
// during reset. Set false for a single LED on GPIO1.
constexpr bool USE_SECOND_IR_LED = false; // safe 1-LED default
constexpr unsigned char IR_LED_PIN_1 = 1;
constexpr unsigned char IR_LED_PIN_2 = 2;
