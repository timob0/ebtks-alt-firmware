#pragma once
// Generic read-only framed SPI diagnostic, Teensy 4.1 master (4 MHz MODE0).
// CRITICAL EBTKS COMPATIBILITY: NEVER attach a GPIO interrupt here.
// The HP-85 bus is serviced by the custom IRQ_GPIO6789 / pinChange_isr;
// Arduino attachInterrupt() on the READY GPIO disrupts that shared IRQ and
// can lock up the HP-85. READY is polled only, with interrupts untouched.
#include <Arduino.h>
#include <SPI.h>
#include "EBTKS_SPI_Test.h"
#include "EBTKS_SPI_Stream_Frame.h"

static uint32_t ebtksSpiStreamReadyLowPhases = 0;
static uint32_t ebtksSpiStreamReadyWaitUs = 0;
static uint32_t ebtksSpiStreamMaxReadyWaitUs = 0;
static uint8_t ebtksSpiStreamWaitFailure = 0;
// 0 = none, 1 = previous frame's READY did not become LOW,
// 2 = next queued descriptor did not assert READY HIGH.
// These counters belong to the diagnostic only, not HP bus interrupt state.

inline void ebtksSpiStreamBeginSession()
{
  ebtksSpiTestInit();
  ebtksSpiStreamReadyLowPhases = 0;
  ebtksSpiStreamReadyWaitUs = 0;
  ebtksSpiStreamMaxReadyWaitUs = 0;
  ebtksSpiStreamWaitFailure = 0;
}

// A READY HIGH is accepted only after the previous SPI frame was observed
// to finish (READY LOW) by the non-interrupt polling below. ESP deliberately
// holds LOW for several milliseconds before rearming the next RX descriptor,
// so the LOW state is never an instantaneous pulse that SD work could miss.
// Calling this sends one 528-byte frame, then waits for READY LOW only when
// another frame remains. The waiting loop keeps HP-85 bus IRQs enabled.
inline bool ebtksSpiStreamSendFrame(const uint8_t *frame, uint32_t &elapsedUs,
                                    bool expectNextFrame)
{
  elapsedUs = 0;
  if (!frame) return false;
  ebtksSpiTestInit();

  uint32_t waitStart = micros();
  while (!digitalReadFast(EBTKS_SPI_TEST_READY)) {
    if ((uint32_t)(micros() - waitStart) > 2000000UL) {
      ebtksSpiStreamWaitFailure = 2;
      return false;
    }
  }
  uint32_t waitedUs = (uint32_t)(micros() - waitStart);
  ebtksSpiStreamReadyWaitUs += waitedUs;
  if (waitedUs > ebtksSpiStreamMaxReadyWaitUs)
    ebtksSpiStreamMaxReadyWaitUs = waitedUs;

  SPI.beginTransaction(SPISettings(4000000UL, MSBFIRST, SPI_MODE0));
  digitalWriteFast(EBTKS_SPI_TEST_CS, LOW);
  delayMicroseconds(3);
  const uint32_t started = micros();
  for (size_t i = 0; i < EbtksStream::FRAME_BYTES; ++i)
    SPI.transfer(frame[i]);
  elapsedUs = (uint32_t)(micros() - started);
  digitalWriteFast(EBTKS_SPI_TEST_CS, HIGH);
  SPI.endTransaction();

  // Do this IMMEDIATELY after releasing CS, BEFORE another SD.read(), so
  // Teensy will observe the explicit idle LOW interval imposed by ESP32.
  // For the final frame, no further descriptor is expected or awaited.
  if (expectNextFrame) {
    waitStart = micros();
    while (digitalReadFast(EBTKS_SPI_TEST_READY)) {
      if ((uint32_t)(micros() - waitStart) > 2000000UL) {
        ebtksSpiStreamWaitFailure = 1;
        return false;
      }
    }
    ++ebtksSpiStreamReadyLowPhases;
    waitedUs = (uint32_t)(micros() - waitStart);
    ebtksSpiStreamReadyWaitUs += waitedUs;
    if (waitedUs > ebtksSpiStreamMaxReadyWaitUs)
      ebtksSpiStreamMaxReadyWaitUs = waitedUs;
  }
  return true;
}
