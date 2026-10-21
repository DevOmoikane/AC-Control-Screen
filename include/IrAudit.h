#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "AcProtocol.h"
#include "IrDecode.h"
#include "MideaRG57Codec.h"

// Analysis of captured AC remote frames.
//
// The recordings come from tools/ir_record.py, one capture per line:
//
//   <label> | <bytes> | <edges> | <key>=<value> ...
//
// The label doubles as a state descriptor ("cool_high_22", "off", "turbo"),
// which is what makes it possible to say what the codec has to change.
//
// Kept free of Arduino dependencies so it can be exercised on the host.

namespace iraudit {

constexpr int FRAME_BYTES = 6;

inline std::string trim(const std::string& text) {
  const size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

inline std::vector<std::string> split(const std::string& text, char separator) {
  std::vector<std::string> parts;
  std::string current;
  for (const char c : text) {
    if (c == separator) {
      parts.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  parts.push_back(current);
  return parts;
}

inline std::vector<std::string> splitWhitespace(const std::string& text) {
  std::vector<std::string> parts;
  std::string current;
  for (const char c : text) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

inline std::string lowerCase(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

inline std::string hex2(uint8_t value) {
  char buffer[4];
  snprintf(buffer, sizeof(buffer), "%02X", value);
  return buffer;
}

// One recorded capture.
struct Capture {
  std::string label;
  std::string bytesText;   // the bytes the firmware reported, for cross checking
  std::vector<irrx::Edge> edges;
  std::string captureNumber;
  std::string repeats;
  std::string timestamp;
  std::string note;        // what the user said the capture should be
  bool incomplete = false; // the capture never finished
  std::string error;       // set when the line could not be understood
};

// Parse one line of a capture file. Returns false for comments and blank
// lines; a malformed capture is reported through Capture::error.
inline bool parseCaptureLine(const std::string& line, Capture& out) {
  const std::string text = trim(line);
  if (text.empty() || text[0] == '#') return false;

  out = Capture();
  const std::vector<std::string> fields = split(text, '|');
  if (fields.size() < 3) {
    out.error = "expected at least 3 fields separated by '|'";
    return true;
  }
  out.label = trim(fields[0]);
  out.bytesText = trim(fields[1]);
  for (const std::string& token : splitWhitespace(fields[2])) {
    const size_t colon = token.find(':');
    if (colon == std::string::npos) continue;
    irrx::Edge edge{};
    edge.level = token[0] == 'H' ? 1 : 0;
    edge.us = static_cast<uint32_t>(
        strtoul(token.c_str() + colon + 1, nullptr, 10));
    out.edges.push_back(edge);
  }
  for (size_t i = 3; i < fields.size(); i++) {
    const std::string extra = trim(fields[i]);
    const size_t equals = extra.find('=');
    if (equals == std::string::npos) continue;
    const std::string key = trim(extra.substr(0, equals));
    const std::string value = trim(extra.substr(equals + 1));
    if (key == "capture") out.captureNumber = value;
    else if (key == "repeats") out.repeats = value;
    else if (key == "at") out.timestamp = value;
    else if (key == "note") out.note = value;
    else if (key == "incomplete") out.incomplete = value == "1" || value == "yes";
  }
  if (out.edges.empty()) out.error = "no pulses in this capture";
  return true;
}

// Parse "4D B2 FC 03 06 F9" into bytes. Returns false when the text is not
// a full frame.
inline bool parseBytes(const std::string& text, uint8_t out[FRAME_BYTES]) {
  const std::vector<std::string> tokens = splitWhitespace(text);
  if (tokens.size() != FRAME_BYTES) return false;
  for (int i = 0; i < FRAME_BYTES; i++) {
    char* end = nullptr;
    const unsigned long value = strtoul(tokens[i].c_str(), &end, 16);
    if (end == tokens[i].c_str() || *end != '\0' || value > 0xFF) return false;
    out[i] = static_cast<uint8_t>(value);
  }
  return true;
}

inline std::string formatBytes(const uint8_t bytes[FRAME_BYTES]) {
  std::string text;
  for (int i = 0; i < FRAME_BYTES; i++) {
    if (i) text += ' ';
    text += hex2(bytes[i]);
  }
  return text;
}

enum class FrameKind { Unknown, State, Toggle };

// What a frame says, read straight from the bytes.
struct FrameInfo {
  FrameKind kind = FrameKind::Unknown;
  std::string kindName = "unknown";
  bool checksumOk = false;
  uint8_t bytes[FRAME_BYTES] = {0};
  uint8_t fanByte = 0;
  uint8_t invFanByte = 0;
  uint8_t modeTempByte = 0;
  std::string fanName = "unknown";
  std::string modeName = "unknown";
  int tempC = -1;          // -1 when the nibble is not in the codec's table
  bool tempKnown = false;
};

inline std::string fanNameOf(uint8_t byte) {
  switch (byte) {
    case 0xFD: return "Auto";
    case 0xF9: return "Low";
    case 0xFA: return "Medium";
    case 0xFC: return "High";
    case 0xF8: return "Auto override";
    default: return "unknown";
  }
}

inline std::string modeNameOf(uint8_t modeTempByte) {
  switch (modeTempByte & 0xF0) {
    case 0x00: return "Cool";
    case 0x10: return "Auto";
    case 0x30: return "Heat";
    default: return "unknown";
  }
}

// The temperature a nibble stands for, according to the codec's table.
inline int tempOfNibble(uint8_t nibble) {
  for (int i = 0; i < 14; i++) {
    if (midea::TEMP_NIBBLE[i] == nibble) return 17 + i;
  }
  return -1;
}

inline FrameInfo describeFrame(const uint8_t bytes[FRAME_BYTES]) {
  FrameInfo info;
  for (int i = 0; i < FRAME_BYTES; i++) info.bytes[i] = bytes[i];
  info.checksumOk = midea::checksum(bytes) == bytes[5];
  if (bytes[0] == 0x4D && bytes[1] == 0xB2) {
    info.kind = FrameKind::State;
    info.kindName = "state";
    info.fanByte = bytes[2];
    info.invFanByte = bytes[3];
    info.modeTempByte = bytes[4];
    info.fanName = fanNameOf(bytes[2]);
    info.modeName = modeNameOf(bytes[4]);
    info.tempC = tempOfNibble(bytes[4] & 0x0F);
    info.tempKnown = info.tempC > 0;
  } else if (bytes[0] == 0xAD && bytes[1] == 0x52) {
    info.kind = FrameKind::Toggle;
    info.kindName = "toggle";
    if (bytes[4] == 0xA5) info.fanName = "LED";
    else if (bytes[4] == 0x45) info.fanName = "Turbo";
    else info.fanName = "unknown";
  }
  return info;
}

// The state a label asks for.
enum class ExpectKind { None, State, Off, ToggleLed, ToggleTurbo, Named };

struct Expectation {
  ExpectKind kind = ExpectKind::None;
  ac::State state{};
  bool codecCanEncode = false;  // the codec can produce a frame for this
  double temp = 0.0;            // degrees, halves included
  std::string fan;              // auto, low, med, high, f1 to f7
  std::string mode;             // cool, auto, heat, dry, fan
  std::string note;
};

inline bool modeFromName(const std::string& name, uint8_t& out) {
  if (name == "cool") { out = static_cast<uint8_t>(ac::Mode::Cool); return true; }
  if (name == "auto") { out = static_cast<uint8_t>(ac::Mode::Auto); return true; }
  if (name == "heat") { out = static_cast<uint8_t>(ac::Mode::Heat); return true; }
  return false;
}

inline bool fanFromName(const std::string& name, uint8_t& out) {
  if (name == "auto") { out = static_cast<uint8_t>(ac::Fan::Auto); return true; }
  if (name == "low") { out = static_cast<uint8_t>(ac::Fan::Low); return true; }
  if (name == "med" || name == "medium") {
    out = static_cast<uint8_t>(ac::Fan::Medium);
    return true;
  }
  if (name == "high") { out = static_cast<uint8_t>(ac::Fan::High); return true; }
  return false;
}

inline bool fanValueOf(const std::string& name, uint8_t& out) {
  return fanFromName(lowerCase(name), out);
}

// Buttons of the remote that send a frame of their own, with no state in it.
inline bool isNamedFrame(const std::string& name) {
  static const char* names[] = {"m",         "soft",     "vane",    "vaneangle",
                                "vane_angle", "selfclean", "self_clean",
                                "swing",     "silent",   "timer",   "up",
                                "down",      "mode",     "power",   "fan"};
  for (const char* candidate : names) {
    if (name == candidate) return true;
  }
  return false;
}

// The fan of the remote has seven points plus automatic. The codec knows
// only auto, low, medium and high.
inline bool fanFromLabel(const std::string& name, std::string& canonical,
                          bool& codecKnows) {
  uint8_t codecFan = 0;
  if (fanFromName(name, codecFan)) {
    canonical = name == "medium" ? "med" : name;
    codecKnows = true;
    return true;
  }
  if (name == "max") {
    canonical = "f7";
    codecKnows = false;
    return true;
  }
  if (name.size() == 2 && name[0] == 'f' && name[1] >= '1' && name[1] <= '7') {
    canonical = name;
    codecKnows = false;
    return true;
  }
  if (name.size() == 1 && name[0] >= '1' && name[0] <= '7') {
    canonical = std::string("f") + name;
    codecKnows = false;
    return true;
  }
  return false;
}

inline bool modeFromLabel(const std::string& name, std::string& canonical,
                          bool& codecKnows) {
  if (name == "cool" || name == "auto" || name == "heat") {
    canonical = name;
    codecKnows = true;
    return true;
  }
  if (name == "dry" || name == "fan") {
    canonical = name;
    codecKnows = false;
    return true;
  }
  return false;
}

// A temperature of 21 or 21.5 degrees.
inline bool tempFromLabel(const std::string& name, double& out) {
  char* end = nullptr;
  const double value = strtod(name.c_str(), &end);
  if (end == name.c_str() || *end != '\0') return false;
  if (value < 17.0 || value > 30.0) return false;
  const double doubled = value * 2.0;
  if (doubled != floor(doubled)) return false;
  out = value;
  return true;
}

// A label of the form <mode>_<fan>_<temp> names the state the frame should
// carry. "off", "led" and "turbo" name the other frames the remote sends.
// Spaces and dashes work as separators too, so "cool high 22" is understood.
inline Expectation expectationForLabel(const std::string& label) {
  Expectation expectation;
  std::string name = lowerCase(trim(label));
  for (char& c : name) {
    if (c == ' ' || c == '-' || c == ',') c = '_';
  }
  if (name.empty()) {
    expectation.note = "empty label";
    return expectation;
  }
  if (name == "off" || name == "power_off" || name == "poweroff") {
    expectation.kind = ExpectKind::Off;
    expectation.state.power = 0;
    expectation.codecCanEncode = true;
    return expectation;
  }
  if (name == "led" || name == "display" || name == "led_toggle") {
    expectation.kind = ExpectKind::ToggleLed;
    expectation.codecCanEncode = true;
    return expectation;
  }
  if (name == "turbo" || name == "turbo_toggle") {
    expectation.kind = ExpectKind::ToggleTurbo;
    expectation.codecCanEncode = true;
    return expectation;
  }
  if (isNamedFrame(name)) {
    expectation.kind = ExpectKind::Named;
    expectation.note = "the codec has no frame for this button";
    return expectation;
  }
  const std::vector<std::string> parts = split(name, '_');
  if (parts.size() != 3) {
    expectation.note = "label is not a state descriptor";
    return expectation;
  }
  bool modeKnown = false;
  bool fanKnown = false;
  if (!modeFromLabel(parts[0], expectation.mode, modeKnown)) {
    expectation.note = "unknown mode '" + parts[0] + "'";
    return expectation;
  }
  if (!fanFromLabel(parts[1], expectation.fan, fanKnown)) {
    expectation.note = "unknown fan '" + parts[1] + "'";
    return expectation;
  }
  if (!tempFromLabel(parts[2], expectation.temp)) {
    expectation.note = "temperature out of range: " + parts[2];
    return expectation;
  }
  expectation.kind = ExpectKind::State;
  const bool wholeDegrees = expectation.temp == floor(expectation.temp);
  expectation.codecCanEncode = modeKnown && fanKnown && wholeDegrees;
  expectation.state.power = 1;
  if (modeKnown) {
    modeFromName(expectation.mode, expectation.state.mode);
  } else {
    expectation.state.mode = static_cast<uint8_t>(ac::Mode::Cool);
  }
  if (fanKnown) {
    fanFromName(expectation.fan, expectation.state.fan);
  } else {
    expectation.state.fan = static_cast<uint8_t>(ac::Fan::Auto);
  }
  expectation.state.tempC = static_cast<uint8_t>(expectation.temp);
  if (!modeKnown) {
    expectation.note = "the codec has no " + expectation.mode + " mode";
  } else if (!fanKnown) {
    expectation.note = "the codec has no fan level " + expectation.fan;
  } else if (!wholeDegrees) {
    expectation.note = "the codec has no half degrees";
  }
  return expectation;
}

// The bytes the codec produces for an expectation.
inline bool expectedBytes(const Expectation& expectation, uint8_t out[FRAME_BYTES]) {
  switch (expectation.kind) {
    case ExpectKind::Off:
      for (int i = 0; i < FRAME_BYTES; i++) out[i] = midea::OFF_PACKET[i];
      return true;
    case ExpectKind::ToggleLed:
      return midea::encodeAction(ac::Action::LedToggle, expectation.state, out);
    case ExpectKind::ToggleTurbo:
      return midea::encodeAction(ac::Action::TurboToggle, expectation.state, out);
    case ExpectKind::State:
      if (!expectation.codecCanEncode) return false;
      return midea::encodeState(expectation.state, out);
    default:
      return false;
  }
}

// The state a frame describes, so the codec can be asked for the same thing.
inline bool stateFromFrame(const FrameInfo& info, ac::State& out) {
  if (info.kind != FrameKind::State) return false;
  const std::string mode = lowerCase(info.modeName);
  const std::string fan = lowerCase(info.fanName);
  if (mode == "unknown" || mode.empty()) return false;
  if (!info.tempKnown) return false;
  uint8_t fanValue = 0;
  if (fan == "auto" || fan == "auto override") {
    fanValue = static_cast<uint8_t>(ac::Fan::Auto);
  } else if (!fanFromName(fan, fanValue)) {
    return false;
  }
  out.power = 1;
  out.mode = mode == "cool"  ? static_cast<uint8_t>(ac::Mode::Cool)
            : mode == "auto" ? static_cast<uint8_t>(ac::Mode::Auto)
            : mode == "heat" ? static_cast<uint8_t>(ac::Mode::Heat)
                             : static_cast<uint8_t>(ac::Mode::Cool);
  out.fan = fanValue;
  out.tempC = static_cast<uint8_t>(info.tempC);
  return true;
}

// One byte where the remote and the codec disagree.
struct Difference {
  int index = -1;
  uint8_t remote = 0;
  uint8_t codec = 0;
  std::string what;
  std::string suggestion;
};

// Compare a captured frame with what the codec produces, and say what to
// change in include/MideaRG57Codec.h for every byte that differs.
inline std::vector<Difference> compareFrames(const uint8_t remote[FRAME_BYTES],
                                             const uint8_t codec[FRAME_BYTES],
                                             const ac::State& state) {
  std::vector<Difference> differences;
  for (int i = 0; i < FRAME_BYTES; i++) {
    if (remote[i] == codec[i]) continue;
    // A different checksum is normally the consequence of a different
    // payload. It is only a difference in the formula when the remote's own
    // checksum does not add up.
    if (i == 5 && midea::checksum(remote) == remote[5]) continue;
    Difference difference;
    difference.index = i;
    difference.remote = remote[i];
    difference.codec = codec[i];
    switch (i) {
      case 0:
        difference.what = "frame prefix";
        difference.suggestion =
            "the codec starts the frame with 0x" + hex2(codec[i]) +
            ", the remote sends 0x" + hex2(remote[i]) +
            " -> change the first byte in encodeState()";
        break;
      case 1:
        difference.what = "frame prefix";
        difference.suggestion =
            "the codec uses 0x" + hex2(codec[i]) + " as the second byte, the remote sends 0x" +
            hex2(remote[i]) + " -> change the second byte in encodeState()";
        break;
      case 2:
        difference.what = "fan byte";
        difference.suggestion =
            "the codec encodes fan " + fanNameOf(codec[i]) + " as 0x" + hex2(codec[i]) +
            ", the remote sends 0x" + hex2(remote[i]) +
            " -> change the fan byte of this fan in encodeState()";
        break;
      case 3:
        difference.what = "inverse fan byte";
        difference.suggestion =
            "byte 3 has to be the one's complement of byte 2: the remote sends 0x" +
            hex2(remote[i]) + ", the codec sends 0x" + hex2(codec[i]) +
            " -> change the ~fanByte line in encodeState()";
        break;
      case 4: {
        const uint8_t remoteMode = remote[i] & 0xF0;
        const uint8_t codecMode = codec[i] & 0xF0;
        const uint8_t remoteTemp = remote[i] & 0x0F;
        const uint8_t codecTemp = codec[i] & 0x0F;
        if (remoteMode != codecMode) {
          difference.what = "mode bits";
          difference.suggestion =
              "the codec encodes the mode as 0x" + hex2(codecMode) +
              ", the remote sends 0x" + hex2(remoteMode) +
              " -> change the modeHigh value in encodeState()";
        } else {
          difference.what = "temperature nibble";
          const int index = static_cast<int>(state.tempC) - 17;
          difference.suggestion =
              "TEMP_NIBBLE[" + std::to_string(index) + "] (" +
              std::to_string(state.tempC) + " C) is 0x" + hex2(codecTemp) +
              " in the codec, the remote sends 0x" + hex2(remoteTemp) +
              " -> change that entry in include/MideaRG57Codec.h";
        }
        break;
      }
      case 5:
        difference.what = "checksum";
        difference.suggestion =
            "the codec computes 0x" + hex2(codec[i]) + ", the remote sends 0x" +
            hex2(remote[i]) + " -> change the checksum formula";
        break;
      default:
        difference.what = "byte " + std::to_string(i);
        difference.suggestion = "the remote sends 0x" + hex2(remote[i]) +
                                ", the codec sends 0x" + hex2(codec[i]);
        break;
    }
    differences.push_back(difference);
  }
  return differences;
}

// The pulse widths of a captured frame, measured the way the remote sends
// them: a header, then the mark and the space of every bit.
struct TimingInfo {
  bool measured = false;
  uint32_t headerMarkUs = 0;
  uint32_t headerSpaceUs = 0;
  uint32_t markUs = 0;
  uint32_t spaceZeroUs = 0;
  uint32_t spaceOneUs = 0;
  uint32_t frameGapUs = 0;
};

// Measure the timings of a captured burst.
inline TimingInfo measureTimings(const std::vector<irrx::Edge>& edges,
                                  const irrx::Frame& frame) {
  TimingInfo info;
  if (!frame.headerFound) return info;
  info.measured = true;
  info.headerMarkUs = frame.headerMarkUs;
  info.headerSpaceUs = frame.headerSpaceUs;

  const uint8_t markLevel = frame.inverted ? 0 : 1;
  uint32_t markSum = 0;
  uint32_t markCount = 0;
  uint32_t shortSum = 0;
  uint32_t shortCount = 0;
  uint32_t longSum = 0;
  uint32_t longCount = 0;
  int i = frame.headerIndex + 2;
  while (i + 1 < static_cast<int>(edges.size())) {
    const irrx::Edge& mark = edges[i];
    const irrx::Edge& space = edges[i + 1];
    if (mark.level != markLevel) break;
    if (mark.us >= 2000 || space.us >= 2000) break;  // the next header
    markSum += mark.us;
    markCount++;
    if (space.us >= 1200) {
      longSum += space.us;
      longCount++;
    } else {
      shortSum += space.us;
      shortCount++;
    }
    i += 2;
  }
  if (markCount) info.markUs = markSum / markCount;
  if (shortCount) info.spaceZeroUs = shortSum / shortCount;
  if (longCount) info.spaceOneUs = longSum / longCount;

  // The gap between the two copies: the trailing mark and the space after
  // it.
  if (i + 1 < static_cast<int>(edges.size()) && edges[i + 1].us >= 2000) {
    info.frameGapUs = edges[i + 1].us;
  }
  return info;
}

// One timing where the remote and the transmitter disagree.
struct TimingDifference {
  std::string what;
  uint32_t remote = 0;
  uint32_t transmitter = 0;
  std::string suggestion;
};

// Compare the timings of the remote with the ones the transmitter uses.
inline std::vector<TimingDifference> compareTimings(const TimingInfo& info) {
  std::vector<TimingDifference> differences;
  struct Entry {
    const char* what;
    uint32_t remote;
    uint32_t transmitter;
    const char* constant;
  };
  const Entry entries[] = {
      {"header mark", info.headerMarkUs, midea::HEADER_MARK_US, "HEADER_MARK_US"},
      {"header space", info.headerSpaceUs, midea::HEADER_SPACE_US, "HEADER_SPACE_US"},
      {"mark", info.markUs, midea::MARK_US, "MARK_US"},
      {"space of a zero", info.spaceZeroUs, midea::SPACE_ZERO_US, "SPACE_ZERO_US"},
      {"space of a one", info.spaceOneUs, midea::SPACE_ONE_US, "SPACE_ONE_US"},
      {"gap between the copies", info.frameGapUs, midea::FRAME_GAP_US, "FRAME_GAP_US"},
  };
  for (const Entry& entry : entries) {
    if (entry.remote == 0) continue;
    // A fifth of the width is well beyond the spread of the measurements.
    const uint32_t slack = entry.transmitter / 5;
    const uint32_t low = entry.transmitter > slack ? entry.transmitter - slack : 0;
    const uint32_t high = entry.transmitter + slack;
    if (entry.remote >= low && entry.remote <= high) continue;
    TimingDifference difference;
    difference.what = entry.what;
    difference.remote = entry.remote;
    difference.transmitter = entry.transmitter;
    difference.suggestion =
        "the remote uses " + std::to_string(entry.remote) + " us, the transmitter sends " +
        std::to_string(entry.transmitter) + " us -> change " + entry.constant +
        " in include/MideaRG57Codec.h";
    differences.push_back(difference);
  }
  return differences;
}

}  // namespace iraudit
