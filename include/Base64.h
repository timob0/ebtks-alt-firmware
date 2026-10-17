#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// EBTKS compatibility wrapper around Densaugeo/base64_arduino.
//
// Keep Densaugeo's implementation out of this public header. Its base64.hpp
// contains function definitions, so including it in more than one translation
// unit causes duplicate linker symbols. The implementation lives in
// src/Base64.cpp and is compiled exactly once.
class Base64Class
{
public:
  int encode(char *output, char *input, int inputLength);
  int decode(char *output, char *input, int inputLength);
  int encodedLength(int plainLength);
  int decodedLength(char *input, int inputLength);

  // Compatibility with the original patched EBTKS Base64 library. The remote
  // CRT alpha-screen path streams 512 raw bytes; we buffer them and perform a
  // single Densaugeo encode in finalStreamEncode().
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
