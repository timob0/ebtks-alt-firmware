#include <Arduino.h>
#include <stdio.h>
#include "Inc_Common_Headers.h"
#include "EBTKS_CPM85_Rmidle_Probe.h"

#if EBTKS_CPM85_RMIDLE_PROBE_ENABLE
// Deliberately no dynamic allocations, timers, ESP32 writes or blocking
// HP-IB calls. The only asynchronous event is the existing AUXROM
// RMIDLE callback. CRT DMA is performed only in the Teensy task loop.
extern bool Is8687;

namespace Cpm85RmidleProbe {
static volatile uint8_t active = 0;
static volatile uint8_t page = 1;
static volatile uint8_t mode = 0;
static volatile uint8_t generation = 0;
static volatile uint32_t rmidleCount = 0;

// All these fields are task-loop-only.
static uint8_t seenGeneration = 0;
static bool wasDrawing = false;
static uint32_t lastDrawMs = 0;
static uint32_t lastRmidleCount = 0;
static uint32_t lastRmidleChangeMs = 0;
static uint8_t lastPage = 0;
static uint8_t lastMode = 255;

void onControl(uint8_t value) {
  if (value == 2) {
    page = 1;
    mode = 0;
    rmidleCount = 0;
    active = 1;
    ++generation;
  } else if (value == 0 || value == 1) {
    active = 0;
  }
}

bool onKey(uint8_t key) {
  if (!active) return false;
  if (key == 0200) page = 1;           // K1
  else if (key == 0201) page = 2;      // K2
  else if (key == 0202) page = 3;      // K3
  else if (key == 0203) mode ^= 1;     // K4 UI / Control demo
  // K8 is handled on the HP side BEFORE reaching this function.
  // All remaining keystrokes are consumed locally, never forwarded.
  return true;
}

void onRmidle() {
  if (active) ++rmidleCount; // regular AUXROM task context, no DMA here
}

static void drawRow(uint16_t row, const char* str) {
  Write_on_CRT_Alpha(row, 0, str); // existing task-side EBTKS DMA writer
}

void service() {
  const uint32_t now = systick_millis_count;
  const uint8_t gen = generation;
  if (!active) {
    if (wasDrawing) {
      wasDrawing = false;
      // CRT_capture_screen/CRT_restore_screen use a single global
      // scratch buffer. Restore once after normal K8/CPOFF exit.
      CRT_restore_screen();
    }
    return;
  }
  // No unsupported 86/87 paths and no screen emulation disabled.
  if (Is8687 || !get_screenEmu()) return;
  if (!wasDrawing || gen != seenGeneration) {
    // This snapshot must run before the first display update.
    // The existing HP BASIC program in memory is untouched.
    CRT_capture_screen();
    wasDrawing = true;
    seenGeneration = gen;
    lastDrawMs = now - 250;
    lastRmidleCount = rmidleCount;
    lastRmidleChangeMs = now;
    lastPage = 0;
    lastMode = 255;
    drawRow(1, "C85MENU - RMIDLE TEST         ");
    drawRow(4, "K1-3 PAGES K4 MODE K8 EXIT    ");
    drawRow(6, "PRINTER/LST: DISABLED         ");
  }

  const uint32_t count = rmidleCount;
  if (count != lastRmidleCount) {
    lastRmidleCount = count;
    lastRmidleChangeMs = now;
  }
  const uint8_t selectedPage = page;
  const uint8_t selectedMode = mode;
  const bool changed = selectedPage != lastPage || selectedMode != lastMode;
  if (!changed && (uint32_t)(now - lastDrawMs) < 250) return;
  lastDrawMs = now;

  if (changed) {
    lastPage = selectedPage;
    lastMode = selectedMode;
    drawRow(2, selectedMode ? "MODE: CP/M CONTROL (NO KEYS)    "
                            : "MODE: MANAGEMENT               ");
    drawRow(3, selectedPage == 1 ? "PAGE: DRIVES                   "
                : selectedPage == 2 ? "PAGE: DEVICES                  "
                                    : "PAGE: SYSTEM                   ");
  }

  // Distinguish live HP RMIDLE callbacks from a free-running Teensy
  // millis() timer: stale RMIDLE count is visibly flagged after 2 s.
  char counterLine[33];
  const bool stale = (uint32_t)(now - lastRmidleChangeMs) > 2000;
  snprintf(counterLine, sizeof(counterLine), "RMIDLE: %10lu %-8s",
           (unsigned long)count, stale ? "STALLED" : "ACTIVE");
  drawRow(5, counterLine);
}
} // namespace Cpm85RmidleProbe
#endif
