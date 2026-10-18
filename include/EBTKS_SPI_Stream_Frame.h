#pragma once
// Generic, versioned, 4-byte-aligned Teensy-master -> ESP32-slave SPI frame.
// Identical on both processors. Transport knows nothing about SD, CP/M, CRT.
// Source adapters fill the 512-byte payload; this layer checks framing/CRC.
// All multi-byte fields little-endian. Fixed 528-byte DMA transaction.
#include <stdint.h>
#include <stddef.h>

namespace EbtksStream {
static constexpr uint8_t MAGIC0 = 0x85;
static constexpr uint8_t MAGIC1 = 0x53;
static constexpr uint8_t VERSION = 1;
static constexpr uint8_t KIND_SD_READ = 1;
// Reserved for a future HP-85 alpha/graphics producer (not implemented):
static constexpr uint8_t KIND_CRT = 2;
static constexpr uint16_t FLAG_SOURCE_ERROR = 1;
static constexpr size_t HEADER_BYTES = 16;
static constexpr size_t PAYLOAD_BYTES = 512;
static constexpr size_t FRAME_BYTES = HEADER_BYTES + PAYLOAD_BYTES;

inline uint16_t get16(const uint8_t *b) {
  return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}
inline void put16(uint8_t *b, uint16_t value) {
  b[0] = (uint8_t)value;
  b[1] = (uint8_t)(value >> 8);
}
inline uint16_t crc16(const uint8_t *bytes, size_t len, uint16_t crc = 0xFFFF) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U)
                       : (uint16_t)(crc >> 1);
  }
  return crc;
}

//  0:85 1:53 2:version 3:kind 4-5:session 6-7:block index
//  8-9:payload length 10-11:payload CRC 12-13:flags
// 14-15:header CRC of bytes 0..13; data payload follows at offset 16.
inline void stamp(uint8_t *frame, uint8_t kind, uint16_t session,
                  uint16_t block, uint16_t flags = 0) {
  frame[0] = MAGIC0;
  frame[1] = MAGIC1;
  frame[2] = VERSION;
  frame[3] = kind;
  put16(frame + 4, session);
  put16(frame + 6, block);
  put16(frame + 8, (uint16_t)PAYLOAD_BYTES);
  put16(frame + 10, crc16(frame + HEADER_BYTES, PAYLOAD_BYTES));
  put16(frame + 12, flags);
  put16(frame + 14, crc16(frame, 14));
}

enum class Check : uint8_t {
  OK, BAD_MAGIC, BAD_VERSION, BAD_HEADER_CRC, BAD_KIND,
  BAD_SESSION, BAD_BLOCK, BAD_SIZE, BAD_PAYLOAD_CRC, SOURCE_ERROR
};

inline Check check(const uint8_t *frame, uint8_t kind, uint16_t session,
                   uint16_t block) {
  if (frame[0] != MAGIC0 || frame[1] != MAGIC1) return Check::BAD_MAGIC;
  if (frame[2] != VERSION) return Check::BAD_VERSION;
  if (get16(frame + 14) != crc16(frame, 14)) return Check::BAD_HEADER_CRC;
  if (frame[3] != kind) return Check::BAD_KIND;
  if (get16(frame + 4) != session) return Check::BAD_SESSION;
  if (get16(frame + 6) != block) return Check::BAD_BLOCK;
  if (get16(frame + 8) != PAYLOAD_BYTES) return Check::BAD_SIZE;
  if (get16(frame + 10) != crc16(frame + HEADER_BYTES, PAYLOAD_BYTES))
    return Check::BAD_PAYLOAD_CRC;
  if (get16(frame + 12) & FLAG_SOURCE_ERROR) return Check::SOURCE_ERROR;
  if (get16(frame + 12) & (uint16_t)~FLAG_SOURCE_ERROR)
    return Check::SOURCE_ERROR;
  return Check::OK;
}
inline const char *checkName(Check x) {
  switch (x) {
    case Check::OK: return "ok";
    case Check::BAD_MAGIC: return "bad_magic";
    case Check::BAD_VERSION: return "bad_version";
    case Check::BAD_HEADER_CRC: return "bad_header_crc";
    case Check::BAD_KIND: return "bad_kind";
    case Check::BAD_SESSION: return "bad_session";
    case Check::BAD_BLOCK: return "bad_block";
    case Check::BAD_SIZE: return "bad_size";
    case Check::BAD_PAYLOAD_CRC: return "bad_payload_crc";
    case Check::SOURCE_ERROR: return "source_error";
  }
  return "unknown";
}
} // namespace EbtksStream
