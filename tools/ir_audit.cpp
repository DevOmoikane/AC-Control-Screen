// Compare the frames captured from the original AC remote with what the
// codec produces, and write a report with the changes to make in
// include/MideaRG57Codec.h.
//
//   g++ -std=c++17 -Iinclude tools/ir_audit.cpp -o /tmp/ir_audit
//   /tmp/ir_audit captures/remote-20261021-123456.txt
//
// The report is written next to the capture file, with the suffix
// "-report.md", and printed to stdout.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "IrAudit.h"

namespace {

struct Analysis {
  std::string label;
  std::string error;
  std::string bytesText;      // the bytes the firmware reported
  std::string hostBytes;      // what the host decoder made of the pulses
  uint8_t bytes[iraudit::FRAME_BYTES] = {0};
  bool haveBytes = false;
  bool decoded = false;
  bool incomplete = false;
  int bitCount = 0;
  uint32_t headerMark = 0;
  uint32_t headerSpace = 0;
  bool inverted = false;
  iraudit::FrameInfo info;
  iraudit::TimingInfo timings;
  std::vector<iraudit::TimingDifference> timingDifferences;
  std::string verdict;
  std::string codecBytes;
  bool compared = false;
  std::vector<iraudit::Difference> differences;
};

std::string bytesToString(const uint8_t bytes[iraudit::FRAME_BYTES]) {
  return iraudit::formatBytes(bytes);
}

Analysis analyse(const iraudit::Capture& capture) {
  Analysis analysis;
  analysis.label = capture.label;
  analysis.bytesText = capture.bytesText;
  analysis.incomplete = capture.incomplete;

  if (!capture.error.empty()) {
    analysis.verdict = "could not be read: " + capture.error;
    return analysis;
  }

  // Decode the pulses with the host decoder, so the analysis does not rely
  // on the bytes the firmware reported.
  const irrx::Frame frame =
      irrx::decode(capture.edges.data(), static_cast<int>(capture.edges.size()));
  analysis.bitCount = frame.bitCount;
  analysis.headerMark = frame.headerMarkUs;
  analysis.headerSpace = frame.headerSpaceUs;
  analysis.inverted = frame.inverted;
  if (frame.full) {
    analysis.decoded = true;
    for (int i = 0; i < iraudit::FRAME_BYTES; i++) analysis.bytes[i] = frame.bytes[i];
    analysis.haveBytes = true;
    analysis.hostBytes = bytesToString(analysis.bytes);
    analysis.info = iraudit::describeFrame(analysis.bytes);
  } else {
    analysis.verdict = capture.incomplete
                            ? "incomplete capture, " + std::to_string(capture.edges.size()) +
                                  " pulse(s) arrived"
                            : "not a Midea frame (" + std::to_string(capture.edges.size()) +
                                  " pulses)";
    return analysis;
  }

  if (capture.incomplete) {
    analysis.verdict = "incomplete capture, the frame was cut short";
    analysis.error = "the capture never finished, so it is not compared";
    return analysis;
  }

  // The pulse widths of the remote, measured from the capture.
  analysis.timings = iraudit::measureTimings(capture.edges, frame);
  analysis.timingDifferences = iraudit::compareTimings(analysis.timings);

  if (!capture.bytesText.empty() && capture.bytesText != analysis.hostBytes) {
    analysis.verdict = "the firmware and the host decoder disagree";
    analysis.error = "firmware reported " + capture.bytesText + ", the host decoder got " +
                     analysis.hostBytes;
    return analysis;
  }

  // What the codec produces for the label.
  const iraudit::Expectation expectation = iraudit::expectationForLabel(capture.label);
  uint8_t expected[iraudit::FRAME_BYTES] = {0};
  if (expectation.kind == iraudit::ExpectKind::Named) {
    analysis.verdict = "a frame of its own, the codec has no counterpart";
    analysis.error = expectation.note;
    return analysis;
  }
  if (expectation.kind == iraudit::ExpectKind::State && !expectation.codecCanEncode) {
    // The state was named but the codec cannot express it yet. Comparing it
    // with something else would say nothing about the state that was asked
    // for, so the bytes are reported and the reason is given instead.
    analysis.verdict = "the codec cannot encode this state yet";
    analysis.error = expectation.note;
    return analysis;
  }
  if (expectation.kind != iraudit::ExpectKind::None && expectation.codecCanEncode &&
      iraudit::expectedBytes(expectation, expected)) {
    analysis.compared = true;
    analysis.codecBytes = bytesToString(expected);
    analysis.differences = iraudit::compareFrames(analysis.bytes, expected, expectation.state);
    analysis.verdict = analysis.differences.empty() ? "matches the codec" : "differs from the codec";
    return analysis;
  }

  // No state named by the label: ask the codec for the state the frame
  // itself describes.
  ac::State state{};
  if (iraudit::stateFromFrame(analysis.info, state)) {
    uint8_t codecBytes[iraudit::FRAME_BYTES] = {0};
    if (midea::encodeState(state, codecBytes)) {
      analysis.compared = true;
      analysis.codecBytes = bytesToString(codecBytes);
      analysis.differences = iraudit::compareFrames(analysis.bytes, codecBytes, state);
      analysis.verdict = analysis.differences.empty()
                             ? "matches the codec for the state it describes"
                             : "differs from the codec for the state it describes";
      return analysis;
    }
  }

  analysis.verdict = "no codec comparison possible";
  if (!expectation.note.empty()) analysis.error = expectation.note;
  return analysis;
}

std::string describe(const Analysis& analysis) {
  const iraudit::FrameInfo& info = analysis.info;
  std::ostringstream text;
  text << info.kindName;
  if (info.kind == iraudit::FrameKind::State) {
    // The raw bytes are shown as well, because that is what a frame with a
    // value the codec does not know yet looks like.
    text << ", fan byte 0x" << iraudit::hex2(info.fanByte) << " (" << info.fanName << ")";
    text << ", inverse 0x" << iraudit::hex2(info.invFanByte);
    text << ", mode 0x" << iraudit::hex2(info.modeTempByte & 0xF0) << " (" << info.modeName << ")";
    text << ", temperature nibble 0x" << iraudit::hex2(info.modeTempByte & 0x0F);
    if (info.tempKnown) {
      text << " (" << info.tempC << " C)";
    } else {
      text << " (not in the codec's table)";
    }
  } else if (info.kind == iraudit::FrameKind::Toggle) {
    text << ", " << info.fanName;
    text << ", command byte 0x" << iraudit::hex2(info.bytes[4]);
  }
  text << ", checksum " << (info.checksumOk ? "ok" : "wrong");
  return text.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: ir_audit <capture file>\n";
    return 2;
  }
  const std::string path = argv[1];
  std::ifstream file(path);
  if (!file) {
    std::cerr << "cannot read " << path << "\n";
    return 1;
  }

