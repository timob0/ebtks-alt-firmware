#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// EBTKS compatibility wrapper around the vendored tkislan/base64 implementation.
// The implementation lives in src/Base64.cpp so the rest of the firmware keeps
// one small, stable Base64 API while the underlying codec remains replaceable.
class Base64Class
{
public:
  int encode(char *output, char *input, int inputLength);
  int decode(char *output, char *input, int inputLength);
  int encodedLength(int plainLength);
  int decodedLength(char *input, int inputLength);

  // Compatibility with the original patched EBTKS Base64 library. The remote
  // CRT alpha-screen path streams 512 raw bytes; we buffer them and perform a
  // single bounded encode in finalStreamEncode().
  void beginStreamEncode(char *output);
  void streamEncode(uint8_t value);
  int finalStreamEncode();

private:
  static const size_t STREAM_CAPACITY = 512;
  uint8_t _streamInput[STREAM_CAPACITY];
  size_t _streamLength = 0;
  char *_streamOutput = nullptr;
  bool _streamOverflow = false;
};

// Existing EBTKS sources use both a global Base64 object and their own
// Base64Class instance. Keep a TU-local global for compatibility.
static Base64Class Base64;
