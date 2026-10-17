#pragma once
// CN3 SPI stress diagnostic ONLY; Teensy 4.1 SPI0 MASTER -> ESP32 SPI2 SLAVE.
// 512 bytes/transaction, no SD or CP/M disk access, interrupts left enabled.
// CN3: T11=33 MOSI, T12=29 MISO, T13=24 SCK, T26=30 CS_N,
//       T06=26 ESP_READY input. CS_N has an external pullup at ESP GPIO32.
#include <Arduino.h>
#include <SPI.h>

static constexpr uint8_t EBTKS_SPI_TEST_CS = 26;
static constexpr uint8_t EBTKS_SPI_TEST_READY = 6;
static constexpr uint16_t EBTKS_SPI_TEST_SIZE = 512;
static bool ebtksSpiTestInitialized = false;

inline bool ebtksSpiTestValidHz(uint32_t hz)
{
  return hz == 1000000UL || hz == 2000000UL ||
         hz == 4000000UL || hz == 8000000UL;
}

// The same byte generator is implemented independently at the ESP32 receiver.
// Pattern 0, block 0 remains identical to the original successful test.
// 0=sequence, 1=zero, 2=FF, 3=55/AA, 4=walking bit.
inline uint8_t ebtksSpiTestByte(uint16_t index, uint8_t seq,
                                uint16_t block, uint8_t pattern)
{
  switch (pattern) {
    case 0: return (uint8_t)(0xA5U ^
                            (uint8_t)(index * 37U + seq + block * 13U));
    case 1: return 0x00;
    case 2: return 0xFF;
    case 3: return (index & 1U) ? 0xAA : 0x55;
    case 4: return (uint8_t)(1U << ((index + block) & 7U));
    default: return 0; // Rejected by caller.
  }
}

// Reverse (ESP32 -> Teensy) data deliberately differs from MOSI, even for
// constant-value patterns: a loopback or crossed wire must not appear valid.
// This exact function is independently duplicated in the ESP32 test header.
inline uint8_t ebtksSpiTestReverseByte(uint16_t index, uint8_t seq,
                                       uint16_t block, uint8_t pattern)
{
  return (uint8_t)(ebtksSpiTestByte(index, (uint8_t)(seq ^ 0xA7U),
                                   block, pattern) ^
                   (uint8_t)(0x5AU + index * 11U + block * 7U));
}

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
  digitalWrite(EBTKS_SPI_TEST_CS, HIGH);
  pinMode(EBTKS_SPI_TEST_CS, OUTPUT);
  pinMode(EBTKS_SPI_TEST_READY, INPUT_PULLDOWN);
  SPI.begin();
  ebtksSpiTestInitialized = true;
}

// Each UART spi_probe command corresponds to exactly ONE armed ESP DMA
// transaction. The next probe is sent only after UART confirmation, so
// successive sectors do not rely on detecting a short READY-low pulse.
// Returns whether a 512-byte SPI transaction completed, independently of
// any received-data mismatch. Such a mismatch is reported to the ESP32 via
// firstMisoMismatch / misoCrc, not confused with a READY timeout.
// Full-duplex remains optional; omitting bidi preserves the proven MOSI test.
inline bool ebtksSpiTestRead(uint8_t sequence, uint16_t block, uint8_t pattern,
                             uint32_t hz, bool bidi, uint16_t &mosiCrc,
                             uint16_t &misoCrc, int &firstMisoMismatch,
                             uint32_t &elapsedUs)
{
  mosiCrc = 0xFFFF;
  misoCrc = bidi ? 0xFFFF : 0;
  firstMisoMismatch = -1;
  elapsedUs = 0;
  if (!ebtksSpiTestValidHz(hz) || pattern > 4) return false;
  ebtksSpiTestInit();

  const uint32_t startWait = millis();
  while (!digitalRead(EBTKS_SPI_TEST_READY)) {
    // Do not turn off interrupts: the HP-85 bus ISR must still run.
    if ((uint32_t)(millis() - startWait) > 500) return false;
  }

  SPI.beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
  digitalWriteFast(EBTKS_SPI_TEST_CS, LOW);
  delayMicroseconds(3);
  const uint32_t startUs = micros();
  for (uint16_t i = 0; i < EBTKS_SPI_TEST_SIZE; ++i) {
    const uint8_t value = ebtksSpiTestByte(i, sequence, block, pattern);
    const uint8_t incoming = SPI.transfer(value);
    mosiCrc = ebtksSpiTestCrcByte(mosiCrc, value);
    if (bidi) {
      misoCrc = ebtksSpiTestCrcByte(misoCrc, incoming);
      if (firstMisoMismatch < 0 &&
          incoming != ebtksSpiTestReverseByte(i, sequence, block, pattern))
        firstMisoMismatch = i;
    }
  }
  elapsedUs = (uint32_t)(micros() - startUs);
  digitalWriteFast(EBTKS_SPI_TEST_CS, HIGH);
  SPI.endTransaction();
  return true;
}
