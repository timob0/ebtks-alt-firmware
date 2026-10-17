#include "Base64.h"
#include "TkislanBase64.h"

namespace
{
bool isBase64Char(char c)
{
  return (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') ||
         c == '+' || c == '/';
}

bool validBase64(const char *input, int inputLength)
{
  if (!input || inputLength < 0)
  {
    return false;
  }

  if (inputLength == 0)
  {
    return true;
  }

  if ((inputLength & 3) != 0)
  {
    return false;
  }

  int padding = 0;
  if (input[inputLength - 1] == '=') padding++;
  if (inputLength > 1 && input[inputLength - 2] == '=') padding++;

  const int dataLength = inputLength - padding;
  for (int i = 0; i < dataLength; ++i)
  {
    if (!isBase64Char(input[i]))
    {
      return false;
    }
  }

  for (int i = dataLength; i < inputLength; ++i)
  {
    if (input[i] != '=')
    {
      return false;
    }
  }

  return padding <= 2;
}
} // namespace

int Base64Class::encode(char *output, char *input, int inputLength)
{
  if (!output || !input || inputLength < 0)
  {
    return -1;
  }

  const size_t encodedLength = TkBase64::EncodedLength((size_t)inputLength);
  if (!TkBase64::Encode(input, (size_t)inputLength, output, encodedLength))
  {
    return -1;
  }

  // Base64 text is consumed as a C string throughout EBTKS.
  output[encodedLength] = '\0';
  return (int)encodedLength;
}

int Base64Class::decode(char *output, char *input, int inputLength)
{
  if (!output || !validBase64(input, inputLength))
  {
    return -1;
  }

  if (inputLength == 0)
  {
    return 0;
  }

  const size_t decodedLength = TkBase64::DecodedLength(input, (size_t)inputLength);
  if (!TkBase64::Decode(input, (size_t)inputLength, output, decodedLength))
  {
    return -1;
  }

  // Binary output is deliberately not NUL terminated.
  return (int)decodedLength;
}

int Base64Class::encodedLength(int plainLength)
{
  if (plainLength < 0)
  {
    return -1;
  }

  return (int)TkBase64::EncodedLength((size_t)plainLength);
}

int Base64Class::decodedLength(char *input, int inputLength)
{
  if (!validBase64(input, inputLength))
  {
    return -1;
  }

  if (inputLength == 0)
  {
    return 0;
  }

  return (int)TkBase64::DecodedLength(input, (size_t)inputLength);
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

  const size_t encodedLength = TkBase64::EncodedLength(_streamLength);
  if (!TkBase64::Encode((const char *)_streamInput,
                        _streamLength,
                        _streamOutput,
                        encodedLength))
  {
    _streamOutput[0] = '\0';
    return -1;
  }

  _streamOutput[encodedLength] = '\0';
  return (int)encodedLength;
}
