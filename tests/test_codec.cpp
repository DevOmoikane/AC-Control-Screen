#include <cassert>
#include <cstdio>
#include <cstring>
#include "AcProtocol.h"
#include "MideaRG57Codec.h"

int main() {
  uint8_t frame[6] = {};
  ac::State s{1, (uint8_t)ac::Mode::Cool, (uint8_t)ac::Fan::High, 21};
  assert(midea::encodeState(s, frame));
  const uint8_t knownOn[] = {0x4D,0xB2,0xFC,0x03,0x06,0xF9};
  assert(std::memcmp(frame, knownOn, 6) == 0);
  s.power = 0;
  assert(midea::encodeState(s, frame));
  assert(std::memcmp(frame, midea::OFF_PACKET, 6) == 0);
  s.power = 1;
  s.mode = (uint8_t)ac::Mode::Auto;
  s.fan = (uint8_t)ac::Fan::High;
  assert(midea::encodeState(s, frame));
  assert(frame[2] == 0xF8 && frame[3] == 0x07 && frame[4] == 0x16);
  for (int temp = 17; temp <= 30; ++temp) {
    s.tempC = temp;
    assert(midea::encodeState(s, frame));
    assert(frame[5] == midea::checksum(frame));
  }
  s.tempC = 31;
  assert(!midea::encodeState(s, frame));
  s.tempC = 24;
  assert(midea::encodeAction(ac::Action::LedToggle, s, frame));
  assert(frame[0] == 0xAD && frame[4] == 0xA5 && frame[5] == midea::checksum(frame));
  assert(midea::encodeAction(ac::Action::TurboToggle, s, frame));
  assert(frame[4] == 0x45 && frame[5] == midea::checksum(frame));
  auto msg = ac::command(ac::Action::SetState, 1234, s);
  assert(ac::validFrame(msg));
  assert(ac::ack(msg, ac::Result::Ok).seq == 1234);
  std::puts("All codec and protocol unit tests passed.");
}
