#pragma once

#include <Arduino.h>
#include <base64.hpp>
#include <stddef.h>
#include <stdint.h>

// EBTKS compatibility wrapper around Densaugeo/base64_arduino.
//
// The Densaugeo library is a better fit for binary transfers than the previous
// Base64 dependency: decode_base64() returns the exact number of binary bytes
// written and does not append a NUL byte to binary output.  This wrapper keeps
// the small Base64Class API used by the existing EBTKS sources and restores the
// stream-style encoder methods required by the remote CRT alpha-screen path.
class Base64Class
{
public:
  int encode(char *output, char *input, int inputLength)
  {
    if (!output || !input || inputLength < 0) return -1;
    return (int)encode_base64((const unsigned char *)input,
                              (unsigned int)inputLength,
                              (unsigned char *)output);
  }

  int decode(char *output, char *input, int inputLength)
  {
    if (!output || !input || inputLength < 0) return -1;
    return (int)decode_base64((const unsigned char *)input,
                              (unsigned int)inputLength,
                              (unsigned char *)output);
  }

  int encodedLength(int plainLength)
  {
    if (plainLength < 0) return -1;
    return (int)encode_base64_length((unsigned int)plainLength);
  }

  int decodedLength(char *input, int inputLength)
  {
    if (!input || inputLength < 0) return -1;
    return (int)decode_base64_length((const unsigned char *)input,
                                     (unsigned int)inputLength);
  }

  // Compatibility with the original patched EBTKS Base64 library.  The only
  // surviving streaming caller is the 512-byte visible HP-85 alpha screen.
  // Buffer the raw bytes, then encode them in one well-defined library call.
  void beginStreamEncode(char *output)
  {
    _streamOutput = output;
    _streamLength = 0;
    _streamOverflow = false;
    if (_streamOutput) _streamOutput[0] = '\0';
  }

  void streamEncode(uint8_t value)
  {
    if (_streamLength < sizeof(_streamInput)) {
      _streamInput[_streamLength++] = value;
    } else {
      _streamOverflow = true;
    }
  }

  int finalStreamEncode()
  {
    if (!_streamOutput || _streamOverflow) {
      if (_streamOutput) _streamOutput[0] = '\0';
      return -1;
    }
    return (int)encode_base64(_streamInput,
                              (unsigned int)_streamLength,
                              (unsigned char *)_streamOutput);
  }

private:
  static const size_t STREAM_CAPACITY = 512;
  uint8_t _streamInput[STREAM_CAPACITY];
  size_t _streamLength = 0;
  char *_streamOutput = nullptr;
  bool _streamOverflow = false;
};

// Existing EBTKS sources use both a global Base64 object and their own
// Base64Class instance.  Keep a TU-local global for compatibility.
static Base64Class Base64;
