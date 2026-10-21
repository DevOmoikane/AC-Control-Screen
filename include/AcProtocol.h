#pragma once
#include <cstdint>

namespace ac {
constexpr uint32_t MAGIC = 0x41434331UL; // fixed protocol guard: ACC1
constexpr uint8_t VERSION = 1;

enum class Type : uint8_t { Hello = 1, Command = 2, Ack = 3 };
enum class Role : uint8_t { Remote = 1, IrNode = 2 };
enum class Action : uint8_t { SetState = 1, LedToggle = 2, TurboToggle = 3 };
enum class Mode : uint8_t { Cool = 0, Auto = 1, Heat = 2, Dry = 3, Fan = 4 };
enum class Fan : uint8_t { Auto = 0, Low = 1, Medium = 2, High = 3 };
enum class Result : uint8_t { Ok = 0, InvalidCommand = 1, IrError = 2 };

#pragma pack(push, 1)
struct State {
  uint8_t power = 0;  // 0=off; 1=on
  uint8_t mode = static_cast<uint8_t>(Mode::Cool);
  uint8_t fan = static_cast<uint8_t>(Fan::Auto);
  uint8_t tempC = 24;
};

struct Frame {
  uint32_t magic = MAGIC;
  uint8_t version = VERSION;
  uint8_t type = 0;
  uint8_t role = 0;
  uint8_t action = 0;
  uint32_t seq = 0;
  State state{};
  uint8_t result = 0;
  uint8_t reserved = 0;
};
#pragma pack(pop)
static_assert(sizeof(State) == 4, "Unexpected State wire size");
static_assert(sizeof(Frame) == 18, "Unexpected ESPNOW wire size");

inline bool validFrame(const Frame& f) {
  return f.magic == MAGIC && f.version == VERSION &&
         f.type >= 1 && f.type <= 3 && f.role >= 1 && f.role <= 2;
}
inline bool validState(const State& s) {
  return s.power <= 1 && s.mode <= static_cast<uint8_t>(Mode::Fan) &&
         s.fan <= static_cast<uint8_t>(Fan::High) &&
         s.tempC >= 17 && s.tempC <= 30;
}
inline bool sameState(const State& a, const State& b) {
  return a.power == b.power && a.mode == b.mode &&
         a.fan == b.fan && a.tempC == b.tempC;
}
inline Frame hello(Role role) {
  Frame f;
  f.type = static_cast<uint8_t>(Type::Hello);
  f.role = static_cast<uint8_t>(role);
  return f;
}
inline Frame command(Action action, uint32_t seq, const State& s) {
  Frame f;
  f.type = static_cast<uint8_t>(Type::Command);
  f.role = static_cast<uint8_t>(Role::Remote);
  f.action = static_cast<uint8_t>(action);
  f.seq = seq;
  f.state = s;
  return f;
}
inline Frame ack(const Frame& in, Result result) {
  Frame f = in;
  f.type = static_cast<uint8_t>(Type::Ack);
  f.role = static_cast<uint8_t>(Role::IrNode);
  f.result = static_cast<uint8_t>(result);
  return f;
}
} // namespace ac
