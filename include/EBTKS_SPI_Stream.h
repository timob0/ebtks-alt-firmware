#pragma once
// Generic Teensy-master frame transmit (4 MHz / MODE0 for first prototype).
// Source-specific SD/CRT logic must stay outside this transport layer.
#include <Arduino.h>
#include <SPI.h>
#include "EBTKS_SPI_Test.h"
#include "EBTKS_SPI_Stream_Frame.h"

inline bool ebtksSpiStreamSendFrame(const uint8_t *frame, uint32_t &elapsedUs)
{
  elapsedUs = 0;
  if (!frame) return false;
  ebtksSpiTestInit();
  const uint32_t waitStarted = millis();
  while (!digitalRead(EBTKS_SPI_TEST_READY)) {
    // Keep HP-85 bus interrupts enabled even when ESP32 is slow/late.
    if ((uint32_t)(millis() - waitStarted) > 2000) return false;
  }
  SPI.beginTransaction(SPISettings(4000000UL, MSBFIRST, SPI_MODE0));
  digitalWriteFast(EBTKS_SPI_TEST_CS, LOW);
  delayMicroseconds(3);
  const uint32_t started = micros();
  for (size_t i = 0; i < EbtksStream::FRAME_BYTES; ++i)
    SPI.transfer(frame[i]);
  elapsedUs = (uint32_t)(micros() - started);
  digitalWriteFast(EBTKS_SPI_TEST_CS, HIGH);
  SPI.endTransaction();
  return true;
}
