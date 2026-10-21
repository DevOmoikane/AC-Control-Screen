// Host test for the pulse ring buffer.
//
// The interrupt writes and the main loop reads. The buffer has to wrap
// around, otherwise the receiver goes quiet after a few frames.
//
//   g++ -std=c++17 -I include tests/test_ir_ring.cpp -o /tmp/t && /tmp/t

#include <cstdio>
#include <vector>

#include "IrReceiver.h"

using namespace irrx;

static int failures = 0;

static void check(const char* what, bool ok) {
  printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
  if (!ok) failures++;
}

int main() {
  // 1. The pulses come out in the order they went in.
  {
    EdgeRing ring;
    for (int i = 0; i < 10; i++) ring.write(1, 100 + i);
    bool ok = true;
    for (int i = 0; i < 10; i++) {
      Edge edge{};
      if (!ring.read(edge) || edge.us != static_cast<uint32_t>(100 + i) || edge.level != 1) ok = false;
    }
    check("the pulses come out in order", ok);
    Edge edge{};
    check("nothing is left afterwards", !ring.read(edge));
  }

  // 2. The buffer wraps around and keeps working. This is the case that
  // broke: a frame is about a hundred pulses, so after a few frames the
  // buffer was full for good.
  {
    EdgeRing ring;
    const int frames = 50;
    const int pulsesPerFrame = 100;
    for (int frame = 0; frame < frames; frame++) {
      for (int i = 0; i < pulsesPerFrame; i++) {
        ring.write(i % 2, 560);
      }
      // The main loop reads the frame right away.
      int read = 0;
      Edge edge{};
      while (ring.read(edge)) read++;
      if (read != pulsesPerFrame) {
        check("every frame is read whole", false);
        break;
      }
    }
    check("every frame is read whole", true);
    check("nothing was dropped", ring.dropped() == 0);
    check("more pulses than the buffer holds went through", ring.written() == frames * pulsesPerFrame);
  }

  // 3. A reader that falls behind loses the oldest pulses, and that is
  // counted instead of going wrong silently.
  {
    EdgeRing ring;
    for (int i = 0; i < EdgeRing::CAPACITY + 10; i++) ring.write(1, 500);
    Edge edge{};
    int read = 0;
    while (ring.read(edge)) read++;
    check("only the newest pulses are left", read == EdgeRing::CAPACITY);
    check("the loss is counted", ring.dropped() == 10);
  }

  // 4. A frame that is read long after it arrived is still whole.
  {
    EdgeRing ring;
    for (int i = 0; i < 100; i++) ring.write(i % 2, 560 + i);
    Edge edge{};
    bool ok = true;
    for (int i = 0; i < 100; i++) {
      if (!ring.read(edge) || edge.us != static_cast<uint32_t>(560 + i)) ok = false;
    }
    check("a late frame is still whole", ok);
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
