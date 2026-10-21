#include "IrReceiver.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_timer.h>

#include <cstdio>

#include "AcConfig.h"
#include "IrReceiver.h"

namespace irrx {
namespace {

// Silence longer than this ends a burst of pulses.
constexpr uint32_t BURST_GAP_US = 20000;
// Pulses at least this long delimit a frame and are called out separately.
constexpr uint32_t LONG_PULSE_US = 1500;

constexpr int MAX_EDGES = 512;

volatile Edge g_edges[MAX_EDGES];
volatile uint32_t g_edgeCount = 0;  // total edges ever seen
volatile uint32_t g_lastEdgeUs = 0;
volatile uint8_t g_lastLevel = 0;
volatile bool g_started = false;
volatile uint32_t g_isrCount = 0;   // every interrupt, even filtered ones
volatile uint32_t g_edgeTotal = 0;  // pulses that passed the glitch filter
volatile uint32_t g_glitchCount = 0; // edges too short to be real pulses
volatile uint8_t g_lastSeenLevel = 0;

int g_gpio = -1;
uint32_t g_frames = 0;

// Buffers for the analysis.
Edge g_burst[MAX_EDGES];
int g_burstCount = 0;
uint32_t g_burstLastUs = 0;

void isr(void* arg) {
  (void)arg;
  const uint32_t now = esp_timer_get_time();
  const uint8_t level = gpio_get_level(static_cast<gpio_num_t>(g_gpio)) ? 1 : 0;

  g_isrCount++;
  g_lastSeenLevel = level;
  if (g_started) {
    const uint32_t dt = now - g_lastEdgeUs;
    // Too short to be a real pulse: ignore it and keep the previous edge as
    // the timing reference, so the noise is absorbed into the next pulse
    // instead of shortening it.
    if (dt >= MIN_PULSE_US && g_edgeCount < MAX_EDGES) {
      g_edges[g_edgeCount].level = g_lastLevel;
      g_edges[g_edgeCount].us = dt;
      g_edgeCount++;
      g_edgeTotal++;
    } else {
      g_glitchCount++;
    }
    g_lastLevel = level;
  }
  g_lastEdgeUs = now;
}

// Summarise a burst without assuming any protocol: a histogram of the pulse
// widths is what identifies an unknown encoding, because a mark/space scheme
// shows up as a small set of distinct widths.
void summariseBurst() {
  // Histogram of the pulse widths, per level, rounded to 10 us.
  struct Bucket { uint32_t width; uint8_t level; uint32_t count; };
  Bucket buckets[64];
  int bucketCount = 0;

  for (int i = 0; i < g_burstCount; i++) {
    const uint32_t w = (g_burst[i].us / 10) * 10;
    int found = -1;
    for (int b = 0; b < bucketCount; b++) {
      if (buckets[b].width == w && buckets[b].level == g_burst[i].level) { found = b; break; }
    }
    if (found < 0) {
      if (bucketCount < (int)(sizeof(buckets) / sizeof(buckets[0]))) {
        buckets[bucketCount].width = w;
        buckets[bucketCount].level = g_burst[i].level;
        buckets[bucketCount].count = 1;
        bucketCount++;
      }
    } else {
      buckets[found].count++;
    }
  }

  // Sort by width so the distinct pulse lengths line up.
  for (int i = 1; i < bucketCount; i++) {
    Bucket key = buckets[i];
    int j = i - 1;
    while (j >= 0 && buckets[j].width > key.width) { buckets[j + 1] = buckets[j]; j--; }
    buckets[j + 1] = key;
  }

  Serial.printf("[ir rx] pulse width histogram (level:width us x count):\n");
  for (int b = 0; b < bucketCount; b++) {
    Serial.printf("[ir rx]   %c:%-6u x %lu\n", buckets[b].level ? 'H' : 'L',
                  buckets[b].width, (unsigned long)buckets[b].count);
  }

  // The long pulses delimit the frame; list them in order.
  Serial.printf("[ir rx] long pulses (>= %u us):", LONG_PULSE_US);
  int longs = 0;
  for (int i = 0; i < g_burstCount; i++) {
    if (g_burst[i].us >= LONG_PULSE_US) {
      Serial.printf(" %c:%u", g_burst[i].level ? 'H' : 'L', g_burst[i].us);
      longs++;
    }
  }
  if (!longs) Serial.printf(" none");
  Serial.printf("\n");
  Serial.printf("[ir rx] %d edges, %d distinct widths, %d long pulses\n",
                g_burstCount, bucketCount, longs);
}

// Print the raw timings of one burst. No protocol is assumed: the raw trace
// plus the width histogram is what identifies an unknown encoding.
void analyseBurst() {
  Serial.printf("[ir rx] burst of %d edges\n", g_burstCount);
  Serial.printf("[ir rx] raw (level:width us):\n");
  for (int i = 0; i < g_burstCount; i++) {
    if ((i & 7) == 0) Serial.printf("[ir rx]  ");
    Serial.printf("%c:%-5u", g_burst[i].level ? 'H' : 'L', g_burst[i].us);
    Serial.printf(((i & 7) == 7 || i == g_burstCount - 1) ? "\n" : "  ");
  }
  summariseBurst();
  g_frames++;
}

}  // namespace

bool begin(int gpio) {
  g_gpio = gpio;
  // The pull has to match the module: a push-pull output needs none, an
  // open-collector output needs a pull-up. A pull-down on an open-collector
  // output holds the line low forever, and no edge would ever arrive.
  if (IR_RX_PULL == 1) {
    pinMode(g_gpio, INPUT_PULLUP);
  } else if (IR_RX_PULL == 2) {
    pinMode(g_gpio, INPUT_PULLDOWN);
  } else {
    pinMode(g_gpio, INPUT);
  }

  g_started = false;
  g_lastEdgeUs = esp_timer_get_time();
  g_lastLevel = gpio_get_level(static_cast<gpio_num_t>(g_gpio)) ? 1 : 0;

  // The GPIO ISR service is installed lazily by the Arduino core, the first
  // time attachInterrupt() is used. Nothing else in this firmware uses it, so
  // install it here. A failure here is not fatal on its own: the service may
  // already be there, which is reported as ESP_ERR_INVALID_STATE.
  const esp_err_t svc = gpio_install_isr_service((int)ARDUINO_ISR_FLAG);
  if (svc != ESP_OK && svc != ESP_ERR_INVALID_STATE) {
    Serial.printf("[ir rx] GPIO ISR service not installed: %d\\n", (int)svc);
  }

  // The ESP32-C3's GPIO driver has the legacy three argument form and
  // triggers on any edge set through gpio_set_intr_type().
  gpio_set_intr_type(static_cast<gpio_num_t>(g_gpio), GPIO_INTR_ANYEDGE);
  const esp_err_t err = gpio_isr_handler_add(static_cast<gpio_num_t>(g_gpio), isr, nullptr);
  if (err != ESP_OK) {
    Serial.printf("[ir rx] cannot install ISR on GPIO%d: %d\\n", g_gpio, (int)err);
    return false;
  }
  g_started = true;
  Serial.printf("[ir rx] listening on GPIO%d, idle level %s\n", g_gpio,
                g_lastLevel ? "HIGH" : "LOW");
  Serial.printf("[ir rx] if nothing is ever printed, check the module's VCC and "
                "GND, and that its OUT really reaches GPIO%d\n", g_gpio);
  return true;
}

void poll() {
  if (!g_started) return;

  static uint32_t consumed = 0;
  const uint32_t total = g_edgeCount;
  while (consumed < total) {
    Edge e;
    e.level = g_edges[consumed % MAX_EDGES].level;
    e.us = g_edges[consumed % MAX_EDGES].us;
    consumed++;

    // A long silence ends the burst.
    if (g_burstCount > 0 && g_lastEdgeUs - g_burstLastUs > BURST_GAP_US) {
      analyseBurst();
      g_burstCount = 0;
    }
    g_burst[g_burstCount++] = e;
    g_burstLastUs = g_lastEdgeUs;
    if (g_burstCount >= MAX_EDGES) {
      analyseBurst();
      g_burstCount = 0;
    }
  }

  // A burst that has gone quiet is finished.
  if (g_burstCount > 0 && esp_timer_get_time() - g_burstLastUs > BURST_GAP_US) {
    analyseBurst();
    g_burstCount = 0;
  }
}

uint32_t frameCount() { return g_frames; }

// Number of interrupts seen on the input pin, and how many of them were
// long enough to be a real pulse.
uint32_t interruptCount() { return g_isrCount; }
uint32_t pulseCount() { return g_edgeTotal; }
uint32_t glitchCount() { return g_glitchCount; }
uint8_t currentLevel() { return g_lastSeenLevel; }

}  // namespace irrx
