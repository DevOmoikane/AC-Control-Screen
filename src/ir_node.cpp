#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <driver/rmt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>

#include "AcConfig.h"
#include "AcProtocol.h"
#include "MideaRG57Codec.h"
#include "IrReceiver.h"

namespace {
constexpr uint8_t BROADCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
constexpr size_t IR_ITEMS = 50; // header + 48 bits + stop mark/gap
constexpr uint32_t HELLO_INTERVAL_MS = 2500;

struct Incoming {
  uint8_t mac[6];
  ac::Frame frame;
};
QueueHandle_t incomingQueue = nullptr;
uint8_t remoteMac[6] = {};
bool haveRemote = false;
uint32_t previousSeq = 0;
bool havePreviousSeq = false;
ac::Result previousResult = ac::Result::InvalidCommand;
uint32_t lastHello = 0;
uint32_t lastChannelFix = 0;
uint32_t lastRemoteFrame = 0;
bool irReady = false;

bool addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0; // current Wi-Fi channel
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false; // see security note in README
  return esp_now_add_peer(&peer) == ESP_OK;
}

void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != static_cast<int>(sizeof(ac::Frame)) || !incomingQueue) return;
  Incoming incoming{};
  memcpy(incoming.mac, mac, 6);
  memcpy(&incoming.frame, data, sizeof(incoming.frame));
  if (!ac::validFrame(incoming.frame)) return;

  // Answer a Hello right here, in the Wi-Fi task. The reply then goes out on
  // the very channel the request just arrived on. Answering later, from the
  // main loop, means the radio may have moved to a scan channel in between and
  // the reply is lost -- which is exactly what made the link look dead even
  // though both sides reported the same channel.
  if (incoming.frame.role == static_cast<uint8_t>(ac::Role::Remote) &&
      incoming.frame.type == static_cast<uint8_t>(ac::Type::Hello)) {
    ac::Frame reply = ac::hello(ac::Role::IrNode);
    esp_now_send(mac, reinterpret_cast<const uint8_t*>(&reply), sizeof(reply));
  }
  xQueueSend(incomingQueue, &incoming, 0);
}

bool initRmtChannel(gpio_num_t pin, rmt_channel_t channel) {
  rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX(pin, channel);
  cfg.clk_div = 80; // APB 80 MHz => 1 microsecond per tick
  cfg.mem_block_num = 1;
  cfg.tx_config.carrier_en = true;
  cfg.tx_config.carrier_freq_hz = 38000;
  cfg.tx_config.carrier_duty_percent = 33;
  cfg.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
  cfg.tx_config.idle_output_en = true;
  cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  cfg.tx_config.loop_en = false;
  if (rmt_config(&cfg) != ESP_OK) return false;
  return rmt_driver_install(channel, 0, 0) == ESP_OK;
}

bool initIr() {
  if (!initRmtChannel(static_cast<gpio_num_t>(IR_LED_PIN_1), RMT_CHANNEL_0)) return false;
  if (USE_SECOND_IR_LED &&
      !initRmtChannel(static_cast<gpio_num_t>(IR_LED_PIN_2), RMT_CHANNEL_1)) return false;
  return true;
}

void makePulse(rmt_item32_t& item, uint16_t markUs, uint16_t spaceUs) {
  item.level0 = 1;
  item.duration0 = markUs;
  item.level1 = 0;
  item.duration1 = spaceUs;
}

bool transmitPacket(const uint8_t bytes[6]) {
  if (!irReady) return false;
  rmt_item32_t items[IR_ITEMS]{};
  size_t index = 0;
  makePulse(items[index++], 4350, 4400);
  for (size_t byte = 0; byte < 6; ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      const bool one = (bytes[byte] >> bit) & 1;
      makePulse(items[index++], 560, one ? 1690 : 560);
    }
  }
  makePulse(items[index++], 560, 5200);
  if (index != IR_ITEMS) return false;

  // Each logical packet is transmitted twice, as observed from the handset.
  // Two hardware RMT channels transmit simultaneously to two IR LEDs.
  for (int repeat = 0; repeat < 2; ++repeat) {
    if (rmt_write_items(RMT_CHANNEL_0, items, IR_ITEMS, false) != ESP_OK) return false;
    if (USE_SECOND_IR_LED &&
        rmt_write_items(RMT_CHANNEL_1, items, IR_ITEMS, false) != ESP_OK) return false;
    if (rmt_wait_tx_done(RMT_CHANNEL_0, pdMS_TO_TICKS(350)) != ESP_OK) return false;
    if (USE_SECOND_IR_LED &&
        rmt_wait_tx_done(RMT_CHANNEL_1, pdMS_TO_TICKS(350)) != ESP_OK) return false;
  }
  return true;
}

ac::Result sendIr(const ac::Frame& f) {
  uint8_t bytes[6]{};
  if (!midea::encodeAction(static_cast<ac::Action>(f.action), f.state, bytes))
    return ac::Result::InvalidCommand;
  Serial.printf("IR %02X %02X %02X %02X %02X %02X  (seq %lu)\n",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                static_cast<unsigned long>(f.seq));
  return transmitPacket(bytes) ? ac::Result::Ok : ac::Result::IrError;
}

