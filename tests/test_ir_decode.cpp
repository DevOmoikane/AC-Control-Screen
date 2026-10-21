// Host test for the IR frame decoder: feed synthetic Midea frames through
// the real decode() and check what it reports.
//
//   g++ -std=c++17 -I include tests/test_ir_decode.cpp -o /tmp/t && /tmp/t

#include <cstdio>
#include <cstring>
#include <vector>

#include "IrDecode.h"
#include "MideaRG57Codec.h"

using namespace irrx;

// Build the edge list for a Midea frame: header mark/space, then one
// mark/space pair per bit (LSB first within each byte), then a trailing mark.
static std::vector<Edge> makeFrame(const uint8_t bytes[6], uint32_t headerMark = 4350,
                                    uint32_t headerSpace = 4400, uint32_t mark = 560,
                                    uint32_t space0 = 560, uint32_t space1 = 1690) {
  std::vector<Edge> e;
  e.push_back({1, headerMark});
  e.push_back({0, headerSpace});
  for (int byte = 0; byte < 6; byte++) {
    for (int bit = 0; bit < 8; bit++) {
      const bool one = (bytes[byte] >> bit) & 1;
      e.push_back({1, mark});
      e.push_back({0, one ? space1 : space0});
    }
  }
  e.push_back({1, 560});   // trailing mark
  e.push_back({0, 5200});  // trailing space
  return e;
}

static int failures = 0;

static void check(const char* what, bool ok) {
  printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
  if (!ok) failures++;
}

int main() {
  // 1. The known capture: ON Cool/High/21 C.
  const uint8_t known[6] = {0x4D, 0xB2, 0xFC, 0x03, 0x06, 0xF9};
  {
    auto e = makeFrame(known);
    Frame f = decode(e.data(), (int)e.size());
    check("known capture: header found", f.headerFound);
    check("known capture: header mark 4350", f.headerMarkUs == 4350);
    check("known capture: header space 4400", f.headerSpaceUs == 4400);
    check("known capture: 48 bits", f.bitCount == 48);
    check("known capture: full frame", f.full);
    check("known capture: bytes match", memcmp(f.bytes, known, 6) == 0);
    uint8_t sum = midea::checksum(known);
    check("known capture: checksum matches the codec", sum == known[5]);
  }

  // 2. The known OFF packet.
  {
    auto e = makeFrame(midea::OFF_PACKET);
    Frame f = decode(e.data(), (int)e.size());
    check("off packet: full frame", f.full);
    check("off packet: bytes match",
          memcmp(f.bytes, midea::OFF_PACKET, 6) == 0);
  }

  // 3. The LED toggle action packet.
  {
    const uint8_t led[6] = {0xAD, 0x52, 0xAF, 0x50, 0xA5, 0x00};
    uint8_t frame[6];
    memcpy(frame, led, 6);
    frame[5] = midea::checksum(frame);
    auto e = makeFrame(frame);
    Frame f = decode(e.data(), (int)e.size());
    check("led toggle: full frame", f.full);
    check("led toggle: bytes match", memcmp(f.bytes, frame, 6) == 0);
  }

  // 4. A frame with a different header width must still be found.
  {
    auto e = makeFrame(known, 4000, 5000);
    Frame f = decode(e.data(), (int)e.size());
    check("tolerates a different header width", f.headerFound && f.full);
    check("tolerates a different header width: bytes match",
          memcmp(f.bytes, known, 6) == 0);
  }

  // 5. A truncated frame must be reported as incomplete, not decoded wrongly.
  {
    auto e = makeFrame(known);
    e.resize(30);
    Frame f = decode(e.data(), (int)e.size());
    check("truncated frame: header still found", f.headerFound);
    check("truncated frame: not reported as full", !f.full);
  }

  // 6. Noise without a header must not be decoded.
  {
    std::vector<Edge> e = {{1, 400}, {0, 500}, {1, 380}, {0, 1500}};
    Frame f = decode(e.data(), (int)e.size());
    check("noise: no header", !f.headerFound);
    check("noise: no frame", !f.full);
  }

  // 7. An inverted module (idle high) must still decode.
  {
    auto e = makeFrame(known);
    for (auto& edge : e) edge.level = edge.level ? 0 : 1;
    Frame f = decode(e.data(), (int)e.size());
    check("inverted module: header found", f.headerFound);
    check("inverted module: reported as inverted", f.inverted);
    check("inverted module: full frame", f.full);
    check("inverted module: bytes match", memcmp(f.bytes, known, 6) == 0);
  }

  // 8. Bit polarity: a long space must decode as 1, a short space as 0.
  {
    const uint8_t alt[6] = {0x4D, 0xB2, 0xFC, 0x03, 0x06, 0xF9};
    auto e = makeFrame(alt, 4350, 4400, 560, 1690, 560);  // inverted spaces
    Frame f = decode(e.data(), (int)e.size());
    bool allInverted = true;
    for (int b = 0; b < 6; b++) {
      for (int bit = 0; bit < 8; bit++) {
        const bool expected = (alt[b] >> bit) & 1;
        if (f.bits[b * 8 + bit] == expected) allInverted = false;
      }
    }
    check("inverted spaces decode to the complement", allInverted);
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
