#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Compatibility implementation matching the agdl/Base64 public API used by
// the EBTKS firmware.  This avoids include-path recursion from a local
// Base64.h while keeping existing call sites unchanged.
class Base64Class
{
public:
  int encode(char *output, char *input, int inputLength)
  {
    if (!output || !input || inputLength < 0) return -1;

    static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int i = 0;
    int encodedLength = 0;
    unsigned char a3[3];
    unsigned char a4[4];

    while (inputLength--)
    {
      a3[i++] = (unsigned char)*(input++);
      if (i == 3)
      {
        fromA3ToA4(a4, a3);
        for (i = 0; i < 4; i++) output[encodedLength++] = alphabet[a4[i]];
        i = 0;
      }
    }

    if (i)
    {
      for (int j = i; j < 3; j++) a3[j] = 0;
      fromA3ToA4(a4, a3);
      for (int j = 0; j < i + 1; j++) output[encodedLength++] = alphabet[a4[j]];
      while (i++ < 3) output[encodedLength++] = '=';
    }

    output[encodedLength] = '\0';
    return encodedLength;
  }

  int decode(char *output, char *input, int inputLength)
  {
    if (!output || !input || inputLength < 0) return -1;

    int i = 0;
    int decodedLength = 0;
    unsigned char a3[3];
    unsigned char a4[4];

    while (inputLength--)
    {
      if (*input == '=') break;

      a4[i++] = (unsigned char)*(input++);
      if (i == 4)
      {
        for (i = 0; i < 4; i++) a4[i] = lookupTable((char)a4[i]);
        fromA4ToA3(a3, a4);
        for (i = 0; i < 3; i++) output[decodedLength++] = (char)a3[i];
        i = 0;
      }
    }

    if (i)
    {
      for (int j = i; j < 4; j++) a4[j] = 0;
      for (int j = 0; j < 4; j++) a4[j] = lookupTable((char)a4[j]);
      fromA4ToA3(a3, a4);
      for (int j = 0; j < i - 1; j++) output[decodedLength++] = (char)a3[j];
    }

    // Retain agdl/Base64 semantics.  Callers in EBTKS use binary buffers
    // large enough for one extra byte.
    output[decodedLength] = '\0';
    return decodedLength;
  }

  int encodedLength(int plainLength)
  {
    int n = plainLength;
    return (n + 2 - ((n + 2) % 3)) / 3 * 4;
  }

  int decodedLength(char *input, int inputLength)
  {
    if (!input || inputLength <= 0) return 0;
    int numEq = 0;
    for (int i = inputLength - 1; i >= 0 && input[i] == '='; i--) numEq++;
    return ((6 * inputLength) / 8) - numEq;
  }

private:
  static inline void fromA3ToA4(unsigned char *a4, unsigned char *a3)
  {
    a4[0] = (a3[0] & 0xfc) >> 2;
    a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
    a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
    a4[3] = a3[2] & 0x3f;
  }

  static inline void fromA4ToA3(unsigned char *a3, unsigned char *a4)
  {
    a3[0] = (a4[0] << 2) + ((a4[1] & 0x30) >> 4);
    a3[1] = ((a4[1] & 0x0f) << 4) + ((a4[2] & 0x3c) >> 2);
    a3[2] = ((a4[2] & 0x03) << 6) + a4[3];
  }

  static inline unsigned char lookupTable(char c)
  {
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A');
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 0;
  }
};

// Some EBTKS sources use the global Base64 object directly; others create
// their own Base64Class instance.  A TU-local instance avoids linker clashes.
static Base64Class Base64;
