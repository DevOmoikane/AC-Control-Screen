#pragma once
#include <cstdint>

// IR receiver for capturing the pulses of a remote control.
//
// The receiver module (TSOP38238 / VS1838B style) demodulates the 38 kHz
// carrier, so its output is a clean digital waveform: one level for a "mark"
// and the other for a "space". This module measures the width of every pulse
// and reports what it saw.
//
// It deliberately assumes no protocol: the raw trace plus a histogram of the
// pulse widths is what identifies an unknown encoding.
//
// Everything is captured in a GPIO interrupt, so the pulse widths do not
// depend on polling jitter. The analysis happens in poll(), which must be
// called regularly from the main loop.

namespace irrx {

// One measured pulse. `level` is the logic level of the pulse, `us` its width.
struct Edge {
  uint8_t level;
  uint32_t us;
};

// A ring buffer of pulses.
//
// The interrupt writes and the main loop reads, so the buffer has to wrap
// around. A frame is about a hundred pulses, so a buffer that is not a ring
// runs out after a few frames and the receiver goes quiet for good.
//
// When the reader falls behind by more than the capacity, the oldest pulses
// are gone. They are counted, so that shows up in the log instead of
// silently going wrong.
class EdgeRing {
 public:
  static constexpr int CAPACITY = 512;

  // Called from the interrupt. Never blocks.
  void write(uint8_t level, uint32_t us) {
    buffer_[writeIndex_ % CAPACITY].level = level;
    buffer_[writeIndex_ % CAPACITY].us = us;
    writeIndex_++;
  }

  // Called from the main loop. False when there is nothing to read.
  bool read(Edge& out) {
    if (writeIndex_ - readIndex_ > CAPACITY) {
      const uint32_t lost = writeIndex_ - readIndex_ - CAPACITY;
      dropped_ += lost;
      readIndex_ += lost;
    }
    if (readIndex_ >= writeIndex_) return false;
    out.level = buffer_[readIndex_ % CAPACITY].level;
    out.us = buffer_[readIndex_ % CAPACITY].us;
    readIndex_++;
    return true;
  }

  uint32_t written() const { return writeIndex_; }
  uint32_t dropped() const { return dropped_; }
  uint32_t pending() const { return writeIndex_ - readIndex_; }

 private:
  volatile Edge buffer_[CAPACITY];
  volatile uint32_t writeIndex_ = 0;
  volatile uint32_t readIndex_ = 0;
  volatile uint32_t dropped_ = 0;
};

// A real Midea pulse is 560 us (mark) or 560/1690 us (space). Anything
// shorter is contact noise or a glitch on a shaky connection.
constexpr uint32_t MIN_PULSE_US = 200;

// Start capturing on the given GPIO. Returns false if the pin could not be
// set up.
bool begin(int gpio);

// Analyse whatever has been captured since the last call. Cheap to call
// often; it only prints when there is something to report.
void poll();

// Number of bursts reported since boot.
uint32_t frameCount();

// Diagnostics: how many interrupts the input pin has produced, how many were
// long enough to be a real pulse, and the level the pin is at right now.
uint32_t interruptCount();
uint32_t pulseCount();
uint32_t glitchCount();
// Pulses that were lost because the reader did not keep up with the
// interrupt. This must stay at zero.
uint32_t droppedCount();
uint8_t currentLevel();

}  // namespace irrx
