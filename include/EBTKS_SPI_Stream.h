#pragma once
// Generic Teensy-master framed transport. Source-specific SD/CRT operations
// live in independent producers. 4 MHz mode 0 during this test milestone.
#include <Arduino.h>
#include <SPI.h>
#include "EBTKS_SPI_Test.h"
#include "EBTKS_SPI_Stream_Frame.h"

// Previous version waited only for READY HIGH, which could still be HIGH
// from the PREVIOUS DMA descriptor. A rare immediate next-sector launch
// could then clock SPI before the ESP32 had rearmed its slave DMA.
//
// Observe the previous frame's READY FALLING transition using a hardware
// GPIO interrupt, even if it happens during SD reads. For every frame after
// the first require BOTH: a new FALL since the previous transaction started,
// AND the current READY HIGH from the next queued descriptor.
// A short LOW pulse cannot be missed merely because SD.read() was running.
// The ISR does no SPI, UART, SD access or blocking work.

static volatile uint32_t ebtksSpiStreamReadyFalls = 0;
static bool ebtksSpiStreamReadyIrqInstalled = false;
static uint32_t ebtksSpiStreamPreviousFall = 0;
static bool ebtksSpiStreamRequireNewFall = false;
static uint32_t ebtksSpiStreamReadyWaitUs = 0;
static uint32_t ebtksSpiStreamMaxReadyWaitUs = 0;
static uint8_t ebtksSpiStreamWaitFailure = 0;  // 0=none,1=no fresh fall,2=no ready high

inline void ebtksSpiStreamReadyFallIsr()
{
  ++ebtksSpiStreamReadyFalls;
}

inline void ebtksSpiStreamBeginSession()
{
  ebtksSpiTestInit();
  if (!ebtksSpiStreamReadyIrqInstalled) {
    attachInterrupt(digitalPinToInterrupt(EBTKS_SPI_TEST_READY),
                    ebtksSpiStreamReadyFallIsr, FALLING);
    ebtksSpiStreamReadyIrqInstalled = true;
  }
  ebtksSpiStreamPreviousFall = ebtksSpiStreamReadyFalls;
  ebtksSpiStreamRequireNewFall = false;
  ebtksSpiStreamReadyWaitUs = 0;
  ebtksSpiStreamMaxReadyWaitUs = 0;
  ebtksSpiStreamWaitFailure = 0;
}

inline bool ebtksSpiStreamSendFrame(const uint8_t *frame, uint32_t &elapsedUs)
{
  elapsedUs = 0;
  if (!frame) return false;
  ebtksSpiTestInit();
  const uint32_t started = micros();

  // The first frame is explicitly armed before the stream-send UART command.
  // After that, requiring a fresh FALL rejects the previous DMA's stale HIGH.
  if (ebtksSpiStreamRequireNewFall) {
    while (ebtksSpiStreamReadyFalls == ebtksSpiStreamPreviousFall) {
      if ((uint32_t)(micros() - started) > 2000000UL) {
        ebtksSpiStreamWaitFailure = 1;
        return false;
      }
    }
  }

  while (!digitalReadFast(EBTKS_SPI_TEST_READY)) {
    if ((uint32_t)(micros() - started) > 2000000UL) {
      ebtksSpiStreamWaitFailure = 2;
      return false;
    }
  }
  const uint32_t readyUs = (uint32_t)(micros() - started);
  ebtksSpiStreamReadyWaitUs += readyUs;
  if (readyUs > ebtksSpiStreamMaxReadyWaitUs)
    ebtksSpiStreamMaxReadyWaitUs = readyUs;

  // Capture the counter BEFORE clocking this frame. The DMA post-transfer
  // falling edge may arrive while bytes are still being processed by Teensy.
  ebtksSpiStreamPreviousFall = ebtksSpiStreamReadyFalls;
  ebtksSpiStreamRequireNewFall = true;
  SPI.beginTransaction(SPISettings(4000000UL, MSBFIRST, SPI_MODE0));
  digitalWriteFast(EBTKS_SPI_TEST_CS, LOW);
  delayMicroseconds(3);
  const uint32_t spiStarted = micros();
  for (size_t i = 0; i < EbtksStream::FRAME_BYTES; ++i)
    SPI.transfer(frame[i]);
  elapsedUs = (uint32_t)(micros() - spiStarted);
  digitalWriteFast(EBTKS_SPI_TEST_CS, HIGH);
  SPI.endTransaction();
  return true;
}
