#pragma once
// CN3 SPI link bring-up: Teensy 4.1 SPI0 MASTER -> ESP32 SPI2 SLAVE.
// Experimental diagnostic ONLY; invoked via JSON {"spi_probe":{...}}.
// Does not access SD, CP/M drives, or the HP bus. SPI clocks only on demand.
// CN3: T11=33 MOSI, T12=29 MISO, T13=24 SCK, T26=30 CS_N,
//       T06=26 ESP_READY input. CS_N external pullup is on ESP32 GPIO35.
#include <Arduino.h>
#include <SPI.h>

static constexpr uint8_t EBTKS_SPI_TEST_CS = 26;    // Teensy T26
static constexpr uint8_t EBTKS_SPI_TEST_READY = 6; // Teensy T06
static constexpr uint16_t EBTKS_SPI_TEST_SIZE = 512;
static constexpr uint32_t EBTKS_SPI_TEST_HZ = 1000000; // conservative bring-up
static bool ebtksSpiTestInitialized = false;

inline uint16_t ebtksSpiTestCrcByte(uint16_t crc, uint8_t value)
{
  crc ^= value;
  for (unsigned j = 0; j < 8; ++j)
    crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
  return crc;
}

inline void ebtksSpiTestInit()
{
  if (ebtksSpiTestInitialized) return;
  // Preload HIGH before enabling CS output: inactive unless a test runs.
  digitalWrite(EBTKS_SPI_TEST_CS, HIGH);
  pinMode(EBTKS_SPI_TEST_CS, OUTPUT);
  pinMode(EBTKS_SPI_TEST_READY, INPUT_PULLDOWN);
  SPI.begin(); // Hardware SPI0, standard pins 11 / 12 / 13.
  ebtksSpiTestInitialized = true;
}

// Call from the existing ESPComms JSON handler, not from a bus ISR.
// Return false if ESP32 does not signal readiness; never toggle CS then.
inline bool ebtksSpiTestRead(uint8_t sequence, uint16_t &crc, uint32_t &elapsedUs)
{
  ebtksSpiTestInit();
  crc = 0xFFFF;
  elapsedUs = 0;

  const uint32_t startWait = millis();
  while (!digitalRead(EBTKS_SPI_TEST_READY)) {
    // Interrupts stay enabled: HP bus ISRs can execute. The polling loop
    // cannot service non-interrupt background work during this short wait.
    if ((uint32_t)(millis() - startWait) > 500) return false;
  }

  SPI.beginTransaction(SPISettings(EBTKS_SPI_TEST_HZ, MSBFIRST, SPI_MODE0));
  digitalWriteFast(EBTKS_SPI_TEST_CS, LOW);
  delayMicroseconds(3);
  const uint32_t startUs = micros();
  for (uint16_t i = 0; i < EBTKS_SPI_TEST_SIZE; ++i) {
    const uint8_t value = (uint8_t)(0xA5U ^ (uint8_t)(i * 37U + sequence));
    SPI.transfer(value); // ESP32 queues receive-only slave DMA transaction.
    crc = ebtksSpiTestCrcByte(crc, value);
  }
  elapsedUs = (uint32_t)(micros() - startUs);
  digitalWriteFast(EBTKS_SPI_TEST_CS, HIGH);
  SPI.endTransaction();
  return true;
}
