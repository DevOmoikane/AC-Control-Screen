#pragma once
#include "AcProtocol.h"
#include <cstdint>

// Captured RG57A7/BGEF byte order: 4D B2 ... , 48-bit, LSB-first.
// IMPORTANT: this is NOT the generic Midea library's byte representation.
namespace midea {
constexpr uint8_t OFF_PACKET[6] = {0x4D, 0xB2, 0xDE, 0x21, 0x07, 0xF8};
constexpr uint8_t TEMP_NIBBLE[14] = {
    0x0, 0x8, 0xC, 0x4, 0x6, 0xE, 0xA,
    0x2, 0x3, 0xB, 0x9, 0x1, 0x5, 0xD}; // 17..30 C

inline uint8_t checksum(const uint8_t* data) {
  uint16_t sum = 0;
  for (int i = 0; i < 5; ++i) sum += data[i];
  return static_cast<uint8_t>(0xFD - sum);
}

inline bool encodeState(const ac::State& s, uint8_t out[6]) {
  if (!ac::validState(s)) return false;
  if (!s.power) {
    // Verified OFF capture. This explicitly does NOT encode an arbitrary
    // temperature into OFF; restored desired settings are sent on power-on.
    for (int i = 0; i < 6; ++i) out[i] = OFF_PACKET[i];
    return true;
  }
  uint8_t modeHigh;
  switch (static_cast<ac::Mode>(s.mode)) {
    case ac::Mode::Cool: modeHigh = 0x00; break;
    case ac::Mode::Auto: modeHigh = 0x10; break;
    case ac::Mode::Heat: modeHigh = 0x30; break;
    default: return false;
  }
  uint8_t fanByte;
  if (s.mode == static_cast<uint8_t>(ac::Mode::Auto)) {
    fanByte = 0xF8; // Auto-mode override is mandatory
  } else {
    switch (static_cast<ac::Fan>(s.fan)) {
      case ac::Fan::Auto: fanByte = 0xFD; break;
      case ac::Fan::Low: fanByte = 0xF9; break;
      case ac::Fan::Medium: fanByte = 0xFA; break;
      case ac::Fan::High: fanByte = 0xFC; break;
      default: return false;
    }
  }
  out[0] = 0x4D;
  out[1] = 0xB2;
  out[2] = fanByte;
  out[3] = static_cast<uint8_t>(~fanByte);
  out[4] = static_cast<uint8_t>(modeHigh | TEMP_NIBBLE[s.tempC - 17]);
  out[5] = checksum(out);
  return true;
}

inline bool encodeAction(ac::Action action, const ac::State& s, uint8_t out[6]) {
  if (action == ac::Action::SetState) return encodeState(s, out);
  uint8_t commandByte;
  switch (action) {
    case ac::Action::LedToggle: commandByte = 0xA5; break;
    case ac::Action::TurboToggle: commandByte = 0x45; break;
    default: return false;
  }
  out[0] = 0xAD;
  out[1] = 0x52;
  out[2] = 0xAF;
  out[3] = 0x50;
  out[4] = commandByte;
  out[5] = checksum(out);
  return true;
}
} // namespace midea
