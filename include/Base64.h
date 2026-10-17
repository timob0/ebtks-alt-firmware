#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Local Base64 compatibility layer for the EBTKS firmware.
//
// The original EBTKS tree carried a patched Base64 implementation.  That
// implementation conflicted with newer Teensy/PlatformIO builds because of
// its PROGMEM declarations and was replaced temporarily by agdl/Base64.
// The replacement exposed different/unsafe behaviour in the ESP->Teensy
// file-write path (only the first 64 decoded bytes were reliable).
//
// Keep the small API used by the EBTKS sources, but implement it locally with
// no PROGMEM dependency.  encode() NUL-terminates its textual output; decode()
// returns the exact number of binary bytes produced and does not append a NUL
// byte to the binary output buffer.

class Base64Class
{
public:
  int encode(char *output, char *input, int inputLength)
  {
    if (!output || !input || inputLength < 0) return -1;

    static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int in = 0;
    int out = 0;

    while (in + 2 < inputLength)
    {
      const uint8_t a = (uint8_t)input[in++];
      const uint8_t b = (uint8_t)input[in++];
      const uint8_t c = (uint8_t)input[in++];

      output[out++] = alphabet[a >> 2];
      output[out++] = alphabet[((a & 0x03) << 4) | (b >> 4)];
      output[out++] = alphabet[((b & 0x0F) << 2) | (c >> 6)];
      output[out++] = alphabet[c & 0x3F];
    }

    const int remain = inputLength - in;
    if (remain == 1)
    {
      const uint8_t a = (uint8_t)input[in];
      output[out++] = alphabet[a >> 2];
      output[out++] = alphabet[(a & 0x03) << 4];
      output[out++] = '=';
      output[out++] = '=';
    }
    else if (remain == 2)
    {
      const uint8_t a = (uint8_t)input[in++];
      const uint8_t b = (uint8_t)input[in];
      output[out++] = alphabet[a >> 2];
      output[out++] = alphabet[((a & 0x03) << 4) | (b >> 4)];
      output[out++] = alphabet[(b & 0x0F) << 2];
      output[out++] = '=';
    }

    output[out] = '\0';
    return out;
  }

  int decode(char *output, char *input, int inputLength)
  {
    if (!output || !input || inputLength < 0) return -1;
    if (inputLength == 0) return 0;
    if ((inputLength & 3) != 0) return -1;

    int out = 0;

    for (int in = 0; in < inputLength; in += 4)
    {
      const int v0 = decodeValue(input[in]);
      const int v1 = decodeValue(input[in + 1]);
      const bool pad2 = input[in + 2] == '=';
      const bool pad3 = input[in + 3] == '=';
      const int v2 = pad2 ? 0 : decodeValue(input[in + 2]);
      const int v3 = pad3 ? 0 : decodeValue(input[in + 3]);

      if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return -1;

      // Padding is legal only in the final quartet.  "xx==" produces one
      // byte, "xxx=" produces two, and an unpadded quartet produces three.
      if ((pad2 || pad3) && in + 4 != inputLength) return -1;
      if (pad2 && !pad3) return -1;

      output[out++] = (char)((v0 << 2) | (v1 >> 4));

      if (!pad2)
      {
        output[out++] = (char)(((v1 & 0x0F) << 4) | (v2 >> 2));
        if (!pad3)
          output[out++] = (char)(((v2 & 0x03) << 6) | v3);
      }
    }

    return out;
  }

  int encodedLength(int plainLength)
  {
    if (plainLength < 0) return -1;
    return ((plainLength + 2) / 3) * 4;
  }

  int decodedLength(char *input, int inputLength)
  {
    if (!input || inputLength < 0 || (inputLength & 3) != 0) return -1;
    if (inputLength == 0) return 0;

    int padding = 0;
    if (input[inputLength - 1] == '=') ++padding;
    if (inputLength > 1 && input[inputLength - 2] == '=') ++padding;
    return (inputLength / 4) * 3 - padding;
  }

private:
  static int decodeValue(char c)
  {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  }
};

// The class is stateless.  A TU-local instance avoids linker/ODR issues and
// preserves the global Base64 object expected by EBTKS_CRT.cpp.
static Base64Class Base64;
