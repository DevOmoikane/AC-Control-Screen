#pragma once
#include <cstdint>

#include "IrReceiver.h"

// Pure decoding of an IR burst into a Midea RG57 frame.
//
// Kept free of Arduino dependencies so it can be exercised on the host. The
// firmware itself does not use this: the receiver reports raw pulses only,
// because the remote it is currently connected to is not a Midea one.

namespace irrx {

// Tuning. A real Midea pulse is 560 us (mark) or 560/1690 us (space); the
// header is a ~4350 us mark followed by a ~4400 us space.
constexpr uint32_t HEADER_MIN_US = 2000;
constexpr uint32_t HEADER_MAX_US = 20000;
constexpr uint32_t ONE_BIT_MIN_US = 1000;
constexpr uint32_t TRAILING_MARK_US = 2000;
constexpr int FRAME_BITS = 48;
constexpr int FRAME_BYTES = FRAME_BITS / 8;
constexpr int MAX_BITS = 64;

struct Frame {
  bool headerFound = false;
  int headerIndex = -1;
  uint32_t headerMarkUs = 0;
  uint32_t headerSpaceUs = 0;
  int bitCount = 0;
  bool bits[MAX_BITS] = {false};
  uint8_t bytes[FRAME_BYTES] = {0};
  bool full = false;     // a complete 48 bit frame was decoded
  bool inverted = false; // the levels had to be flipped to find the header
};

// Find the header and decode the bits that follow. Bits are LSB first within
// each byte, matching the Midea RG57 capture.
//
// Some receiver modules output the complement of the usual mark/space levels.
// If no header is found the whole burst is tried the other way round.
inline Frame decode(const Edge* edges, int count) {
  Frame f;
  bool flip = false;

  for (int attempt = 0; attempt < 2; attempt++) {
    auto levelOf = [&](int i) -> uint8_t {
      const uint8_t l = edges[i].level;
      return flip ? static_cast<uint8_t>(!l) : l;
    };

    f = Frame();
    f.inverted = flip;

    for (int i = 0; i + 1 < count; i++) {
      if (levelOf(i) == 1 && edges[i].us >= HEADER_MIN_US &&
          edges[i].us <= HEADER_MAX_US && levelOf(i + 1) == 0 &&
          edges[i + 1].us >= HEADER_MIN_US && edges[i + 1].us <= HEADER_MAX_US) {
        f.headerFound = true;
        f.headerIndex = i;
        f.headerMarkUs = edges[i].us;
        f.headerSpaceUs = edges[i + 1].us;
        break;
      }
    }
    if (f.headerFound) break;
    flip = true;
  }
  if (!f.headerFound) return f;

  auto levelOf = [&](int i) -> uint8_t {
    const uint8_t l = edges[i].level;
    return f.inverted ? static_cast<uint8_t>(!l) : l;
  };

  int i = f.headerIndex + 2;
  while (i + 1 < count && f.bitCount < MAX_BITS) {
    const Edge& mark = edges[i];
    const Edge& space = edges[i + 1];
    if (levelOf(i) != 1) break;              // expected a mark
    if (mark.us >= TRAILING_MARK_US) break;  // trailing mark, frame is over
    if (levelOf(i + 1) != 0) break;          // expected a space
    if (space.us >= TRAILING_MARK_US) break; // long space, frame is over
    f.bits[f.bitCount++] = (space.us >= ONE_BIT_MIN_US);
    i += 2;
  }

  if (f.bitCount >= FRAME_BITS) {
    f.full = true;
    for (int byte = 0; byte < FRAME_BYTES; byte++) {
      uint8_t v = 0;
      for (int bit = 0; bit < 8; bit++) {
        if (f.bits[byte * 8 + bit]) v |= static_cast<uint8_t>(1u << bit);
      }
      f.bytes[byte] = v;
    }
  }
  return f;
}

}  // namespace irrx
