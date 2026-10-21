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
uint8_t currentLevel();

}  // namespace irrx