  std::vector<Analysis> analyses;
  std::string line;
  while (std::getline(file, line)) {
    iraudit::Capture capture;
    if (!iraudit::parseCaptureLine(line, capture)) continue;
    analyses.push_back(analyse(capture));
  }

  std::set<std::string> suggestions;
  std::set<std::string> timingSuggestions;
  bool anyTimings = false;
  uint32_t remoteHeaderMark = 0;
  uint32_t remoteHeaderSpace = 0;
  uint32_t remoteMark = 0;
  uint32_t remoteSpaceZero = 0;
  uint32_t remoteSpaceOne = 0;
  uint32_t remoteGap = 0;
  for (const Analysis& analysis : analyses) {
    for (const iraudit::Difference& difference : analysis.differences) {
      suggestions.insert(difference.suggestion);
    }
    for (const iraudit::TimingDifference& difference : analysis.timingDifferences) {
      timingSuggestions.insert(difference.suggestion);
    }
    if (!analysis.timings.measured) continue;
    if (!anyTimings) {
      remoteHeaderMark = analysis.timings.headerMarkUs;
      remoteHeaderSpace = analysis.timings.headerSpaceUs;
      remoteMark = analysis.timings.markUs;
      remoteSpaceZero = analysis.timings.spaceZeroUs;
      remoteSpaceOne = analysis.timings.spaceOneUs;
      remoteGap = analysis.timings.frameGapUs;
      anyTimings = true;
    }
  }

