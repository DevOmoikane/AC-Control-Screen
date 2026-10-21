// Host test for the capture analysis: parse a recorded capture, work out
// what its frame means, and check what the codec would send for it.
//
//   g++ -std=c++17 -I include tests/test_ir_audit.cpp -o /tmp/t && /tmp/t

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "IrAudit.h"

using namespace iraudit;

static int failures = 0;

static void check(const std::string& what, bool ok) {
  printf("%-58s %s\n", what.c_str(), ok ? "PASS" : "FAIL");
  if (!ok) failures++;
}

// Build the edge list of a Midea frame: header mark/space, then one
// mark/space pair per bit (LSB first within each byte), then a trailing mark.
static std::vector<irrx::Edge> makeFrame(const uint8_t bytes[6]) {
  std::vector<irrx::Edge> edges;
  edges.push_back({1, 4350});
  edges.push_back({0, 4400});
  for (int byte = 0; byte < 6; byte++) {
    for (int bit = 0; bit < 8; bit++) {
      const bool one = (bytes[byte] >> bit) & 1;
      edges.push_back({1, 560});
      edges.push_back({0, one ? 1690u : 560u});
    }
  }
  edges.push_back({1, 560});
  edges.push_back({0, 5200});
  return edges;
}

int main() {
  // 1. A recorded capture line is parsed.
  {
    Capture capture;
    const bool isCapture = parseCaptureLine(
        "cool_high_21 | 4D B2 FC 03 06 F9 | H:4350 L:4400 H:560 L:1690 | "
        "capture=3 | repeats=2 | at=2026-10-21T12:00:00",
        capture);
    check("a capture line is recognised", isCapture);
    check("the label is read", capture.label == "cool_high_21");
    check("the bytes are read", capture.bytesText == "4D B2 FC 03 06 F9");
    check("the pulses are read", capture.edges.size() == 4);
    check("the pulse levels are read", capture.edges[0].level == 1 && capture.edges[1].level == 0);
    check("the pulse widths are read", capture.edges[0].us == 4350 && capture.edges[2].us == 560);
    check("the capture number is read", capture.captureNumber == "3");
    check("the repeat count is read", capture.repeats == "2");
    check("the timestamp is read", capture.timestamp == "2026-10-21T12:00:00");
    check("no error is reported", capture.error.empty());
  }

  // 2. Comments and blank lines are not captures.
  {
    Capture capture;
    check("a comment is not a capture", !parseCaptureLine("# a comment", capture));
    check("a blank line is not a capture", !parseCaptureLine("   ", capture));
  }

  // 3. A malformed capture is reported, not silently accepted.
  {
    Capture capture;
    const bool isCapture = parseCaptureLine("label | only | two", capture);
    check("a malformed capture is still a capture", isCapture);
    check("a malformed capture has an error", !capture.error.empty());
  }

  // 4. The known capture decodes and says what it means.
  {
    const uint8_t known[6] = {0x4D, 0xB2, 0xFC, 0x03, 0x06, 0xF9};
    const std::vector<irrx::Edge> edges = makeFrame(known);
    const irrx::Frame frame = irrx::decode(edges.data(), static_cast<int>(edges.size()));
    check("the known capture decodes", frame.full);
    const FrameInfo info = describeFrame(frame.bytes);
    check("it is a state frame", info.kind == FrameKind::State);
    check("the fan is High", info.fanName == "High");
    check("the mode is Cool", info.modeName == "Cool");
    check("the temperature is 21 C", info.tempC == 21);
    check("the checksum is right", info.checksumOk);
  }

  // 5. A label names the state the frame should carry.
  {
    const uint8_t known[6] = {0x4D, 0xB2, 0xFC, 0x03, 0x06, 0xF9};
    const Expectation expectation = expectationForLabel("cool_high_21");
    check("a state label is understood", expectation.kind == ExpectKind::State);
    check("the mode is taken from the label", expectation.state.mode == static_cast<uint8_t>(ac::Mode::Cool));
    check("the fan is taken from the label", expectation.state.fan == static_cast<uint8_t>(ac::Fan::High));
    check("the temperature is taken from the label", expectation.state.tempC == 21);
    uint8_t expected[FRAME_BYTES] = {0};
    check("the codec can encode it", expectedBytes(expectation, expected));
    check("the codec produces the known capture", memcmp(expected, known, 6) == 0);
  }

  // 6. The off label expects the captured OFF packet.
  {
    const Expectation expectation = expectationForLabel("off");
    check("the off label is understood", expectation.kind == ExpectKind::Off);
    uint8_t expected[FRAME_BYTES] = {0};
    check("the codec can encode it", expectedBytes(expectation, expected));
    check("it is the OFF packet", memcmp(expected, midea::OFF_PACKET, 6) == 0);
  }

  // 7. The toggle labels expect the toggle packets.
  {
    const Expectation led = expectationForLabel("led");
    uint8_t expected[FRAME_BYTES] = {0};
    check("the led label is understood", led.kind == ExpectKind::ToggleLed);
    check("the codec can encode it", expectedBytes(led, expected));
    check("it starts with AD 52", expected[0] == 0xAD && expected[1] == 0x52);
    check("it carries A5", expected[4] == 0xA5);
  }

  // 8. A label that names nothing is reported as such.
  {
    const Expectation expectation = expectationForLabel("some button");
    check("an unknown label has no expectation", expectation.kind == ExpectKind::None);
    check("an unknown label has a note", !expectation.note.empty());
  }

  // 9. The separators do not matter: spaces and dashes work like underscores.
  {
    const Expectation spaced = expectationForLabel("cool high 21");
    check("a label with spaces is understood", spaced.kind == ExpectKind::State);
    check("the spaces label names High", spaced.state.fan == static_cast<uint8_t>(ac::Fan::High));
    check("the spaces label names 21 C", spaced.state.tempC == 21);
    const Expectation dashed = expectationForLabel("cool-high-21");
    check("a label with dashes is understood", dashed.kind == ExpectKind::State);
    check("the dashes label names 21 C", dashed.state.tempC == 21);
    const Expectation mixed = expectationForLabel("Cool High 21");
    check("the case does not matter", mixed.kind == ExpectKind::State);
  }

  // 10. A half degree is understood, and the codec is told it cannot do it.
  {
    const Expectation half = expectationForLabel("cool_high_21.5");
    check("a half degree is understood", half.kind == ExpectKind::State);
    check("the half degree is 21.5", half.temp == 21.5);
    check("the codec cannot encode a half degree", !half.codecCanEncode);
    check("the reason is given", half.note.find("half") != std::string::npos);
    uint8_t expected[FRAME_BYTES] = {0};
    check("no frame is produced", !expectedBytes(half, expected));
  }

  // 11. The seven fan points of the remote are understood.
  {
    const Expectation fan = expectationForLabel("cool_f5_22");
    check("a numbered fan point is understood", fan.kind == ExpectKind::State);
    check("the fan point is kept", fan.fan == "f5");
    check("the codec cannot encode it yet", !fan.codecCanEncode);
    check("the reason is given", fan.note.find("fan level") != std::string::npos);
    const Expectation max = expectationForLabel("cool_max_22");
    check("max is the top point", max.fan == "f7");
    const Expectation plain = expectationForLabel("cool_high_22");
    check("a codec fan still works", plain.codecCanEncode);
  }

  // 12. The buttons that send a frame of their own are recognised.
  {
    for (const char* name : {"swing", "timer", "vane", "vaneangle", "soft", "silent",
                              "selfclean", "m", "up", "down"}) {
      const Expectation named = expectationForLabel(name);
      check(std::string(name) + " is a named frame", named.kind == ExpectKind::Named);
    }
  }

  // 9. A wrong temperature nibble is found and explained.
  {
    const uint8_t remote[6] = {0x4D, 0xB2, 0xFC, 0x03, 0x04, 0xF9};
    const Expectation expectation = expectationForLabel("cool_high_21");
    uint8_t expected[FRAME_BYTES] = {0};
    expectedBytes(expectation, expected);
    const std::vector<Difference> differences = compareFrames(remote, expected, expectation.state);
    check("the wrong nibble is found", differences.size() == 1);
    if (differences.size() == 1) {
      check("it is byte 4", differences[0].index == 4);
      check("it is called a temperature nibble", differences[0].what == "temperature nibble");
      check("the suggestion names the table entry",
            differences[0].suggestion.find("TEMP_NIBBLE[4]") != std::string::npos);
      check("the suggestion names the new value",
            differences[0].suggestion.find("0x04") != std::string::npos);
    }
  }

  // 10. A wrong fan byte is found and explained.
  {
    const uint8_t remote[6] = {0x4D, 0xB2, 0xFA, 0x05, 0x06, 0xF9};
    const Expectation expectation = expectationForLabel("cool_high_21");
    uint8_t expected[FRAME_BYTES] = {0};
    expectedBytes(expectation, expected);
    const std::vector<Difference> differences = compareFrames(remote, expected, expectation.state);
    check("the wrong fan byte is found", differences.size() == 2);
    if (differences.size() == 2) {
      check("the fan byte is byte 2", differences[0].index == 2);
      check("it is called the fan byte", differences[0].what == "fan byte");
      check("the inverse fan byte is byte 3", differences[1].index == 3);
    }
  }

  // 11. A checksum that follows from the payload is not a difference of its
  // own, but a wrong checksum rule is.
  {
    const Expectation expectation = expectationForLabel("cool_high_21");
    uint8_t expected[FRAME_BYTES] = {0};
    expectedBytes(expectation, expected);
    // The remote sends another temperature but keeps a correct checksum.
    uint8_t remote[6] = {0x4D, 0xB2, 0xFC, 0x03, 0x04, 0x00};
    remote[5] = midea::checksum(remote);
    const std::vector<Difference> differences = compareFrames(remote, expected, expectation.state);
    check("only the payload is reported", differences.size() == 1);
    if (differences.size() == 1) {
      check("it is the temperature nibble", differences[0].index == 4);
    }
    // A checksum that does not add up is a difference of the formula.
    uint8_t broken[6] = {0x4D, 0xB2, 0xFC, 0x03, 0x04, 0x00};
    const std::vector<Difference> brokenDifferences =
        compareFrames(broken, expected, expectation.state);
    bool mentionsChecksum = false;
    for (const Difference& difference : brokenDifferences) {
      if (difference.index == 5) mentionsChecksum = true;
    }
    check("a wrong checksum is reported", mentionsChecksum);
  }

  // 12. A frame describes the state it carries.
  {
    const uint8_t known[6] = {0x4D, 0xB2, 0xFC, 0x03, 0x06, 0xF9};
    const FrameInfo info = describeFrame(known);
    ac::State state{};
    check("the state is recovered", stateFromFrame(info, state));
    check("the fan is recovered", state.fan == static_cast<uint8_t>(ac::Fan::High));
    check("the mode is recovered", state.mode == static_cast<uint8_t>(ac::Mode::Cool));
    check("the temperature is recovered", state.tempC == 21);
  }

  // 13. A capture with an unknown temperature nibble does not pretend to know.
  {
    const uint8_t remote[6] = {0x4D, 0xB2, 0xFC, 0x03, 0x07, 0xF8};
    const FrameInfo info = describeFrame(remote);
    check("the temperature is unknown", !info.tempKnown);
    ac::State state{};
    check("no state is recovered", !stateFromFrame(info, state));
  }

  // 14. A capture that was cut short is marked as such.
  {
    Capture capture;
    const bool isCapture = parseCaptureLine(
        "cool_high_21 | 4D B2 FC 03 06 F9 | H:4350 L:4400 H:560 | "
        "capture=3 | repeats=0 | at=2026-10-21T12:00:00 | incomplete=1 | pulses=3",
        capture);
    check("an incomplete capture is a capture", isCapture);
    check("the incomplete flag is read", capture.incomplete);
    check("the pulses are read", capture.edges.size() == 3);
    check("no error is reported", capture.error.empty());
  }

  // 15. Dry and fan, as the original remote sends them at 17 C.
  {
    const uint8_t dry[6] = {0x4D, 0xB2, 0xF8, 0x07, 0x20, 0xDF};
    const FrameInfo dryInfo = describeFrame(dry);
    check("the dry frame is a state frame", dryInfo.kind == FrameKind::State);
    check("the dry frame is Dry", dryInfo.modeName == "Dry");
    check("the dry frame uses the auto override fan", dryInfo.fanName == "Auto override");
    check("the dry frame carries 17 C", dryInfo.tempC == 17 && dryInfo.tempKnown);
    check("the dry frame has a valid checksum", dryInfo.checksumOk);

    const uint8_t fan[6] = {0x4D, 0xB2, 0xFD, 0x02, 0x27, 0xD8};
    const FrameInfo fanInfo = describeFrame(fan);
    check("the fan frame is a state frame", fanInfo.kind == FrameKind::State);
    check("the fan frame is Fan", fanInfo.modeName == "Fan");
    check("the fan frame uses the auto fan", fanInfo.fanName == "Auto");
    check("the fan frame has no setpoint", !fanInfo.tempKnown);
    check("the fan frame has a valid checksum", fanInfo.checksumOk);
  }

  // 16. The dry and fan labels expect the frames the remote sends.
  {
    const Expectation dry = expectationForLabel("dry_auto_17");
    check("the dry label is understood", dry.kind == ExpectKind::State);
    check("the dry mode is taken from the label", dry.state.mode == static_cast<uint8_t>(ac::Mode::Dry));
    check("the codec can encode dry", dry.codecCanEncode);
    uint8_t expected[FRAME_BYTES] = {0};
    check("the codec produces the captured dry frame", expectedBytes(dry, expected));
    const uint8_t knownDry[6] = {0x4D, 0xB2, 0xF8, 0x07, 0x20, 0xDF};
    check("the dry frame is the captured one", memcmp(expected, knownDry, 6) == 0);

    const Expectation fan = expectationForLabel("fan_auto_17");
    check("the fan label is understood", fan.kind == ExpectKind::State);
    check("the fan mode is taken from the label", fan.state.mode == static_cast<uint8_t>(ac::Mode::Fan));
    check("the codec can encode fan", fan.codecCanEncode);
    uint8_t fanExpected[FRAME_BYTES] = {0};
    check("the codec produces the captured fan frame", expectedBytes(fan, fanExpected));
    const uint8_t knownFan[6] = {0x4D, 0xB2, 0xFD, 0x02, 0x27, 0xD8};
    check("the fan frame is the captured one", memcmp(fanExpected, knownFan, 6) == 0);
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
