#include <Arduino.h>
#include "Base64.h"

// EBTKS_CRT.cpp owns the mirrored CRT state. Keep this definition in sync with
// video_capt_t there so this source file can expose the visible HP-85 alpha
// page without changing the ISR-facing CRT emulation code.
typedef struct
{
  uint8_t vram[16384];
  uint16_t sadAddr;
  uint16_t badAddr;
  uint8_t  ctrl;
} video_capt_t;

extern video_capt_t current_screen;
extern bool Is8687;

static int encodeVisibleHp85Alpha(char *buff, size_t capacity)
{
  if (!buff || capacity == 0)
    return -1;

  buff[0] = '\0';

  if (Is8687)
  {
    Serial.printf("Remote CRT alpha display is not yet supported on HP86/87\r\n");
    return -1;
  }

  // HP-85 SAD is a nibble address. The mirror stores packed bytes, so divide
  // by two. Alpha RAM is 32 x 64 characters and wraps after byte 2047.
  static uint8_t visible[512];
  int addr = current_screen.sadAddr / 2;

  for (size_t i = 0; i < sizeof(visible); ++i)
  {
    visible[i] = current_screen.vram[addr++];
    if (addr > 2047)
      addr = 0;
  }

  const int required = Base64.encodedLength(sizeof(visible));
  if (required < 0 || capacity <= (size_t)required)
  {
    Serial.printf("Remote CRT Base64 buffer too small: need %d + terminator, have %u\r\n",
                  required, (unsigned)capacity);
    return -1;
  }

  const int encoded = Base64.encode(buff, (char *)visible, sizeof(visible));
  if (encoded < 0)
  {
    buff[0] = '\0';
    Serial.printf("Remote CRT alpha Base64 encode failed\r\n");
    return -1;
  }

  return encoded;
}

// Live JSON CRT hook used by EBTKS_ESP.h. The legacy function in
// EBTKS_CRT.cpp remains for historical reference, but the active alpha path
// is restored here as ordinary compiled source rather than build middleware.
void dumpCrtRemoteAsBase64(char *buff, bool comp_graph)
{
  // EBTKS_ESP.h supplies its MAX_STR_LEN+2 buffer (4098 bytes).
  static const size_t OUTPUT_CAPACITY = 4098;

  if (!buff)
    return;

  buff[0] = '\0';

  if (comp_graph)
  {
    // The historical graphics path can expand beyond the 4 KiB JSON buffer.
    // Leave it disabled until that protocol is given an explicit bounded
    // buffer or a binary transport.
    Serial.printf("Remote CRT graphics dump is disabled pending bounded transport\r\n");
    return;
  }

  encodeVisibleHp85Alpha(buff, OUTPUT_CAPACITY);
}