  std::ostringstream report;
  report << "# AC remote capture audit\n\n";
  report << "- captures: `" << path << "`\n";
  report << "- analysed: " << analyses.size() << " capture(s)\n\n";

  report << "## Verdict\n\n";
  report << "| label | remote bytes | verdict |\n";
  report << "| --- | --- | --- |\n";
  for (const Analysis& analysis : analyses) {
    report << "| " << analysis.label << " | `"
           << (analysis.bytesText.empty() ? "-" : analysis.bytesText) << "` | "
           << analysis.verdict << " |\n";
  }
  report << "\n";

  report << "## Changes to make in include/MideaRG57Codec.h\n\n";
  if (suggestions.empty() && timingSuggestions.empty()) {
    size_t compared = 0;
    for (const Analysis& analysis : analyses) {
      if (analysis.compared) compared++;
    }
    report << "None.\n";
    if (compared == 0) {
      report << "No capture could be compared with the codec, so there is nothing to say.\n\n";
    } else {
      report << "Every capture the codec can be compared with matches it.\n\n";
    }
  } else {
    for (const std::string& suggestion : suggestions) {
      report << "- " << suggestion << "\n";
    }
    for (const std::string& suggestion : timingSuggestions) {
      report << "- " << suggestion << "\n";
    }
    report << "\n";
  }

  report << "## Pulse timings\n\n";
  if (!anyTimings) {
    report << "No capture could be measured.\n\n";
  } else {
    report << "What the remote sends and what the transmitter sends:\n\n";
    report << "| what | the remote | the transmitter |\n";
    report << "| --- | --- | --- |\n";
    report << "| header mark | " << remoteHeaderMark << " us | " << midea::HEADER_MARK_US << " us |\n";
    report << "| header space | " << remoteHeaderSpace << " us | " << midea::HEADER_SPACE_US << " us |\n";
    report << "| mark | " << remoteMark << " us | " << midea::MARK_US << " us |\n";
    report << "| space of a zero | " << remoteSpaceZero << " us | " << midea::SPACE_ZERO_US << " us |\n";
    report << "| space of a one | " << remoteSpaceOne << " us | " << midea::SPACE_ONE_US << " us |\n";
    report << "| gap between the copies | " << remoteGap << " us | " << midea::FRAME_GAP_US << " us |\n\n";
  }

  report << "## Per capture\n\n";
  for (const Analysis& analysis : analyses) {
    report << "### " << analysis.label << "\n\n";
    report << "- remote bytes: `" << (analysis.bytesText.empty() ? "-" : analysis.bytesText) << "`\n";
    if (analysis.decoded) {
      report << "- decoded: " << describe(analysis) << "\n";
      report << "- header: " << analysis.headerMark << "/" << analysis.headerSpace << " us, "
             << analysis.bitCount << " bits"
             << (analysis.inverted ? ", inverted" : "") << "\n";
    }
    if (!analysis.codecBytes.empty()) {
      report << "- codec produces: `" << analysis.codecBytes << "`\n";
    }
    report << "- verdict: " << analysis.verdict << "\n";
    if (!analysis.error.empty()) {
      report << "- note: " << analysis.error << "\n";
    }
    for (const iraudit::Difference& difference : analysis.differences) {
      report << "- byte " << difference.index << " (" << difference.what << "): remote 0x"
             << iraudit::hex2(difference.remote) << ", codec 0x"
             << iraudit::hex2(difference.codec) << "\n";
      report << "  - " << difference.suggestion << "\n";
    }
    report << "\n";
  }

  const std::string reportPath = path + "-report.md";
  std::ofstream out(reportPath);
  if (!out) {
    std::cerr << "cannot write " << reportPath << "\n";
    return 1;
  }
  out << report.str();
  out.close();

  std::cout << report.str();
  std::cout << "report written to " << reportPath << "\n";
  return 0;
}
