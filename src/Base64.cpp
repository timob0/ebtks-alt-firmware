#include "Base64.h"
#include <base64.hpp>

int Base64Class::encode(char *output, char *input, int inputLength)
{
  if (!output || !input || inputLength < 0)
  {
    return -1;
  }

  return (int)encode_base64((const unsigned char *)input,
                            (unsigned int)inputLength,
                            (unsigned char *)output);
}

int Base64Class::decode(char *output, char *input, int inputLength)
{
  if (!output || !input || inputLength < 0)
  {
    return -1;
  }

  return (int)decode_base64((const unsigned char *)input,
                            (unsigned int)inputLength,
                            (unsigned char *)output);
}

int Base64Class::encodedLength(int plainLength)
{
  if (plainLength < 0)
  {
    return -1;
  }

  return (int)encode_base64_length((unsigned int)plainLength);
}

int Base64Class::decodedLength(char *input, int inputLength)
{
  if (!input || inputLength < 0)
  {
    return -1;
  }

  return (int)decode_base64_length((const unsigned char *)input,
                                   (unsigned int)inputLength);
}

void Base64Class::beginStreamEncode(char *output)
{
  _streamOutput = output;
  _streamLength = 0;
  _streamOverflow = false;

  if (_streamOutput)
  {
    _streamOutput[0] = '\0';
  }
}

void Base64Class::streamEncode(uint8_t value)
{
  if (_streamLength < sizeof(_streamInput))
  {
    _streamInput[_streamLength++] = value;
  }
  else
  {
    _streamOverflow = true;
  }
}

int Base64Class::finalStreamEncode()
{
  if (!_streamOutput || _streamOverflow)
  {
    if (_streamOutput)
    {
      _streamOutput[0] = '\0';
    }
    return -1;
  }

  return (int)encode_base64(_streamInput,
                            (unsigned int)_streamLength,
                            (unsigned char *)_streamOutput);
}
