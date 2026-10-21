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

  // Dry, as the original remote sends it at 17 C.
  s.mode = (uint8_t)ac::Mode::Dry;
  s.fan = (uint8_t)ac::Fan::Auto;
  s.tempC = 17;
  assert(midea::encodeState(s, frame));
  const uint8_t knownDry[] = {0x4D,0xB2,0xF8,0x07,0x20,0xDF};
  assert(std::memcmp(frame, knownDry, 6) == 0);
  // Dry always uses the auto override fan byte, whatever the fan card says.
  s.fan = (uint8_t)ac::Fan::High;
  assert(midea::encodeState(s, frame));
  assert(frame[2] == 0xF8 && frame[3] == 0x07);
  // Dry does carry the setpoint.
  s.tempC = 22;
  assert(midea::encodeState(s, frame));
  const uint8_t dry22[] = {0x4D,0xB2,0xF8,0x07,0x2E,0xD1};
  assert(std::memcmp(frame, dry22, 6) == 0);
  for (int temp = 17; temp <= 30; ++temp) {
    s.tempC = temp;
    assert(midea::encodeState(s, frame));
    assert(frame[5] == midea::checksum(frame));
  }

  // Fan, as the original remote sends it at 17 C.
  s.mode = (uint8_t)ac::Mode::Fan;
  s.fan = (uint8_t)ac::Fan::Auto;
  s.tempC = 17;
  assert(midea::encodeState(s, frame));
  const uint8_t knownFan[] = {0x4D,0xB2,0xFD,0x02,0x27,0xD8};
  assert(std::memcmp(frame, knownFan, 6) == 0);
  // Fan mode has no setpoint, so the byte does not follow the temperature.
  s.tempC = 30;
  assert(midea::encodeState(s, frame));
  assert(std::memcmp(frame, knownFan, 6) == 0);
  // The fan speed is still carried.
  s.fan = (uint8_t)ac::Fan::High;
  assert(midea::encodeState(s, frame));
  assert(frame[2] == 0xFC && frame[3] == 0x03 && frame[4] == 0x27);
  assert(frame[5] == midea::checksum(frame));

  s.mode = (uint8_t)ac::Mode::Cool;
  s.fan = (uint8_t)ac::Fan::High;
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
