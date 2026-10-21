// Host test for the recorded captures of the original remote.
//
// Every capture in captures/ has to decode to a full frame with a valid
// checksum, and both halves of a capture have to be the same frame. The
// test skips itself when there are no captures.
//
//   g++ -std=c++17 -I include tests/test_ir_captures.cpp -o /tmp/t && /tmp/t

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <vector>

#include "IrAudit.h"

static int failures = 0;

static void check(const std::string& what, bool ok) {
  printf("%-64s %s\n", what.c_str(), ok ? "PASS" : "FAIL");
  if (!ok) failures++;
}

static std::vector<std::string> captureFiles() {
  std::vector<std::string> paths;
  DIR* directory = opendir("captures");
  if (!directory) return paths;
  while (struct dirent* entry = readdir(directory)) {
    const std::string name = entry->d_name;
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0) {
      paths.push_back("captures/" + name);
    }
  }
  closedir(directory);
  return paths;
}

int main() {
  const std::vector<std::string> files = captureFiles();
  if (files.empty()) {
    printf("no captures found, nothing to check\n");
    return 0;
  }

  int total = 0;
  int decoded = 0;
  int checksumOk = 0;
  int halvesEqual = 0;
  int onlyOneHalf = 0;
  for (const std::string& path : files) {
    std::ifstream file(path);
    if (!file) continue;
    std::string line;
    while (std::getline(file, line)) {
      iraudit::Capture capture;
      if (!parseCaptureLine(line, capture)) continue;
      total++;
      const irrx::Frame frame =
          irrx::decode(capture.edges.data(), static_cast<int>(capture.edges.size()));
      if (!frame.full) {
        check(path + ": " + capture.label + " decodes to a full frame", false);
        continue;
      }
      decoded++;
      if (midea::checksum(frame.bytes) == frame.bytes[5]) checksumOk++;
      else check(path + ": " + capture.label + " has a valid checksum", false);

      // The remote sends the frame twice. Both halves have to be the same,
      // but only when the capture holds both of them.
      const std::vector<irrx::Edge> edges = capture.edges;
      const uint8_t markLevel = frame.inverted ? 0 : 1;
      std::vector<int> headers;
      for (int i = 0; i + 1 < static_cast<int>(edges.size()); i++) {
        if (edges[i].level == markLevel && edges[i].us >= 2000 &&
            edges[i + 1].level != markLevel && edges[i + 1].us >= 2000) {
          headers.push_back(i);
        }
      }
      if (headers.size() < 2 || capture.edges.size() < 190) {
        onlyOneHalf++;  // only one half was captured
        continue;
      }
      int second = headers[1];
      uint8_t secondBytes[iraudit::FRAME_BYTES] = {0};
      int bit = 0;
      int i = second + 2;
      while (i + 1 < static_cast<int>(edges.size()) && bit < 48) {
        if (edges[i].level != markLevel || edges[i].us >= 2000) break;
        const bool one = edges[i + 1].us >= 1200;
        if (one) secondBytes[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
        bit++;
        i += 2;
      }
      if (bit == 48 && memcmp(secondBytes, frame.bytes, 6) == 0) halvesEqual++;
      else
        check(path + ": " + capture.label + " sends the same frame twice", false);
    }
  }

  check("every capture decodes to a full frame", decoded == total);
  check("every capture has a valid checksum", checksumOk == total);
  check("both halves of a capture are the same frame", halvesEqual + onlyOneHalf == total);
  printf("\n%d capture(s) in %d file(s)\n", total, static_cast<int>(files.size()));

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