void sendHello(const uint8_t* address = BROADCAST) {
  ac::Frame f = ac::hello(ac::Role::IrNode);
  const esp_err_t err = esp_now_send(address, reinterpret_cast<const uint8_t*>(&f), sizeof(f));
  if (err != ESP_OK) {
    Serial.printf("hello to %02X:%02X:%02X:%02X:%02X:%02X failed: %d\n",
                  address[0],address[1],address[2],address[3],address[4],address[5],
                  (int)err);
  }
}

void process(const Incoming& in) {
  const ac::Frame& f = in.frame;
  if (f.role != static_cast<uint8_t>(ac::Role::Remote)) return;
  // Single-controller pairing: the first discovered remote wins until restart.
  // Security note: this is identification, NOT cryptographic authentication.
  if (haveRemote && memcmp(remoteMac, in.mac, 6) != 0) return;
  if (!addPeer(in.mac)) return;
  lastRemoteFrame = millis();
  static uint32_t lastRxLog = 0;
  if (millis() - lastRxLog > 3000) {
    lastRxLog = millis();
    Serial.printf("frame from remote, type %d, channel %d\n", (int)f.type,
                  WiFi.channel());
  }
  if (!haveRemote) {
    memcpy(remoteMac, in.mac, 6);
    haveRemote = true;
    Serial.printf("Paired remote %02X:%02X:%02X:%02X:%02X:%02X\n",
                  in.mac[0],in.mac[1],in.mac[2],in.mac[3],in.mac[4],in.mac[5]);
  }
  if (f.type == static_cast<uint8_t>(ac::Type::Hello)) {
    // The Hello is already answered in the receive callback, where the reply
    // still goes out on the channel the request arrived on.
    return;
  }
  if (f.type != static_cast<uint8_t>(ac::Type::Command)) return;
  ac::Result result;
  if (havePreviousSeq && f.seq == previousSeq) {
    // A retry should re-send the ACK, NOT the IR. LED/Turbo are toggles!
    result = previousResult;
  } else {
    if (f.action == static_cast<uint8_t>(ac::Action::SetState) &&
        !ac::validState(f.state)) {
      result = ac::Result::InvalidCommand;
    } else {
      result = sendIr(f);
    }
    previousSeq = f.seq;
    previousResult = result;
    havePreviousSeq = true;
  }
  const ac::Frame reply = ac::ack(f, result);
  esp_now_send(in.mac, reinterpret_cast<const uint8_t*>(&reply), sizeof(reply));
}
} // namespace

void setup() {
  // Log over the native USB (Serial == USB CDC on this board). Serial1 is
  // UART1 on GPIO9/10, which are not wired to the USB connector.
  Serial.begin(115200);
  delay(400);
  Serial.println("\n[Midea IR node] ESP32-C3 Super Mini");
  irReady = initIr();
  Serial.printf("IR RMT ready: %s (GPIO%d%s)\n", irReady ? "yes" : "NO",
                IR_LED_PIN_1, USE_SECOND_IR_LED ? ", GPIO2" : "");
  if (IR_RX_ENABLED) irrx::begin(IR_RX_PIN);

  incomingQueue = xQueueCreate(12, sizeof(Incoming));
  if (!incomingQueue) { Serial.println("Queue allocation FAILED"); return; }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init FAILED"); return; }
  // Set the channel after esp_now_init(), as the ESP-NOW docs require.
  if (esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK)
    Serial.println("Could not set Wi-Fi channel");
  esp_now_register_recv_cb(onReceive);
  if (!addPeer(BROADCAST)) Serial.println("Broadcast peer FAILED");
  lastHello = millis() - HELLO_INTERVAL_MS;
  Serial.printf("My MAC: %s, ESP-NOW channel: %u\n", WiFi.macAddress().c_str(),
                ESPNOW_CHANNEL);
}

void loop() {
  if (!incomingQueue) { delay(1000); return; }
  Incoming in{};
  while (xQueueReceive(incomingQueue, &in, 0) == pdTRUE) process(in);

  if (IR_RX_ENABLED) irrx::poll();

  // A disconnected station keeps scanning other channels, so the radio is
  // rarely on the data channel when a frame arrives. Re-asserting the channel
  // often pulls the radio back and keeps it listening where we want it.
  if (millis() - lastChannelFix >= 200) {
    lastChannelFix = millis();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  }

  if (millis() - lastHello >= HELLO_INTERVAL_MS) {
    lastHello = millis();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    sendHello();
  }
  static uint32_t lastStatusLog = 0;
  if (millis() - lastStatusLog > 5000) {
    lastStatusLog = millis();
    uint8_t ch = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&ch, &second);
    Serial.printf("radio channel %d (want %u), remote %s, last frame from remote %lums ago",
                  ch, (unsigned)ESPNOW_CHANNEL,
                  haveRemote ? "paired" : "none",
                  (unsigned long)(millis() - lastRemoteFrame));
    if (IR_RX_ENABLED) {
      Serial.printf(", ir interrupts %lu, ir pulses %lu, ir glitches %lu, ir level %s",
                    (unsigned long)irrx::interruptCount(),
                    (unsigned long)irrx::pulseCount(),
                    (unsigned long)irrx::glitchCount(),
                    irrx::currentLevel() ? "HIGH" : "LOW");
    }
    Serial.printf("\n");
  }
  delay(5);
}
